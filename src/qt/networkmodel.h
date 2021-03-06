// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_NETWORKMODEL_H
#define DOGECOIN_QT_NETWORKMODEL_H

#include <QObject>
#include <QDateTime>

#include <atomic>

class AddressTableModel;
class BanTableModel;
class PeerTableModel;
class TransactionTableModel;

class CWallet ;
class CBlockIndex ;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

enum BlockSource {
    BLOCK_SOURCE_NONE,
    BLOCK_SOURCE_REINDEX,
    BLOCK_SOURCE_DISK,
    BLOCK_SOURCE_NETWORK
};

/** Model for the peer of the Dogecoin network */

class NetworkModel : public QObject
{
    Q_OBJECT

public:
    explicit NetworkModel( QObject * parent ) ;
    ~NetworkModel() ;

    PeerTableModel * getPeerTableModel() {  return peerTableModel ;  }
    BanTableModel * getBanTableModel() {  return banTableModel ;  }

    int getNumBlocks() const;
    int getHeaderTipHeight() const;
    int64_t getHeaderTipTime() const;
    //! Return number of transactions in the mempool
    long getMempoolSize() const;
    //! Return the dynamic memory usage of the mempool
    size_t getMempoolDynamicUsage() const;

    quint64 getTotalBytesRecv() const;
    quint64 getTotalBytesSent() const;

    double getVerificationProgress( const CBlockIndex * tip = nullptr ) const ;
    QDateTime getLastBlockDate() const;

    //! Return true if core is doing initial block download
    bool inInitialBlockDownload() const;
    //! Returns enum BlockSource of the current importing/syncing state
    enum BlockSource getBlockSource() const;
    // True if network activity is on
    bool isNetworkActive() const ;
    //! Toggle network activity state in core
    void setNetworkActive(bool active);
    //! Return warnings to be displayed in status bar
    QString getStatusBarWarnings() const;

    QString formatFullVersion() const ;
    QString formatSubVersion() const ;
    QString formatPeerStartupTime() const ;
    QString dataDir() const ;

    // caches for the best header
    mutable std::atomic<int> cachedBestHeaderHeight;
    mutable std::atomic<int64_t> cachedBestHeaderTime;

private:
    PeerTableModel * peerTableModel ;
    BanTableModel * banTableModel ;

    QTimer * pollTimer ;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

Q_SIGNALS:
    void numConnectionsChanged( int count ) ;
    void numBlocksChanged(int count, const QDateTime& blockDate, double nVerificationProgress, bool header);
    void mempoolSizeChanged(long count, size_t mempoolSizeInBytes);
    void networkActiveChanged(bool networkActive);
    void alertsChanged(const QString &warnings);
    void bytesChanged(quint64 totalBytesIn, quint64 totalBytesOut);

    //! Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);

    // Show progress dialog e.g. for verifychain
    void showProgress(const QString &title, int nProgress);

public Q_SLOTS:
    void updateTimer();
    void updateNumConnections( int numConnections ) ;
    void updateNetworkActive( bool networkActive ) ;
    void addrLocalSetForNode() ;
    void updateAlert(const QString &hash, int status);
    void updateBanlist();

} ;

#endif
