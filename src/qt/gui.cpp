// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#if defined(HAVE_CONFIG_H)
#include "config/dogecoin-config.h"
#endif

#include "gui.h"

#include "unitsofcoin.h"
#include "networkmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "chainsyncoverlay.h"
#include "networkstyle.h"
#include "notificator.h"
#include "openuridialog.h"
#include "optionsdialog.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "rpcconsole.h"
#include "utilitydialog.h"

#ifdef ENABLE_WALLET
#include "walletframe.h"
#include "walletmodel.h"
#endif // ENABLE_WALLET

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#include "chainparams.h"
#include "init.h"
#include "ui_interface.h"
#include "util.h"
#include "miner.h"

#include <iostream>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopWidget>
#include <QDragEnterEvent>
#include <QFontDatabase>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#if QT_VERSION < 0x050000
#include <QTextDocument>
#include <QUrl>
#else
#include <QUrlQuery>
#endif

const std::string DogecoinGUI::DEFAULT_UIPLATFORM =
#if defined(Q_OS_MAC)
        "macosx"
#elif defined(Q_OS_WIN)
        "windows"
#else
        "other"
#endif
        ;

/** Display name for default wallet name. Uses tilde to avoid name
 * collisions in the future with additional wallets */
const QString DogecoinGUI::DEFAULT_WALLET = "~Default" ;

DogecoinGUI::DogecoinGUI( const PlatformStyle * style, const NetworkStyle * networkStyle, QWidget * parent ) :
    QMainWindow(parent),
    enableWallet(false),
    networkModel( nullptr ),
    optionsModel( nullptr ),
    walletFrame(0),
    unitDisplayControl(0),
    labelWalletEncryptionIcon(0),
    labelWalletHDStatusIcon(0),
    connectionsControl(0),
    labelBlocksIcon(0),
    generatingLabel( nullptr ),
    progressBarLabel(0),
    progressBar(0),
    progressDialog(0),
    appMenuBar(0),
    overviewTabAction( nullptr ),
    historyTabAction( nullptr ),
    quitAction(0),
    sendCoinsTabAction( nullptr ),
    sendCoinsMenuAction(0),
    usedSendingAddressesAction(0),
    usedReceivingAddressesAction(0),
    signMessageAction(0),
    verifyMessageAction(0),
    aboutAction(0),
    receiveCoinsTabAction( nullptr ),
    receiveCoinsMenuAction(0),
    optionsAction(0),
    toggleHideAction(0),
    encryptWalletAction(0),
    backupWalletAction(0),
    changePassphraseAction(0),
    aboutQtAction(0),
    openRPCConsoleAction(0),
    openAction(0),
    showHelpMessageAction(0),
    digTabAction( nullptr ),
    trayIcon(0),
    trayIconMenu(0),
    notificator(0),
    rpcConsole(0),
    chainsyncOverlay( nullptr ),
    helpMessageDialog( nullptr ),
    prevBlocks(0),
    spinnerFrame(0),
    platformStyle( style ),
    everySecondTimer( new QTimer() )
{
    GUIUtil::restoreWindowGeometry("nWindow", QSize(850, 550), this);

    QString windowTitle = tr(PACKAGE_NAME) + " - ";
#ifdef ENABLE_WALLET
    enableWallet = WalletModel::isWalletEnabled();
#endif // ENABLE_WALLET
    if ( enableWallet ) {
        windowTitle += tr( "Wallet" ) ;
    } else {
        windowTitle += tr( "Node" ) ;
    }
    windowTitle += " " + networkStyle->getTextToAppendToTitle() ;
#ifndef Q_OS_MAC
    QApplication::setWindowIcon(networkStyle->getTrayAndWindowIcon());
    setWindowIcon(networkStyle->getTrayAndWindowIcon());
#else
    MacDockIconHandler::instance()->setIcon(networkStyle->getAppIcon());
#endif
    setWindowTitle(windowTitle);

#if defined(Q_OS_MAC) && QT_VERSION < 0x050000
    // This property is not implemented in Qt 5. Setting it has no effect.
    // A replacement API (QtMacUnifiedToolBar) is available in QtMacExtras
    setUnifiedTitleAndToolBarOnMac(true);
#endif

    rpcConsole = new RPCConsole( platformStyle ) ;

#ifdef ENABLE_WALLET
    if ( enableWallet )
    {
        /** Create wallet frame and make it the central widget */
        walletFrame = new WalletFrame( platformStyle, this ) ;
        setCentralWidget( walletFrame ) ;
    } else
#endif // ENABLE_WALLET
    {
        /* When compiled without wallet or -disablewallet is provided,
         * the central widget is the rpc console
         */
        setCentralWidget( rpcConsole ) ;
    }

    // Dogecoin: load fallback font in case Comic Sans is not availble on the system
    QFontDatabase::addApplicationFont(":fonts/ComicNeue-Bold");
    QFontDatabase::addApplicationFont(":fonts/ComicNeue-Bold-Oblique");
    QFontDatabase::addApplicationFont(":fonts/ComicNeue-Light");
    QFontDatabase::addApplicationFont(":fonts/ComicNeue-Light-Oblique");
    QFontDatabase::addApplicationFont(":fonts/ComicNeue-Regular");
    QFontDatabase::addApplicationFont(":fonts/ComicNeue-Regular-Oblique");
    QFont::insertSubstitution("Comic Sans MS", "Comic Neue");

    // Dogecoin: Specify Comic Sans as new font
    QFont newFont("Comic Sans MS", 10);

    // Dogecoin: Set new application font
    QApplication::setFont(newFont);

    // Accept D&D of URIs
    setAcceptDrops(true);

    // Create actions for the toolbar, menu bar and tray/dock icon
    // Needs walletFrame to be initialized
    createActions();

    // Create application menu bar
    createMenuBar();

    // Create the toolbars
    createToolBars();

    // Create system tray icon and notification
    createTrayIcon(networkStyle);

    // Create bottom bar
    statusBar();

    // Disable size grip because it looks ugly and nobody needs it
    statusBar()->setSizeGripEnabled(false);

    // Bottom bar notification icons
    QFrame * frameBlocks = new QFrame() ;
    frameBlocks->setContentsMargins( 0, 0, 0, 0 ) ;
    frameBlocks->setSizePolicy( QSizePolicy::Fixed, QSizePolicy::Preferred ) ;
    QHBoxLayout * frameBlocksLayout = new QHBoxLayout( frameBlocks ) ;
    frameBlocksLayout->setContentsMargins( 3, 0, 3, 0 ) ;
    frameBlocksLayout->setSpacing( 3 ) ;
    unitDisplayControl = new UnitDisplayStatusBarControl( platformStyle ) ;
    labelWalletEncryptionIcon = new QLabel();
    labelWalletHDStatusIcon = new QLabel();
    connectionsControl = new GUIUtil::ClickableLabel();
    labelBlocksIcon = new GUIUtil::ClickableLabel();
    generatingLabel = new QLabel() ;
    if ( enableWallet )
    {
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(unitDisplayControl);
        frameBlocksLayout->addStretch();
        frameBlocksLayout->addWidget(labelWalletEncryptionIcon);
        frameBlocksLayout->addWidget(labelWalletHDStatusIcon);
    }
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(connectionsControl);
    frameBlocksLayout->addStretch();
    frameBlocksLayout->addWidget(labelBlocksIcon);
    frameBlocksLayout->addStretch();

    QIcon pawprintIcon = platformStyle->SingleColorIcon( ":/icons/pawprint" ) ;
    generatingLabel->setPixmap( pawprintIcon.pixmap( BOTTOMBAR_ICONSIZE, BOTTOMBAR_ICONSIZE - 1 ) ) ;
    generatingLabel->setToolTip( "Digging is <b>on</b> (0 threads)" ) ;
    generatingLabel->setVisible( false ) ;
    frameBlocksLayout->addWidget( generatingLabel ) ;
    frameBlocksLayout->addSpacing( 3 ) ;

    connect( everySecondTimer.get(), SIGNAL( timeout() ), this, SLOT( updateBottomBarShowsDigging() ) ) ;
    everySecondTimer->start( 1000 /* ms */ ) ;

    // Progress bar and label for blocks download
    progressBarLabel = new QLabel();
    progressBarLabel->setVisible(false);
    progressBar = new GUIUtil::ProgressBar();
    progressBar->setAlignment(Qt::AlignCenter);
    progressBar->setVisible(false);

    // Override style sheet for progress bar for styles that have a segmented progress bar,
    // as they make the text unreadable (workaround for issue #1071)
    // See https://qt-project.org/doc/qt-4.8/gallery.html
    QString curStyle = QApplication::style()->metaObject()->className();
    if(curStyle == "QWindowsStyle" || curStyle == "QWindowsXPStyle")
    {
        progressBar->setStyleSheet("QProgressBar { background-color: #e8e8e8; border: 1px solid grey; border-radius: 7px; padding: 1px; text-align: center; } QProgressBar::chunk { background: QLinearGradient(x1: 0, y1: 0, x2: 1, y2: 0, stop: 0 #FF8000, stop: 1 orange); border-radius: 7px; margin: 0px; }");
    }

    statusBar()->addWidget(progressBarLabel);
    statusBar()->addWidget(progressBar);
    statusBar()->addPermanentWidget(frameBlocks);

    // Install event filter to be able to catch status tip events (QEvent::StatusTip)
    this->installEventFilter(this);

    // Initially wallet actions should be disabled
    setWalletActionsEnabled(false);

    // Subscribe to notifications from core
    subscribeToCoreSignals();

    connect(connectionsControl, SIGNAL(clicked(QPoint)), this, SLOT(toggleNetworkActive()));

#ifdef ENABLE_WALLET
    if ( enableWallet ) {
        connect( walletFrame, SIGNAL( requestedSyncWarningInfo() ), this, SLOT( showChainsyncOverlay() ) ) ;
        connect( labelBlocksIcon, SIGNAL( clicked(QPoint) ), this, SLOT( showChainsyncOverlay() ) ) ;
        connect( progressBar, SIGNAL( clicked(QPoint) ), this, SLOT( showChainsyncOverlay() ) ) ;
    }
#endif
}

