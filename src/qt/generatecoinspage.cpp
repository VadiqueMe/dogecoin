// Copyright (c) 2019-2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "generatecoinspage.h"
#include "ui_generatecoinspage.h"

#include "optionsmodel.h"

#include "chainparams.h"
#include "unitsofcoin.h"
#include "util.h"
#include "validation.h"
#include "dogecoin.h"

#include <QDateTime>

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

    ui->newBlockSubsidy->setValue( GetCurrentNewBlockSubsidy() ) ;
    ui->newBlockSubsidy->setReadOnly( true ) ;
}

GenerateCoinsPage::~GenerateCoinsPage()
{
    delete ui ;
}

void GenerateCoinsPage::setWalletModel( WalletModel * model )
{
    walletModel = model ;

    if ( model && model->getOptionsModel() )
    {
        connect( model->getOptionsModel(), SIGNAL( displayUnitChanged(int) ), this, SLOT( updateDisplayUnit() ) ) ;
        updateDisplayUnit() ;
    }
}

QCheckBox & GenerateCoinsPage::getGenerateBlocksCheckbox()
{
    return *( ui->generateBlocksYesNo ) ;
}

QComboBox & GenerateCoinsPage::getNumberOfThreadsList()
{
    return *( ui->numberOfThreadsList ) ;
}

void GenerateCoinsPage::refreshBlockSubsudy()
{
    ui->newBlockSubsidy->setValue( GetCurrentNewBlockSubsidy() ) ;
}

void GenerateCoinsPage::toggleGenerateBlocks( int qtCheckState )
{
    if ( qtCheckState == Qt::Unchecked )
    {
        if ( ui->numberOfThreadsList->currentText() != "0" )
            ui->numberOfThreadsList->setCurrentText( "0" ) ;

        if ( HowManyMiningThreads() > 0 ) {
            GenerateCoins( false, 0, Params() ) ;
            rebuildThreadTabs() ;
        }
    }
    else /* if ( qtCheckState == Qt::Checked ) */
    {
        QString threads = ui->numberOfThreadsList->currentText() ;
        if ( threads == "0" ) {
            threads = QString::number( GetArg( "-genproclimit", DEFAULT_GENERATE_THREADS ) ) ;
            ui->numberOfThreadsList->setCurrentText( threads ) ;
        }

        if ( HowManyMiningThreads() != threads.toInt() ) {
            GenerateCoins( true, threads.toInt(), Params() ) ;
            rebuildThreadTabs() ;
        }
    }
}

void GenerateCoinsPage::changeNumberOfThreads( const QString & threads )
{
    if ( ui->numberOfThreadsList->findText( threads, Qt::MatchExactly ) < 0 ) return ;

    bool generate = ( threads != "0" ) ;
    ui->generateBlocksYesNo->setChecked( generate ) ;
    if ( HowManyMiningThreads() != threads.toInt() ) {
        GenerateCoins( generate, threads.toInt(), Params() ) ;
        rebuildThreadTabs() ;
    }
}

void GenerateCoinsPage::updateDisplayUnit()
{
    if ( walletModel && walletModel->getOptionsModel() )
    {
        ui->newBlockSubsidy->setDisplayUnit( walletModel->getOptionsModel()->getDisplayUnit() ) ;
    }
}

