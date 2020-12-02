// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "sendcoinsentry.h"
#include "ui_sendcoinsentry.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"

#include <QApplication>
#include <QClipboard>

SendCoinsEntry::SendCoinsEntry( const PlatformStyle * style, QWidget * parent ) :
    QStackedWidget( parent ),
    ui( new Ui::SendCoinsEntry ),
    walletModel( nullptr ),
    platformStyle( style )
{
    ui->setupUi( this ) ;

    ui->addressBookButton->setIcon( platformStyle->SingleColorIcon( ":/icons/address-book" ) ) ;
    ui->pasteButton->setIcon( platformStyle->SingleColorIcon( ":/icons/editpaste" ) ) ;
    ui->deleteButton->setIcon( platformStyle->SingleColorIcon( ":/icons/remove" ) ) ;
    ui->deleteButton_is->setIcon( platformStyle->SingleColorIcon( ":/icons/remove" ) ) ;
    ui->deleteButton_s->setIcon( platformStyle->SingleColorIcon( ":/icons/remove" ) ) ;

    setCurrentWidget( ui->SendCoins ) ;

    if (platformStyle->getUseExtraSpacing())
        ui->payToLayout->setSpacing(4);
#if QT_VERSION >= 0x040700
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));
#endif

    // coin address field
    GUIUtil::setupAddressWidget( ui->payTo, this ) ;
    // just a label for showing address(es)
    ui->payTo_is->setFont( GUIUtil::fixedPitchFont() ) ;

    connect( ui->payAmount, SIGNAL( valueChanged(qint64) ), this, SIGNAL( payAmountChanged() ) ) ;
    connect( ui->subtractFeeFromAmountCheckbox, SIGNAL( toggled(bool) ), this, SIGNAL( subtractFeeFromAmountChanged() ) ) ;
    connect( ui->deleteButton, SIGNAL( clicked() ), this, SLOT( deleteClicked() ) ) ;
    connect( ui->deleteButton_is, SIGNAL( clicked() ), this, SLOT( deleteClicked() ) ) ;
    connect( ui->deleteButton_s, SIGNAL( clicked() ), this, SLOT( deleteClicked() ) ) ;
}

SendCoinsEntry::~SendCoinsEntry()
{
    delete ui ;
}

void SendCoinsEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_addressBookButton_clicked()
{
    if ( walletModel == nullptr ) return ;

    AddressBookPage dlg( platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this ) ;
    dlg.setAddressTableModel( walletModel->getAddressTableModel() ) ;
    if ( dlg.exec() )
    {
        ui->payTo->setText( dlg.getReturnValue() ) ;
        ui->payAmount->setFocus() ;
    }
}

void SendCoinsEntry::on_payTo_textChanged( const QString & address )
{
    updateLabel( address ) ;
}

void SendCoinsEntry::setWalletModel( WalletModel * model )
{
    this->walletModel = model ;

    if ( model && model->getOptionsModel() )
        connect( model->getOptionsModel(), SIGNAL( displayUnitChanged(unitofcoin) ), this, SLOT( updateDisplayUnit() ) ) ;

    clear() ;
}

void SendCoinsEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->payAmount->clear();
    ui->subtractFeeFromAmountCheckbox->setCheckState( Qt::Unchecked ) ;
    ui->paymentMessageText->clear() ;
    ui->paymentMessageText->hide() ;
    ui->messageLabel->hide();

    // clear UI elements for unauthenticated payment request
    ui->payTo_is->clear();
    ui->memoTextLabel_is->clear();
    ui->payAmount_is->clear();

    // clear UI elements for authenticated payment request
    ui->payTo_s->clear();
    ui->memoTextLabel_s->clear();
    ui->payAmount_s->clear();

    updateDisplayUnit() ;
}

void SendCoinsEntry::deleteClicked()
{
    Q_EMIT removeEntry( this ) ;
}

bool SendCoinsEntry::validate()
{
    if ( walletModel == nullptr ) return false ;

    // skip checks for initialized payment request
    if ( recipient.paymentRequest.IsInitialized() )
        return true ;

    bool retval = true ;

    if ( ! walletModel->validateAddress( ui->payTo->text() ) )
    {
        ui->payTo->setValid( false ) ;
        retval = false ;
    }

    if ( ! ui->payAmount->validate() )
    {
        retval = false ;
    }

    // sending a zero amount is not valid
    if ( ui->payAmount->value(0) <= 0 )
    {
        ui->payAmount->setValid( false ) ;
        retval = false ;
    }

    return retval ;
}

