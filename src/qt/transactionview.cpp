// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "transactionview.h"

#include "addresstablemodel.h"
#include "unitsofcoin.h"
#include "csvmodelwriter.h"
#include "editaddressdialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactiondescdialog.h"
#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"

#include "ui_interface.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QScrollBar>
#include <QSignalMapper>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

TransactionView::TransactionView( const PlatformStyle * platformStyle, QWidget * parent ) :
    QWidget(parent), walletModel( nullptr ), transactionProxyModel( nullptr ),
    transactionTableView( nullptr ), abandonAction( nullptr ), columnResizingFixer( nullptr )
{
    // Build filter row
    setContentsMargins( 0, 0, 0, 0 ) ;

    QHBoxLayout * hlayout = new QHBoxLayout() ;
    hlayout->setContentsMargins( 0, 0, 0, 0 ) ;

    spacerBeforeFilteringWidgets = new QSpacerItem(
            /* width */ 23, /* height */ 7,
            /* hPolicy */ QSizePolicy::Fixed, /* vPolicy */ QSizePolicy::Fixed ) ;

    hlayout->addItem( spacerBeforeFilteringWidgets ) ;

    watchOnlyWidget = new QComboBox( this ) ;
    watchOnlyWidget->setFixedWidth( 24 ) ;
    watchOnlyWidget->addItem( "", TransactionFilterProxy::WatchOnlyFilter_All ) ;
    watchOnlyWidget->addItem( platformStyle->SingleColorIcon( ":/icons/eye_plus" ), "", TransactionFilterProxy::WatchOnlyFilter_Yes ) ;
    watchOnlyWidget->addItem( platformStyle->SingleColorIcon( ":/icons/eye_minus" ), "", TransactionFilterProxy::WatchOnlyFilter_No ) ;
    hlayout->addWidget( watchOnlyWidget ) ;

    static const int initialWidth = 120 ;

    dateWidget = new QComboBox( this ) ;
    dateWidget->setFixedWidth( initialWidth ) ;
    dateWidget->addItem( tr("All"), All ) ;
    dateWidget->addItem( tr("Today"), Today ) ;
    dateWidget->addItem( tr("This week"), ThisWeek ) ;
    dateWidget->addItem( tr("This month"), ThisMonth ) ;
    dateWidget->addItem( tr("Last month"), LastMonth ) ;
    dateWidget->addItem( tr("This year"), ThisYear ) ;
    dateWidget->addItem( tr("Range..."), Range ) ;
    hlayout->addWidget( dateWidget ) ;

    typeWidget = new QComboBox( this ) ;
    typeWidget->setFixedWidth( initialWidth ) ;

    typeWidget->addItem( tr("All"), TransactionFilterProxy::ALL_TYPES ) ;
    typeWidget->addItem( tr("Received with"), TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) |
                                              TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther) ) ;
    typeWidget->addItem( tr("Sent to"), TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) |
                                        TransactionFilterProxy::TYPE(TransactionRecord::SendToOther) ) ;
    typeWidget->addItem( tr("To self"), TransactionFilterProxy::TYPE(TransactionRecord::SendToSelf) ) ;
    typeWidget->addItem( tr("Mined"), TransactionFilterProxy::TYPE(TransactionRecord::Generated) ) ;
    typeWidget->addItem( tr("Other"), TransactionFilterProxy::TYPE(TransactionRecord::Other) ) ;

    hlayout->addWidget( typeWidget ) ;

    addressWidget = new QLineEdit( this ) ;
#if QT_VERSION >= 0x040700
    addressWidget->setPlaceholderText( tr("Enter address or label to search") ) ;
#endif
    hlayout->addWidget( addressWidget ) ;

    amountWidget = new QLineEdit( this ) ;
#if QT_VERSION >= 0x040700
    amountWidget->setPlaceholderText( tr("Min amount") ) ;
