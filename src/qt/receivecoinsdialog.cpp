// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "receivecoinsdialog.h"
#include "ui_receivecoinsdialog.h"

#include "addressbookpage.h"
#include "addresstablemodel.h"
#include "unitsofcoin.h"
#include "guiutil.h"
#include "guiconstants.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "qrimagewidget.h"
#include "recentrequeststablemodel.h"

#include <QAction>
#include <QCursor>
#include <QItemSelection>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>
#include <QPainter>

ReceiveCoinsDialog::ReceiveCoinsDialog( const PlatformStyle * style, QWidget * parent ) :
    QDialog(parent),
    ui(new Ui::ReceiveCoinsDialog),
    columnResizingFixer(0),
    walletModel( nullptr ),
    platformStyle( style )
{
    ui->setupUi(this);

    if ( ! style->getImagesOnButtons() ) {
        ui->clearButton->setIcon( QIcon() ) ;
        ui->receiveButton->setIcon( QIcon() ) ;
    } else {
        ui->clearButton->setIcon( style->SingleColorIcon( ":/icons/remove" ) ) ;
        ui->receiveButton->setIcon( style->SingleColorIcon( ":/icons/receiving_addresses" ) ) ;
    }

    connect( ui->clearButton, SIGNAL( clicked() ), this, SLOT( clearForm() ) ) ;

    // context menu
    contextMenu = new QMenu( this ) ;

    QAction * copyURIAction = new QAction( "Copy URI", this ) ;
    QAction * copyLabelAction = new QAction( "Copy label", this ) ;
    QAction * copyMessageAction = new QAction( "Copy message", this ) ;
    QAction * copyAmountAction = new QAction( "Copy amount", this ) ;

    contextMenu->addAction( copyURIAction ) ;
    contextMenu->addAction( copyLabelAction ) ;
    contextMenu->addAction( copyMessageAction ) ;
    contextMenu->addAction( copyAmountAction ) ;

    QAction * removeAction = new QAction( tr("Remove"), this ) ;
    QAction * clearHistoryAction = new QAction( "Clear History", this ) ;

    contextMenu->addSeparator() ;
    contextMenu->addAction( removeAction ) ;
    contextMenu->addAction( clearHistoryAction ) ;

    // context menu signals
    connect( ui->recentRequestsView, SIGNAL( customContextMenuRequested(QPoint) ), this, SLOT( showMenu(QPoint) ) ) ;
    connect( copyURIAction, SIGNAL( triggered() ), this, SLOT( copyURI() ) ) ;
    connect( copyLabelAction, SIGNAL( triggered() ), this, SLOT( copyLabel() ) ) ;
    connect( copyMessageAction, SIGNAL( triggered() ), this, SLOT( copyMessage() ) ) ;
    connect( copyAmountAction, SIGNAL( triggered() ), this, SLOT( copyAmount() ) ) ;
    connect( removeAction, SIGNAL( triggered() ), this, SLOT( removeSelection() ) ) ;
    connect( clearHistoryAction, SIGNAL( triggered() ), this, SLOT( clearAllHistory() ) ) ;

#ifndef USE_QRCODE
    ui->btnSaveAs->setVisible( false ) ;
    ui->paymentRequestQRCode->setVisible( false ) ;
#endif

    connect( ui->btnSaveAs, SIGNAL( clicked() ), ui->paymentRequestQRCode, SLOT( saveImage() ) ) ;
}

void ReceiveCoinsDialog::setWalletModel( WalletModel * model )
{
    this->walletModel = model ;

    if ( model && model->getOptionsModel() )
    {
        model->getRecentRequestsTableModel()->sort( RecentRequestsTableModel::Date, Qt::DescendingOrder ) ;
        connect( model->getOptionsModel(), SIGNAL( displayUnitChanged(int) ), this, SLOT( updateDisplayUnit() ) ) ;
        updateDisplayUnit() ;

        QTableView* tableView = ui->recentRequestsView ;

        tableView->verticalHeader()->hide();
        tableView->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded ) ;
        tableView->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded ) ;
        tableView->setModel( model->getRecentRequestsTableModel() ) ;
        tableView->setAlternatingRowColors(true);
        tableView->setSelectionBehavior( QAbstractItemView::SelectRows ) ;
        tableView->setSelectionMode( QAbstractItemView::SingleSelection ) ;
        tableView->setColumnWidth(RecentRequestsTableModel::Date, DATE_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Label, LABEL_COLUMN_WIDTH);
        tableView->setColumnWidth(RecentRequestsTableModel::Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);

        connect( tableView->selectionModel(),
            SIGNAL( selectionChanged(QItemSelection, QItemSelection) ), this,
            SLOT( recentRequestsView_selectionChanged(QItemSelection, QItemSelection) ) ) ;
        // last 2 columns are set by the columnResizingFixer, when the table geometry is ready
        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(tableView, AMOUNT_MINIMUM_COLUMN_WIDTH, DATE_COLUMN_WIDTH, this);

        connect( model->getOptionsModel(), SIGNAL( displayUnitChanged(int) ), this, SLOT( updateRequest() ) ) ;
    }

    updateRequest() ;
}