DogecoinGUI::~DogecoinGUI()
{
    // Unsubscribe from notifications from core
    unsubscribeFromCoreSignals();

    GUIUtil::saveWindowGeometry("nWindow", this);
    if(trayIcon) // Hide tray icon, as deleting will let it linger until quit (on Ubuntu)
        trayIcon->hide();
#ifdef Q_OS_MAC
    delete appMenuBar;
    MacDockIconHandler::cleanup();
#endif

    delete rpcConsole;
}

void DogecoinGUI::createActions()
{
    QActionGroup * tabGroup = new QActionGroup( this ) ;

    overviewTabAction = new QAction( platformStyle->SingleColorIcon( ":/icons/overview" ), "Wow", this ) ;
    overviewTabAction->setCheckable( true ) ;
    overviewTabAction->setShortcut( QKeySequence( Qt::ALT + Qt::Key_1 ) ) ;
    tabGroup->addAction( overviewTabAction ) ;

    sendCoinsTabAction = new QAction( platformStyle->SingleColorIcon( ":/icons/send" ), "Such Send", this ) ;
    sendCoinsTabAction->setCheckable( true ) ;
    sendCoinsTabAction->setShortcut( QKeySequence( Qt::ALT + Qt::Key_2 ) ) ;
    tabGroup->addAction( sendCoinsTabAction ) ;

    if ( sendCoinsTabAction != nullptr ) {
        sendCoinsMenuAction = new QAction( platformStyle->TextColorIcon( ":/icons/send" ), sendCoinsTabAction->text(), this ) ;
        sendCoinsMenuAction->setStatusTip( sendCoinsTabAction->statusTip() ) ;
        sendCoinsMenuAction->setToolTip( sendCoinsMenuAction->statusTip() ) ;
    }

    receiveCoinsTabAction = new QAction( platformStyle->SingleColorIcon( ":/icons/receiving_addresses" ), "Much Receive", this ) ;
    receiveCoinsTabAction->setCheckable( true ) ;
    receiveCoinsTabAction->setShortcut( QKeySequence( Qt::ALT + Qt::Key_3 ) ) ;
    tabGroup->addAction( receiveCoinsTabAction ) ;

    if ( receiveCoinsTabAction != nullptr ) {
        receiveCoinsMenuAction = new QAction( platformStyle->TextColorIcon( ":/icons/receiving_addresses" ), receiveCoinsTabAction->text(), this ) ;
        receiveCoinsMenuAction->setStatusTip( receiveCoinsTabAction->statusTip() ) ;
        receiveCoinsMenuAction->setToolTip( receiveCoinsMenuAction->statusTip() ) ;
    }

    digTabAction = new QAction( platformStyle->SingleColorIcon( ":/icons/dig" ), "Dig", this ) ;
    digTabAction->setCheckable( true ) ;
    digTabAction->setShortcut( QKeySequence( Qt::ALT + Qt::Key_4 ) ) ;
    tabGroup->addAction( digTabAction ) ;

    historyTabAction = new QAction( platformStyle->SingleColorIcon( ":/icons/history" ), tr( "Transactions" ), this ) ;
    historyTabAction->setCheckable( true ) ;
    historyTabAction->setShortcut( QKeySequence( Qt::ALT + Qt::Key_5 ) ) ;
    tabGroup->addAction( historyTabAction ) ;

#ifdef ENABLE_WALLET
    // These showNormalIfMinimized are needed because Send Coins and Receive Coins
    // can be triggered from the tray menu, and need to show the GUI to be useful
    connect( overviewTabAction, SIGNAL( triggered() ), this, SLOT( showNormalIfMinimized() ) ) ;
    connect( overviewTabAction, SIGNAL( triggered() ), this, SLOT( gotoOverviewPage() ) ) ;
    connect( sendCoinsTabAction, SIGNAL( triggered() ), this, SLOT( showNormalIfMinimized() ) ) ;
    connect( sendCoinsTabAction, SIGNAL( triggered() ), this, SLOT( gotoSendCoinsPage() ) ) ;
    connect(sendCoinsMenuAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(sendCoinsMenuAction, SIGNAL(triggered()), this, SLOT(gotoSendCoinsPage()));
    connect( receiveCoinsTabAction, SIGNAL( triggered() ), this, SLOT( showNormalIfMinimized() ) ) ;
    connect( receiveCoinsTabAction, SIGNAL( triggered() ), this, SLOT( gotoReceiveCoinsPage() ) ) ;
    connect(receiveCoinsMenuAction, SIGNAL(triggered()), this, SLOT(showNormalIfMinimized()));
    connect(receiveCoinsMenuAction, SIGNAL(triggered()), this, SLOT(gotoReceiveCoinsPage()));
    connect( digTabAction, SIGNAL( triggered() ), this, SLOT( showNormalIfMinimized() ) ) ;
    connect( digTabAction, SIGNAL( triggered() ), this, SLOT( gotoDigPage() ) ) ;
    connect( historyTabAction, SIGNAL( triggered() ), this, SLOT( showNormalIfMinimized() ) ) ;
    connect( historyTabAction, SIGNAL( triggered() ), this, SLOT( gotoHistoryPage() ) ) ;
#endif // ENABLE_WALLET

    quitAction = new QAction(platformStyle->TextColorIcon(":/icons/quit"), tr("E&xit"), this);
    quitAction->setStatusTip(tr("Quit application"));
    quitAction->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_Q));
    quitAction->setMenuRole(QAction::QuitRole);
    aboutAction = new QAction(platformStyle->TextColorIcon(":/icons/about"), tr("&About %1").arg(tr(PACKAGE_NAME)), this);
    aboutAction->setStatusTip(tr("Show information about %1").arg(tr(PACKAGE_NAME)));
    aboutAction->setMenuRole(QAction::AboutRole);
    aboutAction->setEnabled(false);
    aboutQtAction = new QAction(platformStyle->TextColorIcon(":/icons/about_qt"), tr("About &Qt"), this);
    aboutQtAction->setStatusTip(tr("Show information about Qt"));
    aboutQtAction->setMenuRole(QAction::AboutQtRole);
    optionsAction = new QAction(platformStyle->TextColorIcon(":/icons/options"), tr("&Options..."), this);
    optionsAction->setStatusTip(tr("Modify configuration options for %1").arg(tr(PACKAGE_NAME)));
    optionsAction->setMenuRole(QAction::PreferencesRole);
    optionsAction->setEnabled(false);
    toggleHideAction = new QAction(platformStyle->TextColorIcon(":/icons/about"), tr("&Show / Hide"), this);
    toggleHideAction->setStatusTip(tr("Show or hide the main Window"));

    encryptWalletAction = new QAction(platformStyle->TextColorIcon(":/icons/lock_closed"), tr("&Encrypt Wallet..."), this);
    encryptWalletAction->setStatusTip(tr("Encrypt the private keys that belong to your wallet"));
    encryptWalletAction->setCheckable(true);
    backupWalletAction = new QAction(platformStyle->TextColorIcon(":/icons/filesave"), tr("&Backup Wallet..."), this);
    backupWalletAction->setStatusTip(tr("Backup wallet to another location"));
    changePassphraseAction = new QAction(platformStyle->TextColorIcon(":/icons/key"), tr("&Change Passphrase..."), this);
    changePassphraseAction->setStatusTip(tr("Change the passphrase used for wallet encryption"));
    signMessageAction = new QAction(platformStyle->TextColorIcon(":/icons/edit"), tr("Sign &message..."), this);
    signMessageAction->setStatusTip(tr("Sign messages with your Dogecoin addresses to prove you own them"));
    verifyMessageAction = new QAction(platformStyle->TextColorIcon(":/icons/verify"), tr("&Verify message..."), this);
    verifyMessageAction->setStatusTip(tr("Verify messages to ensure they were signed with specified Dogecoin addresses"));
    paperWalletAction = new QAction(QIcon(":/icons/print"), tr("&Print paper wallets"), this);
    paperWalletAction->setStatusTip(tr("Print paper wallets"));

    openRPCConsoleAction = new QAction(platformStyle->TextColorIcon(":/icons/debugwindow"), tr("&Debug window"), this);
    openRPCConsoleAction->setStatusTip(tr("Open debugging and diagnostic console"));
    // initially disable the debug window menu item
    openRPCConsoleAction->setEnabled(false);

    usedSendingAddressesAction = new QAction(platformStyle->TextColorIcon(":/icons/address-book"), tr("&Such sending addresses..."), this);
    usedSendingAddressesAction->setStatusTip(tr("Show the list of used sending addresses and labels"));
    usedReceivingAddressesAction = new QAction(platformStyle->TextColorIcon(":/icons/address-book"), tr("&Much receiving addresses..."), this);
    usedReceivingAddressesAction->setStatusTip(tr("Show the list of used receiving addresses and labels"));

    openAction = new QAction(platformStyle->TextColorIcon(":/icons/open"), tr("Open &URI..."), this);
    openAction->setStatusTip(tr("Open a dogecoin: URI or payment request"));

    showHelpMessageAction = new QAction(platformStyle->TextColorIcon(":/icons/info"), tr("&Command-line options"), this);
    showHelpMessageAction->setMenuRole(QAction::NoRole);
    showHelpMessageAction->setStatusTip(tr("Show the %1 help message to get a list with possible Dogecoin command-line options").arg(tr(PACKAGE_NAME)));

    connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
    connect(aboutAction, SIGNAL(triggered()), this, SLOT(aboutClicked()));
    connect(aboutQtAction, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
    connect(optionsAction, SIGNAL(triggered()), this, SLOT(optionsClicked()));
    connect(toggleHideAction, SIGNAL(triggered()), this, SLOT(toggleHidden()));
    connect(showHelpMessageAction, SIGNAL(triggered()), this, SLOT(showHelpMessageClicked()));
    connect(openRPCConsoleAction, SIGNAL(triggered()), this, SLOT(showDebugWindow()));
    // prevents an open debug window from becoming stuck/unusable on shutdown
    connect(quitAction, SIGNAL(triggered()), rpcConsole, SLOT(hide()));

#ifdef ENABLE_WALLET
    if(walletFrame)
    {
        connect(encryptWalletAction, SIGNAL(triggered(bool)), walletFrame, SLOT(encryptWallet(bool)));
        connect(backupWalletAction, SIGNAL(triggered()), walletFrame, SLOT(backupWallet()));
        connect(changePassphraseAction, SIGNAL(triggered()), walletFrame, SLOT(changePassphrase()));
        connect(signMessageAction, SIGNAL(triggered()), this, SLOT(gotoSignMessageTab()));
        connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(gotoVerifyMessageTab()));
        connect(usedSendingAddressesAction, SIGNAL(triggered()), walletFrame, SLOT(usedSendingAddresses()));
        connect(usedReceivingAddressesAction, SIGNAL(triggered()), walletFrame, SLOT(usedReceivingAddresses()));
        connect(openAction, SIGNAL(triggered()), this, SLOT(openClicked()));
        connect(paperWalletAction, SIGNAL(triggered()), walletFrame, SLOT(printPaperWallets()));
    }