#endif

    amountWidget->setFixedWidth( initialWidth ) ;
    amountWidget->setValidator( new QDoubleValidator( 0, 1e20, 8, this ) ) ;
    hlayout->addWidget( amountWidget ) ;

    QVBoxLayout *vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0,0,0,0);
    vlayout->setSpacing(0);

    vlayout->addLayout(hlayout);

    this->dateFrom = new QDateTimeEdit( this ) ;
    this->dateTo = new QDateTimeEdit( this ) ;
    dateRangeWidget = createDateRangeWidget( dateFrom, dateTo ) ;
    dateRangeWidget->setVisible( false ) ; // hide by default
    vlayout->addWidget( dateRangeWidget ) ;

    connect( dateFrom, SIGNAL( dateChanged(QDate) ), this, SLOT( dateRangeChanged() ) ) ;
    connect( dateTo, SIGNAL( dateChanged(QDate) ), this, SLOT( dateRangeChanged() ) ) ;

    QTableView *view = new QTableView(this);
    vlayout->addWidget(view);
    vlayout->setSpacing(0);
    int width = view->verticalScrollBar()->sizeHint().width();
    // Cover scroll bar width with spacing
    if (platformStyle->getUseExtraSpacing()) {
        hlayout->addSpacing(width+2);
    } else {
        hlayout->addSpacing(width);
    }
    // Always show scroll bar
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->setTabKeyNavigation(false);
    view->setContextMenuPolicy(Qt::CustomContextMenu);

    view->installEventFilter(this);

    transactionTableView = view ;

    // Actions
    abandonAction = new QAction(tr("Abandon transaction"), this);
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
    QAction * copyTxHashAction = new QAction( tr("Copy transaction hash"), this ) ;
    QAction * copyTxHexAction = new QAction( tr("Copy raw transaction"), this ) ;
    QAction *copyTxPlainText = new QAction(tr("Copy full transaction details"), this);
    QAction *editLabelAction = new QAction(tr("Edit label"), this);
    QAction *showDetailsAction = new QAction(tr("Show transaction details"), this);

    contextMenu = new QMenu(this);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction( copyTxHashAction ) ;
    contextMenu->addAction( copyTxHexAction ) ;
    contextMenu->addAction(copyTxPlainText);
    contextMenu->addAction(showDetailsAction);
    contextMenu->addSeparator();
    contextMenu->addAction(abandonAction);
    contextMenu->addAction(editLabelAction);

    mapperThirdPartyTxUrls = new QSignalMapper( this ) ;

    // Connect actions
    connect( mapperThirdPartyTxUrls, SIGNAL( mapped(QString) ), this, SLOT( openThirdPartyTxUrl(QString) ) ) ;

    connect(dateWidget, SIGNAL(activated(int)), this, SLOT(chooseDate(int)));
    connect(typeWidget, SIGNAL(activated(int)), this, SLOT(chooseType(int)));
    connect(watchOnlyWidget, SIGNAL(activated(int)), this, SLOT(chooseWatchonly(int)));
    connect(addressWidget, SIGNAL(textChanged(QString)), this, SLOT(changedPrefix(QString)));
    connect(amountWidget, SIGNAL(textChanged(QString)), this, SLOT(changedAmount(QString)));

    connect(view, SIGNAL(doubleClicked(QModelIndex)), this, SIGNAL(doubleClicked(QModelIndex)));
    connect(view, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    connect(abandonAction, SIGNAL(triggered()), this, SLOT(abandonTx()));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect( copyTxHashAction, SIGNAL( triggered() ), this, SLOT( copyTxHash() ) ) ;
    connect( copyTxHexAction, SIGNAL( triggered() ), this, SLOT( copyTxHex() ) ) ;
    connect(copyTxPlainText, SIGNAL(triggered()), this, SLOT(copyTxPlainText()));
    connect(editLabelAction, SIGNAL(triggered()), this, SLOT(editLabel()));
    connect(showDetailsAction, SIGNAL(triggered()), this, SLOT(showDetails()));
}

