// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_TRANSACTIONVIEW_H
#define DOGECOIN_QT_TRANSACTIONVIEW_H

#include "guiutil.h"

#include <QWidget>
#include <QKeyEvent>

class PlatformStyle;
class TransactionFilterProxy;
class WalletModel;

QT_BEGIN_NAMESPACE
class QComboBox;
class QDateTimeEdit;
class QFrame;
class QLineEdit;
class QMenu;
class QModelIndex;
class QSignalMapper;
class QTableView ;
class QSpacerItem ;
QT_END_NAMESPACE

/** Widget showing the transaction list for a wallet, including a filter row.
    Using the filter row, the user can view or export a subset of the transactions
  */
class TransactionView : public QWidget
{
    Q_OBJECT

public:
    explicit TransactionView( const PlatformStyle * platformStyle, QWidget * parent = nullptr ) ;

    void setWalletModel( WalletModel * model ) ;

    // Date ranges for filter
    enum DateEnum
    {
        All,
        Today,
        ThisWeek,
        ThisMonth,
        LastMonth,
        ThisYear,
        Range
    };

private:
    WalletModel * walletModel ;
    TransactionFilterProxy * transactionProxyModel ;
    QTableView * transactionTableView ;

    QSpacerItem * spacerBeforeFilteringWidgets ;
    QComboBox * dateWidget ;
    QComboBox * typeWidget ;
    QComboBox * watchOnlyWidget ;
    QLineEdit * addressWidget ;
    QLineEdit * amountWidget ;

    QMenu *contextMenu;
    QSignalMapper *mapperThirdPartyTxUrls;

    QFrame * dateRangeWidget ;
    QDateTimeEdit * dateFrom ;
    QDateTimeEdit * dateTo ;

    QAction * abandonAction ;

    GUIUtil::TableViewLastColumnResizingFixer * columnResizingFixer ;

    static QFrame * createDateRangeWidget( QDateTimeEdit * dateFrom, QDateTimeEdit * dateTo ) ;

    virtual void resizeEvent( QResizeEvent * event ) ;

    bool eventFilter( QObject * obj, QEvent * event ) ;

private Q_SLOTS:
    void updateWidths() ;
    void contextualMenu(const QPoint &);
    void dateRangeChanged();
    void showDetails();
    void copyAddress();
    void editLabel();
    void copyLabel();
    void copyAmount();
    void copyTxID();
    void copyTxHex();
    void copyTxPlainText();
    void openThirdPartyTxUrl(QString url);
    void updateWatchOnlyColumn(bool fHaveWatchOnly);
    void abandonTx();

Q_SIGNALS:
    void doubleClicked(const QModelIndex&);

    void message( const QString & title, const QString & message, unsigned int style ) ;

public Q_SLOTS:
    void chooseDate(int idx);
    void chooseType(int idx);
    void chooseWatchonly(int idx);
    void changedPrefix(const QString &prefix);
    void changedAmount(const QString &amount);
    void exportClicked();
    void focusTransaction(const QModelIndex&);

} ;

#endif
