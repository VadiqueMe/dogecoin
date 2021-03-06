// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "coincontroldialog.h"
#include "ui_coincontroldialog.h"

#include "addresstablemodel.h"
#include "unitsofcoin.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "txmempool.h"
#include "walletmodel.h"

#include "wallet/coincontrol.h"
#include "init.h"
#include "policy/policy.h"
#include "validation.h" // for mempool
#include "wallet/wallet.h"

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFlags>
#include <QIcon>
#include <QSettings>
#include <QString>
#include <QTreeWidget>
#include <QTreeWidgetItem>

QList<CAmount> CoinControlDialog::payAmounts;
CCoinControl* CoinControlDialog::coinControl = new CCoinControl();
bool CoinControlDialog::fSubtractFeeFromAmount = false;

bool CCoinControlWidgetItem::operator<(const QTreeWidgetItem &other) const {
    int column = treeWidget()->sortColumn();
    if (column == CoinControlDialog::COLUMN_AMOUNT || column == CoinControlDialog::COLUMN_DATE || column == CoinControlDialog::COLUMN_CONFIRMATIONS)
        return data(column, Qt::UserRole).toLongLong() < other.data(column, Qt::UserRole).toLongLong();
    return QTreeWidgetItem::operator<(other);
}

CoinControlDialog::CoinControlDialog( const PlatformStyle * style, QWidget * parent ) :
    QDialog(parent),
    ui(new Ui::CoinControlDialog),
    walletModel( nullptr ),
    platformStyle( style )
{
    ui->setupUi( this ) ;

    // context menu actions
    QAction * copyAddressAction = new QAction( tr("Copy address"), this ) ;
    QAction * copyLabelAction = new QAction( tr("Copy label"), this ) ;
    QAction * copyAmountAction = new QAction( tr("Copy amount"), this ) ;
              copyTransactionHashAction = new QAction( tr( "Copy transaction hash" ), this ) ; // need to enable/disable this
              lockAction = new QAction( tr("Lock unspent"), this ) ; // need to enable/disable this
              unlockAction = new QAction( tr("Unlock unspent"), this ) ; // need to enable/disable this

    // context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyTransactionHashAction);
    contextMenu->addSeparator();
    contextMenu->addAction(lockAction);
    contextMenu->addAction(unlockAction);

    // context menu signals
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(showMenu(QPoint)));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyTransactionHashAction, SIGNAL(triggered()), this, SLOT(copyTransactionHash()));
    connect(lockAction, SIGNAL(triggered()), this, SLOT(lockCoin()));
    connect(unlockAction, SIGNAL(triggered()), this, SLOT(unlockCoin()));

    // clipboard actions
    QAction * clipboardQuantityAction = new QAction( tr("Copy quantity"), this ) ;
    QAction * clipboardAmountAction = new QAction( tr("Copy amount"), this ) ;
    QAction * clipboardFeeAction = new QAction( tr("Copy fee"), this ) ;
    QAction * clipboardAfterFeeAction = new QAction( tr("Copy after fee"), this) ;
    QAction * clipboardBytesAction = new QAction( tr("Copy bytes"), this ) ;
    QAction * clipboardChangeAction = new QAction( tr("Copy change"), this ) ;

    connect(clipboardQuantityAction, SIGNAL(triggered()), this, SLOT(clipboardQuantity()));
    connect(clipboardAmountAction, SIGNAL(triggered()), this, SLOT(clipboardAmount()));
    connect(clipboardFeeAction, SIGNAL(triggered()), this, SLOT(clipboardFee()));
    connect(clipboardAfterFeeAction, SIGNAL(triggered()), this, SLOT(clipboardAfterFee()));
    connect(clipboardBytesAction, SIGNAL(triggered()), this, SLOT(clipboardBytes()));
    connect(clipboardChangeAction, SIGNAL(triggered()), this, SLOT(clipboardChange()));

    ui->labelCoinControlQuantity->addAction(clipboardQuantityAction);
    ui->labelCoinControlAmount->addAction(clipboardAmountAction);
    ui->labelCoinControlFee->addAction(clipboardFeeAction);
    ui->labelCoinControlAfterFee->addAction(clipboardAfterFeeAction);
    ui->labelCoinControlBytes->addAction(clipboardBytesAction);
    ui->labelCoinControlChange->addAction(clipboardChangeAction);

    ui->choiceList->setText( QString() ) ;
    ui->choiceList->setIcon( platformStyle->SingleColorIcon( ":/icons/listview" ) ) ;
    ui->choiceList->setIconSize( QSize( 33, 22 ) ) ;

    ui->choiceTree->setText( QString() ) ;
    ui->choiceTree->setIcon( platformStyle->SingleColorIcon( ":/icons/treeview" ) ) ;
    ui->choiceTree->setIconSize( QSize( 33, 22 ) ) ;

    // toggle tree/list view
    connect( ui->choiceTree, SIGNAL( toggled(bool) ), this, SLOT( toTreeView(bool) ) ) ;
    connect( ui->choiceList, SIGNAL( toggled(bool) ), this, SLOT( toListView(bool) ) ) ;

    // click on checkbox
    connect( ui->treeWidget, SIGNAL( itemChanged(QTreeWidgetItem *, int) ), this, SLOT( viewItemChanged(QTreeWidgetItem *, int) ) ) ;

    // click on header