void TransactionView::setWalletModel( WalletModel * model )
{
    this->walletModel = model ;
    if ( model != nullptr )
    {
        transactionProxyModel = new TransactionFilterProxy(this);
        transactionProxyModel->setSourceModel( model->getTransactionTableModel() ) ;
        transactionProxyModel->setDynamicSortFilter(true);
        transactionProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        transactionProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

        transactionProxyModel->setSortRole(Qt::EditRole);

        transactionTableView->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff ) ;
        transactionTableView->setModel( transactionProxyModel ) ;
        transactionTableView->setAlternatingRowColors( true ) ;
        transactionTableView->setSelectionBehavior( QAbstractItemView::SelectRows ) ;
        transactionTableView->setSelectionMode( QAbstractItemView::ExtendedSelection ) ;
        transactionTableView->setSortingEnabled( true ) ;
        transactionTableView->sortByColumn( TransactionTableModel::Date, Qt::DescendingOrder ) ;
        transactionTableView->verticalHeader()->hide() ;

        transactionTableView->resizeColumnsToContents() ;

        static const int add2widths = 12 ;
        transactionTableView->setColumnWidth(
                TransactionTableModel::Date,
                transactionTableView->columnWidth( TransactionTableModel::Date ) + add2widths ) ;
        transactionTableView->setColumnWidth(
                TransactionTableModel::Type,
                transactionTableView->columnWidth( TransactionTableModel::Type ) + add2widths ) ;
        transactionTableView->setColumnWidth(
                TransactionTableModel::Status,
                transactionTableView->columnWidth( TransactionTableModel::Status ) + ( add2widths >> 1 ) ) ;

        static const int minimum_width_of_column = 23 ;
        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(
                                    transactionTableView,
                                    transactionTableView->columnWidth( TransactionTableModel::Amount ),
                                    minimum_width_of_column, this ) ;

        // resize filtering widgets along with table columns
        connect( transactionTableView->horizontalHeader(), SIGNAL( geometriesChanged() ), this, SLOT( updateWidths() ) ) ;
        connect( transactionTableView->horizontalHeader(), SIGNAL( sectionResized(int, int, int) ), this, SLOT( updateWidths() ) ) ;

        if ( model->getOptionsModel() != nullptr )
        {
            // Add third party transaction URLs to context menu
            QStringList listUrls = model->getOptionsModel()->getThirdPartyTxUrls().split( "|", QString::SkipEmptyParts ) ;
            for ( size_t i = 0 ; i < listUrls.size() ; ++ i )
            {
                QString host = QUrl( listUrls[ i ].trimmed(), QUrl::StrictMode ).host() ;
                if ( ! host.isEmpty() )
                {
                    QAction * thirdPartyTxUrlAction = new QAction( host, this ) ; // use host as menu item label
                    if ( i == 0 ) contextMenu->addSeparator() ;
                    contextMenu->addAction( thirdPartyTxUrlAction ) ;
                    connect( thirdPartyTxUrlAction, SIGNAL( triggered() ), mapperThirdPartyTxUrls, SLOT( map() ) ) ;
                    mapperThirdPartyTxUrls->setMapping( thirdPartyTxUrlAction, listUrls[ i ].trimmed() ) ;
                }
            }
        }

        // show/hide column Watch-only
        updateWatchOnlyColumn( model->haveWatchOnly() ) ;

        // Watch-only signal
        connect( model, SIGNAL( notifyWatchonlyChanged(bool) ), this, SLOT( updateWatchOnlyColumn(bool) ) ) ;
    }
}

void TransactionView::updateWidths()
{
    spacerBeforeFilteringWidgets->changeSize(
            /* width */ transactionTableView->columnViewportPosition( TransactionTableModel::Date ),
            /* height */ 7,
            /* hPolicy */ QSizePolicy::Fixed, /* vPolicy */ QSizePolicy::Fixed ) ;

    dateWidget->setFixedWidth( transactionTableView->columnWidth( TransactionTableModel::Date ) ) ;
    typeWidget->setFixedWidth( transactionTableView->columnWidth( TransactionTableModel::Type ) ) ;
    addressWidget->setFixedWidth( transactionTableView->columnWidth( TransactionTableModel::ToAddress ) ) ;
    amountWidget->setFixedWidth( transactionTableView->columnWidth( TransactionTableModel::Amount ) ) ;
}

