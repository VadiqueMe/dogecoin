// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#if defined(HAVE_CONFIG_H)
#include "config/dogecoin-config.h"
#endif

#include "rpcconsole.h"
#include "ui_debugwindow.h"

#include "bantablemodel.h"
#include "networkmodel.h"
#include "guiutil.h"
#include "platformstyle.h"
#include "bantablemodel.h"

#include "chainparams.h"
#include "netbase.h"
#include "rpc/server.h"
#include "rpc/client.h"
#include "util.h"

#include <openssl/crypto.h>

#include <univalue.h>

#ifdef ENABLE_WALLET
#include <db_cxx.h>
#endif

#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QSignalMapper>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QStringList>
#include <QPainter>
#include <QTextStream>

#if QT_VERSION < 0x050000
#include <QUrl>
#endif

// TODO: add a scrollback limit, as there is currently none
// TODO: make it possible to filter out categories (esp debug messages when implemented)
// TODO: receive errors and debug messages through NetworkModel

const int CONSOLE_HISTORY = 50 ;
const int INITIAL_TRAFFIC_GRAPH_MINUTES = 30 ;
const QSize FONT_RANGE( 4, 40 ) ;
const char fontSizeSettingsKey[] = "consoleFontSize" ;

const struct {
    const char *url;
    const char *source;
} ICON_MAPPING[] = {
    {"cmd-request", ":/icons/tx_input"},
    {"cmd-reply", ":/icons/tx_output"},
    {"cmd-error", ":/icons/tx_output"},
    {"misc", ":/icons/tx_inout"},
    {NULL, NULL}
};

namespace {

// don't add private key handling cmd's to the history
const QStringList historyFilter = QStringList()
    << "importprivkey"
    << "importmulti"
    << "signmessagewithprivkey"
    << "signrawtransaction"
    << "walletpassphrase"
    << "walletpassphrasechange"
    << "encryptwallet";

}

/* Object for performing RPC commands in a separate thread
*/
class RPCPerformer : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void request( const QString & command ) ;

Q_SIGNALS:
    void reply( int category, const QString & command ) ;
} ;

/** Class for handling RPC timers
 * (used for e.g. re-locking the wallet after a timeout)
 */
class QtRPCTimerBase: public QObject, public RPCTimerBase
{
    Q_OBJECT
public:
    QtRPCTimerBase( std::function< void( void ) > & f, int64_t millis ):
        func( f )
    {
        timer.setSingleShot( true ) ;
        connect( &timer, SIGNAL( timeout() ), this, SLOT( timeout() ) ) ;
        timer.start( millis ) ;
    }
    ~QtRPCTimerBase() {}
private Q_SLOTS:
    void timeout() {  func() ;  }
private:
    QTimer timer;
    std::function< void( void ) > func ;
};

class QtRPCTimerInterface: public RPCTimerInterface
{
public:
    ~QtRPCTimerInterface() {}
    const char * Name() {  return "Qt" ;  }
    RPCTimerBase * NewTimer( std::function< void( void ) > & func, int64_t millis )
    {
        return new QtRPCTimerBase( func, millis ) ;
    }
} ;


/* Convert number of seconds into a QString like 6:07:54 */
static QString seconds2hmmss( size_t s )
{
    size_t hours = s / 3600 ;
    size_t minutes = ( s % 3600 ) / 60 ;
    size_t seconds = s % 60 ;

    QStringList strList ;
    if ( hours > 0 ) strList.append( QString::number( hours ) ) ;
    strList.append( QString::fromStdString(strprintf( "%02d", minutes )) ) ;
    strList.append( QString::fromStdString(strprintf( "%02d", seconds )) ) ;

    return strList.join( ":" ) ;
}


#include "rpcconsole.moc"

/**
 * Split shell command line into a list of arguments and optionally execute the command(s)
 *
 * - Command nesting is possible with parenthesis; for example: validateaddress(getnewaddress())
 * - Arguments are delimited with whitespace or comma
 * - Extra whitespace at the beginning and end and between arguments will be ignored
 * - Text can be "double" or 'single' quoted
 * - The backslash \c \ is used as escape character
 *   - Outside quotes, any character can be escaped
 *   - Within double quotes, only escape \c " and backslashes before a \c " or another backslash
 *   - Within single quotes, no escaping is possible and no special interpretation takes place
 *
 * @param[out]   result      stringified Result from the executed command(chain)
 * @param[in]    strCommand  Command line to split
 * @param[in]    fExecute    set true if you want the command to be executed
 * @param[out]   pstrFilteredOut  Command line, filtered to remove any sensitive data
 */