ReceiveCoinsDialog::~ReceiveCoinsDialog()
{
    delete ui;
}

void ReceiveCoinsDialog::clearForm()
{
    ui->reqAmount->clear();
    ui->reqLabel->setText("");
    ui->reqMessage->setText("");
    ui->reuseAddress->setChecked(false);
    updateDisplayUnit();
}

void ReceiveCoinsDialog::reject()
{
    clearForm() ;
}

void ReceiveCoinsDialog::accept()
{
    clearForm() ;
}

void ReceiveCoinsDialog::updateDisplayUnit()
{
    if ( walletModel && walletModel->getOptionsModel() )
    {
        ui->reqAmount->setDisplayUnit( walletModel->getOptionsModel()->getDisplayUnit() ) ;
    }
}

void ReceiveCoinsDialog::on_receiveButton_clicked()
{
    if ( ! walletModel || ! walletModel->getOptionsModel() ||
            ! walletModel->getAddressTableModel() || ! walletModel->getRecentRequestsTableModel() )
        return ;

    QString address;
    QString label = ui->reqLabel->text();
    if(ui->reuseAddress->isChecked())
    {
        /* Choose existing receiving address */
        AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::ReceivingTab, this);
        dlg.setAddressTableModel( walletModel->getAddressTableModel() ) ;
        if(dlg.exec())
        {
            address = dlg.getReturnValue();
            if ( label.isEmpty() ) /* when no label provided, use the previously used label */
            {
                label = walletModel->getAddressTableModel()->labelForAddress( address ) ;
            }
        } else {
            return;
        }
    } else {
        /* Generate new receiving address */
        address = walletModel->getAddressTableModel()->addRow( AddressTableModel::Receive, label, "" ) ;
    }

    SendCoinsRecipient info( address, label,
        ui->reqAmount->value(), ui->reqMessage->text() ) ;
    setInfoAboutRequest( info ) ;
    clearForm() ;

    /* Store request for later reference */
    walletModel->getRecentRequestsTableModel()->addNewRequest( info ) ;
    ui->recentRequestsView->clearSelection() ;
}

void ReceiveCoinsDialog::recentRequestsView_selectionChanged( const QItemSelection & selected, const QItemSelection & deselected )
{
    QModelIndexList chosen = ui->recentRequestsView->selectionModel()->selectedRows() ;

    if ( chosen.count() == 1 )
    {
        QModelIndex index = chosen.at( 0 ) ;
        const RecentRequestsTableModel * submodel = walletModel->getRecentRequestsTableModel() ;
        setInfoAboutRequest( submodel->entry( index.row() ).recipient ) ;
    }
}

void ReceiveCoinsDialog::removeSelection()
{
    if ( ! walletModel || ! walletModel->getRecentRequestsTableModel() || ! ui->recentRequestsView->selectionModel() )
        return ;
    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows() ;
    if ( selection.empty() )
        return ;

    // okay for ContiguousSelection too
    QModelIndex firstIndex = selection.at( 0 ) ;
    walletModel->getRecentRequestsTableModel()->removeRows( firstIndex.row(), selection.length(), firstIndex.parent() ) ;
}

void ReceiveCoinsDialog::clearAllHistory()
{
    ui->recentRequestsView->clearSelection() ;

    if ( ! walletModel || ! walletModel->getRecentRequestsTableModel() )
        return ;

    RecentRequestsTableModel * tableModel = walletModel->getRecentRequestsTableModel() ;
    tableModel->removeRows( 0, tableModel->rowCount() ) ;

    SendCoinsRecipient info ;
    setInfoAboutRequest( info ) ;
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width
void ReceiveCoinsDialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(RecentRequestsTableModel::Message);
}

void ReceiveCoinsDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return)
    {
        // press return -> submit form
        if (ui->reqLabel->hasFocus() || ui->reqAmount->hasFocus() || ui->reqMessage->hasFocus())
        {
            event->ignore();
            on_receiveButton_clicked();
            return;
        }
    }

    this->QDialog::keyPressEvent(event);
}

QModelIndex ReceiveCoinsDialog::selectedRow()
{
    if ( ! walletModel || ! walletModel->getRecentRequestsTableModel() || ! ui->recentRequestsView->selectionModel() )
        return QModelIndex();

    QModelIndexList selection = ui->recentRequestsView->selectionModel()->selectedRows();
    if ( selection.empty() )
        return QModelIndex();

    QModelIndex firstIndex = selection.at(0);
    return firstIndex;
}

// copy column of selected row to clipboard
void ReceiveCoinsDialog::copyColumnToClipboard(int column)
{
    QModelIndex firstIndex = selectedRow();
    if (!firstIndex.isValid()) {
        return;
    }
    GUIUtil::setClipboard( walletModel->getRecentRequestsTableModel()->data(
        firstIndex.child( firstIndex.row(), column ), Qt::EditRole
    ).toString() ) ;
}

