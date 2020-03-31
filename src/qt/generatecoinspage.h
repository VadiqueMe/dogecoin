// Copyright (c) 2019-2020 vadique
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
        , newBlockInfoLabel( new QLabel() )
        , solutionLabel( new QLabel() )
        , coinsToLabel( new QLabel() )
        , miningInfoLabel( new QLabel() )
        , resultLabel( new QLabel() )
    {
        labels = {
            newBlockInfoLabel.get(),
            solutionLabel.get(),
            coinsToLabel.get(),
            miningInfoLabel.get(),
            resultLabel.get()
        } ;
        for ( QLabel * label : labels ) {
            label->setWordWrap( true ) ;
            label->setTextFormat( Qt::RichText ) ;
            label->setTextInteractionFlags( Qt::TextSelectableByMouse ) ;
            label->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Maximum ) ;
        }

        QVBoxLayout * tabLayout = new QVBoxLayout() ;
        tabLayout->setSpacing( 6 ) ;
        tabLayout->addStretch() ;
        for ( QLabel * label : labels )
            tabLayout->addWidget( label ) ;
        tabLayout->addStretch() ;
        tabLayout->setSizeConstraint( QLayout::SetMinimumSize ) ;
        setLayout( tabLayout ) ;
    }

    const MiningThread * const getThread() const {  return miningThread ;  }

    QLabel * const getNewBlockInfoLabel() const {  return newBlockInfoLabel.get() ;  }
    QLabel * const getSolutionLabel() const {  return solutionLabel.get() ;  }
    QLabel * const getCoinsToLabel() const {  return coinsToLabel.get() ;  }
    QLabel * const getMiningInfoLabel() const {  return miningInfoLabel.get() ;  }
    QLabel * const getResultLabel() const {  return resultLabel.get() ;  }

    void setFont( const QFont & font )
    {
        QWidget::setFont( font ) ;

        for ( QLabel * label : labels )
            label->setFont( font ) ;
    }

    void resetLabels()
    {
        for ( QLabel * label : labels )
            label->setText( QString() ) ;
    }

    void hideEmpties()
    {
        for ( QLabel * label : labels )
            label->setVisible( ! label->text().isEmpty() ) ;
    }

private:
    const MiningThread * const miningThread ;

    std::unique_ptr< QLabel > newBlockInfoLabel ;
    std::unique_ptr< QLabel > solutionLabel ;
    std::unique_ptr< QLabel > coinsToLabel ;
    std::unique_ptr< QLabel > miningInfoLabel ;
    std::unique_ptr< QLabel > resultLabel ;

    std::vector< QLabel * > labels ;

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
