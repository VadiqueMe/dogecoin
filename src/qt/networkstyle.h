// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_NETWORKSTYLE_H
#define DOGECOIN_QT_NETWORKSTYLE_H

#include <QIcon>
#include <QPixmap>
#include <QString>

/* Coin network-specific GUI style information */
class NetworkStyle
{
public:
    /** Get style associated with the network id, or nil pointer if not known */
    static const NetworkStyle * instantiate( const QString & networkId ) ;

    const QString & getAppName() const {  return appName ;  }
    const QIcon & getAppIcon() const {  return appIcon ;  }
    const QIcon & getTrayAndWindowIcon() const {  return trayAndWindowIcon ;  }
    const QString & getTextToAppendToTitle() const {  return textToAppendToTitle ;  }

private:
    NetworkStyle( const QString & appName, const int iconColorHueShift, const int iconColorSaturationReduction, const std::string & titleAddText ) ;

    QString appName ;
    QIcon appIcon ;
    QIcon trayAndWindowIcon ;
    QString textToAppendToTitle ;
};

#endif
