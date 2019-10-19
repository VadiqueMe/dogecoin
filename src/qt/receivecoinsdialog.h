// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef BITCOIN_QT_RECEIVECOINSDIALOG_H
#define BITCOIN_QT_RECEIVECOINSDIALOG_H

#include "guiutil.h"
#include "walletmodel.h"

#include <QDialog>
#include <QHeaderView>
#include <QItemSelection>
#include <QKeyEvent>
#include <QMenu>
#include <QPoint>
#include <QVariant>

class OptionsModel;
class PlatformStyle;
class WalletModel;

namespace Ui {
    class ReceiveCoinsDialog;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Dialog for requesting payment of bitcoins */
class ReceiveCoinsDialog : public QDialog
{
    Q_OBJECT

public:
    enum ColumnWidths {
        DATE_COLUMN_WIDTH = 130,
        LABEL_COLUMN_WIDTH = 120,
        AMOUNT_MINIMUM_COLUMN_WIDTH = 180,
        MINIMUM_COLUMN_WIDTH = 130
    };

    explicit ReceiveCoinsDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~ReceiveCoinsDialog();

    void setModel(WalletModel *model);

    void setInfoAboutRequest( const SendCoinsRecipient & info ) ;

public Q_SLOTS:
    void clearForm() ;
    void reject() ;
    void accept() ;

protected:
    virtual void keyPressEvent(QKeyEvent *event);

private:
    Ui::ReceiveCoinsDialog *ui;
    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;
    WalletModel *model;
    QMenu *contextMenu;
    const PlatformStyle *platformStyle;
    SendCoinsRecipient info ;

    QModelIndex selectedRow();
    void copyColumnToClipboard(int column);
    virtual void resizeEvent(QResizeEvent *event);

private Q_SLOTS:
    void on_receiveButton_clicked() ;
    void recentRequestsView_selectionChanged( const QItemSelection & selected, const QItemSelection & deselected ) ;
    void updateDisplayUnit() ;
    void showMenu( const QPoint & point ) ;
    void copyURI() ;
    void copyLabel() ;
    void copyMessage() ;
    void copyAmount() ;
    void removeSelection() ;
    void clearAllHistory() ;

    void on_btnCopyURI_clicked();
    void on_btnCopyAddress_clicked();
    void updateRequest();
} ;

#endif