#endif // ENABLE_WALLET

    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C), this, SLOT(showDebugWindowActivateConsole()));
    new QShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_D), this, SLOT(showDebugWindow()));
}

void DogecoinGUI::createMenuBar()
{
#ifdef Q_OS_MAC
    // Create a decoupled menu bar on Mac which stays even if the window is closed
    appMenuBar = new QMenuBar();
#else
    // Get the main window's menu bar on other platforms
    appMenuBar = menuBar();
#endif

    // Configure the menus
    QMenu *file = appMenuBar->addMenu(tr("&File"));
    if(walletFrame)
    {
        file->addAction(openAction);
        file->addAction(backupWalletAction);
        file->addAction(signMessageAction);
        file->addAction(verifyMessageAction);
        file->addAction(paperWalletAction);
        file->addSeparator();
        file->addAction(usedSendingAddressesAction);
        file->addAction(usedReceivingAddressesAction);
        file->addSeparator();
    }
    file->addAction(quitAction);

    QMenu *settings = appMenuBar->addMenu(tr("&Settings"));
    if(walletFrame)
    {
        settings->addAction(encryptWalletAction);
        settings->addAction(changePassphraseAction);
        settings->addSeparator();
    }
    settings->addAction(optionsAction);

    QMenu *help = appMenuBar->addMenu(tr("&Help"));
    if(walletFrame)
    {
        help->addAction(openRPCConsoleAction);
    }
    help->addAction(showHelpMessageAction);
    help->addSeparator();
    help->addAction(aboutAction);
    help->addAction(aboutQtAction);
}