#if QT_VERSION < 0x050000
    ui->treeWidget->header()->setClickable(true);
#else
    ui->treeWidget->header()->setSectionsClickable(true);
#endif
    connect(ui->treeWidget->header(), SIGNAL(sectionClicked(int)), this, SLOT(headerSectionClicked(int)));

    // ok button
    connect( ui->buttonBox, SIGNAL( clicked(QAbstractButton *) ), this, SLOT( buttonBoxClicked(QAbstractButton *) ) ) ;

    // (un)select all
    connect( ui->selectAllButton, SIGNAL( clicked() ), this, SLOT( selectAllClicked() ) ) ;

    // change coin control first column label due Qt4 bug
    // see https://github.com/bitcoin/bitcoin/issues/5716
    ui->treeWidget->headerItem()->setText( COLUMN_CHECKBOX, QString() ) ;

    ui->treeWidget->setColumnWidth( COLUMN_CHECKBOX, 75 ) ;
    ui->treeWidget->setColumnWidth( COLUMN_AMOUNT, 155 ) ;
    ui->treeWidget->setColumnWidth( COLUMN_ADDRESS, 320 ) ;
    ui->treeWidget->setColumnWidth( COLUMN_LABEL, 130 ) ;
    ui->treeWidget->setColumnWidth( COLUMN_DATE, 166 ) ;
    ui->treeWidget->setColumnWidth( COLUMN_CONFIRMATIONS, 99 ) ;
    ui->treeWidget->setColumnHidden( COLUMN_TXHASH, true ) ;     // store transaction hash in this column, but don't show it
    ui->treeWidget->setColumnHidden( COLUMN_VOUT_INDEX, true ) ; // store vout index in this column, but don't show it

    // default is to sort by amount descending
    sortView( COLUMN_AMOUNT, Qt::DescendingOrder ) ;

    QSettings settings ;

    if ( ! settings.contains( "fCoinControlListView" ) )
        settings.setValue( "fCoinControlListView", false ) ;

    if ( settings.value( "fCoinControlListView" ).toBool() )
        ui->choiceList->click() ;
    else
        ui->choiceTree->click() ;

    if ( settings.contains( "nCoinControlSortColumn" ) && settings.contains( "nCoinControlSortOrder" ) )
        sortView( settings.value( "nCoinControlSortColumn" ).toInt(), (Qt::SortOrder)settings.value( "nCoinControlSortOrder" ).toInt() ) ;
}

CoinControlDialog::~CoinControlDialog()
{
    QSettings settings ;
    settings.setValue( "fCoinControlListView", ui->choiceList->isChecked() ) ;
    settings.setValue( "nCoinControlSortColumn", sortColumn ) ;
    settings.setValue( "nCoinControlSortOrder", (int)sortOrder ) ;

    delete ui ;
}

void CoinControlDialog::setWalletModel( WalletModel * model )
{
    this->walletModel = model ;

    if ( model && model->getOptionsModel() && model->getAddressTableModel() )
    {
        updateView() ;
        updateLockedLabel() ;
        CoinControlDialog::updateLabels( model, this ) ;
    }
}

