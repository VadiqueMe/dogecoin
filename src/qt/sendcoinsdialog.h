// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_SENDCOINSDIALOG_H
#define DOGECOIN_QT_SENDCOINSDIALOG_H

#include "walletmodel.h"
#include "unitsofcoin.h"

#include <QDialog>
#include <QMessageBox>
#include <QString>
#include <QTimer>

class NetworkModel ;
class OptionsModel ;
class PlatformStyle;
class QButtonGroup ;
class SendCoinsEntry ;
class SendCoinsRecipient ;

namespace Ui {
    class SendCoinsDialog ;
}

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

/** Dialog for sending bitcoins */
class SendCoinsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SendCoinsDialog( const PlatformStyle * style, QWidget * parent = nullptr ) ;
    ~SendCoinsDialog() ;

    void setWalletModel( WalletModel * model ) ;

    /* Set up the tab chain manually, as Qt messes up the tab chain by default in some cases
       ( issue https://bugreports.qt-project.org/browse/QTBUG-10907 ) */
    QWidget * setupTabChain( QWidget * prev ) ;

    void setAddress( const QString & address ) ;
    void pasteEntry( const SendCoinsRecipient & rv ) ;
    bool handlePaymentRequest( const SendCoinsRecipient & recipient ) ;

    static QString makeAreYouSureToSendCoinsString( const WalletModelTransaction & theTransaction, const unitofcoin & unit ) ;

public Q_SLOTS:
    void clear() ;
    void reject() ;
    void accept() ;
    SendCoinsEntry * addEntry() ;
    void updateListOfEntries() ;
    void updateTabsAndLabels() ;
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance);

private:
    Ui::SendCoinsDialog * ui ;
    WalletModel * walletModel ;
    const PlatformStyle * platformStyle ;
    QButtonGroup * whichFeeChoice ;

    bool fNewRecipientAllowed ;

    QString getFeeString() ;

    // Process WalletModel::SendCoinsReturn and generate a pair consisting
    // of a message and message flags for use in Q_EMIT message()
    void processSendCoinsReturn( const WalletModel::SendCoinsReturn & sendCoinsReturn ) ;

    void minimizeCoinControl( bool fMinimize ) ;

private Q_SLOTS:
    void on_sendButton_clicked();
    void showCoinControlClicked() ;
    void hideCoinControlClicked() ;
    void removeEntry( SendCoinsEntry * entry ) ;
    void updateDisplayUnit() ;
    void changeCustomFeeUnit( unitofcoin newUnit ) ;
    void coinControlButtonClicked() ;
    void coinControlChangeChecked( int ) ;
    void coinControlChangeEdited( const QString & ) ;
    void coinControlUpdateLabels() ;

    void coinControlQuantityToClipboard() ;
    void coinControlAmountToClipboard() ;
    void coinControlFeeToClipboard() ;
    void coinControlAfterFeeToClipboard() ;
    void coinControlBytesToClipboard() ;
    void coinControlChangeToClipboard() ;

    void updateFeeSection();
    void updateGlobalFeeVariable() ;

Q_SIGNALS:
    // fired when there's a message to the user
    void message( const QString & title, const QString & message, unsigned int style ) ;
} ;


class SendConfirmationDialog : public QMessageBox
{
    Q_OBJECT

public:
    SendConfirmationDialog(const QString &title, const QString &text, int secDelay = 0, QWidget *parent = 0);
    int exec();

private Q_SLOTS:
    void countDown();
    void updateYesButton();

private:
    QAbstractButton *yesButton;
    QTimer countDownTimer;
    int secDelay;
};

#endif
