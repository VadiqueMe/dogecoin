// Copyright (c) 2014-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "networkstyle.h"

#include "guiconstants.h"

#include <QApplication>

static const struct {
    const char * networkId ;
    const char * appName ;
    const int iconColorHueShift ;
    const int iconColorSaturationReduction ;
    const char * textToAppendToTitle ;
} network_styles[] = {
    { "main", QAPP_APP_NAME_DEFAULT, 0, 0, "" },
    { "inu", QAPP_APP_NAME_INU, 333, 22, "( inu )" },
    { "test", QAPP_APP_NAME_TESTNET, 70, 30, "[testnet]" },
    { "regtest", QAPP_APP_NAME_TESTNET, 160, 30, "[regtest]" }
} ;
static const size_t network_styles_count = sizeof( network_styles ) / sizeof( *network_styles ) ;

NetworkStyle::NetworkStyle( const QString & name, const int iconColorHueShift, const int iconColorSaturationReduction, const std::string & textToAppend ):
    appName( name ),
    textToAppendToTitle( QString::fromStdString( textToAppend ) )
{
    // load pixmap
    QPixmap pixmap(":/icons/bitcoin");

    if(iconColorHueShift != 0 && iconColorSaturationReduction != 0)
    {
        // generate QImage from QPixmap
        QImage img = pixmap.toImage();

        int h,s,l,a;

        // traverse though lines
        for(int y=0;y<img.height();y++)
        {
            QRgb *scL = reinterpret_cast< QRgb *>( img.scanLine( y ) );

            // loop through pixels
            for(int x=0;x<img.width();x++)
            {
                // preserve alpha because QColor::getHsl doesen't return the alpha value
                a = qAlpha(scL[x]);
                QColor col(scL[x]);

                // get hue value
                col.getHsl(&h,&s,&l);

                // rotate color on RGB color circle
                // 70° should end up with the typical "testnet" green
                h+=iconColorHueShift;

                // change saturation value
                if(s>iconColorSaturationReduction)
                {
                    s -= iconColorSaturationReduction;
                }
                col.setHsl(h,s,l,a);

                // set the pixel
                scL[x] = col.rgba();
            }
        }

        //convert back to QPixmap
#if QT_VERSION >= 0x040700
        pixmap.convertFromImage(img);
#else
        pixmap = QPixmap::fromImage(img);
#endif
    }

    appIcon             = QIcon(pixmap);
    trayAndWindowIcon   = QIcon(pixmap.scaled(QSize(256,256)));
}

const NetworkStyle * NetworkStyle::instantiate( const QString & networkId )
{
    for ( size_t x = 0 ; x < network_styles_count ; ++ x )
    {
        if ( networkId == network_styles[ x ].networkId )
        {
            return new NetworkStyle(
                    network_styles[ x ].appName,
                    network_styles[ x ].iconColorHueShift,
                    network_styles[ x ].iconColorSaturationReduction,
                    network_styles[ x ].textToAppendToTitle
            ) ;
        }
    }

    return nullptr ;
}
