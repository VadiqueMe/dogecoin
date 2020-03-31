// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "sendcoinsdialog.h"
#include "ui_sendcoinsdialog.h"

#include "addresstablemodel.h"
#include "unitsofcoin.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "sendcoinsentry.h"
#include "walletmodel.h"

#include "base58.h"
#include "chainparams.h"
#include "wallet/coincontrol.h"
#include "validation.h"
#include "ui_interface.h"
#include "txmempool.h"
#include "wallet/wallet.h"

#include <QButtonGroup>
#include <QFontMetrics>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QTimer>

#define SEND_CONFIRM_DELAY   3

SendCoinsDialog::SendCoinsDialog( const PlatformStyle * style, QWidget * parent )
    : QDialog( parent )
    , ui( new Ui::SendCoinsDialog )
    , walletModel( nullptr )
    , platformStyle( style )
    , whichFeeChoice( new QButtonGroup( this ) )
    , fNewRecipientAllowed( true )
{
    ui->setupUi( this ) ;

    if ( ! style->getImagesOnButtons() ) {
        ui->addRecipientButton->setIcon( QIcon() ) ;
        ui->clearButton->setIcon( QIcon() ) ;
        ui->sendButton->setIcon( QIcon() ) ;
    } else {
        ui->addRecipientButton->setIcon( style->SingleColorIcon( ":/icons/add" ) ) ;
        ui->clearButton->setIcon( style->SingleColorIcon( ":/icons/remove" ) ) ;
        ui->sendButton->setIcon( style->SingleColorIcon( ":/icons/send" ) ) ;
    }

    GUIUtil::setupAddressWidget( ui->coinControlCustomChange, this ) ;

    addEntry() ;

    connect( ui->addRecipientButton, SIGNAL( clicked() ), this, SLOT( addEntry() ) ) ;
    connect( ui->clearButton, SIGNAL( clicked() ), this, SLOT( clear() ) ) ;

    // Coin Control
    connect( ui->pushButtonCoinControl, SIGNAL( clicked() ), this, SLOT( coinControlButtonClicked() ) ) ;
    connect( ui->checkBoxCoinControlChange, SIGNAL( stateChanged(int) ), this, SLOT( coinControlChangeChecked(int) ) ) ;
    connect( ui->coinControlCustomChange, SIGNAL( textEdited(const QString &) ), this, SLOT( coinControlChangeEdited(const QString &) ) ) ;

    // Coin Control: clipboard actions
    QAction * copyQuantityToClipboard = new QAction( tr("Copy quantity"), this ) ;
    QAction * copyAmountToClipboard = new QAction( tr("Copy amount"), this ) ;
    QAction * copyFeeToClipboard = new QAction( tr("Copy fee"), this ) ;
    QAction * copyAfterFeeToClipboard = new QAction( tr("Copy after fee"), this ) ;
    QAction * copyBytesToClipboard = new QAction( tr("Copy bytes"), this ) ;
    QAction * copyChangeToClipboard = new QAction( tr("Copy change"), this ) ;
    connect( copyQuantityToClipboard, SIGNAL( triggered() ), this, SLOT( coinControlQuantityToClipboard() ) ) ;
    connect( copyAmountToClipboard, SIGNAL( triggered() ), this, SLOT( coinControlAmountToClipboard() ) ) ;
    connect( copyFeeToClipboard, SIGNAL( triggered() ), this, SLOT( coinControlFeeToClipboard() ) ) ;
    connect( copyAfterFeeToClipboard, SIGNAL( triggered() ), this, SLOT( coinControlAfterFeeToClipboard() ) ) ;
    connect( copyBytesToClipboard, SIGNAL( triggered() ), this, SLOT( coinControlBytesToClipboard() ) ) ;
    connect( copyChangeToClipboard, SIGNAL( triggered() ), this, SLOT( coinControlChangeToClipboard() ) ) ;
    ui->labelCoinControlQuantity->addAction( copyQuantityToClipboard ) ;
    ui->labelCoinControlAmount->addAction( copyAmountToClipboard ) ;
    ui->labelCoinControlFee->addAction( copyFeeToClipboard ) ;
    ui->labelCoinControlAfterFee->addAction( copyAfterFeeToClipboard ) ;
    ui->labelCoinControlBytes->addAction( copyBytesToClipboard ) ;
    ui->labelCoinControlChange->addAction( copyChangeToClipboard ) ;

    connect( ui->showCoinControlButton, SIGNAL( clicked() ), this, SLOT( showCoinControlClicked() ) ) ;
    connect( ui->hideCoinControlButton, SIGNAL( clicked() ), this, SLOT( hideCoinControlClicked() ) ) ;

    QSettings settings ;
    if ( ! settings.contains( "isCoinControlMinimized" ) )
        settings.setValue( "isCoinControlMinimized", true ) ;

    minimizeCoinControl( settings.value( "isCoinControlMinimized" ).toBool() ) ;

    if ( ! settings.contains( "nWhichFee" ) )
        settings.setValue( "nWhichFee", 1 ) ; // 1: zero fee, 2: fixed fee, 3: fee per kilobyte
    if ( ! settings.contains( "nTransactionFee" ) )
        settings.setValue( "nTransactionFee", static_cast< qint64 >( DEFAULT_TRANSACTION_FEE ) ) ;

    ui->choiceZeroFee->setEnabled( true ) ;
    ui->choiceFixedFee->setEnabled( true ) ;
    ui->choiceFeePerKilobyte->setEnabled( true ) ;

    whichFeeChoice->addButton( ui->choiceZeroFee ) ;
    whichFeeChoice->addButton( ui->choiceFixedFee ) ;
    whichFeeChoice->addButton( ui->choiceFeePerKilobyte ) ;
    whichFeeChoice->setId( ui->choiceZeroFee, 1 ) ;
    whichFeeChoice->setId( ui->choiceFixedFee, 2 ) ;
    whichFeeChoice->setId( ui->choiceFeePerKilobyte, 3 ) ;
    whichFeeChoice->setExclusive( true ) ;

    whichFeeChoice->button( (int)std::max( 1, std::min( 3, settings.value( "nWhichFee" ).toInt() ) ) )->setChecked( true ) ;

    ui->customFee->setValue( 0 ) ;
    ui->customFee->setMaximumValue( maxTxFee ) ;

    ui->pictureOfCoins->setPixmap( platformStyle->SingleColorIcon( ":/icons/coins_black" ).pixmap( 20, 20 ) ) ;
}