bool RPCConsole::RPCParseCommandLine(std::string &strResult, const std::string &strCommand, const bool fExecute, std::string * const pstrFilteredOut)
{
    std::vector< std::vector<std::string> > stack;
    stack.push_back(std::vector<std::string>());

    enum CmdParseState
    {
        STATE_EATING_SPACES,
        STATE_EATING_SPACES_IN_ARG,
        STATE_EATING_SPACES_IN_BRACKETS,
        STATE_ARGUMENT,
        STATE_SINGLEQUOTED,
        STATE_DOUBLEQUOTED,
        STATE_ESCAPE_OUTER,
        STATE_ESCAPE_DOUBLEQUOTED,
        STATE_COMMAND_EXECUTED,
        STATE_COMMAND_EXECUTED_INNER
    } state = STATE_EATING_SPACES;
    std::string curarg;
    UniValue lastResult;
    unsigned nDepthInsideSensitive = 0;
    size_t filter_begin_pos = 0, chpos;
    std::vector<std::pair<size_t, size_t>> filter_ranges;

    auto add_to_current_stack = [&](const std::string& strArg) {
        if (stack.back().empty() && (!nDepthInsideSensitive) && historyFilter.contains(QString::fromStdString(strArg), Qt::CaseInsensitive)) {
            nDepthInsideSensitive = 1;
            filter_begin_pos = chpos;
        }
        // Make sure stack is not empty before adding something
        if (stack.empty()) {
            stack.push_back(std::vector<std::string>());
        }
        stack.back().push_back(strArg);
    };

    auto close_out_params = [&]() {
        if (nDepthInsideSensitive) {
            if (!--nDepthInsideSensitive) {
                assert(filter_begin_pos);
                filter_ranges.push_back(std::make_pair(filter_begin_pos, chpos));
                filter_begin_pos = 0;
            }
        }
        stack.pop_back();
    };

    std::string strCommandTerminated = strCommand;
    if (strCommandTerminated.back() != '\n')
        strCommandTerminated += "\n";
    for (chpos = 0; chpos < strCommandTerminated.size(); ++chpos)
    {
        char ch = strCommandTerminated[chpos];
        switch(state)
        {
            case STATE_COMMAND_EXECUTED_INNER:
            case STATE_COMMAND_EXECUTED:
            {
                bool breakParsing = true;
                switch(ch)
                {
                    case '[': curarg.clear(); state = STATE_COMMAND_EXECUTED_INNER; break;
                    default:
                        if (state == STATE_COMMAND_EXECUTED_INNER)
                        {
                            if (ch != ']')
                            {
                                // append char to the current argument (which is also used for the query command)
                                curarg += ch;
                                break;
                            }
                            if (curarg.size() && fExecute)
                            {
                                // if we have a value query, query arrays with index and objects with a string key
                                UniValue subelement;
                                if (lastResult.isArray())
                                {
                                    for(char argch: curarg)
                                        if (!std::isdigit(argch))
                                            throw std::runtime_error("Invalid result query");
                                    subelement = lastResult[atoi(curarg.c_str())];
                                }
                                else if (lastResult.isObject())
                                    subelement = find_value(lastResult, curarg);
                                else
                                    throw std::runtime_error("Invalid result query"); //no array or object: abort
                                lastResult = subelement;
                            }

                            state = STATE_COMMAND_EXECUTED;
                            break;
                        }
                        // don't break parsing when the char is required for the next argument
                        breakParsing = false;

                        // pop the stack and return the result to the current command arguments
                        close_out_params();

                        // don't stringify the json in case of a string to avoid doublequotes
                        if (lastResult.isStr())
                            curarg = lastResult.get_str();
                        else
                            curarg = lastResult.write(2);

                        // if we have a non empty result, use it as stack argument otherwise as general result
                        if (curarg.size())
                        {
                            if (stack.size())
                                add_to_current_stack(curarg);
                            else
                                strResult = curarg;
                        }
                        curarg.clear();
                        // assume eating space state
                        state = STATE_EATING_SPACES;
                }
                if (breakParsing)
                    break;
            }
            case STATE_ARGUMENT: // In or after argument
            case STATE_EATING_SPACES_IN_ARG:
            case STATE_EATING_SPACES_IN_BRACKETS:
            case STATE_EATING_SPACES: // Handle runs of whitespace
                switch(ch)
            {
                case '"': state = STATE_DOUBLEQUOTED; break;
                case '\'': state = STATE_SINGLEQUOTED; break;
                case '\\': state = STATE_ESCAPE_OUTER; break;
                case '(': case ')': case '\n':
                    if (state == STATE_EATING_SPACES_IN_ARG)
                        throw std::runtime_error("Invalid Syntax");
                    if (state == STATE_ARGUMENT)
                    {
                        if (ch == '(' && stack.size() && stack.back().size() > 0)
                        {
                            if (nDepthInsideSensitive) {
                                ++nDepthInsideSensitive;
                            }
                            stack.push_back(std::vector<std::string>());
                        }

                        // don't allow commands after executed commands on baselevel
                        if (!stack.size())
                            throw std::runtime_error("Invalid Syntax");

                        add_to_current_stack(curarg);
                        curarg.clear();
                        state = STATE_EATING_SPACES_IN_BRACKETS;
                    }
                    if ((ch == ')' || ch == '\n') && stack.size() > 0)
                    {
                        if (fExecute) {
                            // Convert argument list to JSON objects in method-dependent way,
                            // and pass it along with the method name to the dispatcher.
                            JSONRPCRequest req;
                            req.params = RPCConvertValues(stack.back()[0], std::vector<std::string>(stack.back().begin() + 1, stack.back().end()));
                            req.strMethod = stack.back()[0];
                            lastResult = tableRPC.execute(req);
                        }

                        state = STATE_COMMAND_EXECUTED;
                        curarg.clear();
                    }
                    break;
                case ' ': case ',': case '\t':
                    if(state == STATE_EATING_SPACES_IN_ARG && curarg.empty() && ch == ',')
                        throw std::runtime_error("Invalid Syntax");

                    else if(state == STATE_ARGUMENT) // Space ends argument
                    {
                        add_to_current_stack(curarg);
                        curarg.clear();
                    }
                    if ((state == STATE_EATING_SPACES_IN_BRACKETS || state == STATE_ARGUMENT) && ch == ',')
                    {
                        state = STATE_EATING_SPACES_IN_ARG;
                        break;
                    }
                    state = STATE_EATING_SPACES;
                    break;
                default: curarg += ch; state = STATE_ARGUMENT;
            }
                break;
            case STATE_SINGLEQUOTED: // Single-quoted string
                switch(ch)
            {
                case '\'': state = STATE_ARGUMENT; break;
                default: curarg += ch;
            }
                break;
            case STATE_DOUBLEQUOTED: // Double-quoted string
                switch(ch)
            {
                case '"': state = STATE_ARGUMENT; break;
                case '\\': state = STATE_ESCAPE_DOUBLEQUOTED; break;
                default: curarg += ch;
            }
                break;
            case STATE_ESCAPE_OUTER: // '\' outside quotes
                curarg += ch; state = STATE_ARGUMENT;
                break;
            case STATE_ESCAPE_DOUBLEQUOTED: // '\' in double-quoted text
                if(ch != '"' && ch != '\\') curarg += '\\'; // keep '\' for everything but the quote and '\' itself
                curarg += ch; state = STATE_DOUBLEQUOTED;
                break;
        }
    }
    if (pstrFilteredOut) {
        if (STATE_COMMAND_EXECUTED == state) {
            assert(!stack.empty());
            close_out_params();
        }
        *pstrFilteredOut = strCommand;
        for (auto i = filter_ranges.rbegin(); i != filter_ranges.rend(); ++i) {
            pstrFilteredOut->replace(i->first, i->second - i->first, "(…)");
        }
    }
    switch(state) // final state
    {
        case STATE_COMMAND_EXECUTED:
            if (lastResult.isStr())
                strResult = lastResult.get_str();
            else
                strResult = lastResult.write(2);
        case STATE_ARGUMENT:
        case STATE_EATING_SPACES:
            return true;
        default: // ERROR to end in one of the other states
            return false;
    }
}

void RPCPerformer::request( const QString & command )
{
    try
    {
        std::string result;
        std::string executableCommand = command.toStdString() + "\n";
        if(!RPCConsole::RPCExecuteCommandLine(result, executableCommand))
        {
            Q_EMIT reply(RPCConsole::CMD_ERROR, QString("Parse error: unbalanced ' or \""));
            return;
        }
        Q_EMIT reply(RPCConsole::CMD_REPLY, QString::fromStdString(result));
    }
    catch (UniValue& objError)
    {
        try // Nice formatting for standard-format error
        {
            int code = find_value(objError, "code").get_int();
            std::string message = find_value(objError, "message").get_str();
            Q_EMIT reply(RPCConsole::CMD_ERROR, QString::fromStdString(message) + " (code " + QString::number(code) + ")");
        }
        catch (const std::runtime_error&) // raised when converting to invalid type, i.e. missing code or message
        {   // Show raw JSON object
            Q_EMIT reply(RPCConsole::CMD_ERROR, QString::fromStdString(objError.write()));
        }
    }
    catch (const std::exception& e)
    {
        Q_EMIT reply(RPCConsole::CMD_ERROR, QString("Error: ") + QString::fromStdString(e.what()));
    }
}