void DogecoinGUI::createToolBars()
{
    if ( walletFrame != nullptr )
    {
        QToolBar * toolbar = addToolBar( tr("Tabs toolbar") ) ;
        toolbar->setMovable( false ) ;
        toolbar->setToolButtonStyle( Qt::ToolButtonTextBesideIcon ) ;

        assert( overviewTabAction != nullptr ) ;
        toolbar->addAction( overviewTabAction ) ;
        overviewTabAction->setChecked( true ) ;

        if ( sendCoinsTabAction != nullptr ) toolbar->addAction( sendCoinsTabAction ) ;
        if ( receiveCoinsTabAction != nullptr ) toolbar->addAction( receiveCoinsTabAction ) ;
        if ( digTabAction != nullptr ) toolbar->addAction( digTabAction ) ;
        if ( historyTabAction != nullptr ) toolbar->addAction( historyTabAction ) ;
    }
}

void DogecoinGUI::setNetworkModel( NetworkModel * model )
{
    this->networkModel = model ;
    if ( model != nullptr )
    {
        // Create system tray menu (or setup the dock menu) that late to prevent users from calling actions,
        // while the peer has not yet fully loaded
        createTrayIconMenu();

        // Keep up to date with the peer
        updateNetworkState() ;
        connect( model, SIGNAL( numConnectionsChanged(int) ), this, SLOT( setNumConnections(int) ) ) ;
        connect( model, SIGNAL( networkActiveChanged(bool) ), this, SLOT( setNetworkActive(bool) ) ) ;

        if ( chainsyncOverlay == nullptr ) chainsyncOverlay.reset( new ChainSyncOverlay( this->centralWidget() ) ) ;
        chainsyncOverlay->setKnownBestHeight( model->getHeaderTipHeight(), QDateTime::fromTime_t( model->getHeaderTipTime() ) ) ;
        setNumBlocks( model->getNumBlocks(), model->getLastBlockDate(), model->getVerificationProgress(), false ) ;
        connect( model, SIGNAL( numBlocksChanged(int, QDateTime, double, bool) ), this, SLOT( setNumBlocks(int, QDateTime, double, bool) ) ) ;

        // Receive and report messages from network model
        connect( model, SIGNAL( message(QString, QString, unsigned int) ), this, SLOT( message(QString, QString, unsigned int) ) ) ;

        // Show progress dialog
        connect( model, SIGNAL( showProgress(QString, int) ), this, SLOT( showProgress(QString, int) ) ) ;

        rpcConsole->setNetworkModel( model ) ;
#ifdef ENABLE_WALLET
        if ( walletFrame != nullptr )
        {
            walletFrame->setNetworkModel( model ) ;
        }
#endif
    } else {
        // Disable possibility to show main window via action
        toggleHideAction->setEnabled( false ) ;
        if ( trayIconMenu )
        {
            // Disable context menu on tray icon
            trayIconMenu->clear() ;
        }
        // Propagate cleared model to child objects
        rpcConsole->setNetworkModel( nullptr ) ;
#ifdef ENABLE_WALLET
        if ( walletFrame != nullptr )
        {
            walletFrame->setNetworkModel( nullptr ) ;
        }
#endif // ENABLE_WALLET
    }
}

void DogecoinGUI::setOptionsModel( OptionsModel * model )
{
    this->optionsModel = model ;

    unitDisplayControl->setOptionsModel( model ) ;

    if ( model != nullptr )
    {
        connect( model, SIGNAL( hideTrayIconChanged(bool) ), this, SLOT( setTrayIconVisible(bool) ) ) ;
        setTrayIconVisible( model->getHideTrayIcon() ) ;
    }
}

#ifdef ENABLE_WALLET
bool DogecoinGUI::addWallet(const QString& name, WalletModel *walletModel)
{
    if(!walletFrame)
        return false;
    setWalletActionsEnabled(true);
    return walletFrame->addWallet(name, walletModel);
}

bool DogecoinGUI::setCurrentWallet(const QString& name)
{
    if(!walletFrame)
        return false;
    return walletFrame->setCurrentWallet(name);
}