void SendCoinsDialog::setWalletModel( WalletModel * model )
{
    this->walletModel = model ;

    if ( model && model->getOptionsModel() )
    {
        for ( int i = 0 ; i < ui->entries->count() ; ++ i )
        {
            SendCoinsEntry * entry = qobject_cast< SendCoinsEntry * >( ui->entries->itemAt( i )->widget() ) ;
            if ( entry != nullptr )
                entry->setWalletModel( model ) ;
        }

        setBalance( model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                    model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance() ) ;
        connect( model, SIGNAL( balanceChanged(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount) ),
                 this, SLOT( setBalance(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount) ) ) ;
        connect( model->getOptionsModel(), SIGNAL( displayUnitChanged(int) ), this, SLOT( updateDisplayUnit() ) ) ;
        updateDisplayUnit() ;

        // coin control
        connect( model->getOptionsModel(), SIGNAL( displayUnitChanged(int) ), this, SLOT( coinControlUpdateLabels() ) );
        coinControlUpdateLabels() ;

        // fees
        connect( ui->customFee, SIGNAL( valueChanged(qint64) ), this, SLOT( updateGlobalFeeVariable() ) ) ;
        connect( ui->customFee, SIGNAL( valueChanged(qint64) ), this, SLOT( coinControlUpdateLabels() ) ) ;
        ui->customFee->setSingleStep( 1 /* * ... */ ) ;

        connect( whichFeeChoice, SIGNAL( buttonClicked(int) ), this, SLOT( updateFeeSection() ) ) ;
        connect( whichFeeChoice, SIGNAL( buttonClicked(int) ), this, SLOT( updateGlobalFeeVariable() ) ) ;
        connect( whichFeeChoice, SIGNAL( buttonClicked(int) ), this, SLOT( coinControlUpdateLabels() ) ) ;

        updateFeeSection() ;
        updateGlobalFeeVariable() ;
    }
}

SendCoinsDialog::~SendCoinsDialog()
{
    QSettings settings ;
    settings.setValue( "nWhichFee", whichFeeChoice->checkedId() ) ;
    settings.setValue( "nTransactionFee", static_cast< qint64 >( ui->customFee->value() ) ) ;

    delete ui ;
}

