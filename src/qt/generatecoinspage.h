// Copyright (c) 2019 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_QT_GENERATECOINSPAGE_H
#define DOGECOIN_QT_GENERATECOINSPAGE_H

#include <QDialog>
#include <QWidget>

class PlatformStyle ;

class QCheckBox ;
class QComboBox ;

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

    QCheckBox & getGenerateBlocksCheckbox() ;
    QComboBox & getNumberOfThreadsList() ;

public Q_SLOTS:
    void toggleGenerateBlocks( int qtCheckState ) ;
    void changeNumberOfThreads( const QString & threads ) ;

private:
    Ui::GenerateCoinsPage * ui ;
    const PlatformStyle * platformStyle ;
} ;

#endif