SendCoinsRecipient SendCoinsEntry::getValue()
{
    // payment request
    if ( recipient.paymentRequest.IsInitialized() )
        return recipient ;

    // normal payment
    recipient.address = ui->payTo->text() ;
    recipient.label = ui->addAsLabel->text() ;
    recipient.amount = ui->payAmount->value() ;
    recipient.message = ui->paymentMessageText->text() ;
    recipient.fSubtractFeeFromAmount = ( ui->subtractFeeFromAmountCheckbox->checkState() == Qt::Checked ) ;

    return recipient ;
}

QWidget * SendCoinsEntry::setupTabChain( QWidget * prev )
{
    QWidget::setTabOrder( prev, ui->payTo ) ;
    QWidget::setTabOrder( ui->payTo, ui->addAsLabel ) ;
    QWidget * w = ui->payAmount->setupTabChain( ui->addAsLabel ) ;
    QWidget::setTabOrder( w, ui->subtractFeeFromAmountCheckbox ) ;
    QWidget::setTabOrder( ui->subtractFeeFromAmountCheckbox, ui->addressBookButton ) ;
    QWidget::setTabOrder( ui->addressBookButton, ui->pasteButton ) ;
    QWidget::setTabOrder( ui->pasteButton, ui->deleteButton ) ;
    return ui->deleteButton ;
}

void SendCoinsEntry::setValue( const SendCoinsRecipient & value )
{
    recipient = value ;

    if (recipient.paymentRequest.IsInitialized()) // payment request
    {
        if (recipient.authenticatedMerchant.isEmpty()) // unauthenticated
        {
            ui->payTo_is->setText(recipient.address);
            ui->memoTextLabel_is->setText(recipient.message);
            ui->payAmount_is->setValue(recipient.amount);
            ui->payAmount_is->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_UnauthenticatedPaymentRequest);
        }
        else // authenticated
        {
            ui->payTo_s->setText(recipient.authenticatedMerchant);
            ui->memoTextLabel_s->setText(recipient.message);
            ui->payAmount_s->setValue(recipient.amount);
            ui->payAmount_s->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_AuthenticatedPaymentRequest);
        }
    }
    else // normal payment
    {
        // message
        ui->paymentMessageText->setText( recipient.message ) ;
        ui->paymentMessageText->setVisible( ! recipient.message.isEmpty() ) ;
        ui->messageLabel->setVisible( ! recipient.message.isEmpty() ) ;

        ui->addAsLabel->clear() ;
        ui->payTo->setText( recipient.address ) ; // this may set a label from addressbook
        if ( ! recipient.label.isEmpty() ) // if a label had been set from the addressbook, don't overwrite with an empty label
            ui->addAsLabel->setText( recipient.label ) ;
        ui->payAmount->setValue( recipient.amount ) ;
    }
}

void SendCoinsEntry::setAddress( const QString & address )
{
    ui->payTo->setText( address ) ;
    ui->payAmount->setFocus() ;
}

bool SendCoinsEntry::isClear() const
{
    return ui->payTo->text().isEmpty() && ui->payTo_is->text().isEmpty() && ui->payTo_s->text().isEmpty() ;
}

void SendCoinsEntry::setFocus()
{
    ui->payTo->setFocus() ;
}

void SendCoinsEntry::showHideSubtractFeeFromAmountCheckbox( bool show )
{
    ui->subtractFeeFromAmountCheckbox->setVisible( show ) ;
}

void SendCoinsEntry::showHideHorizontalLine( bool show )
{
    ui->horizontalLine->setVisible( show ) ;
}

void SendCoinsEntry::updateDisplayUnit()
{
    if ( walletModel && walletModel->getOptionsModel() )
    {
        unitofcoin newUnit = walletModel->getOptionsModel()->getDisplayUnit() ;
        ui->payAmount->setUnitOfCoin( newUnit ) ;
        ui->payAmount_is->setUnitOfCoin( newUnit ) ;
        ui->payAmount_s->setUnitOfCoin( newUnit ) ;
    }
}

bool SendCoinsEntry::updateLabel( const QString & address )
{
    if ( walletModel == nullptr ) return false ;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = walletModel->getAddressTableModel()->labelForAddress( address ) ;
    if ( ! associatedLabel.isEmpty() )
    {
        ui->addAsLabel->setText( associatedLabel ) ;
        return true ;
    }

    return false ;
}