void GenerateCoinsPage::updateTipBlockInfo()
{
    if ( chainActive.Tip() == nullptr )
    {   // no blocks in chain
        ui->tipBlockHeight->setText( "0" ) ;
        ui->tipBlockTime->setText( QString( "Genesis block" ) +
                " @ " +  QDateTime::fromTime_t( Params().GenesisBlock().GetBlockTime() ).toString() ) ;

        ui->tipBlockVersion->setVisible( false );
        ui->tipBlockVersionLabel->setVisible( false );
        ui->tipBlockBits->setVisible( false );
        ui->tipBlockBitsLabel->setVisible( false );
        ui->tipBlockNonce->setVisible( false );
        ui->tipBlockNonceLabel->setVisible( false );
        ui->tipBlockHashSublayoutContainer->setVisible( false ) ;
        ui->tipBlockHashLabel->setVisible( false ) ;
        ui->tipBlockGeneratedCoins->setVisible( false );
        ui->tipBlockGeneratedCoinsLabel->setVisible( false );

        return ;
    }

    // height

    int chainHeight = chainActive.Tip()->nHeight /* chainActive.Height() */ ;
    QString heightString = QString::number( chainHeight ) ;

    QChar thinSpace = /* U+2009 THIN SPACE */ 0x2009 ;
    int length = heightString.size() ;
    int digitsInGroup = 3 ;
    if ( length > ( digitsInGroup + 1 ) )
        for ( int i = digitsInGroup ; i < length ; i += digitsInGroup )
            heightString.insert( length - i, thinSpace ) ;

    ui->tipBlockHeight->setText( heightString ) ;

    // time

    int64_t blockTime = chainActive.Tip()->GetBlockTime() ;

    auto ago = std::chrono::system_clock::now() - std::chrono::system_clock::from_time_t( blockTime ) ;
    using days_duration = std::chrono::duration < int, std::ratio_multiply < std::ratio< 24 >, std::chrono::hours::period >::type > ;
    auto days = std::chrono::duration_cast< days_duration >( ago ).count() ;
    auto hours = std::chrono::duration_cast< std::chrono::hours >( ago ).count() - ( 24 * days ) ;
    auto minutes = std::chrono::duration_cast< std::chrono::minutes >( ago ).count() - ( 60 * hours ) - ( 60 * 24 * days ) ;
    auto seconds = std::chrono::duration_cast< std::chrono::seconds >( ago ).count() - ( 60 * minutes ) - ( 60 * 60 * hours ) - ( 60 * 60 * 24 * days ) ;

    QString agoString ;
    if ( days > 0 ) agoString += QString::number( days ) + " day" ;
    if ( days > 1 ) agoString += "s" ;
    if ( agoString.size() > 0 && hours > 0 ) agoString += " " ;
    if ( hours > 0 ) agoString += QString::number( hours ) + " hour" ;
    if ( hours > 1 ) agoString += "s" ;
    if ( agoString.size() > 0 && minutes > 0 ) agoString += " " ;
    if ( minutes > 0 ) agoString += QString::number( minutes ) + " minute" ;
    if ( minutes > 1 ) agoString += "s" ;
    if ( agoString.size() > 0 && seconds > 0 ) agoString += " " ;
    if ( seconds > 0 ) agoString += QString::number( seconds ) + " second" ;
    if ( seconds > 1 ) agoString += "s" ;
    agoString += ( agoString.size() > 0 ) ? " ago" : "just now" ;

    ui->tipBlockTime->setText(
        QDateTime::fromTime_t( blockTime ).toString() + " (" + agoString + ")"
    ) ;

    // get header of tip block
    CBlockHeader tipBlockHeader = chainActive.Tip()->GetBlockHeader( Params().GetConsensus( chainHeight ) ) ;

    // version

    ui->tipBlockVersion->setVisible( true );
    ui->tipBlockVersionLabel->setVisible( true );

    QString justHexVersion = QString::number( tipBlockHeader.nVersion, 16 ) ;
    QString versionString = ( tipBlockHeader.nVersion < 10 ) ? justHexVersion : QString( "0x" ) + justHexVersion ;
    if ( tipBlockHeader.nVersion & CPureBlockHeader::VERSION_AUXPOW )
        versionString += " (auxpow)" ;
    ui->tipBlockVersion->setText( versionString ) ;

    // bits

    ui->tipBlockBits->setVisible( true );
    ui->tipBlockBitsLabel->setVisible( true );

    arith_uint256 expandedBits = arith_uint256().SetCompact( tipBlockHeader.nBits ) ;
    ui->tipBlockBits->setText( QString::asprintf( "%08x = %s", tipBlockHeader.nBits, expandedBits.GetHex().c_str() ) ) ;

    // nonce

    ui->tipBlockNonce->setVisible( true );
    ui->tipBlockNonceLabel->setVisible( true ) ;

    ui->tipBlockNonce->setText( QString::asprintf( "0x%08x", tipBlockHeader.nNonce ) + " = " + QString::number( tipBlockHeader.nNonce ) ) ;

    // hash

    ui->tipBlockHashSublayoutContainer->setVisible( true ) ;
    ui->tipBlockHashLabel->setVisible( true ) ;

    ui->tipBlockHashSha256->setText( QString::fromStdString( tipBlockHeader.GetSha256Hash().ToString() ) ) ;
    ui->tipBlockHashScrypt->setText( QString::fromStdString( tipBlockHeader.GetScryptHash().ToString() ) ) ;
    ui->tipBlockHashLyra2Re2->setText( QString::fromStdString( tipBlockHeader.GetLyra2Re2Hash().ToString() ) ) ;

    // new coins

    ui->tipBlockGeneratedCoins->setVisible( true );
    ui->tipBlockGeneratedCoinsLabel->setVisible( true );

    int unit = UnitsOfCoin::oneCoin ;
    if ( walletModel != nullptr && walletModel->getOptionsModel() != nullptr )
        unit = walletModel->getOptionsModel()->getDisplayUnit() ;

    CAmount tipBlockNewCoins = chainActive.Tip()->nBlockNewCoins ;
    QString tipBlockNewCoinsText = UnitsOfCoin::formatWithUnit( unit, tipBlockNewCoins ) ;

    uint256 hashPrevBlock = ( chainActive.Tip()->pprev == nullptr ) ? uint256()
                                : chainActive.Tip()->pprev->GetBlockSha256Hash() ;
    CAmount maxSubsidyForTipBlock = GetDogecoinBlockSubsidy(
        chainActive.Tip()->nHeight,
        Params().GetConsensus( chainActive.Tip()->nHeight ),
        hashPrevBlock ) ;

    if ( tipBlockNewCoins == maxSubsidyForTipBlock )
        tipBlockNewCoinsText += " (maximum subsidy)" ;
    else
        tipBlockNewCoinsText += " (of maximum subsidy " + UnitsOfCoin::formatWithUnit( unit, maxSubsidyForTipBlock ) + ")" ;

    CBlock tipBlock ;
    bool blockReadOk = ReadBlockFromDisk( tipBlock, chainActive.Tip(), Params().GetConsensus( chainActive.Tip()->nHeight ) ) ;
    CAmount tipBlockFees = 0 ;
    if ( blockReadOk ) {
        for ( unsigned int i = 0 ; i < tipBlock.vtx.size() ; i ++ )
        {
            const CTransaction & tx = *( tipBlock.vtx[ i ] ) ;
            if ( ! tx.IsCoinBase() )
            {
                CAmount txValueIn = 0 ;
                for ( const CTxIn & txin : tx.vin )
                {
                    CTransactionRef prevoutTx ;
                    uint256 blockSha256 ;
                    if ( GetTransaction( txin.prevout.hash, prevoutTx, Params().GetConsensus( 0 ), blockSha256, true ) ) {
                        const CTxOut & vout = prevoutTx->vout[ txin.prevout.n ] ;
                        txValueIn += vout.nValue ;
                    }
                }

                tipBlockFees += txValueIn - tx.GetValueOut() ;
            }
        }
    }

    if ( tipBlockFees != 0 ) {
        tipBlockNewCoinsText += " = " + UnitsOfCoin::format( unit, tipBlock.vtx[ 0 ]->vout[ 0 ].nValue ) ;
        tipBlockNewCoinsText += " - " + UnitsOfCoin::formatWithUnit( unit, tipBlockFees ) + " in fees" ;
    }

    ui->tipBlockGeneratedCoins->setText( tipBlockNewCoinsText ) ;
}