RPCConsole::RPCConsole( const PlatformStyle * style, QWidget * parent )
    : QWidget( parent )
    , ui( new Ui::RPCConsole )
    , networkModel(0)
    , historyPtr(0)
    , platformStyle( style )
    , peersTableContextMenu(0)
    , banTableContextMenu(0)
    , consoleFontSize(0)
    , pathToLogFile( GUIUtil::boostPathToQString(boost::filesystem::path( GetDirForData() / LOG_FILE_NAME )) )
    , logFileWatcher()
    , resetBytesRecv( 0 )
    , resetBytesSent( 0 )
{
    ui->setupUi( this ) ;
    constructPeerDetailsWidget() ;
    GUIUtil::restoreWindowGeometry( "nRPCConsoleWindow", this->size(), this ) ;

    ui->debugLogTextArea->setFrameStyle( /* shape */ QFrame::StyledPanel | /* shadow */ QFrame::Plain ) ;
    ui->debugLogTextArea->setLineWrapMode( QTextEdit::WidgetWidth ) ; // QTextEdit::NoWrap
    ui->debugLogTextArea->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff ) ; // Qt::ScrollBarAlwaysOn

    ui->debugLogTextArea->setContextMenuPolicy( Qt::CustomContextMenu ) ;
    connect( ui->debugLogTextArea, SIGNAL( customContextMenuRequested(const QPoint &) ),
             this, SLOT( showContextMenuForLog(const QPoint &) ) ) ;

    ui->logFilterIconLabel->setPixmap( platformStyle->SingleColorIcon( ":/icons/magnifier" ).pixmap( 24, 24 ) ) ;
    ui->logFilterIconLabel->setScaledContents( false ) ;
    ui->searchFilter->setClearButtonEnabled( false ) ; // it got its own custom, and doesn't need for the built-in one
    ui->clearLogFilterButton->setIcon( platformStyle->SingleColorIcon( ":/icons/remove" ) ) ;
    connect( ui->searchFilter, SIGNAL( textEdited(const QString &) ), this, SLOT( veryLogFile() ) ) ;
    connect( ui->clearLogFilterButton, SIGNAL( clicked() ), this, SLOT( clearLogSearchFilter() ) ) ;

    if ( platformStyle->getImagesOnButtons() )
        ui->openDebugLogButton->setIcon( platformStyle->SingleColorIcon( ":/icons/export" ) ) ;

    connect( &logFileWatcher, SIGNAL( fileChanged(QString) ), this, SLOT( onFileChange(const QString &) ) ) ;

    ui->clearConsoleButton->setIcon( platformStyle->SingleColorIcon( ":/icons/remove" ) ) ;
    ui->fontBiggerButton->setIcon( platformStyle->SingleColorIcon( ":/icons/fontbigger" ) ) ;
    ui->fontSmallerButton->setIcon( platformStyle->SingleColorIcon( ":/icons/fontsmaller" ) ) ;

    // Install event filter for up and down arrow
    ui->lineEdit->installEventFilter(this);
    ui->messagesWidget->installEventFilter(this);

    connect( ui->clearConsoleButton, SIGNAL( clicked() ), this, SLOT( clearConsole() ) ) ;
    connect( ui->fontBiggerButton, SIGNAL( clicked() ), this, SLOT( fontBigger() ) ) ;
    connect( ui->fontSmallerButton, SIGNAL( clicked() ), this, SLOT(fontSmaller()));
    connect( ui->buttonClearTrafficGraph, SIGNAL( clicked() ), ui->trafficGraph, SLOT( clearTrafficGraph() ) ) ;
    connect( ui->buttonResetTrafficValues, SIGNAL( clicked() ), this, SLOT( resetTrafficValues() ) ) ;

    connect( ui->tabWidget, SIGNAL( currentChanged(int) ), this, SLOT( currentTabChangedTo(int) ) ) ;

    // set library version labels
#ifdef ENABLE_WALLET
    ui->berkeleyDBVersion->setText(DbEnv::version(0, 0, 0));
#else
    ui->label_berkeleyDBVersion->hide();
    ui->berkeleyDBVersion->hide();
#endif
    // Register RPC timer interface
    rpcTimerInterface = new QtRPCTimerInterface();
    // avoid accidentally overwriting an existing, non QTThread
    // based timer interface
    RPCSetTimerInterfaceIfUnset(rpcTimerInterface);

    QColor colorForSent( "yellow" ) ;
    QColor colorForReceived( "cyan" ) ;

    {
        ui->colorForReceivedButton->setText( "" ) ;
        int button_height = 3 * ( ui->colorForReceivedButton->height() >> 2 ) ;
        ui->colorForReceivedButton->setFixedHeight( button_height ) ;
        ui->colorForReceivedButton->setFixedWidth( button_height ) ;

        int button_width = ui->colorForReceivedButton->width() ;
        QPixmap pixmap( button_width, button_height ) ;
        QColor transparent( "white" ) ;
        transparent.setAlpha( 255 ) ;
        pixmap.fill( transparent ) ;

        QPainter painter( &pixmap ) ;
        painter.setPen( QPen( colorForReceived ) ) ;
        static const int spacing = 4 ;
        static const int hshift = -1 ;
        static const int vshift = -2 ;
        for ( int p = 1 ; p < 3 ; ++ p ) {
            painter.drawLine( QLine( QPoint( spacing + hshift, ( button_height >> 1 ) + p + vshift ),
                                     QPoint( button_width - spacing + hshift, ( button_height >> 1 ) + p + vshift ) ) ) ;
            painter.drawLine( QLine( QPoint( spacing + hshift, ( button_height >> 1 ) - p + vshift + 1 ),
                                     QPoint( button_width - spacing + hshift, ( button_height >> 1 ) - p + vshift + 1 ) ) ) ;
        }

        QIcon icon( pixmap ) ;
        ui->colorForReceivedButton->setIcon( icon ) ;
        ui->colorForReceivedButton->setIconSize( pixmap.rect().size() ) ;
    }

    {
        ui->colorForSentButton->setText( "" ) ;
        int button_height = 3 * ( ui->colorForSentButton->height() >> 2 ) ;
        ui->colorForSentButton->setFixedHeight( button_height ) ;
        ui->colorForSentButton->setFixedWidth( button_height ) ;

        int button_width = ui->colorForSentButton->width() ;
        QPixmap pixmap( button_width, button_height ) ;
        QColor transparent( "white" ) ;
        transparent.setAlpha( 255 ) ;
        pixmap.fill( transparent ) ;

        QPainter painter( &pixmap ) ;
        painter.setPen( QPen( colorForSent ) ) ;
        static const int spacing = 4 ;
        static const int hshift = -1 ;
        static const int vshift = -3 ;
        for ( int p = 1 ; p < 3 ; ++ p ) {
            painter.drawLine( QLine( QPoint( spacing + hshift, ( button_height >> 1 ) + p + vshift ),
                                     QPoint( button_width - spacing + hshift, ( button_height >> 1 ) + p + vshift ) ) ) ;
            painter.drawLine( QLine( QPoint( spacing + hshift, ( button_height >> 1 ) - p + vshift + 1 ),
                                     QPoint( button_width - spacing + hshift, ( button_height >> 1 ) - p + vshift + 1 ) ) ) ;
        }

        QIcon icon( pixmap ) ;
        ui->colorForSentButton->setIcon( icon ) ;
        ui->colorForSentButton->setIconSize( pixmap.rect().size() ) ;
    }

    ui->trafficGraph->setReceivedColor( colorForReceived ) ;
    ui->trafficGraph->setSentColor( colorForSent ) ;

    setTrafficGraphRange( INITIAL_TRAFFIC_GRAPH_MINUTES ) ;

    QSettings settings;
    consoleFontSize = settings.value(fontSizeSettingsKey, QFontInfo(QFont()).pointSize()).toInt();
    clearConsole() ;
}

RPCConsole::~RPCConsole()
{
    GUIUtil::saveWindowGeometry( "nRPCConsoleWindow", this ) ;
    RPCUnsetTimerInterface( rpcTimerInterface ) ;
    delete rpcTimerInterface ;
    delete ui ;
}