void SendCoinsDialog::on_sendButton_clicked()
{
    if ( ! walletModel || ! walletModel->getOptionsModel() )
        return ;

    QList< SendCoinsRecipient > recipients ;
    bool valid = true ;

    for ( int i = 0 ; i < ui->entries->count() ; ++ i )
    {
        SendCoinsEntry * entry = qobject_cast< SendCoinsEntry * >( ui->entries->itemAt( i )->widget() ) ;
        if ( entry != nullptr )
        {
            if ( entry->validate() )
                recipients.append( entry->getValue() ) ;
            else
                valid = false ;
        }
    }

    if ( ! valid || recipients.isEmpty() ) return ;

    fNewRecipientAllowed = false ;
    WalletModel::UnlockContext unlock( walletModel->requestUnlock() ) ;
    if ( ! unlock.isValid() )
    {
        // unlock wallet was cancelled
        fNewRecipientAllowed = true ;
        return ;
    }

    // prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction( recipients ) ;
    WalletModel::SendCoinsReturn prepareStatus ;

    // use a CCoinControl instance
    CCoinControl ctrl = *CoinControlDialog::coinControl ;
    ctrl.nConfirmTarget = 0 ;

    prepareStatus = walletModel->prepareTransaction( currentTransaction, &ctrl ) ;

    // process prepareStatus and on error show a message to user
    processSendCoinsReturn( prepareStatus ) ;

    if ( prepareStatus.status != WalletModel::OK ) {
        fNewRecipientAllowed = true ;
        return ;
    }

    // format confirmation message
    QStringList formatted ;
    for ( const SendCoinsRecipient & rcp : currentTransaction.getRecipients() )
    {
        // bold amount string
        QString amount = "<b>" + UnitsOfCoin::formatHtmlWithUnit( walletModel->getOptionsModel()->getDisplayUnit(), rcp.amount ) ;
        amount.append( "</b>" ) ;
        // address string
        QString address = "<span style='font-family: sans-serif;'>" ;
        address += rcp.address ;
        address.append( "</span>" ) ;

        QString recipientElement ;

        if ( ! rcp.paymentRequest.IsInitialized() ) // normal payment
        {
            if ( rcp.label.length() > 0 ) // label with address
            {
                recipientElement = tr("%1 to %2").arg( amount, GUIUtil::HtmlEscape( rcp.label ) ) ;
                recipientElement.append( QString(" (%1)").arg( address ) ) ;
            }
            else // just address
            {
                recipientElement = tr("%1 to %2").arg( amount, address ) ;
            }
        }
        else if ( ! rcp.authenticatedMerchant.isEmpty() ) // authenticated payment request
        {
            recipientElement = tr("%1 to %2").arg( amount, GUIUtil::HtmlEscape( rcp.authenticatedMerchant ) ) ;
        }
        else // unauthenticated payment request
        {
            recipientElement = tr("%1 to %2").arg( amount, address ) ;
        }

        formatted.append( recipientElement ) ;
    }

    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />%1");

    CAmount txFee = currentTransaction.getTransactionFee() ;

    if ( txFee > 0 )
    {
        // append fee string when a fee is added
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append( UnitsOfCoin::formatHtmlWithUnit( walletModel->getOptionsModel()->getDisplayUnit(), txFee ) ) ;
        questionString.append("</span> ");
        questionString.append(tr("added as transaction fee"));

        // append transaction size
        questionString.append( " (" + QString::number( (double)currentTransaction.getTransactionSize() / 1000 ) + " kB)" ) ;
    }

    // add total amount in all subdivision units
    questionString.append( "<hr />" ) ;
    CAmount totalAmount = currentTransaction.getTotalTransactionAmount() + txFee ;
    QStringList alternativeUnits ;
    for ( const UnitsOfCoin::Unit & u : UnitsOfCoin::availableUnits() )
    {
        if ( u != walletModel->getOptionsModel()->getDisplayUnit() )
            alternativeUnits.append( UnitsOfCoin::formatHtmlWithUnit( u, totalAmount ) ) ;
    }
    questionString.append(tr("Total Amount %1")
        .arg( UnitsOfCoin::formatHtmlWithUnit( walletModel->getOptionsModel()->getDisplayUnit(), totalAmount ) )) ;
    questionString.append(QString("<span style='font-size:10pt;font-weight:normal;'><br />(=%2)</span>")
        .arg(alternativeUnits.join(" " + tr("or") + "<br />")));

    SendConfirmationDialog confirmationDialog(tr("Confirm send coins"),
        questionString.arg(formatted.join("<br />")), SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = (QMessageBox::StandardButton)confirmationDialog.result();

    if ( retval != QMessageBox::Yes )
    {
        fNewRecipientAllowed = true ;
        return ;
    }

    // now send the prepared transaction
    WalletModel::SendCoinsReturn sendStatus = walletModel->sendCoins( currentTransaction ) ;
    // process sendStatus and on error generate message shown to user
    processSendCoinsReturn( sendStatus ) ;

    if ( sendStatus.status == WalletModel::OK )
    {
        accept() ;
        CoinControlDialog::coinControl->UnSelectAll() ;
        coinControlUpdateLabels() ;
    }
    fNewRecipientAllowed = true ;
}

void SendCoinsDialog::clear()
{
    // Remove entries until only one left
    while ( ui->entries->count() > 0 )
        ui->entries->takeAt( 0 )->widget()->deleteLater() ;

    addEntry() ;

    updateTabsAndLabels() ;
}

void SendCoinsDialog::reject()
{
    clear();
}

void SendCoinsDialog::accept()
{
    clear();
}

SendCoinsEntry * SendCoinsDialog::addEntry()
{
    SendCoinsEntry * entry = new SendCoinsEntry( platformStyle, this ) ;
    entry->setWalletModel( walletModel ) ;
    ui->entries->addWidget( entry ) ;

    connect( entry, SIGNAL( removeEntry(SendCoinsEntry*) ), this, SLOT( removeEntry(SendCoinsEntry*) ) ) ;
    connect( entry, SIGNAL( payAmountChanged() ), this, SLOT( coinControlUpdateLabels() ) ) ;
    connect( entry, SIGNAL( subtractFeeFromAmountChanged() ), this, SLOT( coinControlUpdateLabels() ) ) ;

    entry->clear() ;
    entry->showHideSubtractFeeFromAmountCheckbox( ui->customFee->value() != 0 ) ;
    entry->setFocus() ; // makes this entry the current

    ui->scrollAreaWidgetContents->resize( ui->scrollAreaWidgetContents->sizeHint() ) ;
    qApp->processEvents() ;

    QScrollBar * bar = ui->scrollArea->verticalScrollBar() ;
    if ( bar != nullptr )
        bar->setSliderPosition( bar->maximum() ) ;

    updateListOfEntries() ;
    updateTabsAndLabels() ;

    return entry ;
}

void SendCoinsDialog::updateListOfEntries()
{
    size_t nEntries = ui->entries->count() ;

    for ( int i = 0 ; i < nEntries ; ++ i )
    {
        SendCoinsEntry * entry = qobject_cast< SendCoinsEntry * >( ui->entries->itemAt( i )->widget() ) ;
        if ( entry != nullptr ) {
            entry->showHideHorizontalLine( i < nEntries - 1 ) ; // hide last entry's line
            entry->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed ) ;
        }
    }

    ui->scrollArea->setWidgetResizable( true ) ;
}

