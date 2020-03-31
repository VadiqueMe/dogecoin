// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_UTILITYDIALOG_H
#define DOGECOIN_QT_UTILITYDIALOG_H

#include <QDialog>
#include <QObject>
#include "walletmodel.h"

class DogecoinGUI ;
class NetworkModel ;

namespace Ui {
    class HelpMessageDialog;
    class PaperWalletDialog;
}

/** "Paper Wallet" dialog box */
class PaperWalletDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PaperWalletDialog(QWidget *parent);
    ~PaperWalletDialog();

    void setNetworkModel( NetworkModel * networkModel ) ;
    void setWalletModel( WalletModel * model ) ;

private:
    Ui::PaperWalletDialog * ui ;
    NetworkModel * networkModel ;
    WalletModel * walletModel ;

    static const int PAPER_WALLET_PAGE_MARGIN = 50 ;

private Q_SLOTS:
    void on_getNewAddress_clicked();
    void on_printButton_clicked();
};

/** "Help message" dialog box */
class HelpMessageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit HelpMessageDialog(QWidget *parent, bool about);
    ~HelpMessageDialog();

    void printToConsole();
    void showOrPrint();

private:
    Ui::HelpMessageDialog *ui;
    QString text;

private Q_SLOTS:
    void on_okButton_accepted();
};


/** "Shutdown" window */
class ShutdownWindow : public QWidget
{
    Q_OBJECT

public:
    ShutdownWindow( QWidget * parent = nullptr, Qt::WindowFlags f = 0 ) ;
    static QWidget * showShutdownWindow( DogecoinGUI * window ) ;

protected:
    void closeEvent( QCloseEvent * event ) ;
};

#endif