void GenerateCoinsPage::rebuildThreadTabs()
{
    miningTabs.clear() ;
    ui->detailsForThreads->setVisible( false ) ;
    ui->spacerAfterThreadTabs->changeSize( 0, 0, QSizePolicy::Maximum, QSizePolicy::Maximum ) ;
    for ( int i = ui->detailsForThreads->count() - 1 ; i >= 0 ; -- i )
        ui->detailsForThreads->removeTab( i ) ;

    size_t nThreads = HowManyMiningThreads() ;
    ui->threadsLabel->setText( nThreads == 1 ? "thread" : "threads" ) ;
    if ( nThreads > 0 )
    {
        for ( size_t thread = 0 ; thread < nThreads ; ++ thread ) {
            const MiningThread * const miner = getMiningThreadByNumber( thread + 1 ) ;
            if ( miner != nullptr ) {
                MiningThreadTab * tab = new MiningThreadTab( /* thread */ miner, /* parent */ ui->detailsForThreads ) ;
                ui->detailsForThreads->addTab( tab,
                    QString::fromStdString( toStringWithOrdinalSuffix(tab->getThread()->getNumberOfThread()) ) + QString(" ") + "thread" ) ;
                miningTabs.push_back( tab ) ;
            }
        }

        ui->spacerAfterThreadTabs->changeSize( 1, 10, QSizePolicy::Fixed, QSizePolicy::Fixed ) ;
        ui->detailsForThreads->setUsesScrollButtons( true ) ;
        ui->detailsForThreads->setVisible( true ) ;
    }
}

