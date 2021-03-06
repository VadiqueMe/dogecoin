// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#if defined(HAVE_CONFIG_H)
#include "config/dogecoin-config.h"
#endif

#include "gui.h"

#include "chainparams.h"
#include "chainparamsutil.h"
#include "networkmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "intro.h"
#include "networkstyle.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "splashscreen.h"
#include "utilitydialog.h"
#include "winshutdownmonitor.h"

#ifdef ENABLE_WALLET
#include "paymentserver.h"
#include "walletmodel.h"
#endif

#include "init.h"
#include "rpc/server.h"
#include "scheduler.h"
#include "ui_interface.h"
#include "util.h"
#include "utillog.h"
#include "utilstr.h"
#include "utilthread.h"
#include "warnings.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <stdint.h>

#include <boost/filesystem/operations.hpp>

#include <QApplication>
#include <QDebug>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QSslConfiguration>

#if defined(QT_STATICPLUGIN)
#include <QtPlugin>
#if QT_VERSION < 0x050000
Q_IMPORT_PLUGIN(qcncodecs)
Q_IMPORT_PLUGIN(qjpcodecs)
Q_IMPORT_PLUGIN(qtwcodecs)
Q_IMPORT_PLUGIN(qkrcodecs)
Q_IMPORT_PLUGIN(qtaccessiblewidgets)
#else
#if QT_VERSION < 0x050400
Q_IMPORT_PLUGIN(AccessibleFactory)
#endif
#if defined(QT_QPA_PLATFORM_XCB)
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin);
#elif defined(QT_QPA_PLATFORM_WINDOWS)
Q_IMPORT_PLUGIN(QMinimalIntegrationPlugin);
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
Q_IMPORT_PLUGIN(QWindowsPrinterSupportPlugin);
#elif defined(QT_QPA_PLATFORM_COCOA)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin);
#endif
#endif
#endif

#if QT_VERSION < 0x050000
#include <QTextCodec>
#endif

// Declare meta types used for QMetaObject::invokeMethod
Q_DECLARE_METATYPE(bool*)
Q_DECLARE_METATYPE(CAmount)

static void InitMessage( const std::string & message )
{
    LogPrintf( "init message: %s\n", message ) ;
}

/*
   Translate string to current locale using Qt
 */
static std::string Translate( const char * psz )
{
    return QCoreApplication::translate( "dogecoin-core", psz ).toStdString() ;
}

static QString GetLangTerritory()
{
    QSettings settings;
    // Get desired locale (e.g. "es_ES")
    // 1) System default language
    QString lang_territory = QLocale::system().name();
    // 2) Language from QSettings
    QString lang_territory_qsettings = settings.value("language", "").toString();
    if(!lang_territory_qsettings.isEmpty())
        lang_territory = lang_territory_qsettings;
    // 3) -lang command line argument
    lang_territory = QString::fromStdString(GetArg("-lang", lang_territory.toStdString()));
    return lang_territory;
}

