// Copyright (c) 2019 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_QT_GENERATECOINSPAGE_H
#define DOGECOIN_QT_GENERATECOINSPAGE_H

#include <QDialog>
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

#include "miner.h"
#include "walletmodel.h"

class PlatformStyle ;

class QCheckBox ;
class QComboBox ;

class MiningThreadTab : public QWidget
{
    Q_OBJECT

public:
    explicit MiningThreadTab( const MiningThread * const thread, QWidget * parent = nullptr )
        : QWidget( parent )
        , miningThread( thread )
        , bigLabel( new QLabel( "mining thread" ) )
    {
        bigLabel->setWordWrap( true ) ;
        QVBoxLayout * tabLayout = new QVBoxLayout() ;
        tabLayout->addWidget( bigLabel.get() ) ;
        setLayout( tabLayout ) ;
    }

    const MiningThread * const getThread() const {  return miningThread ;  }

    void setTextOfLabel( const std::string & string )
        {  setTextOfLabel( QString::fromStdString( string ) ) ;  }

    void setTextOfLabel( const QString & text ) {  bigLabel->setText( text ) ;  }

private:
    const MiningThread * const miningThread ;

    std::unique_ptr< QLabel > bigLabel ;

} ;

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

    void setWalletModel( WalletModel * model ) ;

    QCheckBox & getGenerateBlocksCheckbox() ;
    QComboBox & getNumberOfThreadsList() ;

    void refreshBlockSubsidy() ;

public Q_SLOTS:
    void toggleGenerateBlocks( int qtCheckState ) ;
    void changeNumberOfThreads( const QString & threads ) ;
    void pickWayForAmountOfNewCoins( const QString & way ) ;
    void newBlockCoinsEdited( qint64 amount ) ;
    void partOfMaxCoinsEdited() ;
    void updateDisplayUnit() ;
    void updateThreadTabs() ;
    void updateTipBlockInfo() ;

private:
    Ui::GenerateCoinsPage * ui ;
    const PlatformStyle * platformStyle ;
    WalletModel * walletModel ;

    int lastNumerator ;
    int lastDenominator ;
    double lastMultiplier ;
    CAmount lastCustomAmount ;

    std::vector < MiningThreadTab * > miningTabs ;

    void currentWayForAmountOfNewCoins() ;
    void updateKindOfHowManyCoinsToGenerate() ;
    void rebuildThreadTabs() ;

} ;

#endif