bool RPCConsole::eventFilter(QObject* obj, QEvent *event)
{
    if(event->type() == QEvent::KeyPress) // Special key handling
    {
        QKeyEvent *keyevt = static_cast<QKeyEvent*>(event);
        int key = keyevt->key();
        Qt::KeyboardModifiers mod = keyevt->modifiers();
        switch(key)
        {
        case Qt::Key_Up: if(obj == ui->lineEdit) { browseHistory(-1); return true; } break;
        case Qt::Key_Down: if(obj == ui->lineEdit) { browseHistory(1); return true; } break;
        case Qt::Key_PageUp: /* pass paging keys to messages widget */
        case Qt::Key_PageDown:
            if(obj == ui->lineEdit)
            {
                QApplication::postEvent(ui->messagesWidget, new QKeyEvent(*keyevt));
                return true;
            }
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // forward these events to lineEdit
            if(obj == autoCompleter->popup()) {
                QApplication::postEvent(ui->lineEdit, new QKeyEvent(*keyevt));
                return true;
            }
            break;
        default:
            // Typing in messages widget brings focus to line edit, and redirects key there
            // Exclude most combinations and keys that emit no text, except paste shortcuts
            if(obj == ui->messagesWidget && (
                  (!mod && !keyevt->text().isEmpty() && key != Qt::Key_Tab) ||
                  ((mod & Qt::ControlModifier) && key == Qt::Key_V) ||
                  ((mod & Qt::ShiftModifier) && key == Qt::Key_Insert)))
            {
                ui->lineEdit->setFocus();
                QApplication::postEvent(ui->lineEdit, new QKeyEvent(*keyevt));
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void RPCConsole::setNetworkModel( NetworkModel * model )
{
    networkModel = model ;
    ui->trafficGraph->setNetworkModel( model ) ;
    if ( networkModel && networkModel->getPeerTableModel() && networkModel->getBanTableModel() )
    {
        // update
        setNumConnections( model->getNumConnections() ) ;
        connect( model, SIGNAL( numConnectionsChanged(int) ), this, SLOT( setNumConnections(int) ) ) ;

        setNumBlocks( model->getNumBlocks(), model->getLastBlockDate(), model->getVerificationProgress(), false ) ;
        connect( model, SIGNAL( numBlocksChanged(int, QDateTime, double, bool) ), this, SLOT( setNumBlocks(int, QDateTime, double, bool) ) ) ;

        updateNetworkInfo() ;
        connect( model, SIGNAL( networkActiveChanged(bool) ), this, SLOT( setNetworkActive(bool) ) ) ;

        updateTrafficStats() ;
        connect( model, SIGNAL( bytesChanged(quint64, quint64) ), this, SLOT( updateTrafficStats() ) ) ;

        connect( model, SIGNAL( mempoolSizeChanged(long, size_t) ), this, SLOT( setMempoolSize(long, size_t) ) ) ;

        // set up peer table
        ui->peerWidget->setModel( model->getPeerTableModel() ) ;
        ui->peerWidget->verticalHeader()->hide();
        ui->peerWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->peerWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->peerWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
        ui->peerWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        ui->peerWidget->setColumnWidth(PeerTableModel::Address, ADDRESS_COLUMN_WIDTH);
        ui->peerWidget->setColumnWidth(PeerTableModel::Subversion, SUBVERSION_COLUMN_WIDTH);
        ui->peerWidget->setColumnWidth(PeerTableModel::Ping, PING_COLUMN_WIDTH);
        ui->peerWidget->horizontalHeader()->setStretchLastSection(true);

        // create peer table context menu actions
        QAction* sendMessageAction = new QAction( tr("Send message") + "...", this ) ;
        QAction* disconnectAction = new QAction( tr("&Disconnect"), this ) ;
        QAction* banAction1h = new QAction( tr("Ban for") + " " + tr("1 hour"), this ) ;
        QAction* banAction24h = new QAction( tr("Ban for") + " " + tr("1 day"), this ) ;
        QAction* banAction7d = new QAction( tr("Ban for") + " " + tr("1 week"), this ) ;

        // create peer table context menu
        peersTableContextMenu = new QMenu( this ) ;
        peersTableContextMenu->addAction( sendMessageAction );
        peersTableContextMenu->addAction( disconnectAction ) ;
        peersTableContextMenu->addAction( banAction1h ) ;
        peersTableContextMenu->addAction( banAction24h ) ;
        peersTableContextMenu->addAction( banAction7d ) ;

        // add signal mapping for items of dynamic menu
        QSignalMapper* signalMapper = new QSignalMapper( this ) ;
        signalMapper->setMapping( banAction1h, 60 * 60 ) ;
        signalMapper->setMapping( banAction24h, 60 * 60 * 24 ) ;
        signalMapper->setMapping( banAction7d, 60 * 60 * 24 * 7 ) ;
        connect( banAction1h, SIGNAL( triggered() ), signalMapper, SLOT( map() ) ) ;
        connect( banAction24h, SIGNAL( triggered() ), signalMapper, SLOT( map() ) ) ;
        connect( banAction7d, SIGNAL( triggered() ), signalMapper, SLOT( map() ) ) ;
        connect( signalMapper, SIGNAL( mapped(int) ), this, SLOT( banSelectedNode(int) ) ) ;

        // peer table context menu signals
        connect( ui->peerWidget, SIGNAL( customContextMenuRequested(const QPoint&) ),
                 this, SLOT( showPeersTableContextMenu(const QPoint&) ) ) ;
        connect( disconnectAction, SIGNAL( triggered() ), this, SLOT( disconnectSelectedNode() ) ) ;
        connect( sendMessageAction, SIGNAL( triggered() ), this, SLOT( textMessageToSelectedNode() ) ) ;

        // peer table signal handling - update peer details when selecting new node
        connect( ui->peerWidget->selectionModel(), SIGNAL( selectionChanged(const QItemSelection &, const QItemSelection &) ),
            this, SLOT( peerSelected(const QItemSelection &, const QItemSelection &) ) ) ;
        // peer table signal handling - update peer details when new nodes are added to the model
        connect(model->getPeerTableModel(), SIGNAL(layoutChanged()), this, SLOT(peerLayoutChanged()));
        // peer table signal handling - cache selected node ids
        connect(model->getPeerTableModel(), SIGNAL(layoutAboutToBeChanged()), this, SLOT(peerLayoutAboutToChange()));

        // set up ban table
        ui->banlistWidget->setModel( model->getBanTableModel() ) ;
        ui->banlistWidget->verticalHeader()->hide();
        ui->banlistWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->banlistWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
        ui->banlistWidget->setSelectionMode(QAbstractItemView::SingleSelection);
        ui->banlistWidget->setContextMenuPolicy(Qt::CustomContextMenu);
        ui->banlistWidget->setColumnWidth(BanTableModel::Address, BANSUBNET_COLUMN_WIDTH);
        ui->banlistWidget->setColumnWidth(BanTableModel::Bantime, BANTIME_COLUMN_WIDTH);
        ui->banlistWidget->horizontalHeader()->setStretchLastSection(true);

        // create ban table context menu action
        QAction* unbanAction = new QAction(tr("&Unban"), this);

        // create ban table context menu
        banTableContextMenu = new QMenu( this ) ;
        banTableContextMenu->addAction( unbanAction ) ;

        // ban table context menu signals
        connect( ui->banlistWidget, SIGNAL( customContextMenuRequested(const QPoint &) ),
                 this, SLOT( showBanTableContextMenu(const QPoint &) ) ) ;
        connect( unbanAction, SIGNAL( triggered() ), this, SLOT( unbanSelectedNode() ) ) ;

        // ban table signal handling - clear peer details when clicking a peer in the ban table
        connect( ui->banlistWidget, SIGNAL( clicked(const QModelIndex&) ), this, SLOT( clearSelectedNode() ) ) ;
        // ban table signal handling - ensure ban table is shown or hidden (if empty)
        connect( model->getBanTableModel(), SIGNAL( layoutChanged() ), this, SLOT( showOrHideBanTableIfNeeded() ) ) ;
        showOrHideBanTableIfNeeded() ;

        // Provide initial values
        ui->versionOfThisPeer->setText( model->formatFullVersion() ) ;
        ui->nodeUserAgent->setText( model->formatSubVersion() ) ;
        ui->dataDir->setText( model->dataDir() ) ;
        ui->startupTime->setText( model->formatPeerStartupTime() ) ;
        ui->networkName->setText( QString::fromStdString( NameOfChain() ) ) ;

        // Setup autocomplete and attach it
        QStringList wordList ;
        std::vector< std::string > listOfCommands = tableRPC.listCommands() ;
        for ( const std::string & command : listOfCommands )
            wordList << command.c_str() ;

        autoCompleter = new QCompleter( wordList, this ) ;
        ui->lineEdit->setCompleter( autoCompleter ) ;
        autoCompleter->popup()->installEventFilter( this ) ;

        // Start thread to execute RPC commands
        startPerformer() ;
    }

    if ( model == nullptr ) {
        // network model is being set to 0, this means shutdown() is about to be called
        // make sure to clean up the performer thread
        Q_EMIT stopPerformer() ;
        thread.wait() ;
    }
}

static QString categoryClass(int category)
{
    switch(category)
    {
    case RPCConsole::CMD_REQUEST:  return "cmd-request"; break;
    case RPCConsole::CMD_REPLY:    return "cmd-reply"; break;
    case RPCConsole::CMD_ERROR:    return "cmd-error"; break;
    default:                       return "misc";
    }
}

void RPCConsole::fontBigger()
{
    setFontSize(consoleFontSize+1);
}

void RPCConsole::fontSmaller()
{
    setFontSize(consoleFontSize-1);
}

void RPCConsole::setFontSize(int newSize)
{
    QSettings settings;

    //don't allow a insane font size
    if (newSize < FONT_RANGE.width() || newSize > FONT_RANGE.height())
        return;

    // temp. store the console content
    QString str = ui->messagesWidget->toHtml();

    // replace font tags size in current content
    str.replace(QString("font-size:%1pt").arg(consoleFontSize), QString("font-size:%1pt").arg(newSize));

    // store the new font size
    consoleFontSize = newSize;
    settings.setValue(fontSizeSettingsKey, consoleFontSize);

    // clear console (reset icon sizes, default stylesheet) and re-add the content
    float oldPosFactor = 1.0 / ui->messagesWidget->verticalScrollBar()->maximum() * ui->messagesWidget->verticalScrollBar()->value();
    clearConsole( false ) ;
    ui->messagesWidget->setHtml(str);
    ui->messagesWidget->verticalScrollBar()->setValue(oldPosFactor * ui->messagesWidget->verticalScrollBar()->maximum());
}

void RPCConsole::clearConsole( bool clearHistory )
{
    ui->messagesWidget->clear();
    if(clearHistory)
    {
        history.clear();
        historyPtr = 0;
    }
    ui->lineEdit->clear();
    ui->lineEdit->setFocus();

    // Add smoothly scaled icon images
    // (when using width/height on an img, Qt uses nearest instead of linear interpolation)
    for(int i=0; ICON_MAPPING[i].url; ++i)
    {
        ui->messagesWidget->document()->addResource(
                    QTextDocument::ImageResource,
                    QUrl(ICON_MAPPING[i].url),
                    platformStyle->SingleColorImage(ICON_MAPPING[i].source).scaled(QSize(consoleFontSize*2, consoleFontSize*2), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }

    // Set default style sheet
    QFontInfo fixedFontInfo(GUIUtil::fixedPitchFont());
    ui->messagesWidget->document()->setDefaultStyleSheet(
        QString(
                "table { }"
                "td.time { color: #808080; font-size: %2; padding-top: 3px; } "
                "td.message { font-family: %1; font-size: %2; white-space:pre-wrap; } "
                "td.cmd-request { color: #006060; } "
                "td.cmd-error { color: red; } "
                ".secwarning { color: red; }"
                "b { color: #006060; } "
            ).arg(fixedFontInfo.family(), QString("%1pt").arg(consoleFontSize))
        );

    message(CMD_REPLY, (tr("Welcome to the %1 RPC console.").arg(tr(PACKAGE_NAME)) + "<br>" +
                        tr("Use up and down arrows to navigate history, and <b>Ctrl-L</b> to clear screen.") + "<br>" +
                        tr("Type <b>help</b> for an overview of available commands.")) +
                        "<br><span class=\"secwarning\">" +
                        tr("WARNING: Scammers have been active, telling users to type commands here, stealing their wallet contents. Do not use this console without fully understanding the ramification of a command.") +
                        "</span>",
                        true);
}

void RPCConsole::keyPressEvent(QKeyEvent *event)
{
    if(windowType() != Qt::Widget && event->key() == Qt::Key_Escape)
    {
        close();
    }
}

void RPCConsole::message(int category, const QString &message, bool html)
{
    QTime time = QTime::currentTime();
    QString timeString = time.toString();
    QString out;
    out += "<table><tr><td class=\"time\" width=\"65\">" + timeString + "</td>";
    out += "<td class=\"icon\" width=\"32\"><img src=\"" + categoryClass(category) + "\"></td>";
    out += "<td class=\"message " + categoryClass(category) + "\" valign=\"middle\">";
    if(html)
        out += message;
    else
        out += GUIUtil::HtmlEscape(message, false);
    out += "</td></tr></table>";
    ui->messagesWidget->append(out);
}

void RPCConsole::updateNetworkInfo()
{
    QString connections = QString::number( networkModel->getNumConnections() ) + " (" ;
    connections += tr("In:") + " " + QString::number( networkModel->getNumConnections( CONNECTIONS_IN ) ) + " / " ;
    connections += tr("Out:") + " " + QString::number( networkModel->getNumConnections( CONNECTIONS_OUT ) ) + ")" ;

    if ( ! networkModel->isNetworkActive() ) {
        connections += " (" + tr("Network activity is off") + ")" ;
    }

    ui->numberOfConnections->setText( connections ) ;
}

void RPCConsole::setNumConnections( int count )
{
    if ( networkModel == nullptr ) return ;

    updateNetworkInfo() ;
}

void RPCConsole::setNetworkActive(bool networkActive)
{
    updateNetworkInfo() ;
}

void RPCConsole::setNumBlocks( int count, const QDateTime & blockDate, double progress, bool headers )
{
    ( void ) progress ;
    if ( ! headers ) {
        ui->numberOfBlocks->setText( QString::number( count ) ) ;
        ui->tipBlockTime->setText( blockDate.toString() ) ;
    }
}

void RPCConsole::setMempoolSize(long numberOfTxs, size_t dynUsage)
{
    ui->mempoolNumberTxs->setText(QString::number(numberOfTxs));

    if (dynUsage < 1000000)
        ui->mempoolSize->setText(QString::number(dynUsage/1000.0, 'f', 2) + " KB");
    else
        ui->mempoolSize->setText(QString::number(dynUsage/1000000.0, 'f', 2) + " MB");
}

void RPCConsole::on_lineEdit_returnPressed()
{
    QString cmd = ui->lineEdit->text();

    if(!cmd.isEmpty())
    {
        std::string strFilteredCmd;
        try {
            std::string dummy;
            if (!RPCParseCommandLine(dummy, cmd.toStdString(), false, &strFilteredCmd)) {
                // Failed to parse command, so we cannot even filter it for the history
                throw std::runtime_error("Invalid command line");
            }
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Error", QString("Error: ") + QString::fromStdString(e.what()));
            return;
        }

        ui->lineEdit->clear();

        cmdBeforeBrowsing = QString();

        message(CMD_REQUEST, cmd);
        Q_EMIT cmdRequest(cmd);

        cmd = QString::fromStdString(strFilteredCmd);

        // Remove command, if already in history
        history.removeOne(cmd);
        // Append command to history
        history.append(cmd);
        // Enforce maximum history size
        while(history.size() > CONSOLE_HISTORY)
            history.removeFirst();
        // Set pointer to end of history
        historyPtr = history.size();

        // Scroll console view to end
        scrollToEnd();
    }
}

void RPCConsole::browseHistory(int offset)
{
    // store current text when start browsing through the history
    if (historyPtr == history.size()) {
        cmdBeforeBrowsing = ui->lineEdit->text();
    }

    historyPtr += offset;
    if(historyPtr < 0)
        historyPtr = 0;
    if(historyPtr > history.size())
        historyPtr = history.size();
    QString cmd;
    if(historyPtr < history.size())
        cmd = history.at(historyPtr);
    else if (!cmdBeforeBrowsing.isNull()) {
        cmd = cmdBeforeBrowsing;
    }
    ui->lineEdit->setText(cmd);
}

void RPCConsole::startPerformer()
{
    RPCPerformer * performer = new RPCPerformer() ;
    performer->moveToThread( & thread ) ;

    // Replies from performer object must go to this object
    connect( performer, SIGNAL( reply(int, QString) ), this, SLOT( message(int, QString) ) ) ;
    // Requests from this object must go to performer
    connect( this, SIGNAL( cmdRequest(QString) ), performer, SLOT( request(QString) ) ) ;

    // On stopPerformer signal
    // - quit the Qt event loop in the execution thread
    connect( this, SIGNAL( stopPerformer() ), &thread, SLOT( quit() ) ) ;
    // - queue performer for deletion (in execution thread)
    connect( &thread, SIGNAL( finished() ), performer, SLOT( deleteLater() ), Qt::DirectConnection ) ;

    // Default implementation of QThread::run() simply spins up an event loop in the thread,
    // which is what we want
    thread.start() ;
}

void RPCConsole::currentTabChangedTo( int index )
{
    if ( ui->tabWidget->widget( index ) == ui->tab_console )
        ui->lineEdit->setFocus() ;
    else if ( ui->tabWidget->widget( index ) == ui->tab_log )
        veryLogFile() ;
    else if ( ui->tabWidget->widget( index ) != ui->tab_peers )
        clearSelectedNode() ;
}

void RPCConsole::onFileChange( const QString & whatsChanged )
{
    if ( whatsChanged == pathToLogFile )
        if ( ui->tabWidget->currentWidget() == ui->tab_log )
            veryLogFile() ;
}

void RPCConsole::veryLogFile()
{
    if ( QFile::exists( pathToLogFile ) ) // log file can be removed and then recreated
        logFileWatcher.addPath( pathToLogFile ) ;

    ui->debugLogTextArea->clear() ;

    QFile logFile( pathToLogFile ) ;
    if ( ! logFile.open( QIODevice::ReadOnly ) )
    {
        ui->debugLogTextArea->setPlainText( "(can't open)" ) ;
        return ;
    }

    bool autoScroll = true ;
    /* = ui->debugLogTextArea->verticalScrollBar()->value() >
           ( ui->debugLogTextArea->verticalScrollBar()->maximum() - ui->debugLogTextArea->verticalScrollBar()->pageStep() ) */

    if ( logFile.size() > 0 )
    {
        {
            QTextStream logText( &logFile ) ;
            QStringList logLines ;
            bool isPlainText = true ;
            const QString & filter = ui->searchFilter->text() ;

            while ( ! logText.atEnd() )
            {
                QString line = logText.readLine() ;
                if ( filter.isEmpty() )
                    logLines.append( line ) ; // it's faster than ui->debugLogTextArea->appendPlainText( line )
                else if ( line.contains( filter, Qt::CaseSensitive ) )
                {
                    isPlainText = false ;
                    line.replace( "<", "&lt;" ) ;
                    line.replace( ">", "&gt;" ) ;
                    line.replace( "&", "&amp;" ) ;
                    line.replace( "\"", "&quot;" ) ;

                    QString filterHtml( filter ) ;
                    filterHtml.replace( "<", "&lt;" ) ;
                    filterHtml.replace( ">", "&gt;" ) ;
                    filterHtml.replace( "&", "&amp;" ) ;
                    filterHtml.replace( "\"", "&quot;" ) ;

                    int pos = 0 ;
                    while ( ( pos = line.indexOf( filterHtml, pos, Qt::CaseSensitive ) ) != -1 ) {
                        line.replace( pos, filterHtml.size(), QString( "<b>" ) + filterHtml + QString( "</b>" ) ) ;
                        pos += filterHtml.size() + 7 ;
                    }

                    logLines.append( line ) ;
                }
            }

            if ( isPlainText ) {
                if ( logLines.count() > 0 )
                    ui->debugLogTextArea->setPlainText( logLines.join( "\n" ) ) ;
                else
                    ui->debugLogTextArea->setPlainText( filter.isEmpty() ? "(empty)" : "(not found)" ) ;
            } else {
                QString filteredLog = logLines.join( "<br>" ) ;
                filteredLog.replace( "&lt;", "<" ) ;
                filteredLog.replace( "&gt;", ">" ) ;
                filteredLog.replace( "&amp;", "&" ) ;
                filteredLog.replace( "&quot;", "\"" ) ;
                ui->debugLogTextArea->setHtml( filteredLog ) ;
            }
        }
    } else {
        ui->debugLogTextArea->setPlainText( "(empty)" ) ;
    }

    if ( autoScroll ) {
        /* ui->debugLogTextArea->verticalScrollBar()->setValue( ui->debugLogTextArea->verticalScrollBar()->maximum() ) ; */
        ui->debugLogTextArea->moveCursor( QTextCursor::End ) ;
        ui->debugLogTextArea->ensureCursorVisible() ;
    }

    ui->debugLogTextArea->setCursorWidth( 0 ) ; // hide cursor

    logFile.close() ;
}

void RPCConsole::clearLogSearchFilter()
{
    ui->searchFilter->clear() ;
    veryLogFile() ;
}

void RPCConsole::showContextMenuForLog( const QPoint & where )
{
    QMenu * logAreaContextMenu = ui->debugLogTextArea->createStandardContextMenu() ;
    logAreaContextMenu->addSeparator() ;

    QAction * reloadLogAction = new QAction( "Refresh Log", this ) ;
    connect( reloadLogAction, SIGNAL( triggered() ), this, SLOT( veryLogFile() ) ) ;
    logAreaContextMenu->addAction( reloadLogAction ) ;

    logAreaContextMenu->popup( mapToGlobal( where ) ) ;
}

void RPCConsole::on_openDebugLogButton_clicked()
{
    GUIUtil::openDebugLogfile() ;
}

void RPCConsole::scrollToEnd()
{
    QScrollBar *scrollbar = ui->messagesWidget->verticalScrollBar();
    scrollbar->setValue(scrollbar->maximum());
}

void RPCConsole::on_sldGraphRange_valueChanged(int value)
{
    const int multiplier = 5 ; // each position on the slider represents 5 min
    int minutes = value * multiplier ;
    setTrafficGraphRange( minutes ) ;
}

void RPCConsole::setTrafficGraphRange( int minutes )
{
    ui->trafficGraph->setGraphRangeMinutes( minutes ) ;
    ui->graphRangeInMinutes->setText( QString::number( minutes ) + " minute" + ( minutes != 1 ? "s" : "" ) ) ;
}

void RPCConsole::updateTrafficStats( quint64 totalBytesIn, quint64 totalBytesOut )
{
    ui->bytesInLabel->setText( QString::fromStdString( FormatBytes( totalBytesIn - resetBytesRecv ) ) ) ;
    ui->bytesOutLabel->setText( QString::fromStdString( FormatBytes( totalBytesOut - resetBytesSent ) ) ) ;
}

void RPCConsole::updateTrafficStats()
{
    updateTrafficStats( networkModel->getTotalBytesRecv(), networkModel->getTotalBytesSent() ) ;
}

void RPCConsole::resetTrafficValues()
{
    resetBytesRecv = networkModel->getTotalBytesRecv() ;
    resetBytesSent = networkModel->getTotalBytesSent() ;
    updateTrafficStats() ;
}

void RPCConsole::peerSelected( const QItemSelection & selected, const QItemSelection & deselected )
{
    Q_UNUSED( deselected ) ;

    if ( ! selected.indexes().isEmpty() ) {
        const CNodeCombinedStats * stats = nullptr ;
        if ( networkModel && networkModel->getPeerTableModel() )
            networkModel->getPeerTableModel()->getNodeStats( selected.indexes().first().row() ) ;
        if ( stats != nullptr )
            updateNodeDetail( stats ) ;
    } else {
        clearSelectedNode() ;
    }
}

void RPCConsole::peerLayoutAboutToChange()
{
    QModelIndexList selected = ui->peerWidget->selectionModel()->selectedIndexes();
    cachedNodeids.clear();
    for(int i = 0; i < selected.size(); i++)
    {
        const CNodeCombinedStats * stats = networkModel->getPeerTableModel()->getNodeStats( selected.at( i ).row() ) ;
        cachedNodeids.append( stats->nodeStats.nodeid ) ;
    }
}

void RPCConsole::peerLayoutChanged()
{
    if ( ! networkModel || ! networkModel->getPeerTableModel() )
        return ;

    const CNodeCombinedStats * stats = nullptr ;
    bool fUnselect = false;
    bool fReselect = false;

    if (cachedNodeids.empty()) // no node selected yet
        return;

    // find the currently selected row
    int selectedRow = -1;
    QModelIndexList selectedModelIndex = ui->peerWidget->selectionModel()->selectedIndexes();
    if (!selectedModelIndex.isEmpty()) {
        selectedRow = selectedModelIndex.first().row();
    }

    // check if our detail node has a row in the table (it may not necessarily
    // be at selectedRow since its position can change after a layout change)
    int detailNodeRow = networkModel->getPeerTableModel()->getRowByNodeId( cachedNodeids.first() ) ;

    if ( detailNodeRow < 0 )
    {
        // detail node disappeared from table (node disconnected)
        fUnselect = true ;
    }
    else
    {
        if (detailNodeRow != selectedRow)
        {
            // detail node moved position
            fUnselect = true;
            fReselect = true;
        }

        // get fresh stats on the detail node
        stats = networkModel->getPeerTableModel()->getNodeStats( detailNodeRow ) ;
    }

    if ( fUnselect && selectedRow >= 0 ) {
        clearSelectedNode() ;
    }

    if (fReselect)
    {
        for ( int i = 0 ; i < cachedNodeids.size() ; i ++ )
        {
            ui->peerWidget->selectRow( networkModel->getPeerTableModel()->getRowByNodeId( cachedNodeids.at( i ) ) ) ;
        }
    }

    if ( stats != nullptr )
        updateNodeDetail( stats ) ;
}

void RPCConsole::constructPeerDetailsWidget()
{
    static bool didOnce = false ;
    if ( didOnce ) return ;

    Qt::TextInteractionFlags interactionWithLabel =
            Qt::TextSelectableByMouse /* | Qt::TextSelectableByKeyboard */ ;

    //
    // peerHeading
    //

    peerHeading.reset( new QLabel() ) ;
    peerHeading->setText( tr( "Select a peer to view detailed information" ) ) ;
    peerHeading->setAlignment( Qt::AlignHCenter | Qt::AlignTop ) ;
    peerHeading->setWordWrap( true ) ;
    peerHeading->setTextInteractionFlags( interactionWithLabel ) ;
    peerHeading->setCursor( Qt::IBeamCursor ) ;
    peerHeading->setMinimumSize( /* width */ 300, /* height */ 25 ) ;
    QSizePolicy sizePolicy ;
       sizePolicy.setHorizontalPolicy( QSizePolicy::Preferred ) ;
       sizePolicy.setVerticalPolicy( QSizePolicy::Minimum ) ;
       sizePolicy.setHorizontalStretch( 0 ) ;
       sizePolicy.setVerticalStretch( 0 ) ;
    peerHeading->setSizePolicy( sizePolicy ) ;

    ui->gridLayoutForPeersTab->addWidget( peerHeading.get(), /* row */ 0, /* column */ 1 ) ;

    //
    // peerDetailsWidget and company
    //

    peerDirection.reset( new QLabel( "?" ) ) ;
    peerVersion.reset( new QLabel( "?" ) ) ;
    peerSubversion.reset( new QLabel( "?" ) ) ;
    peerServices.reset( new QLabel( "?" ) ) ;
    peerHeight.reset( new QLabel( "?" ) ) ;
    peerSyncHeight.reset( new QLabel( "?" ) ) ;
    peerCommonHeight.reset( new QLabel( "?" ) ) ;
    peerConnTime.reset( new QLabel( "?" ) ) ;
    peerLastSend.reset( new QLabel( "?" ) ) ;
    peerLastRecv.reset( new QLabel( "?" ) ) ;
    peerBytesSent.reset( new QLabel( "?" ) ) ;
    peerBytesRecv.reset( new QLabel( "?" ) ) ;
    peerPingTime.reset( new QLabel( "*" ) ) ;
    peerPingWait.reset( new QLabel( "*" ) ) ;
    peerMinPing.reset( new QLabel( "*" ) ) ;
    peerTimeOffset.reset( new QLabel( "?" ) ) ;
    peerWhitelisted.reset( new QLabel( "?" ) ) ;
    peerBanScore.reset( new QLabel( "?" ) ) ;

    peerDetails.clear() ;
    peerDetails.push_back( std::make_pair( tr( "Direction" ), peerDirection.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Version" ), peerVersion.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "User Agent" ), peerSubversion.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Services" ), peerServices.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Starting Block" ), peerHeight.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Synced Headers" ), peerSyncHeight.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Synced Blocks" ), peerCommonHeight.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Connection Time" ), peerConnTime.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Last Send" ), peerLastSend.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Last Receive" ), peerLastRecv.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Sent" ), peerBytesSent.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Received" ), peerBytesRecv.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Ping Time" ), peerPingTime.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Ping Wait" ), peerPingWait.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Min Ping" ), peerMinPing.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Time Offset" ), peerTimeOffset.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Whitelisted" ), peerWhitelisted.get() ) ) ;
    peerDetails.push_back( std::make_pair( tr( "Ban Score" ), peerBanScore.get() ) ) ;

    peerDetailsWidget.reset( new QWidget() ) ;
    peerDetailsWidget->hide() ;

    QGridLayout * peerDetailsLayout = new QGridLayout() ;
    int row = 0 ;
    for ( std::pair< QString, QLabel * > detail : peerDetails )
    {
        QLabel * textLabel = new QLabel( detail.first, peerDetailsWidget.get() ) ;
        peerDetailsLayout->addWidget( textLabel, row, /* column */ 0 ) ;

        detail.second->setTextFormat( Qt::PlainText ) ;
        detail.second->setCursor( Qt::IBeamCursor ) ;
        detail.second->setTextInteractionFlags( interactionWithLabel ) ;
        detail.second->setBuddy( textLabel ) ;
        peerDetailsLayout->addWidget( detail.second, row, /* column */ 2 ) ;

        row ++ ;
    }

    QSpacerItem * spacerAfterPeerDetails = new QSpacerItem( /* width */ 20, /* height */ 40, QSizePolicy::Fixed, QSizePolicy::Expanding ) ;
    peerDetailsLayout->addItem( spacerAfterPeerDetails, row, /* column */ 1 ) ;

    peerDetailsWidget->setMinimumSize( /* width */ 300, /* height */ 0 ) ;
    peerDetailsWidget->setLayout( peerDetailsLayout ) ;
    ui->gridLayoutForPeersTab->addWidget( peerDetailsWidget.get(), /* row */ 1, /* column */ 1 ) ;

    didOnce = true ;
}

void RPCConsole::updateNodeDetail( const CNodeCombinedStats * stats )
{
    QString peerAddrDetails( QString::fromStdString( stats->nodeStats.addrName ) + " " ) ;
    peerAddrDetails += tr( "(node id: %1)" ).arg( QString::number( stats->nodeStats.nodeid ) ) ;
    if ( ! stats->nodeStats.addrLocal.empty() )
        peerAddrDetails += "<br />" + tr( "via %1" ).arg( QString::fromStdString( stats->nodeStats.addrLocal ) ) ;
    peerHeading->setText( peerAddrDetails ) ;

    peerServices->setText( GUIUtil::formatServices( stats->nodeStats.nServices ).replace( "&", "&&" ) ) ;
    peerLastSend->setText( stats->nodeStats.nLastSend != 0 ? seconds2hmmss( GetSystemTimeInSeconds() - stats->nodeStats.nLastSend ) : tr("never") ) ;
    peerLastRecv->setText( stats->nodeStats.nLastRecv != 0 ? seconds2hmmss( GetSystemTimeInSeconds() - stats->nodeStats.nLastRecv ) : tr("never") ) ;
    peerBytesSent->setText( QString::fromStdString( FormatBytes( stats->nodeStats.nSendBytes ) ) ) ;
    peerBytesRecv->setText( QString::fromStdString( FormatBytes( stats->nodeStats.nRecvBytes ) ) ) ;
    peerConnTime->setText( seconds2hmmss( GetSystemTimeInSeconds() - stats->nodeStats.nTimeConnected ) ) ;
    peerPingTime->setText( GUIUtil::formatPingTime( stats->nodeStats.dPingTime ) ) ;
    {
       double pingWait = stats->nodeStats.dPingWait ;
       peerPingWait->setText( GUIUtil::formatPingTime( pingWait ) ) ;
       peerPingWait->setVisible( pingWait > 0 ) ;
       if ( peerPingWait->buddy() != nullptr ) peerPingWait->buddy()->setVisible( pingWait > 0 ) ;
    }
    peerMinPing->setText( GUIUtil::formatPingTime( stats->nodeStats.dMinPing ) ) ;
    peerTimeOffset->setText( GUIUtil::formatTimeOffset( stats->nodeStats.nTimeOffset ) ) ;
    peerVersion->setText( QString::number( stats->nodeStats.nVersion ) ) ;
    peerSubversion->setText( QString::fromStdString( stats->nodeStats.cleanSubVer ) ) ;
    peerDirection->setText( stats->nodeStats.fInbound ? tr("Inbound") : tr("Outbound") ) ;
    peerHeight->setText( QString::number( stats->nodeStats.nStartingHeight ) ) ;
    peerWhitelisted->setText( stats->nodeStats.fWhitelisted ? tr("Yes") : tr("No") ) ;

    // This check fails for example if the lock was busy and nodeStateStats couldn't be fetched
    if ( stats->fNodeStateStatsAvailable ) {
        // Sync height is init to -1
        if ( stats->nodeStateStats.nSyncHeight > -1 )
            peerSyncHeight->setText( QString( "%1" ).arg( stats->nodeStateStats.nSyncHeight ) ) ;
        else
            peerSyncHeight->setText( tr("Unknown") ) ;

        // Common height is init to -1
        if ( stats->nodeStateStats.nCommonHeight > -1 )
            peerCommonHeight->setText( QString( "%1" ).arg( stats->nodeStateStats.nCommonHeight ) ) ;
        else
            peerCommonHeight->setText( tr("Unknown") ) ;

        // Ban score is init to 0
        int banScore = stats->nodeStateStats.nMisbehavior ;
        peerBanScore->setText( QString::number( banScore ) ) ;
        peerBanScore->setVisible( banScore > 0 ) ;
        if ( peerBanScore->buddy() != nullptr )
            peerBanScore->buddy()->setVisible( banScore > 0 ) ;
    }

    peerDetailsWidget->show() ;
}

void RPCConsole::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void RPCConsole::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);

    if ( ! networkModel || ! networkModel->getPeerTableModel() )
        return ;

    // start PeerTableModel auto refresh
    networkModel->getPeerTableModel()->startAutoRefresh() ;
}