void TransactionView::chooseDate(int idx)
{
    if(!transactionProxyModel)
        return;
    QDate current = QDate::currentDate();
    dateRangeWidget->setVisible(false);
    switch(dateWidget->itemData(idx).toInt())
    {
    case All:
        transactionProxyModel->setDateRange(
                TransactionFilterProxy::MIN_DATE,
                TransactionFilterProxy::MAX_DATE);
        break;
    case Today:
        transactionProxyModel->setDateRange(
                QDateTime(current),
                TransactionFilterProxy::MAX_DATE);
        break;
    case ThisWeek: {
        // Find last Monday
        QDate startOfWeek = current.addDays(-(current.dayOfWeek()-1));
        transactionProxyModel->setDateRange(
                QDateTime(startOfWeek),
                TransactionFilterProxy::MAX_DATE);

        } break;
    case ThisMonth:
        transactionProxyModel->setDateRange(
                QDateTime(QDate(current.year(), current.month(), 1)),
                TransactionFilterProxy::MAX_DATE);
        break;
    case LastMonth:
        transactionProxyModel->setDateRange(
                QDateTime(QDate(current.year(), current.month(), 1).addMonths(-1)),
                QDateTime(QDate(current.year(), current.month(), 1)));
        break;
    case ThisYear:
        transactionProxyModel->setDateRange(
                QDateTime(QDate(current.year(), 1, 1)),
                TransactionFilterProxy::MAX_DATE);
        break;
    case Range:
        dateRangeWidget->setVisible(true);
        dateRangeChanged();
        break;
    }
}

void TransactionView::chooseType(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setTypeFilter(
        typeWidget->itemData(idx).toInt());
}

void TransactionView::chooseWatchonly(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setWatchOnlyFilter(
        (TransactionFilterProxy::WatchOnlyFilter)watchOnlyWidget->itemData(idx).toInt());
}

void TransactionView::changedPrefix( const QString & prefix )
{
    if ( transactionProxyModel == nullptr ) return ;

    transactionProxyModel->setAddressPrefix( prefix ) ;
}

void TransactionView::changedAmount( const QString & amount )
{
    if ( transactionProxyModel == nullptr ) return ;

    CAmount amount_parsed = 0 ;
    if ( UnitsOfCoin::parseString( walletModel->getOptionsModel()->getDisplayUnit(), amount, &amount_parsed ) )
        transactionProxyModel->setMinAmount( amount_parsed ) ;
    else
        transactionProxyModel->setMinAmount( 0 ) ;
}

void TransactionView::exportClicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName( this,
        tr("Export Transaction History"), QString(),
        tr("Comma separated file (*.csv)"), NULL ) ;

    if ( filename.isNull() )
        return ;

    CSVModelWriter writer( filename ) ;

    writer.setModel( transactionProxyModel ) ;
    // name, column, role
    writer.addColumn( tr("Confirmed"), 0, TransactionTableModel::ConfirmedRole ) ;
    if ( walletModel && walletModel->haveWatchOnly() )
        writer.addColumn( tr("Watch-only"), TransactionTableModel::Watchonly ) ;
    writer.addColumn( tr("Date"), 0, TransactionTableModel::DateRole ) ;
    writer.addColumn( tr("Type"), TransactionTableModel::Type, Qt::EditRole ) ;
    writer.addColumn( tr("Label"), 0, TransactionTableModel::LabelRole ) ;
    writer.addColumn( tr("Address"), 0, TransactionTableModel::AddressRole ) ;
    writer.addColumn( GUIUtil::makeTitleForAmountColumn( walletModel->getOptionsModel()->getDisplayUnit() ), 0, TransactionTableModel::FormattedAmountRole ) ;
    writer.addColumn( tr("Hash"), 0, TransactionTableModel::TxHashRole ) ;

    if ( ! writer.write() ) {
        Q_EMIT message( tr("Exporting Failed"), tr("There was an error trying to save the transaction history to %1").arg( filename ),
            CClientUserInterface::MSG_ERROR ) ;
    }
    else {
        Q_EMIT message( tr("Exporting Successful"), tr("The transaction history was successfully saved to %1").arg( filename ),
            CClientUserInterface::MSG_INFORMATION ) ;
    }
}