void SendCoinsDialog::updateTabsAndLabels()
{
    setupTabChain( nullptr ) ;
    coinControlUpdateLabels() ;
}

void SendCoinsDialog::removeEntry( SendCoinsEntry * entry )
{
    entry->hide() ;

    // if the last entry is about to be removed add an empty one
    if ( ui->entries->count() == 1 ) addEntry() ;

    ui->entries->removeWidget( entry ) ;
    entry->deleteLater() ;

    updateListOfEntries() ;
    updateTabsAndLabels() ;
}

QWidget * SendCoinsDialog::setupTabChain( QWidget * prev )
{
    for ( int i = 0 ; i < ui->entries->count() ; ++ i )
    {
        SendCoinsEntry * entry = qobject_cast< SendCoinsEntry * >( ui->entries->itemAt( i )->widget() ) ;
        if ( entry != nullptr )
            prev = entry->setupTabChain( prev ) ;
    }

    QWidget::setTabOrder( prev, ui->sendButton ) ;
    QWidget::setTabOrder( ui->sendButton, ui->clearButton ) ;
    QWidget::setTabOrder( ui->clearButton, ui->addRecipientButton ) ;
    return ui->addRecipientButton ;
}

void SendCoinsDialog::setAddress( const QString & address )
{
    SendCoinsEntry * entry = nullptr ;
    // replace the first entry if it is still unused
    if ( ui->entries->count() == 1 )
    {
        SendCoinsEntry * first = qobject_cast< SendCoinsEntry* >( ui->entries->itemAt( 0 )->widget() ) ;
        if ( first->isClear() )
            entry = first ;
    }
    if ( entry == nullptr )
        entry = addEntry() ;

    entry->setAddress( address ) ;
}

