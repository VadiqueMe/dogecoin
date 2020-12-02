// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_WALLETVIEW_H
#define DOGECOIN_QT_WALLETVIEW_H

#include "amount.h"
#include "unitsofcoin.h"

#include <QStackedWidget>

class DogecoinGUI ;
class NetworkModel ;
class OverviewPage;
class PlatformStyle;
class ReceiveCoinsDialog;
class SendCoinsDialog;
class SendCoinsRecipient;
class TransactionView;
class WalletModel ;
enum class WalletEncryptionStatus ;
class AddressBookPage;
class GenerateCoinsPage ;

QT_BEGIN_NAMESPACE
class QModelIndex;
class QProgressDialog;
QT_END_NAMESPACE

/*
  WalletView class. This class represents the view to a single wallet.
  It was added to support multiple wallet functionality. Each wallet gets its own WalletView instance.
  It communicates with both the network and the wallet models to give the user an up-to-date view
*/
class WalletView : public QStackedWidget
{
    Q_OBJECT

public:
    explicit WalletView( const PlatformStyle * style, QWidget * parent ) ;
    ~WalletView() ;

    void setGUI( DogecoinGUI * gui ) ;

    /** Set the network model
        The network model represents the part of the core that communicates with the P2P network, and is wallet-agnostic
    */
    void setNetworkModel( NetworkModel * networkModel ) ;

    /** Set the wallet model
        The wallet model represents a dogecoin wallet, and offers access to the list of transactions, address book and sending
        functionality
    */
    void setWalletModel( WalletModel * model ) ;

    bool handlePaymentRequest(const SendCoinsRecipient& recipient);

    void showOutOfSyncWarning(bool fShow);

private:
    NetworkModel * networkModel ;
    WalletModel * walletModel ;

    OverviewPage * overviewPage ;
    QWidget * transactionsPage ;
    ReceiveCoinsDialog * receiveCoinsPage ;
    SendCoinsDialog * sendCoinsPage ;
    GenerateCoinsPage * generateCoinsPage ;

    AddressBookPage *usedSendingAddressesPage;
    AddressBookPage *usedReceivingAddressesPage;

    TransactionView *transactionView;

    QProgressDialog *progressDialog;
    const PlatformStyle *platformStyle;

public Q_SLOTS:
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

    void updateDigPage() ;

    /** Show Sign/Verify Message dialog and switch to sign message tab */
    void gotoSignMessageTab(QString addr = "");
    /** Show Sign/Verify Message dialog and switch to verify message tab */
    void gotoVerifyMessageTab(QString addr = "");

    /** Show incoming transaction notification for new transactions.

        The new items are those between start and end inclusive, under the given parent item.
    */
    void processNewTransaction(const QModelIndex& parent, int start, int /*end*/);
    /** Encrypt the wallet */
    void encryptWallet(bool status);
    /** Backup the wallet */
    void backupWallet();
    /** Change encrypted wallet passphrase */
    void changePassphrase();
    /** Ask for passphrase to unlock wallet temporarily */
    void unlockWallet();
    /** Open the print paper wallets dialog **/
    void printPaperWallets();

    /** Show used sending addresses */
    void usedSendingAddresses();
    /** Show used receiving addresses */
    void usedReceivingAddresses();

    /** Re-emit encryption status signal */
    void updateEncryptionStatus() ;

    /** Show progress dialog e.g. for rescan */
    void showProgress(const QString &title, int nProgress);

    /** User has requested more information about the out of sync state */
    void requestedSyncWarningInfo();

Q_SIGNALS:
    /** Signal that we want to show the main window */
    void showNormalIfMinimized();
    /**  Fired when a message should be reported to the user */
    void message(const QString &title, const QString &message, unsigned int style);
    /** Encryption status of wallet changed */
    void encryptionStatusChanged( WalletEncryptionStatus status ) ;
    /** HD-Enabled status of wallet changed (only possible during startup) */
    void hdEnabledStatusChanged( int hdEnabled ) ;
    /** Notify that a new transaction appeared */
    void incomingTransaction( const QString & date, const unitofcoin & unit, const CAmount & amount, const QString & type, const QString & address, const QString & label ) ;
    /** Notify that the out of sync warning icon has been pressed */
    void outOfSyncWarningClicked();
};

#endif