void TransactionView::contextualMenu(const QPoint &point)
{
    QModelIndex index = transactionTableView->indexAt( point ) ;
    QModelIndexList selection = transactionTableView->selectionModel()->selectedRows( 0 ) ;
    if (selection.empty())
        return;

    // check if transaction can be abandoned, disable context menu action in case it doesn't
    uint256 hash;
    hash.SetHex(selection.at(0).data(TransactionTableModel::TxHashRole).toString().toStdString());
    abandonAction->setEnabled( walletModel->transactionCanBeAbandoned( hash ) ) ;

    if(index.isValid())
    {
        contextMenu->exec(QCursor::pos());
    }
}

void TransactionView::abandonTx()
{
    if ( ! transactionTableView || ! transactionTableView->selectionModel() )
        return ;

    QModelIndexList selection = transactionTableView->selectionModel()->selectedRows( 0 ) ;

    // get the hash from the TxHashRole (QVariant / QString)
    uint256 hash;
    QString hashQStr = selection.at(0).data(TransactionTableModel::TxHashRole).toString();
    hash.SetHex(hashQStr.toStdString());

    // Abandon the wallet transaction over the walletModel
    walletModel->abandonTransaction( hash ) ;

    // Update the table
    walletModel->getTransactionTableModel()->updateTransaction( hashQStr, CT_UPDATED, false ) ;
}

void TransactionView::copyAddress()
{
    GUIUtil::copyEntryData( transactionTableView, 0, TransactionTableModel::AddressRole ) ;
}

void TransactionView::copyLabel()
{
    GUIUtil::copyEntryData( transactionTableView, 0, TransactionTableModel::LabelRole ) ;
}

void TransactionView::copyAmount()
{
    GUIUtil::copyEntryData( transactionTableView, 0, TransactionTableModel::FormattedAmountRole ) ;
}

void TransactionView::copyTxHash()
{
    GUIUtil::copyEntryData( transactionTableView, 0, TransactionTableModel::TxHashRole ) ;
}

void TransactionView::copyTxHex()
{
    GUIUtil::copyEntryData( transactionTableView, 0, TransactionTableModel::TxHexRole ) ;
}

void TransactionView::copyTxPlainText()
{
    GUIUtil::copyEntryData( transactionTableView, 0, TransactionTableModel::TxPlainTextRole ) ;
}

void TransactionView::editLabel()
{
    if ( ! transactionTableView->selectionModel() || ! walletModel ) return ;

    QModelIndexList selection = transactionTableView->selectionModel()->selectedRows() ;
    if ( ! selection.isEmpty() )
    {
        AddressTableModel * addressBook = walletModel->getAddressTableModel() ;
        if(!addressBook)
            return;
        QString address = selection.at(0).data(TransactionTableModel::AddressRole).toString();
        if(address.isEmpty())
        {
            // If this transaction has no associated address, exit
            return;
        }
        // Is address in address book? Address book can miss address when a transaction is
        // sent from outside the UI
        int idx = addressBook->lookupAddress(address);
        if(idx != -1)
        {
            // Edit sending / receiving address
            QModelIndex modelIdx = addressBook->index(idx, 0, QModelIndex());
            // Determine type of address, launch appropriate editor dialog type
            QString type = modelIdx.data(AddressTableModel::TypeRole).toString();

            EditAddressDialog dlg(
                type == AddressTableModel::Receive
                ? EditAddressDialog::EditReceivingAddress
                : EditAddressDialog::EditSendingAddress, this);
            dlg.setAddressTableModel( addressBook ) ;
            dlg.loadRow(idx);
            dlg.exec();
        }
        else
        {
            // Add sending address
            EditAddressDialog dlg( EditAddressDialog::NewSendingAddress, this ) ;
            dlg.setAddressTableModel( addressBook ) ;
            dlg.setAddress(address);
            dlg.exec();
        }
    }
}