void DogecoinGUI::removeAllWallets()
{
    if(!walletFrame)
        return;
    setWalletActionsEnabled(false);
    walletFrame->removeAllWallets();
}
#endif // ENABLE_WALLET

void DogecoinGUI::setWalletActionsEnabled( bool enabled )
{
    assert( overviewTabAction != nullptr ) ;
    overviewTabAction->setEnabled( enabled ) ;
    if ( sendCoinsTabAction != nullptr ) sendCoinsTabAction->setEnabled( enabled ) ;
    if ( sendCoinsMenuAction != nullptr ) sendCoinsMenuAction->setEnabled( enabled ) ;
    if ( receiveCoinsTabAction != nullptr ) receiveCoinsTabAction->setEnabled( enabled ) ;
    if ( receiveCoinsMenuAction != nullptr ) receiveCoinsMenuAction->setEnabled( enabled ) ;
    if ( digTabAction != nullptr ) digTabAction->setEnabled( enabled ) ;
    if ( historyTabAction != nullptr ) historyTabAction->setEnabled( enabled ) ;
    encryptWalletAction->setEnabled(enabled);
    backupWalletAction->setEnabled(enabled);
    changePassphraseAction->setEnabled(enabled);
    signMessageAction->setEnabled(enabled);
    verifyMessageAction->setEnabled(enabled);
    usedSendingAddressesAction->setEnabled(enabled);
    usedReceivingAddressesAction->setEnabled(enabled);
    openAction->setEnabled(enabled);
    paperWalletAction->setEnabled(enabled);
}

void DogecoinGUI::createTrayIcon(const NetworkStyle *networkStyle)
{
#ifndef Q_OS_MAC
    trayIcon = new QSystemTrayIcon(this);
    QString toolTip = tr(PACKAGE_NAME) + " peer " + networkStyle->getTextToAppendToTitle() ;
    trayIcon->setToolTip(toolTip);
    trayIcon->setIcon(networkStyle->getTrayAndWindowIcon());
    trayIcon->hide();
#endif

    notificator = new Notificator(QApplication::applicationName(), trayIcon, this);
}