void GenerateCoinsPage::updateThreadTabs()
{
    if ( ui->detailsForThreads->count() != HowManyMiningThreads() )
        rebuildThreadTabs() ;

    for ( MiningThreadTab * tab : miningTabs )
        if ( tab != nullptr && tab->getThread() != nullptr ) {
            QString bigText ;

            const CBlockTemplate * const candidate = tab->getThread()->getNewBlockCandidate() ;
            if ( candidate != nullptr ) {
                bigText += "new block candidate: " ;
                bigText += "version 0x" + QString::number( candidate->block.nVersion, 16 ) ;
                bigText += ", " ;
                bigText += "transactions " + QString::number( candidate->block.vtx.size() ) ;
                bigText += ", " ;
                int unit = UnitsOfCoin::oneCoin ;
                if ( walletModel != nullptr && walletModel->getOptionsModel() != nullptr )
                    unit = walletModel->getOptionsModel()->getDisplayUnit() ;
                bigText += "fees " + UnitsOfCoin::formatWithUnit( unit, - candidate->vTxFees[ 0 ] ) ;
                bigText += "\n\n" ;

                arith_uint256 bitsUint256 = arith_uint256().SetCompact( candidate->block.nBits ) ;
                bigText += "solution is " ;
                bigText += "scrypt hash <= " + QString::fromStdString( ( bitsUint256 ).GetHex() ) ;
                if ( NameOfChain() == "inu" ) {
                    bigText += " and lyra2re2 hash <= " + QString::fromStdString( ( bitsUint256 ).GetHex() ) ;
                    bigText += " and sha256 hash <= " + QString::fromStdString( ( bitsUint256 << 1 ).GetHex() ) ;
                }
                bigText += "\n\n" ;

                if ( candidate->block.vtx[ 0 ] != nullptr ) {
                    const CScript & scriptPublicKey = candidate->block.vtx[ 0 ]->vout[ 0 ].scriptPubKey ;
                    CTxDestination destination ;
                    bigText += "generated coins will go to " ;
                    if ( ExtractDestination( scriptPublicKey, destination ) )
                        bigText += "address " + QString::fromStdString( CDogecoinAddress( destination ).ToString() ) ;
                    else bigText += "unknown address" ;
                    bigText += "\n\n" ;
                }
            }

            bigText += QString::fromStdString( tab->getThread()->threadMiningInfoString() ) ;
            bigText += "\n\n" ;

            size_t blocksGenerated = tab->getThread()->getNumberOfBlocksGeneratedByThisThread() ;
            if ( blocksGenerated == 0 )
                bigText += "no blocks were generated by this thread yet" ;
            else
                bigText += "this thread has generated " + QString::number( blocksGenerated )
                                + " block" + ( blocksGenerated > 1 ? "s" : "" )
                                + " for now" ;

            tab->setTextOfLabel( bigText ) ;
        }
}
