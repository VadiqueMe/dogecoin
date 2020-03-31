// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_SENDCOINSENTRY_H
#define DOGECOIN_QT_SENDCOINSENTRY_H

#include "walletmodel.h"

#include <QStackedWidget>

class WalletModel;
class PlatformStyle;

namespace Ui {
    class SendCoinsEntry;
}

/**
 * A single entry in the dialog for sending coins.
 * Stacked widget, with different UIs for payment requests
 * with a strong payee identity
 */
class SendCoinsEntry : public QStackedWidget
{
    Q_OBJECT

public:
    explicit SendCoinsEntry( const PlatformStyle * style, QWidget * parent = nullptr ) ;
    ~SendCoinsEntry() ;

    void setWalletModel( WalletModel * model ) ;
    bool validate() ;
    SendCoinsRecipient getValue() ;

    /** Return whether the entry is still empty and unedited */
    bool isClear() const ;

    void setValue( const SendCoinsRecipient & value ) ;
    void setAddress( const QString & address ) ;

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases
     *  (issue https://bugreports.qt-project.org/browse/QTBUG-10907)
     */
    QWidget * setupTabChain( QWidget * prev ) ;

    void setFocus() ;

    void showHideSubtractFeeFromAmountCheckbox( bool show ) ;
    void showHideHorizontalLine( bool show ) ;

public Q_SLOTS:
    void clear() ;

Q_SIGNALS:
    void removeEntry(SendCoinsEntry *entry);
    void payAmountChanged();
    void subtractFeeFromAmountChanged() ;

private Q_SLOTS:
    void deleteClicked();
    void on_payTo_textChanged( const QString & address ) ;
    void on_addressBookButton_clicked();
    void on_pasteButton_clicked();
    void updateDisplayUnit();

private:
    SendCoinsRecipient recipient ;
    Ui::SendCoinsEntry * ui ;
    WalletModel * walletModel ;
    const PlatformStyle * platformStyle ;

    bool updateLabel( const QString & address ) ;
} ;

#endif
