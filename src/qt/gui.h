// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_GUI_H
#define DOGECOIN_QT_GUI_H

#if defined(HAVE_CONFIG_H)
#include "config/dogecoin-config.h"
#endif

#include "amount.h"
#include "unitsofcoin.h"

#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QPoint>
#include <QSystemTrayIcon>
#include <QTimer>

#include <memory> // std::unique_ptr

class NetworkModel ;
class NetworkStyle ;
class Notificator;
class OptionsModel;
class PlatformStyle;
class RPCConsole;
class SendCoinsRecipient;
class UnitDisplayStatusBarControl;
class WalletFrame;
class WalletModel ;
enum class WalletEncryptionStatus ;
class ChainSyncOverlay ;
class HelpMessageDialog ;

class CWallet ;

QT_BEGIN_NAMESPACE
class QAction;
class QProgressBar;
class QProgressDialog;
class QToolButton ;
QT_END_NAMESPACE

/**
  GUI main class. This class represents the main window of the Dogecoin UI. It communicates
  with both the network and wallet models to give the user an up-to-date view
*/
class DogecoinGUI : public QMainWindow
{
    Q_OBJECT

public:
    static const QString DEFAULT_WALLET;
    static const std::string DEFAULT_UIPLATFORM;

    explicit DogecoinGUI( const PlatformStyle * style, const NetworkStyle * networkStyle, QWidget * parent = nullptr ) ;
    ~DogecoinGUI() ;

    /** Set the network model
        The network model represents the part that communicates with the P2P network, and is wallet-agnostic
     */
    void setNetworkModel( NetworkModel * model ) ;

    void setOptionsModel( OptionsModel * model ) ;

#ifdef ENABLE_WALLET
    /** Set the wallet model which represents a dogecoin wallet
     */
    bool addWallet(const QString& name, WalletModel *walletModel);
    bool setCurrentWallet(const QString& name);
    void removeAllWallets();
#endif // ENABLE_WALLET
    bool enableWallet;

protected:
    void changeEvent(QEvent *e);
    void closeEvent(QCloseEvent *event);
    void showEvent(QShowEvent *event);
    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);
    bool eventFilter(QObject *object, QEvent *event);

private:
    NetworkModel * networkModel ;
    OptionsModel * optionsModel ;
    WalletFrame * walletFrame ;

    UnitDisplayStatusBarControl *unitDisplayControl;
    QLabel *labelWalletEncryptionIcon;
    QLabel *labelWalletHDStatusIcon;
    QLabel * connectionsControl ;
    QLabel * onionIcon ;
    QLabel *labelBlocksIcon;
    QLabel * generatingLabel ;
    QLabel *progressBarLabel;
    QProgressBar *progressBar;
    QProgressDialog *progressDialog;

    QMenuBar * appMenuBar ;
    QAction * overviewTabAction ;
    QAction * historyTabAction ;
    QAction * quitAction ;
    QAction * sendCoinsTabAction ;
    QAction * sendCoinsMenuAction ;
    QAction * usedSendingAddressesAction ;
    QAction * usedReceivingAddressesAction ;
    QAction * signMessageAction ;
    QAction * verifyMessageAction ;
    QAction * paperWalletAction ;
    QAction * aboutAction ;
    QAction * receiveCoinsTabAction ;
    QAction * receiveCoinsMenuAction ;
    QAction * optionsAction ;
    QAction * toggleHideAction ;
    QAction * encryptWalletAction ;
    QAction * backupWalletAction ;
    QAction * changePassphraseAction ;
    QAction * aboutQtAction ;
    QAction * showGutsWindowMenuAction ;
    QAction * openAction ;
    QAction * showHelpMessageAction ;
    QAction * digTabAction ;
    QToolButton * showGutsWindowButton ;

    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    Notificator *notificator;
    RPCConsole *rpcConsole;
    std::unique_ptr< ChainSyncOverlay > chainsyncOverlay ;
    std::unique_ptr< HelpMessageDialog > helpMessageDialog ;

    /** Keep track of previous number of blocks, to detect progress */
    int prevBlocks;
    int spinnerFrame;

    const PlatformStyle * platformStyle ;

    std::unique_ptr< QTimer > everySecondTimer ;

    /** Create the main UI actions */
    void createActions();
    /** Create the menu bar and sub-menus */
    void createMenuBar();
    /** Create the toolbars */
    void createToolBars();
    /** Create system tray icon and notification */
    void createTrayIcon(const NetworkStyle *networkStyle);
    /** Create system tray menu (or setup the dock menu) */
    void createTrayIconMenu();

    /** Enable or disable all wallet-related actions */
    void setWalletActionsEnabled(bool enabled);

    /** Connect core signals to GUI */
    void subscribeToCoreSignals() ;
    /** Disconnect core signals from GUI */
    void unsubscribeFromCoreSignals() ;

    void updateHeadersSyncProgressLabel();