void SendCoinsDialog::pasteEntry( const SendCoinsRecipient & rv )
{
    if ( ! fNewRecipientAllowed ) return ;

    SendCoinsEntry * entry = nullptr ;
    // replace the first entry if it is still unused
    if ( ui->entries->count() == 1 )
    {
        SendCoinsEntry * first = qobject_cast< SendCoinsEntry* >( ui->entries->itemAt( 0 )->widget() ) ;
        if ( first->isClear() )
            entry = first ;
    }
    if ( entry == nullptr )
        entry = addEntry() ;

    entry->setValue( rv ) ;
    updateTabsAndLabels() ;
}

bool SendCoinsDialog::handlePaymentRequest( const SendCoinsRecipient & rv )
{
    // just paste the entry, all pre-checks are done in paymentserver.cpp
    pasteEntry( rv ) ;
    return true ;
}

void SendCoinsDialog::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                                 const CAmount& watchBalance, const CAmount& watchUnconfirmedBalance, const CAmount& watchImmatureBalance)
{
    Q_UNUSED(unconfirmedBalance);
    Q_UNUSED(immatureBalance);
    Q_UNUSED(watchBalance);
    Q_UNUSED(watchUnconfirmedBalance);
    Q_UNUSED(watchImmatureBalance);

    if ( walletModel && walletModel->getOptionsModel() )
        ui->labelBalance->setText( UnitsOfCoin::formatWithUnit( walletModel->getOptionsModel()->getDisplayUnit(), balance ) ) ;
}

void SendCoinsDialog::updateDisplayUnit()
{
    setBalance( walletModel->getBalance(), 0, 0, 0, 0, 0 ) ;
    ui->customFee->setDisplayUnit( walletModel->getOptionsModel()->getDisplayUnit() ) ;
}

void SendCoinsDialog::processSendCoinsReturn( const WalletModel::SendCoinsReturn & sendCoinsReturn )
{
    QPair< QString, CClientUIInterface::MessageBoxFlags > msgParams ;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING ;

    // This comment is specific to SendCoinsDialog usage of WalletModel::SendCoinsReturn
    // WalletModel::TransactionCommitFailed is used only in WalletModel::sendCoins()
    // all others are used only in WalletModel::prepareTransaction()
    switch ( sendCoinsReturn.status )
    {
    case WalletModel::InvalidAmount:
        msgParams.first = "Amount ≤ 0" ;
        break ;
    case WalletModel::InvalidAddress:
        msgParams.first = tr("The recipient address is not valid, please recheck") ;
        break;
    case WalletModel::AmountExceedsBalance:
        msgParams.first = tr("The amount exceeds your balance") ;
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        msgParams.first = tr("The total exceeds your balance when the transaction fee is included") ;
        break;
    case WalletModel::DuplicateAddress:
        msgParams.first = tr("Duplicate address found, can only send to each address once per transaction") ;
        break ;
    case WalletModel::TransactionCreationFailed:
        msgParams.first = tr("Transaction creation failed") ;
        msgParams.second = CClientUIInterface::MSG_ERROR ;
        break;
    case WalletModel::TransactionCommitFailed:
        msgParams.first = tr("The transaction was rejected with the following reason: %1").arg( sendCoinsReturn.reasonCommitFailed ) ;
        msgParams.second = CClientUIInterface::MSG_ERROR ;
        break;
    case WalletModel::AbsurdFee:
        msgParams.first = tr("A fee higher than %1 is considered an absurdly high fee").arg( UnitsOfCoin::formatWithUnit( walletModel->getOptionsModel()->getDisplayUnit(), maxTxFee ) ) ;
        break;
    case WalletModel::PaymentRequestExpired:
        msgParams.first = tr("Payment request expired") ;
        msgParams.second = CClientUIInterface::MSG_ERROR ;
        break;
    case WalletModel::OK: // included to avoid a compiler warning
        return ;
    default:
        return ;
    }

    if ( ! msgParams.first.isEmpty() )
        Q_EMIT message( tr("Send Coins"), msgParams.first, msgParams.second ) ;
}

