// Copyright (c) 2019 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "generatecoinspage.h"
#include "ui_generatecoinspage.h"

#include "miner.h"
#include "chainparams.h"
#include "util.h"

GenerateCoinsPage::GenerateCoinsPage( const PlatformStyle * style, QWidget * parent )
    : QDialog( parent )
    , ui( new Ui::GenerateCoinsPage )
    , platformStyle( style )
{
    ui->setupUi( this ) ;

    ui->generateBlocksYesNo->setChecked( GetBoolArg( "-gen", DEFAULT_GENERATE ) ) ;

    // clear number of threads list before filling it
    while ( ui->numberOfThreadsList->count() > 0 )
        ui->numberOfThreadsList->removeItem( 0 ) ;

    ui->numberOfThreadsList->addItem( "0" ) ;
    int numcores = GetNumCores() ;
    for ( int i = 1 ; i <= numcores ; ++ i )
        ui->numberOfThreadsList->addItem( QString::number( i ) ) ;

    QString genthreads = QString::number( GetArg( "-genproclimit", DEFAULT_GENERATE_THREADS ) ) ;
    if ( ui->numberOfThreadsList->findText( genthreads, Qt::MatchExactly ) < 0 )
        ui->numberOfThreadsList->addItem( genthreads ) ;

    ui->numberOfThreadsList->setCurrentText( ui->generateBlocksYesNo->isChecked() ? genthreads : "0" ) ;

    connect( ui->generateBlocksYesNo, SIGNAL( stateChanged(int) ),
                this, SLOT( toggleGenerateBlocks(int) ) ) ;
    connect( ui->numberOfThreadsList, SIGNAL( currentIndexChanged(const QString &) ),
                this, SLOT( changeNumberOfThreads(const QString &) ) ) ;
}

GenerateCoinsPage::~GenerateCoinsPage()
{
    delete ui ;
}

QCheckBox & GenerateCoinsPage::getGenerateBlocksCheckbox()
{
    return *( ui->generateBlocksYesNo ) ;
}

QComboBox & GenerateCoinsPage::getNumberOfThreadsList()
{
    return *( ui->numberOfThreadsList ) ;
}

void GenerateCoinsPage::toggleGenerateBlocks( int qtCheckState )
{
    if ( qtCheckState == Qt::Unchecked )
    {
        if ( ui->numberOfThreadsList->currentText() != "0" )
            ui->numberOfThreadsList->setCurrentText( "0" ) ;

        GenerateDogecoins( false, 0, Params() ) ;
    }
    else /* if ( qtCheckState == Qt::Checked ) */
    {
        QString threads = ui->numberOfThreadsList->currentText() ;
        if ( threads == "0" ) {
            threads = QString::number( GetArg( "-genproclimit", DEFAULT_GENERATE_THREADS ) ) ;
            ui->numberOfThreadsList->setCurrentText( threads ) ;
        }

        GenerateDogecoins( true, threads.toInt(), Params() ) ;
    }
}

void GenerateCoinsPage::changeNumberOfThreads( const QString & threads )
{
    if ( ui->numberOfThreadsList->findText( threads, Qt::MatchExactly ) < 0 ) return ;

    bool generate = ( threads != "0" ) ;
    ui->generateBlocksYesNo->setChecked( generate ) ;
    GenerateDogecoins( generate, threads.toInt(), Params() ) ;
}