Q_SIGNALS:
    /** Signal raised when a URI was entered or dragged to the GUI */
    void receivedURI(const QString &uri);

public Q_SLOTS:
    /** update user interface with the latest network info from the model */
    void updateNetworkInfo() ;

    /** Set number of blocks and last block date shown in the UI */
    void setNumBlocks( int count, const QDateTime & blockDate, double progress, bool headers ) ;

    /** Notify the user of an event from the core network or transaction handling code
       @param[in] title     the message box / notification title
       @param[in] message   the displayed text
       @param[in] style     modality and style definitions (icon and used buttons - buttons only for message boxes)
                            @see CClientUserInterface::MessageBoxFlags
       @param[in] ret       pointer to a bool that will be modified to whether Ok was clicked (modal only)
    */
    void message(const QString &title, const QString &message, unsigned int style, bool *ret = NULL);

#ifdef ENABLE_WALLET
    /** Set the encryption status as shown in the UI
    */
    void setEncryptionStatus( const WalletEncryptionStatus & status ) ;

    /** Set the hd-enabled status as shown in the UI
     */
    void setHDStatus( int hdEnabled ) ;

    bool handlePaymentRequest( const SendCoinsRecipient & recipient ) ;

    /** Show incoming transaction notification for new transactions */
    void incomingTransaction( const QString & date, const unitofcoin & unit, const CAmount & amount, const QString & type, const QString & address, const QString & label ) ;
#endif // ENABLE_WALLET

private Q_SLOTS:
#ifdef ENABLE_WALLET
    /** Switch to overview (home) page */
    void gotoOverviewPage() ;
    /** Switch to history (transactions) page */
    void gotoHistoryPage() ;
    /** Switch to receive coins page */
    void gotoReceiveCoinsPage() ;
    /** Switch to send coins page */
    void gotoSendCoinsPage( QString addr = "" ) ;
    /** Switch to dig page */
    void gotoDigPage() ;

    /** Show Sign/Verify Message dialog and switch to sign message tab */
    void gotoSignMessageTab(QString addr = "");
    /** Show Sign/Verify Message dialog and switch to verify message tab */
    void gotoVerifyMessageTab(QString addr = "");

    /** Show open dialog */
    void openClicked();
#endif // ENABLE_WALLET

    /** Show configuration dialog */
    void optionsClicked();
    /** Show about dialog */
    void aboutClicked();
    /** Show guts window */
    void showGutsWindow() ;
    /** Show guts window with active console page */
    void showGutsWindowActivateConsole() ;
    /** Show help message dialog */
    void showHelpMessageClicked();
#ifndef Q_OS_MAC
    /** Handle tray icon clicked */
    void trayIconActivated(QSystemTrayIcon::ActivationReason reason);
#endif

    void updateBottomBarShowsDigging() ;

    void showNewTextMessagesIfAny() ;

    /** Show window if hidden, unminimize when minimized, rise when obscured or show if hidden and fToggleHidden is true */
    void showNormalIfMinimized(bool fToggleHidden = false);
    /** Simply calls showNormalIfMinimized(true) for use in SLOT() macro */
    void toggleHidden();

    /** called by a timer to check if a shutdown has been requested **/
    void detectShutdown() ;

    /** Show progress dialog e.g. for verifychain */
    void showProgress(const QString &title, int nProgress);

    /** When hideTrayIcon setting is changed in OptionsModel hide or show the icon accordingly */
    void setTrayIconVisible(bool);

    /** Toggle networking */
    void toggleNetworkActive();

    void showChainsyncOverlay() ;
} ;

class UnitDisplayStatusBarControl : public QLabel
{
    Q_OBJECT

public:
    explicit UnitDisplayStatusBarControl(const PlatformStyle *platformStyle);
    /** Lets the control know about the Options Model (and its signals) */
    void setOptionsModel(OptionsModel *optionsModel);

protected:
    /** So that it responds to left-button clicks */
    void mousePressEvent(QMouseEvent *event);

private:
    OptionsModel * optionsModel ;
    QMenu * menu ;

    /** Shows context menu with Display Unit options by the mouse coordinates */
    void onDisplayUnitsClicked( const QPoint & point ) ;
    /** Creates context menu, its actions, and wires up all the relevant signals for mouse events */
    void createContextMenu() ;

private Q_SLOTS:
    /** When Display Units are changed on OptionsModel it will refresh the display text of the control on the status bar */
    void updateDisplayUnit( unitofcoin newUnit ) ;
    /** Tells underlying optionsModel to update its current display unit */
    void onMenuSelection( QAction * action ) ;
};

#endif