void RPCConsole::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);

    if ( ! networkModel || ! networkModel->getPeerTableModel() )
        return ;

    // stop PeerTableModel auto refresh
    networkModel->getPeerTableModel()->stopAutoRefresh() ;
}

void RPCConsole::showPeersTableContextMenu(const QPoint& point)
{
    QModelIndex index = ui->peerWidget->indexAt(point);
    if (index.isValid())
        peersTableContextMenu->exec(QCursor::pos());
}

void RPCConsole::showBanTableContextMenu(const QPoint& point)
{
    QModelIndex index = ui->banlistWidget->indexAt(point);
    if (index.isValid())
        banTableContextMenu->exec(QCursor::pos());
}

#include <QInputDialog>
#include "netmessagemaker.h"

void RPCConsole::textMessageToSelectedNode()
{
    if ( g_connman == nullptr ) return ;

    // Get picked peers
    QList< QModelIndex > nodes = GUIUtil::getEntryData( ui->peerWidget, PeerTableModel::NetNodeId ) ;
    for ( int i = 0 ; i < nodes.count() ; i ++ )
    {
        NodeId id = nodes.at( i ).data().toLongLong() ;
        CNode* peer = g_connman->FindNode( id ) ;
        if ( peer == nullptr ) continue ;

        QInputDialog dialog( this ) ;
        dialog.setInputMode( QInputDialog::TextInput ) ;
        dialog.setWindowTitle( tr("Send text message to") + " "
                                + tr("peer") + " " + QString::number( peer->id )
                                + " (" + QString::fromStdString( peer->GetAddrName() ) + ")" ) ;
        dialog.setLabelText( tr("Message") ) ;
        dialog.setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::Fixed ) ;
        dialog.setMinimumSize( 555, dialog.height() ) ;
        dialog.resize( dialog.minimumSize() ) ;
        bool ok = dialog.exec() ; // show modal dialog
        QString text = dialog.textValue() ;
        if ( ok && ! text.isEmpty() )
        {
            LogPrintf( "%s: message \"%s\" to peer %i (%s)\n", __func__,
                text.toStdString(), peer->id, peer->GetAddrName() ) ;

            g_connman->PushMessage( peer, CNetMsgMaker( PROTOCOL_VERSION ).Make(
                NetMsgType::TEXTMESSAGE, text.toStdString()
            ) ) ;
        }
    }
}

