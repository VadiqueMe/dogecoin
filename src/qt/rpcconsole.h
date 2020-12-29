// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_RPCCONSOLE_H
#define DOGECOIN_QT_RPCCONSOLE_H

#include "guiutil.h"
#include "peertablemodel.h"

#include "net.h"

#include <QWidget>
#include <QCompleter>
#include <QThread>

#include <QFileSystemWatcher>

class NetworkModel ;
class MempoolModel ;
class PlatformStyle ;
class RPCTimerInterface ;

namespace Ui {
    class RPCConsole;
}

QT_BEGIN_NAMESPACE
class QMenu;
class QItemSelection;
QT_END_NAMESPACE

class RPCConsole: public QWidget
{
    Q_OBJECT

public:
    explicit RPCConsole( const PlatformStyle * style, QWidget * parent = nullptr ) ;
    ~RPCConsole() ;

    static bool RPCParseCommandLine(std::string &strResult, const std::string &strCommand, bool fExecute, std::string * const pstrFilteredOut = NULL);
    static bool RPCExecuteCommandLine(std::string &strResult, const std::string &strCommand, std::string * const pstrFilteredOut = NULL) {
        return RPCParseCommandLine(strResult, strCommand, true, pstrFilteredOut);
    }

    void setNetworkModel( NetworkModel * model ) ;

    enum MessageClass {
        MC_ERROR,
        MC_DEBUG,
        CMD_REQUEST,
        CMD_REPLY,
        CMD_ERROR
    };

protected:
    virtual bool eventFilter(QObject* obj, QEvent *event);
    void keyPressEvent(QKeyEvent *);

private Q_SLOTS:
    void on_lineEdit_returnPressed();
    void currentTabChangedTo( int index ) ;
    void onFileChange( const QString & whatsChanged ) ;
    void veryLogFile() ;
    void clearLogSearchFilter() ;
    /** open the debug log from the current datadir */
    void on_openDebugLogButton_clicked() ;
    /** change the time range of the network traffic graph */
    void on_sldGraphRange_valueChanged(int value);
    /** update traffic statistics */
    void updateTrafficStats( quint64 totalBytesIn, quint64 totalBytesOut ) ;
    void updateTrafficStats() ;
    void resetTrafficValues() ;
    void pickColorForReceived() ;
    void pickColorForSent() ;
    void repaintTrafficColorButtons() ;
    void resizeEvent(QResizeEvent *event);
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);
    /** Show custom context menu on Peers tab */
    void showPeersTableContextMenu(const QPoint& point);
    /** Show custom context menu on Bans tab */
    void showBanTableContextMenu(const QPoint& point);
    /** Hides ban table if no bans are present */
    void showOrHideBanTableIfNeeded() ;
    /** clear the selected node */
    void clearSelectedNode();

public Q_SLOTS:
    void clearConsole( bool clearHistory = true ) ;
    void fontBigger();
    void fontSmaller();
    void setFontSize(int newSize);
    /** Append the message to the message widget */
    void message(int category, const QString &message, bool html = false);
    /** Set number of connections shown in the UI */
    void setNumConnections(int count);
    /** Set network state shown in the UI */
    void setNetworkActive(bool networkActive);
    /** Set number of blocks and last block date shown in the UI */
    void setNumBlocks( int count, const QDateTime & blockDate, double progress, bool headers ) ;
    /** Set size (number of transactions and memory usage) of the mempool in the UI */
    void setMempoolSize( long numberOfTxs, size_t dynUsage ) ;
    /** Go forward or back in history */
    void browseHistory(int offset);
    /** Scroll console view to end */
    void scrollToEnd();
    /** Handle selection of peer in peers list */
    void peerSelected( const QItemSelection & selected, const QItemSelection & deselected ) ;
    /** Handle selection caching before update */
    void peerLayoutAboutToChange();
    /** Handle updated peer information */
    void peerLayoutChanged();

    /** Show dialog to compose a message to peer */
    void textMessageToSelectedNode() ;
    /** Disconnect a selected node */
    void disconnectSelectedNode() ;
    /** Ban a selected node */
    void banSelectedNode( int banSeconds ) ;
    /** Unban a selected node */
    void unbanSelectedNode() ;

    void switchToRPCConsoleTab() ;

    void showContextMenuForLog( const QPoint & where ) ;

Q_SIGNALS:
    // For RPC command performer
    void stopPerformer() ;
    void cmdRequest( const QString & command ) ;

private:
    void startPerformer() ;
    void setTrafficGraphRange( int mins ) ;
    void constructPeerDetailsWidget() ;
    void updateNodeDetails( const CNodeCombinedStats * stats ) ;

    enum ColumnWidths
    {
        ADDRESS_COLUMN_WIDTH = 200,
        SUBVERSION_COLUMN_WIDTH = 150,
        PING_COLUMN_WIDTH = 80,
        BANSUBNET_COLUMN_WIDTH = 200,
        BANTIME_COLUMN_WIDTH = 250
    };

    Ui::RPCConsole * ui ;
    MempoolModel * mempoolModel ;
    NetworkModel * networkModel ;
    QStringList history ;
    int historyPtr ;
    QString cmdBeforeBrowsing ;
    QList< NodeId > cachedNodeids ;
    const PlatformStyle * platformStyle ;
    RPCTimerInterface * rpcTimerInterface ;
    QMenu * peersTableContextMenu ;
    QMenu * banTableContextMenu ;
    int consoleFontSize ;
    QCompleter * autoCompleter ;
    QThread thread ;

    QString pathToLogFile ;
    QFileSystemWatcher logFileWatcher ;

    quint64 resetBytesRecv ;
    quint64 resetBytesSent ;

    /** Update UI with latest network info from model */
    void updateNetworkInfo() ;

    std::unique_ptr< QLabel > peerHeading ;
    std::unique_ptr< QWidget > peerDetailsWidget ;
    std::vector< std::pair< QString, QLabel * > > peerDetails ;
      std::unique_ptr< QLabel > peerDirection ;
      std::unique_ptr< QLabel > peerVersion ;
      std::unique_ptr< QLabel > peerSubversion ;
      std::unique_ptr< QLabel > peerServices ;
      std::unique_ptr< QLabel > peerHeight ;
      std::unique_ptr< QLabel > peerSyncHeight ;
      std::unique_ptr< QLabel > peerCommonHeight ;
      std::unique_ptr< QLabel > peerConnTime ;
      std::unique_ptr< QLabel > peerLastSend ;
      std::unique_ptr< QLabel > peerLastRecv ;
      std::unique_ptr< QLabel > peerBytesSent ;
      std::unique_ptr< QLabel > peerBytesRecv ;
      std::unique_ptr< QLabel > peerPingTime ;
      std::unique_ptr< QLabel > peerPingWait ;
      std::unique_ptr< QLabel > peerMinPing ;
      std::unique_ptr< QLabel > peerTimeOffset ;
      std::unique_ptr< QLabel > peerWhitelisted ;
      std::unique_ptr< QLabel > peerBanScore ;

} ;

#endif