void DogecoinGUI::createTrayIconMenu()
{
#ifndef Q_OS_MAC
    // return if trayIcon is unset (only on non-Mac OSes)
    if (!trayIcon)
        return;

    trayIconMenu = new QMenu(this);
    trayIcon->setContextMenu(trayIconMenu);

    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
#else
    // Note: On Mac, the dock icon is used to provide the tray's functionality.
    MacDockIconHandler *dockIconHandler = MacDockIconHandler::instance();
    dockIconHandler->setMainWindow((QMainWindow *)this);
    trayIconMenu = dockIconHandler->dockMenu();
#endif

    // Configuration of the tray icon (or dock icon) icon menu
    trayIconMenu->addAction(toggleHideAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(sendCoinsMenuAction);
    trayIconMenu->addAction(receiveCoinsMenuAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(signMessageAction);
    trayIconMenu->addAction(verifyMessageAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(optionsAction);
    trayIconMenu->addAction(openRPCConsoleAction);
#ifndef Q_OS_MAC // This is built-in on Mac
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);
#endif
}

#ifndef Q_OS_MAC
void DogecoinGUI::trayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if(reason == QSystemTrayIcon::Trigger)
    {
        // Click on system tray icon triggers show/hide of the main window
        toggleHidden();
    }
}
#endif

void DogecoinGUI::optionsClicked()
{
    if ( optionsModel == nullptr ) return ;

    OptionsDialog dlg( this, enableWallet, /* show third party urls option or not */ NameOfChain() == "main" ) ;
    dlg.setOptionsModel( optionsModel ) ;
    dlg.exec();
}

void DogecoinGUI::aboutClicked()
{
    if ( networkModel == nullptr ) return ;

    HelpMessageDialog dlg( this, true ) ;
    dlg.exec() ;
}

void DogecoinGUI::showDebugWindow()
{
    rpcConsole->showNormal() ;
    rpcConsole->show() ;
    rpcConsole->raise() ;
    rpcConsole->activateWindow() ;
}

void DogecoinGUI::showDebugWindowActivateConsole()
{
    rpcConsole->switchToRPCConsoleTab() ;
    showDebugWindow() ;
}

void DogecoinGUI::showHelpMessageClicked()
{
    if ( helpMessageDialog == nullptr )
        helpMessageDialog.reset( new HelpMessageDialog( this, false ) ) ;

    helpMessageDialog->show() ;
}

#ifdef ENABLE_WALLET
void DogecoinGUI::openClicked()
{
    OpenURIDialog dlg(this);
    if(dlg.exec())
    {
        Q_EMIT receivedURI(dlg.getURI());
    }
}

void DogecoinGUI::gotoOverviewPage()
{
    assert( overviewTabAction != nullptr ) ;
    overviewTabAction->setChecked( true ) ;
    if ( walletFrame != nullptr ) walletFrame->gotoOverviewPage() ;
}

void DogecoinGUI::gotoHistoryPage()
{
    if ( historyTabAction != nullptr ) historyTabAction->setChecked( true ) ;
    if ( walletFrame != nullptr ) walletFrame->gotoHistoryPage() ;
}

void DogecoinGUI::gotoReceiveCoinsPage()
{
    if ( receiveCoinsTabAction != nullptr ) receiveCoinsTabAction->setChecked( true ) ;
    if ( walletFrame != nullptr ) walletFrame->gotoReceiveCoinsPage() ;
}

void DogecoinGUI::gotoSendCoinsPage( QString addr )
{
    if ( sendCoinsTabAction != nullptr ) sendCoinsTabAction->setChecked( true ) ;
    if ( walletFrame != nullptr ) walletFrame->gotoSendCoinsPage( addr ) ;
}

void DogecoinGUI::gotoDigPage()
{
    if ( digTabAction != nullptr ) digTabAction->setChecked( true ) ;
    if ( walletFrame != nullptr ) walletFrame->gotoDigPage() ;
}

void DogecoinGUI::gotoSignMessageTab(QString addr)
{
    if (walletFrame) walletFrame->gotoSignMessageTab(addr);
}

void DogecoinGUI::gotoVerifyMessageTab(QString addr)
{
    if (walletFrame) walletFrame->gotoVerifyMessageTab(addr);
}
#endif // ENABLE_WALLET

void DogecoinGUI::updateNetworkState()
{
    int count = networkModel->getNumConnections() ;
    QString icon ;
    switch ( count )
    {
        case 0: icon = ":/icons/connect_0" ; break ;
        case 1: case 2: case 3: icon = ":/icons/connect_1" ; break ;
        case 4: case 5: case 6: icon = ":/icons/connect_2" ; break ;
        case 7: case 8: case 9: icon = ":/icons/connect_3" ; break ;
        default: icon = ":/icons/connect_4" ; break ;
    }

    QString tooltip ;

    if ( networkModel->isNetworkActive() ) {
        tooltip = tr( "%n active connection(s) to Dogecoin network", "", count ) + QString( ".<br>" ) + tr( "Click to switch network activity off" ) ;
    } else {
        tooltip = tr( "Network activity is off" )  + QString( ".<br>" ) + tr( "Click to turn it back on" ) ;
        icon = ":/icons/network_disabled" ;
    }

    // don't word-wrap this tooltip
    tooltip = QString( "<nobr>" ) + tooltip + QString( "</nobr>" ) ;
    connectionsControl->setToolTip( tooltip ) ;

    connectionsControl->setPixmap( platformStyle->SingleColorIcon( icon ).pixmap( BOTTOMBAR_ICONSIZE, BOTTOMBAR_ICONSIZE ) ) ;
}

void DogecoinGUI::setNumConnections(int count)
{
    updateNetworkState();
}

void DogecoinGUI::setNetworkActive(bool networkActive)
{
    updateNetworkState();
}

void DogecoinGUI::updateHeadersSyncProgressLabel()
{
    int64_t headersTipTime = networkModel->getHeaderTipTime() ;
    int headersTipHeight = networkModel->getHeaderTipHeight() ;
    int estHeadersLeft = ( GetTime() - headersTipTime ) / Params().GetConsensus( headersTipHeight ).nPowTargetSpacing ;
    if (estHeadersLeft > HEADER_HEIGHT_DELTA_SYNC)
        progressBarLabel->setText(tr("Syncing Headers (%1%)...").arg(QString::number(100.0 / (headersTipHeight+estHeadersLeft)*headersTipHeight, 'f', 1)));
}

void DogecoinGUI::setNumBlocks( int count, const QDateTime & blockDate, double progress, bool header )
{
    if ( chainsyncOverlay != nullptr ) {
        if ( header )
            chainsyncOverlay->setKnownBestHeight( count, blockDate ) ;
        else
            chainsyncOverlay->tipUpdate( count, blockDate, progress ) ;
    }

    if ( networkModel == nullptr ) return ;

    // Prevent orphan statusbar messages (e.g. hover Quit in main menu, wait until chain-sync starts -> garbled text)
    statusBar()->clearMessage();

    // Acquire current block source
    enum BlockSource blockSource = networkModel->getBlockSource() ;
    switch (blockSource) {
        case BLOCK_SOURCE_NETWORK:
            if ( header ) {
                updateHeadersSyncProgressLabel() ;
                return ;
            }
            progressBarLabel->setText( tr("Synchronizing with network...") ) ;
            updateHeadersSyncProgressLabel() ;
            break ;
        case BLOCK_SOURCE_DISK:
            if ( header ) {
                progressBarLabel->setText( tr("Indexing blocks on disk...") ) ;
            } else {
                progressBarLabel->setText( tr("Processing blocks on disk...") ) ;
            }
            break ;
        case BLOCK_SOURCE_REINDEX:
            progressBarLabel->setText( tr("Reindexing blocks on disk...") ) ;
            break ;
        case BLOCK_SOURCE_NONE:
            if ( header ) {
                return ;
            }
            progressBarLabel->setText( tr("Connecting to peers...") ) ;
            break ;
    }

    QString tooltip;

    QDateTime currentDate = QDateTime::currentDateTime();
    qint64 secs = blockDate.secsTo(currentDate);

    tooltip = tr( "Processed %n blocks of transaction history", "", count ) ;

    // Set icon state: spinning if catching up, tick otherwise
    if ( secs < 90*60 )
    {
        tooltip = tr("Up to date") + QString(".<br>") + tooltip;
        labelBlocksIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/synced").pixmap( BOTTOMBAR_ICONSIZE, BOTTOMBAR_ICONSIZE ));

#ifdef ENABLE_WALLET
        if ( walletFrame != nullptr ) {
            walletFrame->showOutOfSyncWarning( false ) ;
            if ( chainsyncOverlay == nullptr ) chainsyncOverlay.reset( new ChainSyncOverlay( this->centralWidget() ) ) ;
            chainsyncOverlay->showHide( true, true ) ;
        }
#endif

        progressBarLabel->setVisible(false);
        progressBar->setVisible(false);
    }
    else
    {
        QString timeAgoText = GUIUtil::niceTimeOffset( secs ) ;

        progressBarLabel->setVisible( true ) ;
        progressBar->setFormat( tr("%1 behind").arg( timeAgoText ) ) ;
        static const int max_progress = 1000000000 ;
        progressBar->setMaximum( max_progress ) ;
        progressBar->setValue( progress * (double)max_progress + 0.5 ) ;
        progressBar->setVisible( true ) ;

        tooltip = tr("Catching up...") + QString("<br>") + tooltip;
        if(count != prevBlocks)
        {
            labelBlocksIcon->setPixmap(platformStyle->SingleColorIcon(QString(
                ":/movies/spinner-%1").arg(spinnerFrame, 3, 10, QChar('0')))
                .pixmap( BOTTOMBAR_ICONSIZE, BOTTOMBAR_ICONSIZE ));
            spinnerFrame = (spinnerFrame + 1) % SPINNER_FRAMES;
        }
        prevBlocks = count;

#ifdef ENABLE_WALLET
        if ( walletFrame != nullptr )
        {
            walletFrame->showOutOfSyncWarning( true ) ;
            if ( chainsyncOverlay == nullptr ) chainsyncOverlay.reset( new ChainSyncOverlay( this->centralWidget() ) ) ;
            chainsyncOverlay->showHide() ;
        }
#endif

        tooltip += QString( ".<br>" ) ;
        tooltip += tr( "Last received block was generated %1 ago" ).arg( timeAgoText ) ;
        tooltip += QString( ".<br>" ) ;
        tooltip += tr( "Transactions after this will not yet be visible" ) ;
    }

    // don't word-wrap this tooltip
    tooltip = QString("<nobr>") + tooltip + QString("</nobr>");

    labelBlocksIcon->setToolTip(tooltip);
    progressBarLabel->setToolTip(tooltip);
    progressBar->setToolTip(tooltip);
}

void DogecoinGUI::message( const QString & title, const QString & message, unsigned int style, bool * ret )
{
    QString strTitle = "Dogecoin" ;

    // Default to information icon
    int nMBoxIcon = QMessageBox::Information;
    int nNotifyIcon = Notificator::Information;

    QString msgType;

    // Prefer supplied title over style based title
    if (!title.isEmpty()) {
        msgType = title;
    }
    else {
        switch (style) {
        case CClientUIInterface::MSG_ERROR:
            msgType = tr("Error");
            break;
        case CClientUIInterface::MSG_WARNING:
            msgType = tr("Warning");
            break;
        case CClientUIInterface::MSG_INFORMATION:
            msgType = tr("Information");
            break;
        default:
            break;
        }
    }
    // Append to "Dogecoin - "
    if (!msgType.isEmpty())
        strTitle += " - " + msgType;

    // Check for error/warning icon
    if (style & CClientUIInterface::ICON_ERROR) {
        nMBoxIcon = QMessageBox::Critical;
        nNotifyIcon = Notificator::Critical;
    }
    else if (style & CClientUIInterface::ICON_WARNING) {
        nMBoxIcon = QMessageBox::Warning;
        nNotifyIcon = Notificator::Warning;
    }

    // Display message
    if (style & CClientUIInterface::MODAL) {
        // Check for buttons, use OK as default, if none was supplied
        QMessageBox::StandardButton buttons;
        if (!(buttons = (QMessageBox::StandardButton)(style & CClientUIInterface::BTN_MASK)))
            buttons = QMessageBox::Ok;

        showNormalIfMinimized();
        QMessageBox mBox((QMessageBox::Icon)nMBoxIcon, strTitle, message, buttons, this);
        int r = mBox.exec();
        if (ret != NULL)
            *ret = r == QMessageBox::Ok;
    }
    else
        notificator->notify((Notificator::Class)nNotifyIcon, strTitle, message);
}

void DogecoinGUI::changeEvent( QEvent * e )
{
    QMainWindow::changeEvent( e ) ;

#ifndef Q_OS_MAC // Ignored on Mac
    if ( e->type() == QEvent::WindowStateChange )
    {
        if ( optionsModel && optionsModel->getMinimizeToTray() )
        {
            QWindowStateChangeEvent *wsevt = static_cast<QWindowStateChangeEvent*>(e);
            if(!(wsevt->oldState() & Qt::WindowMinimized) && isMinimized())
            {
                QTimer::singleShot(0, this, SLOT(hide()));
                e->ignore();
            }
        }
    }
#endif
}

void DogecoinGUI::closeEvent( QCloseEvent * event )
{
#ifdef Q_OS_MAC // "minimize on close" is ignored on Mac
    bool minimizeOnClose = false ;
#else
    bool minimizeOnClose = ( optionsModel && optionsModel->getMinimizeOnClose() ) ;
#endif
    if ( ! minimizeOnClose )
    {
        QMessageBox::StandardButton reply ;
        reply = QMessageBox::question( this, tr("Are you sure?"), tr("Really quit?"), QMessageBox::Yes | QMessageBox::No ) ;
        if ( reply != QMessageBox::Yes )
        {
            event->ignore() ;
            return ;
        }
    }

#ifndef Q_OS_MAC // ignored on Mac
    if ( minimizeOnClose )
    {
        QMainWindow::showMinimized() ;
        event->ignore() ;
        return ;
    }

    // close rpcConsole in case it was open to make some space for the shutdown window
    rpcConsole->close() ;

    QApplication::quit() ;
#else
    QMainWindow::closeEvent( event ) ;
#endif
}

void DogecoinGUI::showEvent(QShowEvent *event)
{
    // enable the debug window when the main window shows up
    openRPCConsoleAction->setEnabled(true);
    aboutAction->setEnabled(true);
    optionsAction->setEnabled(true);
}

#ifdef ENABLE_WALLET
void DogecoinGUI::incomingTransaction(const QString& date, int unit, const CAmount& amount, const QString& type, const QString& address, const QString& label)
{
    // On new transaction, make an info balloon
    QString msg = tr("Date: %1\n").arg(date) +
                  tr("Amount: %1\n").arg( UnitsOfCoin::formatWithUnit( unit, amount, true ) ) +
                  tr("Type: %1\n").arg(type);
    if (!label.isEmpty())
        msg += tr("Label: %1\n").arg(label);
    else if (!address.isEmpty())
        msg += tr("Address: %1\n").arg(address);
    message((amount)<0 ? tr("Sent transaction") : tr("Incoming transaction"),
             msg, CClientUIInterface::MSG_INFORMATION);
}
#endif // ENABLE_WALLET

void DogecoinGUI::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept only URIs
    if(event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void DogecoinGUI::dropEvent(QDropEvent *event)
{
    if(event->mimeData()->hasUrls())
    {
        Q_FOREACH(const QUrl &uri, event->mimeData()->urls())
        {
            Q_EMIT receivedURI(uri.toString());
        }
    }
    event->acceptProposedAction();
}

bool DogecoinGUI::eventFilter(QObject *object, QEvent *event)
{
    // Catch status tip events
    if (event->type() == QEvent::StatusTip)
    {
        // Prevent adding text from setStatusTip(), if we currently use the status bar for displaying other stuff
        if (progressBarLabel->isVisible() || progressBar->isVisible())
            return true;
    }
    return QMainWindow::eventFilter(object, event);
}

#ifdef ENABLE_WALLET
bool DogecoinGUI::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    // URI has to be valid
    if (walletFrame && walletFrame->handlePaymentRequest(recipient))
    {
        showNormalIfMinimized();
        gotoSendCoinsPage();
        return true;
    }
    return false;
}

void DogecoinGUI::setHDStatus(int hdEnabled)
{
    labelWalletHDStatusIcon->setPixmap(platformStyle->SingleColorIcon(hdEnabled ? ":/icons/hd_enabled" : ":/icons/hd_disabled").pixmap( BOTTOMBAR_ICONSIZE, BOTTOMBAR_ICONSIZE ));
    labelWalletHDStatusIcon->setToolTip(hdEnabled ? tr("HD key generation is <b>enabled</b>") : tr("HD key generation is <b>disabled</b>"));

    // eventually disable the QLabel to set its opacity to 50%
    labelWalletHDStatusIcon->setEnabled(hdEnabled);
}

void DogecoinGUI::setEncryptionStatus(int status)
{
    switch(status)
    {
    case WalletModel::Unencrypted:
        labelWalletEncryptionIcon->hide();
        encryptWalletAction->setChecked(false);
        changePassphraseAction->setEnabled(false);
        encryptWalletAction->setEnabled(true);
        break;
    case WalletModel::Unlocked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/lock_open").pixmap( BOTTOMBAR_ICONSIZE, BOTTOMBAR_ICONSIZE ));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>unlocked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    case WalletModel::Locked:
        labelWalletEncryptionIcon->show();
        labelWalletEncryptionIcon->setPixmap(platformStyle->SingleColorIcon(":/icons/lock_closed").pixmap( BOTTOMBAR_ICONSIZE, BOTTOMBAR_ICONSIZE ));
        labelWalletEncryptionIcon->setToolTip(tr("Wallet is <b>encrypted</b> and currently <b>locked</b>"));
        encryptWalletAction->setChecked(true);
        changePassphraseAction->setEnabled(true);
        encryptWalletAction->setEnabled(false); // TODO: decrypt currently not supported
        break;
    }
}
#endif // ENABLE_WALLET

void DogecoinGUI::updateBottomBarShowsDigging()
{
    size_t nThreads = HowManyMiningThreads() ;
    generatingLabel->setVisible( nThreads > 0 ) ;
    generatingLabel->setToolTip(
        QString( "<nobr>") + QString( "Digging is <b>on</b>" ) + QString( "</nobr>")
            + " <nobr>(" + QString::number( nThreads ) + " " + ( nThreads == 1 ? "thread" : "threads" ) + ")</nobr>"
    ) ;

    if ( walletFrame != nullptr ) walletFrame->refreshDigPage() ;
}

void DogecoinGUI::showNormalIfMinimized(bool fToggleHidden)
{
    if ( networkModel == nullptr ) return ;

    // activateWindow() (sometimes) helps with keyboard focus on Windows
    if (isHidden())
    {
        show();
        activateWindow();
    }
    else if (isMinimized())
    {
        showNormal();
        activateWindow();
    }
    else if (GUIUtil::isObscured(this))
    {
        raise();
        activateWindow();
    }
    else if(fToggleHidden)
        hide();
}

void DogecoinGUI::toggleHidden()
{
    showNormalIfMinimized(true);
}

void DogecoinGUI::detectShutdown()
{
    if (ShutdownRequested())
    {
        if(rpcConsole)
            rpcConsole->hide();
        qApp->quit();
    }
}

void DogecoinGUI::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0)
    {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    }
    else if (nProgress == 100)
    {
        if (progressDialog)
        {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    }
    else if (progressDialog)
        progressDialog->setValue(nProgress);
}