void RPCConsole::disconnectSelectedNode()
{
    if ( g_connman == nullptr ) return ;

    // Get picked peers
    QList< QModelIndex > nodes = GUIUtil::getEntryData( ui->peerWidget, PeerTableModel::NetNodeId ) ;
    for ( int i = 0 ; i < nodes.count() ; i ++ )
    {
        NodeId id = nodes.at( i ).data().toLongLong() ;
        if ( g_connman->DisconnectNode( id ) )
            clearSelectedNode() ;
    }
}

void RPCConsole::banSelectedNode( int banSeconds )
{
    if ( ! networkModel || ! g_connman )
        return ;

    // Get picked peers
    QList< QModelIndex > nodes = GUIUtil::getEntryData( ui->peerWidget, PeerTableModel::NetNodeId ) ;
    for ( int i = 0 ; i < nodes.count() ; i ++ )
    {
        NodeId id = nodes.at( i ).data().toLongLong() ;

	int detailNodeRow = networkModel->getPeerTableModel()->getRowByNodeId( id ) ;
	if ( detailNodeRow < 0 ) return ;

	// Find possible nodes, ban it and clear the selected node
	const CNodeCombinedStats * stats = networkModel->getPeerTableModel()->getNodeStats( detailNodeRow ) ;
	if ( stats != nullptr )
	    g_connman->Ban( stats->nodeStats.addr, BanReasonManuallyAdded, banSeconds ) ;
    }
    clearSelectedNode() ;
    networkModel->getBanTableModel()->refresh() ;
}