// context menu
void ReceiveCoinsDialog::showMenu(const QPoint &point)
{
    if (!selectedRow().isValid()) {
        return;
    }
    contextMenu->exec(QCursor::pos());
}

void ReceiveCoinsDialog::copyURI()
{
    QModelIndex sel = selectedRow();
    if (!sel.isValid()) {
        return;
    }

    const RecentRequestsTableModel * const submodel = walletModel->getRecentRequestsTableModel() ;
    const QString uri = GUIUtil::formatDogecoinURI( submodel->entry( sel.row() ).recipient ) ;
    GUIUtil::setClipboard(uri);
}

void ReceiveCoinsDialog::copyLabel()
{
    copyColumnToClipboard(RecentRequestsTableModel::Label);
}

void ReceiveCoinsDialog::copyMessage()
{
    copyColumnToClipboard(RecentRequestsTableModel::Message);
}

void ReceiveCoinsDialog::copyAmount()
{
    copyColumnToClipboard(RecentRequestsTableModel::Amount);
}

void ReceiveCoinsDialog::updateRequest()
{
    if ( ! walletModel || ! walletModel->getOptionsModel() )
        return ;

    QString target = info.label;
    if(target.isEmpty())
        target = info.address;
    setWindowTitle(tr("Request payment to %1").arg(target));

    QString uri = GUIUtil::formatDogecoinURI( info ) ;
    ui->btnSaveAs->setEnabled(false);
    QString html;
    html += "<html><font face='verdana, arial, helvetica, sans-serif'>";
    html += "<b>"+tr("Payment information")+"</b><br>";
    html += "<b>"+tr("URI")+"</b>: ";
    html += "<a href=\""+uri+"\">" + GUIUtil::HtmlEscape(uri) + "</a><br>";
    html += "<b>"+tr("Address")+"</b>: " + GUIUtil::HtmlEscape(info.address) + "<br>";
    if ( info.amount )
        html += "<b>" + tr("Amount")+"</b>: " + UnitsOfCoin::formatHtmlWithUnit( walletModel->getOptionsModel()->getDisplayUnit(), info.amount ) + "<br>" ;
    if(!info.label.isEmpty())
        html += "<b>"+tr("Label")+"</b>: " + GUIUtil::HtmlEscape(info.label) + "<br>";
    if(!info.message.isEmpty())
        html += "<b>"+tr("Message")+"</b>: " + GUIUtil::HtmlEscape(info.message) + "<br>";
    ui->outUri->setText(html);

#ifdef USE_QRCODE
    ui->paymentRequestQRCode->setText("");
    if(!uri.isEmpty())
    {
        // limit URI length
        if (uri.length() > MAX_URI_LENGTH)
        {
            ui->paymentRequestQRCode->setText( "Resulting URI is too long, try to reduce the text for label / message" ) ;
        } else {
            QRcode *code = QRcode_encodeString( uri.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1 ) ;
            if ( ! code )
            {
                ui->paymentRequestQRCode->setText( tr("Error encoding URI into QR Code") ) ;
                return ;
            }
            QImage qrImage = QImage(code->width + 8, code->width + 8, QImage::Format_RGB32);
            qrImage.fill(0xffffff);
            unsigned char *p = code->data;
            for (int y = 0; y < code->width; y++)
            {
                for (int x = 0; x < code->width; x++)
                {
                    qrImage.setPixel(x + 4, y + 4, ((*p & 1) ? 0x0 : 0xffffff));
                    p++;
                }
            }
            QRcode_free(code);

            QImage qrAddrImage = QImage(QR_IMAGE_SIZE, QR_IMAGE_SIZE+20, QImage::Format_RGB32);
            qrAddrImage.fill(0xffffff);
            QPainter painter(&qrAddrImage);
            painter.drawImage(0, 0, qrImage.scaled(QR_IMAGE_SIZE, QR_IMAGE_SIZE));
            QFont font = GUIUtil::fixedPitchFont();
            font.setPixelSize(12);
            painter.setFont(font);
            QRect paddedRect = qrAddrImage.rect();
            paddedRect.setHeight(QR_IMAGE_SIZE+12);
            painter.drawText(paddedRect, Qt::AlignBottom|Qt::AlignCenter, info.address);
            painter.end();

            ui->paymentRequestQRCode->setPixmap( QPixmap::fromImage( qrAddrImage ) ) ;
            ui->btnSaveAs->setEnabled( true ) ;
        }
    }
#endif
}

void ReceiveCoinsDialog::on_btnCopyURI_clicked()
{
    GUIUtil::setClipboard( GUIUtil::formatDogecoinURI( info ) ) ;
}

void ReceiveCoinsDialog::on_btnCopyAddress_clicked()
{
    GUIUtil::setClipboard(info.address);
}

void ReceiveCoinsDialog::setInfoAboutRequest( const SendCoinsRecipient & info )
{
    this->info = info ;
    updateRequest() ;
}
