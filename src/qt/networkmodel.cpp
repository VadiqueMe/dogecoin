// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "networkmodel.h"

#include "bantablemodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "peertablemodel.h"

#include "alert.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "peerversion.h"
#include "validation.h"
#include "net.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"

#include <stdint.h>

#include <QDebug>
#include <QTimer>

class CBlockIndex;

static const int64_t nPeerStartupTime = GetTime() ;

static int64_t nLastHeaderTipUpdateNotification = 0;
static int64_t nLastBlockTipUpdateNotification = 0;

NetworkModel::NetworkModel( OptionsModel * options, QObject * parent ) :
    QObject( parent ),
    optionsModel( options ),
    peerTableModel( nullptr ),
    banTableModel( nullptr ),
    pollTimer( 0 )
{
    cachedBestHeaderHeight = -1;
    cachedBestHeaderTime = -1;
    peerTableModel = new PeerTableModel(this);
    banTableModel = new BanTableModel(this);
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateTimer()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

NetworkModel::~NetworkModel()
{
    unsubscribeFromCoreSignals() ;
}

int NetworkModel::getNumConnections( unsigned int flags ) const
{
    CConnman::NumConnections connections = CConnman::CONNECTIONS_NONE;

    if(flags == CONNECTIONS_IN)
        connections = CConnman::CONNECTIONS_IN;
    else if (flags == CONNECTIONS_OUT)
        connections = CConnman::CONNECTIONS_OUT;
    else if (flags == CONNECTIONS_ALL)
        connections = CConnman::CONNECTIONS_ALL;

    if(g_connman)
         return g_connman->GetNodeCount(connections);
    return 0;
}

int NetworkModel::getNumBlocks() const
{
    LOCK(cs_main);
    return chainActive.Height();
}

int NetworkModel::getHeaderTipHeight() const
{
    if (cachedBestHeaderHeight == -1) {
        // make sure we initially populate the cache via a cs_main lock
        // otherwise we need to wait for a tip update
        LOCK(cs_main);
        if (pindexBestHeader) {
            cachedBestHeaderHeight = pindexBestHeader->nHeight;
            cachedBestHeaderTime = pindexBestHeader->GetBlockTime();
        }
    }
    return cachedBestHeaderHeight;
}

int64_t NetworkModel::getHeaderTipTime() const
{
    if (cachedBestHeaderTime == -1) {
        LOCK(cs_main);
        if (pindexBestHeader) {
            cachedBestHeaderHeight = pindexBestHeader->nHeight;
            cachedBestHeaderTime = pindexBestHeader->GetBlockTime();
        }
    }
    return cachedBestHeaderTime;
}

quint64 NetworkModel::getTotalBytesRecv() const
{
    if ( g_connman == nullptr ) return 0;
    return g_connman->GetTotalBytesRecv() ;
}

quint64 NetworkModel::getTotalBytesSent() const
{
    if ( g_connman == nullptr ) return 0 ;
    return g_connman->GetTotalBytesSent() ;
}

QDateTime NetworkModel::getLastBlockDate() const
{
    LOCK( cs_main ) ;

    if ( chainActive.Tip() != nullptr )
        return QDateTime::fromTime_t( chainActive.Tip()->GetBlockTime() ) ;

    // when the chain has no blocks, return time of the genesis block
    return QDateTime::fromTime_t( Params().GenesisBlock().GetBlockTime() ) ;
}

long NetworkModel::getMempoolSize() const
{
    return mempool.size() ;
}

size_t NetworkModel::getMempoolDynamicUsage() const
{
    return mempool.DynamicMemoryUsage();
}

double NetworkModel::getVerificationProgress( const CBlockIndex * tipIn ) const
{
    CBlockIndex *tip = const_cast<CBlockIndex *>(tipIn);
    if (!tip)
    {
        LOCK(cs_main);
        tip = chainActive.Tip();
    }
    return GuessVerificationProgress(Params().TxData(), tip);
}

void NetworkModel::updateTimer()
{
    // no locking required at this point
    // the following calls will acquire the required lock
    Q_EMIT mempoolSizeChanged(getMempoolSize(), getMempoolDynamicUsage());
    Q_EMIT bytesChanged(getTotalBytesRecv(), getTotalBytesSent());
}

void NetworkModel::updateNumConnections( int numConnections )
{
    Q_EMIT numConnectionsChanged( numConnections ) ;
}

void NetworkModel::updateNetworkActive( bool networkActive )
{
    Q_EMIT networkActiveChanged( networkActive ) ;
}

void NetworkModel::updateAlert( const QString & hash, int status )
{
    // Show error message notification for new alert
    if(status == CT_NEW)
    {
        uint256 hash_256;
        hash_256.SetHex(hash.toStdString());
        CAlert alert = CAlert::getAlertByHash(hash_256);
        if(!alert.IsNull())
        {
            Q_EMIT message(tr("Network Alert"), QString::fromStdString(alert.strStatusBar), CClientUIInterface::ICON_ERROR);
        }
    }

    Q_EMIT alertsChanged(getStatusBarWarnings());
}

bool NetworkModel::inInitialBlockDownload() const
{
    return IsInitialBlockDownload() ;
}

enum BlockSource NetworkModel::getBlockSource() const
{
    if (fReindex)
        return BLOCK_SOURCE_REINDEX;
    else if (fImporting)
        return BLOCK_SOURCE_DISK;
    else if (getNumConnections() > 0)
        return BLOCK_SOURCE_NETWORK;

    return BLOCK_SOURCE_NONE;
}

void NetworkModel::setNetworkActive( bool active )
{
    if ( g_connman != nullptr ) {
         g_connman->SetNetworkActive( active ) ;
    }
}

bool NetworkModel::isNetworkActive() const
{
    if ( g_connman != nullptr )
        return g_connman->IsNetworkActive() ;

    return false ;
}

QString NetworkModel::getStatusBarWarnings() const
{
    return QString::fromStdString( GetWarnings( "gui" ) ) ;
}

OptionsModel * NetworkModel::getOptionsModel()
{
    return optionsModel ;
}

PeerTableModel * NetworkModel::getPeerTableModel()
{
    return peerTableModel ;
}

BanTableModel * NetworkModel::getBanTableModel()
{
    return banTableModel ;
}

QString NetworkModel::formatFullVersion() const
{
    return QString::fromStdString( FormatFullVersion() ) ;
}

QString NetworkModel::formatSubVersion() const
{
    return QString::fromStdString( strSubVersion ) ;
}

QString NetworkModel::formatPeerStartupTime() const
{
    return QDateTime::fromTime_t( nPeerStartupTime ).toString() ;
}

QString NetworkModel::dataDir() const
{
    return GUIUtil::boostPathToQString(GetDataDir());
}

void NetworkModel::updateBanlist()
{
    banTableModel->refresh();
}

// Handlers for core signals
static void ShowProgress( NetworkModel * netmodel, const std::string & title, int nProgress )
{
    // emits signal "showProgress"
    QMetaObject::invokeMethod(netmodel, "showProgress", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(title)),
                              Q_ARG(int, nProgress));
}