void TransactionView::showDetails()
{
    if ( transactionTableView == nullptr || transactionTableView->selectionModel() == nullptr ) return ;

    QModelIndexList selection = transactionTableView->selectionModel()->selectedRows() ;
    if ( ! selection.isEmpty() )
    {
        TransactionDescDialog *dlg = new TransactionDescDialog(selection.at(0));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
}

void TransactionView::openThirdPartyTxUrl(QString url)
{
    if ( ! transactionTableView || ! transactionTableView->selectionModel() ) return ;

    QModelIndexList selection = transactionTableView->selectionModel()->selectedRows( 0 ) ;
    if(!selection.isEmpty())
         QDesktopServices::openUrl(QUrl::fromUserInput(url.replace("%s", selection.at(0).data(TransactionTableModel::TxHashRole).toString())));
}

QFrame * TransactionView::createDateRangeWidget( QDateTimeEdit * dateFrom, QDateTimeEdit * dateTo )
{
    QFrame * widget = new QFrame() ;
    widget->setFrameStyle( QFrame::Panel | QFrame::Raised ) ;
    widget->setContentsMargins( 1, 1, 1, 1 ) ;

    QHBoxLayout * layout = new QHBoxLayout( widget ) ;
    layout->setContentsMargins( 0, 0, 0, 0 ) ;
    layout->addSpacing( 23 ) ;
    layout->addWidget( new QLabel( tr("Range:") ) ) ;

    if ( dateFrom != nullptr ) {
        dateFrom->setDisplayFormat( "dd/MM/yy" ) ;
        dateFrom->setCalendarPopup( true ) ;
        dateFrom->setMinimumWidth( 100 ) ;
        dateFrom->setDate( QDate::currentDate().addDays( -7 ) ) ;
        layout->addWidget( dateFrom ) ;
    }

    layout->addWidget( new QLabel( tr("to") ) ) ;

    if ( dateTo != nullptr ) {
        dateTo->setDisplayFormat( "dd/MM/yy" ) ;
        dateTo->setCalendarPopup( true ) ;
        dateTo->setMinimumWidth( 100 ) ;
        dateTo->setDate( QDate::currentDate() ) ;
        layout->addWidget( dateTo ) ;
    }

    layout->addStretch() ;

    return widget ;
}

void TransactionView::dateRangeChanged()
{
    if ( transactionProxyModel == nullptr ) return ;

    transactionProxyModel->setDateRange(
            QDateTime(dateFrom->date()),
            QDateTime(dateTo->date()).addDays(1));
}

void TransactionView::focusTransaction(const QModelIndex &idx)
{
    if ( transactionProxyModel == nullptr ) return ;

    QModelIndex targetIdx = transactionProxyModel->mapFromSource( idx ) ;
    transactionTableView->scrollTo( targetIdx ) ;
    transactionTableView->setCurrentIndex( targetIdx ) ;
    transactionTableView->setFocus() ;
}

// Override the virtual resizeEvent of the QWidget to adjust table's column
// sizes as the tables width is proportional to the page width
void TransactionView::resizeEvent( QResizeEvent * event )
{
    QWidget::resizeEvent( event ) ;
    columnResizingFixer->stretchColumnWidth( TransactionTableModel::ToAddress ) ;
}

// Need to override default Ctrl+C action for amount as default behaviour is just to copy DisplayRole text
bool TransactionView::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_C && ke->modifiers().testFlag(Qt::ControlModifier))
        {
             GUIUtil::copyEntryData( transactionTableView, 0, TransactionTableModel::TxPlainTextRole ) ;
             return true ;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// show/hide column Watch-only
void TransactionView::updateWatchOnlyColumn( bool fHaveWatchOnly )
{
    watchOnlyWidget->setVisible( fHaveWatchOnly ) ;
    transactionTableView->setColumnHidden( TransactionTableModel::Watchonly, ! fHaveWatchOnly ) ;
}