// Coin control show/hide
void SendCoinsDialog::minimizeCoinControl( bool fMinimize )
{
    ui->showCoinControlButton->setVisible( fMinimize ) ;
    ui->hideCoinControlButton->setVisible( ! fMinimize ) ;

    ui->frameCoinControlExpanded->setVisible( ! fMinimize ) ;

    if ( fMinimize )
        CoinControlDialog::coinControl->SetNull() ;

    updateGlobalFeeVariable() ;
    coinControlUpdateLabels() ;

    QSettings settings ;
    settings.setValue( "isCoinControlMinimized", fMinimize ) ;
}

void SendCoinsDialog::showCoinControlClicked()
{
    minimizeCoinControl( false ) ;
}

void SendCoinsDialog::hideCoinControlClicked()
{
    minimizeCoinControl( true ) ;
}

void SendCoinsDialog::updateFeeSection()
{
    ui->horizontalLayoutForFee->removeWidget( ui->customFee ) ;

    int indexOfChoice = ui->horizontalLayoutForFee->indexOf( whichFeeChoice->checkedButton() ) ;
    ui->horizontalLayoutForFee->insertWidget( indexOfChoice + 1, ui->customFee ) ;

    if ( ui->choiceZeroFee->isChecked() )
        ui->customFee->setValue( 0 ) ;

    ui->customFee->setReadOnly( /* ui->choiceFeePerKilobyte->isChecked() || ui->choiceFixedFee->isChecked() */ true ) ;
}

void SendCoinsDialog::updateGlobalFeeVariable()
{
    // payTxFee is global, defined in wallet/wallet.cpp
    payTxFee = CFeeRate( ui->customFee->value() ) ;

    bool noFee = ( ui->customFee->value() == 0 ) ;
    for ( int i = 0 ; i < ui->entries->count() ; ++ i )
    {
        SendCoinsEntry * entry = qobject_cast< SendCoinsEntry * >( ui->entries->itemAt( i )->widget() ) ;
        if ( entry != nullptr )
            entry->showHideSubtractFeeFromAmountCheckbox( ! noFee ) ;
    }
}

/*
QString SendCoinsDialog::getFeeString()
{
    if ( ! walletModel || ! walletModel->getOptionsModel() )
        return ;

    QString feeString = UnitsOfCoin::formatWithUnit( walletModel->getOptionsModel()->getDisplayUnit(), ui->customFee->value() ) ;
    if ( ui->choiceFeePerKilobyte->isChecked() ) feeString += "/kB" ;
    return feeString ;
}
*/

// Coin Control: copy "Quantity" to clipboard
void SendCoinsDialog::coinControlQuantityToClipboard()
{
    GUIUtil::setClipboard( ui->labelCoinControlQuantity->text() ) ;
}

// Coin Control: copy "Amount" to clipboard
void SendCoinsDialog::coinControlAmountToClipboard()
{
    const QString & amount = ui->labelCoinControlAmount->text() ;
    GUIUtil::setClipboard( amount.left( amount.indexOf(" ") ) ) ;
}

// Coin Control: copy "Fee" to clipboard
void SendCoinsDialog::coinControlFeeToClipboard()
{
    const QString & fee = ui->labelCoinControlFee->text() ;
    GUIUtil::setClipboard( fee.left( fee.indexOf( " " ) ).replace( ASYMP_UTF8, "" ) ) ;
}

// Coin Control: copy "After fee" to clipboard
void SendCoinsDialog::coinControlAfterFeeToClipboard()
{
    const QString & afterFee = ui->labelCoinControlAfterFee->text() ;
    GUIUtil::setClipboard( afterFee.left( afterFee.indexOf( " " ) ).replace( ASYMP_UTF8, "" ) ) ;
}

// Coin Control: copy "Bytes" to clipboard
void SendCoinsDialog::coinControlBytesToClipboard()
{
    GUIUtil::setClipboard( ui->labelCoinControlBytes->text().replace( ASYMP_UTF8, "" ) ) ;
}

// Coin Control: copy "Change" to clipboard
void SendCoinsDialog::coinControlChangeToClipboard()
{
    const QString & change = ui->labelCoinControlChange->text() ;
    GUIUtil::setClipboard( change.left( change.indexOf( " " ) ).replace( ASYMP_UTF8, "" ) ) ;
}

// Coin Control: button inputs -> show actual coin control dialog
void SendCoinsDialog::coinControlButtonClicked()
{
    CoinControlDialog dlg( platformStyle ) ;
    dlg.setWalletModel( walletModel ) ;
    dlg.exec() ;
    coinControlUpdateLabels() ;
}