void RPCConsole::unbanSelectedNode()
{
    if ( networkModel == nullptr ) return ;

    // Get picked entries
    QList< QModelIndex > nodes = GUIUtil::getEntryData( ui->banlistWidget, BanTableModel::Address ) ;
    for ( int i = 0 ; i < nodes.count() ; i ++ )
    {
        QString strNode = nodes.at( i ).data().toString() ;
        CSubNet possibleSubnet ;

        LookupSubNet( strNode.toStdString().c_str(), possibleSubnet ) ;
        if ( possibleSubnet.IsValid() && g_connman != nullptr )
        {
            g_connman->Unban( possibleSubnet ) ;
            networkModel->getBanTableModel()->refresh() ;
        }
    }
}

void RPCConsole::clearSelectedNode()
{
    ui->peerWidget->selectionModel()->clearSelection() ;
    cachedNodeids.clear() ;
    peerDetailsWidget->hide() ;
    peerHeading->setText( tr( "Select a peer to view detailed information" ) ) ;
}

void RPCConsole::showOrHideBanTableIfNeeded()
{
    if ( networkModel == nullptr ) return ;

    bool visible = ! networkModel->getBanTableModel()->isEmpty() ;
    ui->banlistWidget->setVisible( visible ) ;
    ui->banHeading->setVisible( visible ) ;
}

void RPCConsole::switchToRPCConsoleTab()
{
    ui->tabWidget->setCurrentWidget( ui->tab_console ) ;
}