static void NotifyNumConnectionsChanged( NetworkModel * netmodel, int newNumConnections )
{
    // Too noisy: qDebug() << "NotifyNumConnectionsChanged: " + QString::number(newNumConnections);
    QMetaObject::invokeMethod(netmodel, "updateNumConnections", Qt::QueuedConnection,
                              Q_ARG(int, newNumConnections));
}

static void NotifyNetworkActiveChanged( NetworkModel * netmodel, bool networkActive )
{
    QMetaObject::invokeMethod(netmodel, "updateNetworkActive", Qt::QueuedConnection,
                              Q_ARG(bool, networkActive));
}

static void NotifyAlertChanged( NetworkModel * netmodel, const uint256 & hash, ChangeType status )
{
    qDebug() << "NotifyAlertChanged: " + QString::fromStdString(hash.GetHex()) + " status=" + QString::number(status);
    QMetaObject::invokeMethod(netmodel, "updateAlert", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(hash.GetHex())),
                              Q_ARG(int, status));
}

static void BannedListChanged( NetworkModel * netmodel )
{
    qDebug() << QString("%1: Requesting update for peer banlist").arg(__func__);
    QMetaObject::invokeMethod( netmodel, "updateBanlist", Qt::QueuedConnection ) ;
}

static void BlockTipChanged( NetworkModel * netmodel, bool initialSync, const CBlockIndex * pIndex, bool fHeader )
{
    // lock free async UI updates in case we have a new block tip
    // during initial sync, only update the UI if the last update
    // was > 250ms (MODEL_UPDATE_DELAY) ago
    int64_t now = 0;
    if (initialSync)
        now = GetTimeMillis();

    int64_t& nLastUpdateNotification = fHeader ? nLastHeaderTipUpdateNotification : nLastBlockTipUpdateNotification;

    if (fHeader) {
        // cache best headers time and height to reduce future cs_main locks
        netmodel->cachedBestHeaderHeight = pIndex->nHeight ;
        netmodel->cachedBestHeaderTime = pIndex->GetBlockTime() ;
    }
    // if we are in-sync, update the UI regardless of last update time
    if (!initialSync || now - nLastUpdateNotification > MODEL_UPDATE_DELAY) {
        //pass a async signal to the UI thread
        QMetaObject::invokeMethod(netmodel, "numBlocksChanged", Qt::QueuedConnection,
                                  Q_ARG(int, pIndex->nHeight),
                                  Q_ARG(QDateTime, QDateTime::fromTime_t(pIndex->GetBlockTime())),
                                  Q_ARG(double, netmodel->getVerificationProgress(pIndex)),
                                  Q_ARG(bool, fHeader));
        nLastUpdateNotification = now;
    }
}

void NetworkModel::subscribeToCoreSignals()
{
    uiInterface.ShowProgress.connect(boost::bind(ShowProgress, this, _1, _2));
    uiInterface.NotifyNumConnectionsChanged.connect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyNetworkActiveChanged.connect(boost::bind(NotifyNetworkActiveChanged, this, _1));
    uiInterface.NotifyAlertChanged.connect(boost::bind(NotifyAlertChanged, this, _1, _2));
    uiInterface.BannedListChanged.connect(boost::bind(BannedListChanged, this));
    uiInterface.NotifyBlockTip.connect(boost::bind(BlockTipChanged, this, _1, _2, false));
    uiInterface.NotifyHeaderTip.connect(boost::bind(BlockTipChanged, this, _1, _2, true));
}

void NetworkModel::unsubscribeFromCoreSignals()
{
    uiInterface.ShowProgress.disconnect(boost::bind(ShowProgress, this, _1, _2));
    uiInterface.NotifyNumConnectionsChanged.disconnect(boost::bind(NotifyNumConnectionsChanged, this, _1));
    uiInterface.NotifyNetworkActiveChanged.disconnect(boost::bind(NotifyNetworkActiveChanged, this, _1));
    uiInterface.NotifyAlertChanged.disconnect(boost::bind(NotifyAlertChanged, this, _1, _2));
    uiInterface.BannedListChanged.disconnect(boost::bind(BannedListChanged, this));
    uiInterface.NotifyBlockTip.disconnect(boost::bind(BlockTipChanged, this, _1, _2, false));
    uiInterface.NotifyHeaderTip.disconnect(boost::bind(BlockTipChanged, this, _1, _2, true));
}
