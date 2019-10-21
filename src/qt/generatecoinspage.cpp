// Copyright (c) 2019 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "generatecoinspage.h"
#include "ui_generatecoinspage.h"

GenerateCoinsPage::GenerateCoinsPage( const PlatformStyle * style, QWidget * parent )
    : QDialog( parent )
    , ui( new Ui::GenerateCoinsPage )
    , platformStyle( style )
{
    ui->setupUi( this ) ;
}

GenerateCoinsPage::~GenerateCoinsPage()
{
    delete ui ;
}