// Coin Control: checkbox custom change address
void SendCoinsDialog::coinControlChangeChecked( int state )
{
    if ( state == Qt::Unchecked )
        CoinControlDialog::coinControl->destChange = CNoDestination() ;
    else
        // use this to re-validate an already entered address
        coinControlChangeEdited( ui->coinControlCustomChange->text() ) ;

    ui->coinControlCustomChange->setEnabled( state == Qt::Checked ) ;
}

// Coin Control: custom change address changed
void SendCoinsDialog::coinControlChangeEdited( const QString & text )
{
    // default to no change address until verified
    CoinControlDialog::coinControl->destChange = CNoDestination() ;

    if ( walletModel && walletModel->getAddressTableModel() )
    {
        if ( text.isEmpty() ) // nothing entered
            return ;

        CDogecoinAddress addr = CDogecoinAddress( text.toStdString() ) ;

        if ( ! addr.IsValid() ) // invalid address
        {
            return ;
        }
        else // valid address
        {
            CKeyID keyid ;
            addr.GetKeyID( keyid ) ;
            if ( ! walletModel->havePrivKey( keyid ) ) // non-wallet address
            {
                // confirmation dialog
                QMessageBox::StandardButton btnRetVal =
                    QMessageBox::question( this,
                        tr("Confirm custom change address"),
                        tr("The address you selected for change is not part of this wallet. Any or all funds in your wallet may be sent to this address. Are you sure?"),
                        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel ) ;

                if ( btnRetVal == QMessageBox::Yes )
                    CoinControlDialog::coinControl->destChange = addr.Get() ;
                else
                    ui->coinControlCustomChange->setText( "" ) ;
            }
            else // known change address
            {
                /* QString associatedLabel = walletModel->getAddressTableModel()->labelForAddress( text ) ;
                if ( ! associatedLabel.isEmpty() ) ... */

                CoinControlDialog::coinControl->destChange = addr.Get() ;
            }
        }
    }
}

// Coin Control: update labels
void SendCoinsDialog::coinControlUpdateLabels()
{
    if ( walletModel == nullptr ) return ;

    // set pay amounts
    CoinControlDialog::payAmounts.clear() ;
    CoinControlDialog::fSubtractFeeFromAmount = false ;
    for ( int i = 0 ; i < ui->entries->count() ; ++ i )
    {
        SendCoinsEntry * entry = qobject_cast< SendCoinsEntry * >( ui->entries->itemAt( i )->widget() ) ;
        if ( entry != nullptr && ! entry->isHidden() )
        {
            SendCoinsRecipient rcp = entry->getValue() ;
            CoinControlDialog::payAmounts.append( rcp.amount ) ;
            if ( rcp.fSubtractFeeFromAmount )
                CoinControlDialog::fSubtractFeeFromAmount = true ;
        }
    }

    if (CoinControlDialog::coinControl->HasSelected())
    {
        // actual coin control calculation
        CoinControlDialog::updateLabels( walletModel, this ) ;

        // show coin control stats
        ui->labelCoinControlAutomaticallySelected->hide();
        ui->widgetCoinControl->show();
    }
    else
    {
        // hide coin control stats
        ui->labelCoinControlAutomaticallySelected->show();
        ui->widgetCoinControl->hide();
        ui->labelCoinControlInsuffFunds->hide();
    }
}

SendConfirmationDialog::SendConfirmationDialog(const QString &title, const QString &text, int _secDelay,
    QWidget *parent) :
    QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent), secDelay(_secDelay)
{
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    updateYesButton();
    connect(&countDownTimer, SIGNAL(timeout()), this, SLOT(countDown()));
}

int SendConfirmationDialog::exec()
{
    updateYesButton();
    countDownTimer.start(1000);
    return QMessageBox::exec();
}

void SendConfirmationDialog::countDown()
{
    secDelay -- ;
    updateYesButton() ;

    if ( secDelay <= 0 )
        countDownTimer.stop() ;
}

void SendConfirmationDialog::updateYesButton()
{
    if(secDelay > 0)
    {
        yesButton->setEnabled(false);
        yesButton->setText(tr("Yes") + " (" + QString::number(secDelay) + ")");
    }
    else
    {
        yesButton->setEnabled(true);
        yesButton->setText(tr("Yes"));
    }
}