// ok button
void CoinControlDialog::buttonBoxClicked(QAbstractButton* button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole)
        done(QDialog::Accepted); // closes the dialog
}

// (un)select all
void CoinControlDialog::selectAllClicked()
{
    Qt::CheckState state = Qt::Checked;
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
    {
        if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != Qt::Unchecked)
        {
            state = Qt::Unchecked;
            break;
        }
    }
    ui->treeWidget->setEnabled(false);
    for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) != state)
                ui->treeWidget->topLevelItem(i)->setCheckState(COLUMN_CHECKBOX, state);
    ui->treeWidget->setEnabled(true);
    if (state == Qt::Unchecked)
        coinControl->UnSelectAll(); // just to be sure
    CoinControlDialog::updateLabels( walletModel, this ) ;
}

// context menu
void CoinControlDialog::showMenu(const QPoint &point)
{
    QTreeWidgetItem *item = ui->treeWidget->itemAt(point);
    if(item)
    {
        contextMenuItem = item;

        // disable some items (like copy transaction hash, lock, unlock) for tree roots in context menu
        if (item->text(COLUMN_TXHASH).length() == 64) // transaction hash is 64 characters (this means its a child node, so its not a parent node in tree view)
        {
            copyTransactionHashAction->setEnabled(true);
            if ( walletModel->isLockedCoin( uint256S( item->text( COLUMN_TXHASH ).toStdString() ), item->text( COLUMN_VOUT_INDEX ).toUInt() ) )
            {
                lockAction->setEnabled(false);
                unlockAction->setEnabled(true);
            }
            else
            {
                lockAction->setEnabled(true);
                unlockAction->setEnabled(false);
            }
        }
        else // this means click on parent node in tree view -> disable all
        {
            copyTransactionHashAction->setEnabled(false);
            lockAction->setEnabled(false);
            unlockAction->setEnabled(false);
        }

        // show context menu
        contextMenu->exec(QCursor::pos());
    }
}

// context menu action: copy amount
void CoinControlDialog::copyAmount()
{
    GUIUtil::setClipboard( UnitsOfCoin::removeSpaces( contextMenuItem->text( COLUMN_AMOUNT ) ) ) ;
}

// context menu action: copy label
void CoinControlDialog::copyLabel()
{
    if ( ui->choiceTree->isChecked() && contextMenuItem->text( COLUMN_LABEL ).length() == 0 && contextMenuItem->parent() )
        GUIUtil::setClipboard( contextMenuItem->parent()->text( COLUMN_LABEL ) ) ;
    else
        GUIUtil::setClipboard( contextMenuItem->text( COLUMN_LABEL ) ) ;
}

// context menu action: copy address
void CoinControlDialog::copyAddress()
{
    if ( ui->choiceTree->isChecked() && contextMenuItem->text( COLUMN_ADDRESS ).length() == 0 && contextMenuItem->parent() )
        GUIUtil::setClipboard( contextMenuItem->parent()->text( COLUMN_ADDRESS ) ) ;
    else
        GUIUtil::setClipboard( contextMenuItem->text( COLUMN_ADDRESS ) ) ;
}

// context menu action: copy transaction hash
void CoinControlDialog::copyTransactionHash()
{
    GUIUtil::setClipboard( contextMenuItem->text( COLUMN_TXHASH ) ) ;
}

// context menu action: lock coin
void CoinControlDialog::lockCoin()
{
    if ( contextMenuItem->checkState( COLUMN_CHECKBOX ) == Qt::Checked )
        contextMenuItem->setCheckState( COLUMN_CHECKBOX, Qt::Unchecked ) ;

    COutPoint outpt( uint256S( contextMenuItem->text( COLUMN_TXHASH ).toStdString() ), contextMenuItem->text( COLUMN_VOUT_INDEX ).toUInt() ) ;
    walletModel->lockCoin( outpt ) ;
    contextMenuItem->setDisabled( true ) ;
    contextMenuItem->setIcon( COLUMN_CHECKBOX, platformStyle->SingleColorIcon( ":/icons/lock_closed" ) ) ;
    updateLockedLabel() ;
}