void DogecoinGUI::setTrayIconVisible(bool fHideTrayIcon)
{
    if (trayIcon)
    {
        trayIcon->setVisible(!fHideTrayIcon);
    }
}

void DogecoinGUI::showChainsyncOverlay()
{
    if ( chainsyncOverlay == nullptr )
        chainsyncOverlay.reset( new ChainSyncOverlay( this->centralWidget() ) ) ;

    if ( progressBar->isVisible() || chainsyncOverlay->isLayerVisible() )
        chainsyncOverlay->toggleVisibility() ;
}

static bool ThreadSafeMessageBox( DogecoinGUI * gui, const std::string & message, const std::string & caption, unsigned int style )
{
    bool modal = (style & CClientUIInterface::MODAL);
    // The SECURE flag has no effect in the Qt GUI.
    // bool secure = (style & CClientUIInterface::SECURE);
    style &= ~CClientUIInterface::SECURE;
    bool ret = false;
    // In case of modal message, use blocking connection to wait for user to click a button
    QMetaObject::invokeMethod(gui, "message",
                               modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                               Q_ARG(QString, QString::fromStdString(caption)),
                               Q_ARG(QString, QString::fromStdString(message)),
                               Q_ARG(unsigned int, style),
                               Q_ARG(bool*, &ret));
    return ret;
}

