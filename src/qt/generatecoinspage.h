// Copyright (c) 2019 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_QT_GENERATECOINSPAGE_H
#define DOGECOIN_QT_GENERATECOINSPAGE_H

#include <QDialog>
#include <QWidget>

class PlatformStyle ;

namespace Ui {
    class GenerateCoinsPage ;
}

/** Page for digging dogecoins */
class GenerateCoinsPage : public QDialog
{
    Q_OBJECT

public:
    explicit GenerateCoinsPage( const PlatformStyle * style, QWidget * parent = nullptr ) ;
    ~GenerateCoinsPage() ;

private:
    Ui::GenerateCoinsPage * ui ;
    const PlatformStyle * platformStyle ;
} ;

#endif