// context menu action: unlock coin
void CoinControlDialog::unlockCoin()
{
    COutPoint outpt( uint256S( contextMenuItem->text( COLUMN_TXHASH ).toStdString() ), contextMenuItem->text( COLUMN_VOUT_INDEX ).toUInt() ) ;
    walletModel->unlockCoin( outpt ) ;
    contextMenuItem->setDisabled( false ) ;
    contextMenuItem->setIcon( COLUMN_CHECKBOX, QIcon() ) ;
    updateLockedLabel() ;
}

// copy label "Quantity" to clipboard
void CoinControlDialog::clipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
}

// copy label "Amount" to clipboard
void CoinControlDialog::clipboardAmount()
{
    GUIUtil::setClipboard(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" ")));
}

// copy label "Fee" to clipboard
void CoinControlDialog::clipboardFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// copy label "After fee" to clipboard
void CoinControlDialog::clipboardAfterFee()
{
    GUIUtil::setClipboard(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// copy label "Bytes" to clipboard
void CoinControlDialog::clipboardBytes()
{
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace(ASYMP_UTF8, ""));
}

// copy label "Change" to clipboard
void CoinControlDialog::clipboardChange()
{
    GUIUtil::setClipboard(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")).replace(ASYMP_UTF8, ""));
}

// treeview: sort
void CoinControlDialog::sortView(int column, Qt::SortOrder order)
{
    sortColumn = column;
    sortOrder = order;
    ui->treeWidget->sortItems(column, order);
    ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
}

// treeview: clicked on header
void CoinControlDialog::headerSectionClicked(int logicalIndex)
{
    if (logicalIndex == COLUMN_CHECKBOX) // click on most left column -> do nothing
    {
        ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
    }
    else
    {
        if (sortColumn == logicalIndex)
            sortOrder = ((sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder);
        else
        {
            sortColumn = logicalIndex;
            sortOrder = ((sortColumn == COLUMN_LABEL || sortColumn == COLUMN_ADDRESS) ? Qt::AscendingOrder : Qt::DescendingOrder); // if label or address then default => asc, else default => desc
        }

        sortView(sortColumn, sortOrder);
    }
}

// switch to tree view
void CoinControlDialog::toTreeView( bool checked )
{
    if ( checked ) updateView() ;
}

// switch to list view
void CoinControlDialog::toListView( bool checked )
{
    if ( checked ) updateView() ;
}

// checkbox clicked by user
void CoinControlDialog::viewItemChanged(QTreeWidgetItem* item, int column)
{
    // transaction hash is 64 characters long (this means it's a child node, not a parent node in tree view)
    if ( column == COLUMN_CHECKBOX && item->text( COLUMN_TXHASH ).length() == 64 )
    {
        COutPoint outpt( uint256S( item->text( COLUMN_TXHASH ).toStdString() ), item->text( COLUMN_VOUT_INDEX ).toUInt() ) ;

        if (item->checkState(COLUMN_CHECKBOX) == Qt::Unchecked)
            coinControl->UnSelect(outpt);
        else if (item->isDisabled()) // locked (this happens if "check all" through parent node)
            item->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        else
            coinControl->Select(outpt);

        // selection changed -> update labels
        if ( ui->treeWidget->isEnabled() ) // do not update on every click for (un)select all
            CoinControlDialog::updateLabels( walletModel, this ) ;
    }

    // TODO: Remove this temporary qt5 fix after Qt5.3 and Qt5.4 are no longer used
    //       Fixed in Qt5.5 and above: https://bugreports.qt.io/browse/QTBUG-43473
#if QT_VERSION >= 0x050000
    else if (column == COLUMN_CHECKBOX && item->childCount() > 0)
    {
        if (item->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked && item->child(0)->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked)
            item->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
    }
#endif
}

// shows count of locked unspent outputs
void CoinControlDialog::updateLockedLabel()
{
    std::vector< COutPoint > vOutpts ;
    walletModel->listLockedCoins( vOutpts ) ;
    if ( vOutpts.size() > 0 )
    {
       ui->lockedLabel->setText( tr("(%1 locked)").arg( vOutpts.size() ) ) ;
       ui->lockedLabel->setVisible( true ) ;
    }
    else ui->lockedLabel->setVisible( false ) ;
}

void CoinControlDialog::updateLabels( WalletModel * model, QDialog * dialog )
{
    if ( model == nullptr ) return ;

    // nPayAmount
    CAmount nPayAmount = 0 ;
    CMutableTransaction txDummy ;
    for ( const CAmount & amount : CoinControlDialog::payAmounts )
    {
        nPayAmount += amount ;

        if ( amount > 0 )
        {
            CTxOut txout( amount, (CScript)std::vector< unsigned char >( 24, 0 ) ) ;
            txDummy.vout.push_back( txout ) ;
        }
    }

    CAmount nAmount             = 0;
    CAmount nAfterFee           = 0;
    CAmount nChange             = 0;
    unsigned int nBytes         = 0;
    unsigned int nBytesInputs   = 0;
    double dPriority            = 0;
    double dPriorityInputs      = 0;
    unsigned int nQuantity      = 0;
    int nQuantityUncompressed   = 0;
    bool fWitness               = false;

    std::vector<COutPoint> vCoinControl;
    std::vector<COutput>   vOutputs;
    coinControl->ListSelected(vCoinControl);
    model->getOutputs(vCoinControl, vOutputs);

    for ( const COutput & out : vOutputs ) {
        // unselect already spent, very unlikely scenario, this could happen
        // when selected are spent elsewhere, like rpc or another computer
        uint256 txhash = out.tx->GetTxHash() ;
        COutPoint outpt(txhash, out.i);
        if (model->isSpent(outpt))
        {
            coinControl->UnSelect(outpt);
            continue;
        }

        // Quantity
        nQuantity++;

        // Amount
        nAmount += out.tx->tx->vout[out.i].nValue;

        // Priority
        dPriorityInputs += (double)out.tx->tx->vout[out.i].nValue * (out.nDepth+1);

        // Bytes
        CTxDestination address;
        int witnessversion = 0;
        std::vector<unsigned char> witnessprogram;
        if (out.tx->tx->vout[out.i].scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram))
        {
            nBytesInputs += (32 + 4 + 1 + (107 / WITNESS_SCALE_FACTOR) + 4);
            fWitness = true;
        }
        else if(ExtractDestination(out.tx->tx->vout[out.i].scriptPubKey, address))
        {
            CPubKey pubkey;
            CKeyID *keyid = boost::get<CKeyID>(&address);
            if (keyid && model->getPubKey(*keyid, pubkey))
            {
                nBytesInputs += (pubkey.IsCompressed() ? 148 : 180);
                if (!pubkey.IsCompressed())
                    nQuantityUncompressed++;
            }
            else
                nBytesInputs += 148; // in all error cases, simply assume 148 here
        }
        else nBytesInputs += 148;
    }

    // calculation
    if ( nQuantity > 0 )
    {
        // Bytes
        nBytes = nBytesInputs + ((CoinControlDialog::payAmounts.size() > 0 ? CoinControlDialog::payAmounts.size() + 1 : 2) * 34) + 10; // always assume +1 output for change here
        if ( fWitness )
        {
            // there is some fudging in these numbers related to the actual virtual
            // transaction size calculation that will keep this estimate from being exact
            //
            // usually, the result will be an overestimate within a couple of satoshis
            // so that the confirmation dialog ends up displaying a slightly smaller fee
            //
            // also, the witness stack size value value is a variable sized integer
            // usually, the number of stack items will be well under the single byte var int limit

            nBytes += 2 ; // account for the serialized marker and flag bytes
            nBytes += nQuantity ; // account for the witness byte that holds the number of stack items for each input
        }

        // in the subtract fee from amount case, we can tell if zero change already
        // and subtract the bytes
        if ( CoinControlDialog::fSubtractFeeFromAmount )
            if ( nAmount - nPayAmount == 0 )
                nBytes -= 34 ;

        if ( nPayAmount > 0 )
        {
            nChange = nAmount - nPayAmount ;
            if ( ! CoinControlDialog::fSubtractFeeFromAmount )
                nChange -= currentTxFee ;

            if ( nChange == 0 && ! CoinControlDialog::fSubtractFeeFromAmount )
                nBytes -= 34 ;
        }

        // after fee
        nAfterFee = std::max< CAmount >( nAmount - currentTxFee, 0 ) ;
    }

    // actually update labels
    unitofcoin displayUnit = unitofcoin::oneCoin ;
    if ( model && model->getOptionsModel() )
        displayUnit = model->getOptionsModel()->getDisplayUnit() ;

    QLabel *l1 = dialog->findChild<QLabel *>("labelCoinControlQuantity");
    QLabel *l2 = dialog->findChild<QLabel *>("labelCoinControlAmount");
    QLabel *l3 = dialog->findChild<QLabel *>("labelCoinControlFee");
    QLabel *l4 = dialog->findChild<QLabel *>("labelCoinControlAfterFee");
    QLabel *l5 = dialog->findChild<QLabel *>("labelCoinControlBytes");
    QLabel *l8 = dialog->findChild<QLabel *>("labelCoinControlChange");

    // enable/disable "change"
    dialog->findChild<QLabel *>("labelCoinControlChangeText")->setEnabled( nPayAmount > 0 ) ;
    dialog->findChild<QLabel *>("labelCoinControlChange")->setEnabled( nPayAmount > 0 ) ;

    // stats
    l1->setText( QString::number( nQuantity ) ) ;                              // Quantity
    l2->setText( UnitsOfCoin::formatWithUnit( displayUnit, nAmount ) ) ;       // Amount
    l3->setText( UnitsOfCoin::formatWithUnit( displayUnit, currentTxFee ) ) ;  // Fee
    l4->setText( UnitsOfCoin::formatWithUnit( displayUnit, nAfterFee ) ) ;     // After Fee
    l5->setText( ((nBytes > 0) ? ASYMP_UTF8 : "") + QString::number(nBytes) ); // Bytes
    l8->setText( UnitsOfCoin::formatWithUnit( displayUnit, nChange ) ) ;       // Change

    CAmount feeVary( 0 ) ; // how many atomary coin units the estimated fee can vary per byte
    if ( currentTxFee > 0 ) // currentTxFee is global, defined in wallet/wallet.cpp
        feeVary = CFeeRate( currentTxFee, 1000 ).GetFeePerBytes( 1 ) ;

    if ( feeVary != 0 && currentTxFee > 0 )
    {
        l3->setText( ASYMP_UTF8 + l3->text() ) ;
        l4->setText( ASYMP_UTF8 + l4->text() ) ;
        if ( nChange > 0 && ! CoinControlDialog::fSubtractFeeFromAmount )
            l8->setText( ASYMP_UTF8 + l8->text() ) ;
    }

    // Insufficient funds
    QLabel * label = dialog->findChild< QLabel * >( "labelCoinControlInsuffFunds" ) ;
    if ( label != nullptr )
        label->setVisible( nChange < 0 ) ;
}

void CoinControlDialog::updateView()
{
    if ( ! walletModel || ! walletModel->getOptionsModel() || ! walletModel->getAddressTableModel() )
        return ;

    bool treeView = ui->choiceTree->isChecked() ;

    ui->treeWidget->clear() ;
    ui->treeWidget->setEnabled( false ) ; // performance, otherwise updateLabels would be called for every checked checkbox
    ui->treeWidget->setAlternatingRowColors( ! treeView ) ;
    QFlags< Qt::ItemFlag > flgCheckbox = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable ;
    QFlags< Qt::ItemFlag > flgTristate = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsTristate ;

    unitofcoin displayUnit = walletModel->getOptionsModel()->getDisplayUnit() ;

    std::map< QString, std::vector< COutput > > mapCoins ;
    walletModel->listCoins( mapCoins ) ;

    for ( const std::pair< QString, std::vector< COutput > > & coins : mapCoins )
    {
        CCoinControlWidgetItem *itemWalletAddress = new CCoinControlWidgetItem();
        itemWalletAddress->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        QString sWalletAddress = coins.first;
        QString sWalletLabel = walletModel->getAddressTableModel()->labelForAddress( sWalletAddress ) ;

        if ( treeView )
        {
            // wallet address
            ui->treeWidget->addTopLevelItem(itemWalletAddress);

            itemWalletAddress->setFlags(flgTristate);
            itemWalletAddress->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

            // label
            itemWalletAddress->setText(COLUMN_LABEL, sWalletLabel);

            // address
            itemWalletAddress->setText(COLUMN_ADDRESS, sWalletAddress);
        }

        CAmount nSum = 0 ;
        int nChildren = 0 ;
        for ( const COutput & out : coins.second )
        {
            nSum += out.tx->tx->vout[ out.i ].nValue ;
            nChildren ++ ;

            CCoinControlWidgetItem * itemOutput ;
            if ( treeView ) itemOutput = new CCoinControlWidgetItem( itemWalletAddress ) ;
            else            itemOutput = new CCoinControlWidgetItem( ui->treeWidget ) ;
            itemOutput->setFlags( flgCheckbox ) ;
            itemOutput->setCheckState( COLUMN_CHECKBOX, Qt::Unchecked ) ;

            // address
            CTxDestination outputAddress ;
            QString sAddress = "" ;
            if ( ExtractDestination( out.tx->tx->vout[ out.i ].scriptPubKey, outputAddress ) )
            {
                sAddress = QString::fromStdString( CBase58Address( outputAddress ).ToString() ) ;

                // if list view or change => show dogecoin address
                // in tree view, address is not shown again for direct wallet address outputs
                if ( ! treeView || sAddress != sWalletAddress )
                    itemOutput->setText( COLUMN_ADDRESS, sAddress ) ;
            }

            // label
            if ( sAddress != sWalletAddress ) // change
            {
                // tooltip from where the change comes from
                QString changeFrom = sWalletLabel.isEmpty() ? sWalletAddress : sWalletLabel + " (" + sWalletAddress + ")" ;
                itemOutput->setToolTip( COLUMN_LABEL, tr("change from %1").arg( changeFrom ) ) ;
                itemOutput->setText( COLUMN_LABEL, tr("(change)") ) ;
            }
            else if ( ! treeView )
            {
                QString sLabel = walletModel->getAddressTableModel()->labelForAddress( sAddress ) ;
                itemOutput->setText( COLUMN_LABEL, sLabel ) ;
            }

            // amount
            itemOutput->setText(COLUMN_AMOUNT, UnitsOfCoin::format( displayUnit, out.tx->tx->vout[out.i].nValue )) ;
            itemOutput->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong)out.tx->tx->vout[out.i].nValue)); // padding so that sorting works correctly

            // date
            itemOutput->setText(COLUMN_DATE, GUIUtil::dateTimeStr(out.tx->GetTxTime()));
            itemOutput->setData(COLUMN_DATE, Qt::UserRole, QVariant((qlonglong)out.tx->GetTxTime()));

            // confirmations
            itemOutput->setText(COLUMN_CONFIRMATIONS, QString::number(out.nDepth));
            itemOutput->setData(COLUMN_CONFIRMATIONS, Qt::UserRole, QVariant((qlonglong)out.nDepth));

            // transaction hash
            uint256 txhash = out.tx->GetTxHash() ;
            itemOutput->setText(COLUMN_TXHASH, QString::fromStdString(txhash.GetHex()));

            // vout index
            itemOutput->setText(COLUMN_VOUT_INDEX, QString::number(out.i));

             // disable locked coins
            if ( walletModel->isLockedCoin( txhash, out.i ) )
            {
                COutPoint outpt(txhash, out.i);
                coinControl->UnSelect(outpt); // just to be sure
                itemOutput->setDisabled(true);
                itemOutput->setIcon(COLUMN_CHECKBOX, platformStyle->SingleColorIcon(":/icons/lock_closed"));
            }

            // set checkbox
            if (coinControl->IsSelected(COutPoint(txhash, out.i)))
                itemOutput->setCheckState(COLUMN_CHECKBOX, Qt::Checked);
        }

        // amount
        if ( treeView )
        {
            itemWalletAddress->setText(COLUMN_CHECKBOX, "(" + QString::number(nChildren) + ")");
            itemWalletAddress->setText(COLUMN_AMOUNT, UnitsOfCoin::format( displayUnit, nSum )) ;
            itemWalletAddress->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong)nSum));
        }
    }

    // expand all partially selected
    if ( treeView )
    {
        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++)
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked)
                ui->treeWidget->topLevelItem(i)->setExpanded(true);
    }

    // sort view
    sortView(sortColumn, sortOrder);
    ui->treeWidget->setEnabled(true);
}