void DogecoinGUI::subscribeToCoreSignals()
{
    uiInterface.ThreadSafeMessageBox.connect(boost::bind(ThreadSafeMessageBox, this, _1, _2, _3));
    uiInterface.ThreadSafeQuestion.connect(boost::bind(ThreadSafeMessageBox, this, _1, _3, _4));
}

void DogecoinGUI::unsubscribeFromCoreSignals()
{
    uiInterface.ThreadSafeMessageBox.disconnect(boost::bind(ThreadSafeMessageBox, this, _1, _2, _3));
    uiInterface.ThreadSafeQuestion.disconnect(boost::bind(ThreadSafeMessageBox, this, _1, _3, _4));
}

void DogecoinGUI::toggleNetworkActive()
{
    if ( networkModel != nullptr )
        networkModel->setNetworkActive( ! networkModel->isNetworkActive() ) ;
}

UnitDisplayStatusBarControl::UnitDisplayStatusBarControl(const PlatformStyle *platformStyle) :
    optionsModel( nullptr ),
    menu(0)
{
    createContextMenu();
    setToolTip( tr("Unit to show amounts in. Click to choose another unit") ) ;
    QList< UnitsOfCoin::Unit > units = UnitsOfCoin::availableUnits() ;
    int max_width = 0;
    const QFontMetrics fm(font());
    Q_FOREACH (const UnitsOfCoin::Unit unit, units)
    {
        max_width = qMax(max_width, fm.width( UnitsOfCoin::name( unit ) )) ;
    }
    setMinimumSize(max_width, 0);
    setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    setStyleSheet(QString("QLabel { color : %1 }").arg(platformStyle->SingleColor().name()));
}

/** So that it responds to button clicks */
void UnitDisplayStatusBarControl::mousePressEvent(QMouseEvent *event)
{
    onDisplayUnitsClicked(event->pos());
}

/** Creates context menu, its actions, and wires up all the relevant signals for mouse events. */
void UnitDisplayStatusBarControl::createContextMenu()
{
    menu = new QMenu(this);
    Q_FOREACH( UnitsOfCoin::Unit u, UnitsOfCoin::availableUnits() )
    {
        QAction *menuAction = new QAction( QString( UnitsOfCoin::name( u ) ), this ) ;
        menuAction->setData(QVariant(u));
        menu->addAction(menuAction);
    }
    connect(menu,SIGNAL(triggered(QAction*)),this,SLOT(onMenuSelection(QAction*)));
}

/** Lets the control know about the Options Model (and its signals) */
void UnitDisplayStatusBarControl::setOptionsModel( OptionsModel * model )
{
    this->optionsModel = model ;
    if ( model != nullptr )
    {
        connect( model, SIGNAL( displayUnitChanged(int) ), this, SLOT( updateDisplayUnit(int) ) ) ;
        updateDisplayUnit( model->getDisplayUnit() ) ;
    }
}

/** When Display Units are changed on OptionsModel it will refresh the display text of the control on the status bar */
void UnitDisplayStatusBarControl::updateDisplayUnit(int newUnits)
{
    setText( UnitsOfCoin::name( newUnits ) ) ;
}

/** Shows context menu with Display Unit options by the mouse coordinates */
void UnitDisplayStatusBarControl::onDisplayUnitsClicked(const QPoint& point)
{
    QPoint globalPos = mapToGlobal(point);
    menu->exec(globalPos);
}

/** Tells underlying optionsModel to update its current display unit */
void UnitDisplayStatusBarControl::onMenuSelection( QAction * action )
{
    if ( action && optionsModel )
    {
        optionsModel->setDisplayUnit( action->data() ) ;
    }
}