/** Set up translations */
static void initTranslations(QTranslator &qtTranslatorBase, QTranslator &qtTranslator, QTranslator &translatorBase, QTranslator &translator)
{
    // Remove old translators
    QApplication::removeTranslator(&qtTranslatorBase);
    QApplication::removeTranslator(&qtTranslator);
    QApplication::removeTranslator(&translatorBase);
    QApplication::removeTranslator(&translator);

    // Get desired locale (e.g. "de_DE")
    // 1) System default language
    QString lang_territory = GetLangTerritory();

    // Convert to "de" only by truncating "_DE"
    QString lang = lang_territory;
    lang.truncate(lang_territory.lastIndexOf('_'));

    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator

    // Load e.g. qt_fr.qm
    if (qtTranslatorBase.load("qt_" + lang, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslatorBase);

    // Load e.g. qt_fr_FR.qm
    if (qtTranslator.load("qt_" + lang_territory, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qtTranslator);

    // Load e.g. dogecoin_pt.qm (shortcut "pt" needs to be defined in dogecoin.qrc)
    if (translatorBase.load(lang, ":/translations/"))
        QApplication::installTranslator(&translatorBase);

    // Load e.g. dogecoin_pt_PT.qm (shortcut "pt_PT" needs to be defined in dogecoin.qrc)
    if (translator.load(lang_territory, ":/translations/"))
        QApplication::installTranslator(&translator);
}

/* qDebug() message handler --> debug log */
#if QT_VERSION < 0x050000
void DebugMessageHandler( QtMsgType type, const char * msg )
{
    std::string category = ( type == QtDebugMsg ) ? "qt" : "" ;
    LogPrint( category, "Qt: %s\n", msg ) ;
}
#else
void DebugMessageHandler( QtMsgType type, const QMessageLogContext & context, const QString & msg )
{
    Q_UNUSED( context ) ;
    std::string category = ( type == QtDebugMsg ) ? "qt" : "" ;
    LogPrint( category, "Qt: %s\n", msg.toStdString() ) ;
}
#endif

/* Class encapsulating Dogecoin Core startup and shutdown.
 * Allows running startup and shutdown in a different thread from the UI thread
 */
class DogecoinCore: public QObject
{
    Q_OBJECT

public:
    explicit DogecoinCore() ;

public Q_SLOTS:
    void initialize();
    void shutdown();

Q_SIGNALS:
    void initializeResult(int retval);
    void shutdownResult(int retval);
    void runawayException(const QString &message);

private:
    std::vector< std::thread > threadGroup ;
    CScheduler scheduler ;

    /// Pass fatal exception message to UI thread
    void handleRunawayException(const std::exception *e);
};

/** Main Dogecoin application object */
class DogecoinApplication: public QApplication
{
    Q_OBJECT
public:
    explicit DogecoinApplication( int & argc, char ** argv ) ;
    ~DogecoinApplication() ;

#ifdef ENABLE_WALLET
    /// Create payment server
    void createPaymentServer();
#endif

    /// Create options model
    void createOptionsModel(bool resetSettings);
    /// Create main window
    void createWindow(const NetworkStyle *networkStyle);
    /// Create splash screen
    void createSplashScreen(const NetworkStyle *networkStyle);

    /// Request core initialization
    void requestInitialize();
    /// Request core shutdown
    void requestShutdown();

    /// Get process return value
    int getReturnValue() { return returnValue; }

    /// Get window identifier of QMainWindow (DogecoinGUI)
    WId getMainWinId() const;

public Q_SLOTS:
    void initializeResult(int retval);
    void shutdownResult(int retval);
    /// Handle runaway exceptions. Shows a message box with the problem and quits the program.
    void handleRunawayException(const QString &message);

Q_SIGNALS:
    void requestedInitialize();
    void requestedShutdown();
    void stopThread();
    void splashFinished(QWidget *window);

private:
    QThread * coreThread ;
    OptionsModel * optionsModel ;
    NetworkModel * networkModel ;
    DogecoinGUI * guiWindow ;
    QTimer * pollShutdownTimer ;
#ifdef ENABLE_WALLET
    PaymentServer* paymentServer;
    WalletModel *walletModel;
#endif
    int returnValue;
    const PlatformStyle *platformStyle;
    std::unique_ptr<QWidget> shutdownWindow;

    void startThread();
};

#include "dogecoin.moc"

DogecoinCore::DogecoinCore():
    QObject()
{
}

void DogecoinCore::handleRunawayException( const std::exception * e )
{
    PrintExceptionContinue(e, "Runaway exception");
    Q_EMIT runawayException(QString::fromStdString(GetWarnings("gui")));
}

void DogecoinCore::initialize()
{
    try
    {
        qDebug() << __func__ << ": Running AppInit2 in thread";
        if (!AppInitBasicSetup())
        {
            Q_EMIT initializeResult(false);
            return;
        }
        if (!AppInitParameterInteraction())
        {
            Q_EMIT initializeResult(false);
            return;
        }
        if (!AppInitSanityChecks())
        {
            Q_EMIT initializeResult(false);
            return;
        }

        int rv = AppInitMain( threadGroup, scheduler ) ;
        Q_EMIT initializeResult( rv ) ;
    } catch ( const std::exception & e ) {
        handleRunawayException( &e ) ;
    } catch ( ... ) {
        handleRunawayException( nullptr ) ;
    }
}

void DogecoinCore::shutdown()
{
    try
    {
        qDebug() << __func__ << ": Running Shutdown in thread";
        StopAndJoinThreads( threadGroup ) ;
        Shutdown() ;
        qDebug() << __func__ << ": Shutdown finished";
        Q_EMIT shutdownResult(1);
    } catch (const std::exception& e) {
        handleRunawayException(&e);
    } catch (...) {
        handleRunawayException(NULL);
    }
}

DogecoinApplication::DogecoinApplication( int & argc, char ** argv ) :
    QApplication(argc, argv),
    coreThread(0),
    optionsModel( nullptr ),
    networkModel( nullptr ),
    guiWindow( nullptr ),
    pollShutdownTimer(0),
#ifdef ENABLE_WALLET
    paymentServer(0),
    walletModel(0),
#endif
    returnValue(0)
{
    setQuitOnLastWindowClosed(false);

    // UI per-platform customization
    // This must be done inside the DogecoinApplication constructor, or after it, because
    // PlatformStyle::instantiate requires a QApplication
    std::string platformName ;
    platformName = GetArg( "-uiplatform", DogecoinGUI::DEFAULT_UIPLATFORM ) ;
    platformStyle = PlatformStyle::instantiate( QString::fromStdString( platformName ) ) ;
    if (!platformStyle) // Fall back to "other" if specified name not found
        platformStyle = PlatformStyle::instantiate("other");
    assert(platformStyle);
}

DogecoinApplication::~DogecoinApplication()
{
    if(coreThread)
    {
        qDebug() << __func__ << ": Stopping thread";
        Q_EMIT stopThread();
        coreThread->wait();
        qDebug() << __func__ << ": Stopped thread";
    }

    delete guiWindow ;
    guiWindow = nullptr ;

#ifdef ENABLE_WALLET
    delete paymentServer;
    paymentServer = 0;
#endif

    delete optionsModel ;
    optionsModel = nullptr ;

    delete networkModel ;
    networkModel = nullptr ;

    delete platformStyle;
    platformStyle = 0;
}

#ifdef ENABLE_WALLET
void DogecoinApplication::createPaymentServer()
{
    paymentServer = new PaymentServer( this ) ;
}
#endif

void DogecoinApplication::createOptionsModel( bool resetSettings )
{
    optionsModel = new OptionsModel( NULL, resetSettings ) ;
}

void DogecoinApplication::createWindow( const NetworkStyle * networkStyle )
{
    guiWindow = new DogecoinGUI( platformStyle, networkStyle ) ;

    pollShutdownTimer = new QTimer( guiWindow ) ;
    connect( pollShutdownTimer, SIGNAL( timeout() ), guiWindow, SLOT( detectShutdown() ) ) ;
    pollShutdownTimer->start( 200 ) ;
}

void DogecoinApplication::createSplashScreen( const NetworkStyle * networkStyle )
{
    SplashScreen * splash = new SplashScreen( 0, networkStyle ) ;
    // We don't hold a direct pointer to the splash screen after creation, but the splash
    // screen will take care of deleting itself when slotFinish happens
    splash->show();
    connect(this, SIGNAL(splashFinished(QWidget*)), splash, SLOT(slotFinish(QWidget*)));
    connect(this, SIGNAL(requestedShutdown()), splash, SLOT(close()));
}

void DogecoinApplication::startThread()
{
    if ( coreThread != nullptr ) return ;
    coreThread = new QThread( this ) ;
    DogecoinCore * core = new DogecoinCore() ;
    core->moveToThread( coreThread ) ;

    /* communication to and from thread */
    connect( core, SIGNAL( initializeResult(int) ), this, SLOT( initializeResult(int) ) ) ;
    connect( core, SIGNAL( shutdownResult(int) ), this, SLOT( shutdownResult(int) ) ) ;
    connect( core, SIGNAL( runawayException(QString) ), this, SLOT( handleRunawayException(QString) ) ) ;
    connect( this, SIGNAL( requestedInitialize() ), core, SLOT( initialize() ) ) ;
    connect( this, SIGNAL( requestedShutdown() ), core, SLOT( shutdown() ) ) ;
    /*  make sure core object is deleted in its own thread */
    connect( this, SIGNAL( stopThread() ), core, SLOT( deleteLater() ) ) ;
    connect( this, SIGNAL( stopThread() ), coreThread, SLOT( quit() ) ) ;

    coreThread->start() ;
}

void DogecoinApplication::requestInitialize()
{
    qDebug() << __func__ << ": Requesting initialize";
    startThread();
    Q_EMIT requestedInitialize();
}

void DogecoinApplication::requestShutdown()
{
    // Show a simple window indicating shutdown status
    // Do this first as some of the steps may take some time below,
    // for example the RPC console may still be running some command
    shutdownWindow.reset( ShutdownWindow::showShutdownWindow( guiWindow ) ) ;

    qDebug() << __func__ << ": Requesting shutdown";
    startThread();
    guiWindow->hide() ;
    guiWindow->setNetworkModel( nullptr ) ;
    guiWindow->setOptionsModel( nullptr ) ;
    pollShutdownTimer->stop();

#ifdef ENABLE_WALLET
    guiWindow->removeAllWallets() ;
    delete walletModel ;
    walletModel = nullptr ;
#endif
    delete networkModel ;
    networkModel = nullptr ;

    RequestShutdown() ;

    // Request shutdown from core thread
    Q_EMIT requestedShutdown();
}

void DogecoinApplication::initializeResult( int retval )
{
    returnValue = ( retval != 0 ) ? 0 : 1 ;
    if ( retval != 0 )
    {
        // Log this only after AppInit2 finishes, as then logging setup is guaranteed complete
        LogPrintf( "Qt platform customization: %s\n", platformStyle->getName().toStdString() ) ;
#ifdef ENABLE_WALLET
        PaymentServer::LoadRootCAs() ;
        paymentServer->setOptionsModel( optionsModel ) ;
#endif

        networkModel = new NetworkModel( /* parent */ nullptr ) ;

        guiWindow->setNetworkModel( networkModel ) ;
        guiWindow->setOptionsModel( optionsModel ) ;

#ifdef ENABLE_WALLET
        if ( pwalletMain != nullptr )
        {
            walletModel = new WalletModel( platformStyle, pwalletMain, optionsModel ) ;

            guiWindow->addWallet( DogecoinGUI::DEFAULT_WALLET, walletModel ) ;
            guiWindow->setCurrentWallet( DogecoinGUI::DEFAULT_WALLET ) ;

            connect( walletModel, SIGNAL( coinsSent(CWallet*, SendCoinsRecipient, QByteArray) ),
                             paymentServer, SLOT( fetchPaymentACK(CWallet*, const SendCoinsRecipient&, QByteArray) ) ) ;
        }
#endif

        if ( GetBoolArg( "-minimized", false ) )
            guiWindow->showMinimized() ;
        else
            guiWindow->show() ;

        Q_EMIT splashFinished( guiWindow ) ;

#ifdef ENABLE_WALLET
        // Now that initialization/startup is done, process any command-line
        // dogecoin: URIs or payment requests
        connect( paymentServer, SIGNAL( receivedPaymentRequest(SendCoinsRecipient) ),
                         guiWindow, SLOT( handlePaymentRequest(SendCoinsRecipient) ) ) ;
        connect( guiWindow, SIGNAL( receivedURI(QString) ),
                         paymentServer, SLOT( handleURIOrFile(QString) ) ) ;
        connect( paymentServer, SIGNAL( message(QString, QString, unsigned int) ),
                         guiWindow, SLOT( message(QString, QString, unsigned int) ) ) ;
        QTimer::singleShot( 100, paymentServer, SLOT( uiReady() ) ) ;
#endif
    } else {
        quit() ; // exit main loop
    }
}

void DogecoinApplication::shutdownResult( int retval )
{
    quit() ; // exit main loop after shutdown finished
}

void DogecoinApplication::handleRunawayException( const QString & message )
{
    QMessageBox::critical( 0, "Runaway exception", tr("A fatal error occurred. Dogecoin can no longer continue safely and will quit.") + QString("\n\n") + message ) ;
    ::exit(EXIT_FAILURE);
}

WId DogecoinApplication::getMainWinId() const
{
    if ( guiWindow == nullptr ) return 0 ;

    return guiWindow->winId() ;
}

#ifndef DOGECOIN_QT_TEST
int main(int argc, char *argv[])
{
    SetupEnvironment();

    /// 1. Parse command-line options
    ParseParameters( argc, argv ) ;

    // Do not refer to data directory yet, this can be overridden by Intro::pickDataDirectory

    /// 2. Basic Qt initialization (not dependent on parameters or configuration)
#if QT_VERSION < 0x050000
    // Internal string conversion is all UTF-8
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForTr());
#endif

    Q_INIT_RESOURCE( dogecoin ) ;
    Q_INIT_RESOURCE( dogecoin_locale ) ;

#if QT_VERSION >= 0x050600
    QGuiApplication::setAttribute( Qt::AA_EnableHighDpiScaling ) ;
#endif
    DogecoinApplication app( argc, argv ) ;
#if QT_VERSION > 0x050100
    // Generate high-dpi pixmaps
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#ifdef Q_OS_MAC
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);
#endif
#if QT_VERSION >= 0x050500
    // Because of the POODLE attack it is recommended to disable SSLv3 https://disablessl3.com/
    // so set SSL protocols to TLS 1.0+
    QSslConfiguration sslconf = QSslConfiguration::defaultConfiguration() ;
    sslconf.setProtocol( QSsl::TlsV1_0OrLater ) ;
    QSslConfiguration::setDefaultConfiguration( sslconf ) ;
#endif

    // Register meta types used for QMetaObject::invokeMethod
    qRegisterMetaType< bool* >();
    //   Need to pass name here as CAmount is a typedef (see http://qt-project.org/doc/qt-5/qmetatype.html#qRegisterMetaType)
    //   IMPORTANT if it is no longer a typedef use the normal variant above
    qRegisterMetaType< CAmount >("CAmount");

    /// 3. Application identification
    // must be set before OptionsModel is initialized or translations are loaded,
    // as it is used to locate QSettings
    QApplication::setOrganizationName(QAPP_ORG_NAME);
    QApplication::setOrganizationDomain(QAPP_ORG_DOMAIN);
    QApplication::setApplicationName(QAPP_APP_NAME_DEFAULT);
    GUIUtil::SubstituteFonts(GetLangTerritory());

    /// 4. Initialization of translations, so that intro dialog is in user's language
    // Now that QSettings are accessible, initialize translations
    QTranslator qtTranslatorBase, qtTranslator, translatorBase, translator;
    initTranslations(qtTranslatorBase, qtTranslator, translatorBase, translator);
    translationInterface.Translate.connect(Translate);

    /// 5. Now that settings and translations are available, ask user for data directory
    // User language is set up: pick a data directory
    if (!Intro::pickDataDirectory())
        return EXIT_SUCCESS;

    /// 6. Determine availability of data directory and parse dogecoin.conf
    /// - Do not call GetDirForData( true ) before this step finishes
    if ( ! boost::filesystem::is_directory( GetDirForData( false ) ) )
    {
        QMessageBox::critical( 0, QObject::tr( PACKAGE_NAME ),
                               QObject::tr("Error: Specified data directory \"%1\" does not exist.").arg( QString::fromStdString( GetArg( "-datadir", "" ) ) )
                             ) ;
        return EXIT_FAILURE ;
    }

    // do this early
    BeginLogging() ;

    try {
        ReadConfigFile( GetArg( "-conf", DOGECOIN_CONF_FILENAME ) ) ;
    } catch (const std::exception& e) {
        QMessageBox::critical(0, QObject::tr(PACKAGE_NAME),
                              QObject::tr("Error: Cannot parse configuration file: %1. Only use key=value syntax.").arg(e.what()));
        return EXIT_FAILURE;
    }

    /// 7. Select network and switch to network specific options
    // - Params() before this step will result in crash
    // - Do this after parsing the configuration file, as the network can be switched there
    // - QSettings() will use the new application name after this, resulting in network-specific settings
    // - Needs to be done before createOptionsModel

    // Look for chain name parameter
    // Params() work only after this clause
    try {
        SelectParams( ChainNameFromArguments() ) ;
    } catch( std::exception & e ) {
        QMessageBox::critical( 0, QObject::tr(PACKAGE_NAME), QObject::tr("Error: %1").arg( e.what() ) ) ;
        return EXIT_FAILURE ;
    }

#ifdef ENABLE_WALLET
    // Parse URIs on command line -- this can affect Params()
    PaymentServer::ipcParseCommandLine( argc, argv ) ;
#endif

    // Show help message after parsing command-line options (for "-lang") and setting locale,
    // but before showing splash screen
    if ( IsArgSet( "-?" ) || IsArgSet( "-h" ) || IsArgSet( "-help" ) || IsArgSet( "-version" ) )
    {
        HelpMessageDialog help( nullptr, IsArgSet( "-version" ) ) ;
        help.showOrPrint() ;
        return EXIT_SUCCESS ;
    }

    QScopedPointer< const NetworkStyle > networkStyle( NetworkStyle::instantiate(
            QString::fromStdString( NameOfChain() ) )
        ) ;
    assert( ! networkStyle.isNull() ) ;

    QApplication::setApplicationName( networkStyle->getAppName() ) ;
    // Re-initialize translations after changing the name of application
    // (language in chain-specific settings can differ)
    initTranslations( qtTranslatorBase, qtTranslator, translatorBase, translator ) ;

#ifdef ENABLE_WALLET
    /// 8. URI IPC sending
    // - Do this early as we don't want to bother initializing if we are just calling IPC
    // - Do this *after* setting up the data directory, as the data directory hash is used in the name
    // of the server
    // - Do this after creating app and setting up translations, so error messages are translated too
    if (PaymentServer::ipcSendCommandLine())
        exit(EXIT_SUCCESS);

    // Start up the payment server early, too, so impatient users that click on
    // dogecoin: links repeatedly have their payment requests routed to this process:
    app.createPaymentServer();
#endif

    /// 9. Main GUI initialization
    // Install global event filter that makes sure that long tooltips can be word-wrapped
    app.installEventFilter(new GUIUtil::ToolTipToRichTextFilter(TOOLTIP_WRAP_THRESHOLD, &app));
#if QT_VERSION < 0x050000
    // Install qDebug() message handler to route to debug log
    qInstallMsgHandler( DebugMessageHandler ) ;
#else
# if defined(Q_OS_WIN)
    // Install global event filter for processing Windows session related Windows messages (WM_QUERYENDSESSION and WM_ENDSESSION)
    qApp->installNativeEventFilter(new WinShutdownMonitor());
# endif
    // Install qDebug() message handler to route to debug log
    qInstallMessageHandler( DebugMessageHandler ) ;
#endif
    // init parameter interaction before creating the options model
    InitParameterInteraction() ;

    // Load GUI settings from QSettings
    app.createOptionsModel(IsArgSet("-resetguisettings"));

    // Subscribe to global signals from core
    uiInterface.InitMessage.connect(InitMessage);

    if ( GetBoolArg( "-splash", DEFAULT_SPLASHSCREEN ) && ! GetBoolArg( "-minimized", false ) )
        app.createSplashScreen( networkStyle.data() ) ;

    try
    {
        app.createWindow(networkStyle.data());
        app.requestInitialize();
#if defined(Q_OS_WIN) && QT_VERSION >= 0x050000
        WinShutdownMonitor::registerShutdownBlockReason( QString( PACKAGE_NAME ) + " didn't yet exit", (HWND)app.getMainWinId() ) ;
#endif
        app.exec();
        app.requestShutdown();
        app.exec();
    } catch (const std::exception& e) {
        PrintExceptionContinue(&e, "Runaway exception");
        app.handleRunawayException(QString::fromStdString(GetWarnings("gui")));
    } catch (...) {
        PrintExceptionContinue(NULL, "Runaway exception");
        app.handleRunawayException(QString::fromStdString(GetWarnings("gui")));
    }
    return app.getReturnValue();
}
#endif // #ifndef DOGECOIN_QT_TEST
