// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_QRImageWidget_H
#define DOGECOIN_QT_QRImageWidget_H

#include <QImage>
#include <QLabel>

QT_BEGIN_NAMESPACE
class QMenu ;
QT_END_NAMESPACE

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h" /* for USE_QRCODE */
#endif

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

/* Label widget for QR code. This image can be dragged, dropped, copied and saved
 * to disk
 */
class QRImageWidget : public QLabel
{
    Q_OBJECT

public:
    explicit QRImageWidget( QWidget * parent = nullptr ) ;
    QImage exportImage() ;

public Q_SLOTS:
    void saveImage() ;
    void copyImage() ;

protected:
    virtual void mousePressEvent( QMouseEvent * event ) ;
    virtual void contextMenuEvent( QContextMenuEvent * event ) ;

private:
    QMenu * contextMenu ;

} ;

#endif
