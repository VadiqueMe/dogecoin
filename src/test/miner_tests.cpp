// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "validation.h"
#include "miner.h"
#include "policy/policy.h"
#include "pubkey.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"

#include "test/test_dogecoin.h"

#include <memory>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(miner_tests, TestingSetup)

static CFeeRate blockMinFeeRate = CFeeRate( 0 ) ;

static
struct {
    unsigned char extranonce ;
    unsigned int nonce ;
} blockinfo [] = {
    { 4, 0x125c4cb0 }, { 2, 0x334af826 }, { 1, 0x3338e803 }, { 3, 0x6accf95f },
    { 2, 0x16c63e11 }, { 3, 0xa388397e }, { 1, 0xdd1cf9bd }, { 8, 0x72184e1c },
    { 5, 0x6e783ce1 }, { 1, 0x43e51633 }, { 1, 0x04bda0ae }, { 2, 0x1c4853bd },
    { 1, 0x7495d5ec }, { 1, 0x6043b5dd }, { 1, 0x1b8489a0 }, { 2, 0x68b37c03 },
    { 2, 0x441d1cbb }, { 1, 0x122e2d0e }, { 2, 0x833620ca }, { 2, 0xf2050b82 },
    { 7, 0xdcc2e490 }, { 3, 0xcee0d38f }, { 6, 0x9849c0eb }, { 4, 0x4434b570 },
    { 2, 0xeb8befbd }, { 1, 0x553bec3d }, { 3, 0x21e6b23d }, { 0, 0xaa6b60e7 },
    { 9, 0xde9fb220 }, { 7, 0x773ef3da }, { 11, 0x10efa5d9 }, { 6, 0x677e6319 },
    { 10, 0xfdc40ff }
} ;

bool TestSequenceLocks(const CTransaction &tx, int flags)
{
    LOCK(mempool.cs);
    return CheckSequenceLocks(tx, flags);
}

// Test suite for ancestor feerate transaction selection.
// Implemented as an additional function, rather than a separate test case,
// to allow reusing the blockchain created in CreateNewBlock_validity
// Note that this test assumes blockprioritysize is 0
void TestPackageSelection(const CChainParams& chainparams, CScript scriptPubKey, std::vector<CTransactionRef>& txFirst)
{
    // Test the ancestor feerate transaction selection
    TestMemPoolEntryHelper entry;

    // Test that a medium fee transaction will be selected after a higher fee
    // rate package with a low fee rate parent
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetTxHash() ;
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 5000000000LL - 1000 ;
    // This tx has a low fee: 1000 satoshis
    uint256 hashParentTx = tx.GetTxHash() ; // save this txid for later use
    mempool.addUnchecked( hashParentTx, entry.Fee( 1000 ).Time( GetTime() ).SpendsCoinbase( true ).FromTx( tx ) ) ;

    // This tx has a medium fee: 10000 satoshis
    tx.vin[0].prevout.hash = txFirst[1]->GetTxHash() ;
    tx.vout[0].nValue = 5000000000LL - 10000 ;
    uint256 hashMediumFeeTx = tx.GetTxHash() ;
    mempool.addUnchecked( hashMediumFeeTx, entry.Fee( 10000 ).Time( GetTime() ).SpendsCoinbase( true ).FromTx( tx ) ) ;

    // This tx has a high fee, but depends on the first transaction
    tx.vin[0].prevout.hash = hashParentTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000 ; // 50k satoshi fee
    uint256 hashHighFeeTx = tx.GetTxHash() ;
    mempool.addUnchecked( hashHighFeeTx, entry.Fee( 50000 ).Time( GetTime() ).SpendsCoinbase( false ).FromTx( tx ) ) ;

    std::unique_ptr< CBlockTemplate > pblocktemplate( BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ) ) ;
    BOOST_CHECK( pblocktemplate->block.vtx[1]->GetTxHash() == hashParentTx ) ;
    BOOST_CHECK( pblocktemplate->block.vtx[2]->GetTxHash() == hashHighFeeTx ) ;
    BOOST_CHECK( pblocktemplate->block.vtx[3]->GetTxHash() == hashMediumFeeTx ) ;

    // Test that a package below the block min tx fee doesn't get included
    tx.vin[0].prevout.hash = hashHighFeeTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000 ; // 0 fee
    uint256 hashFreeTx = tx.GetTxHash() ;
    mempool.addUnchecked( hashFreeTx, entry.Fee( 0 ).FromTx( tx ) ) ;
    size_t freeTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

    // Calculate a fee on child transaction that will put the package just
    // below the block min tx fee (assuming 1 child tx of the same size)
    CAmount feeToUse = blockMinFeeRate.GetFeePerBytes( 2 * freeTxSize ) - 1 ;

    tx.vin[0].prevout.hash = hashFreeTx;
    tx.vout[0].nValue = 5000000000LL - 1000 - 50000 - feeToUse ;
    uint256 hashLowFeeTx = tx.GetTxHash() ;
    mempool.addUnchecked( hashLowFeeTx, entry.Fee( feeToUse ).FromTx( tx ) ) ;
    pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey, true);
    // Verify that the free tx and the low fee tx didn't get selected
    for ( size_t i = 0 ; i < pblocktemplate->block.vtx.size() ; ++ i ) {
        BOOST_CHECK( pblocktemplate->block.vtx[ i ]->GetTxHash() != hashFreeTx ) ;
        BOOST_CHECK( pblocktemplate->block.vtx[ i ]->GetTxHash() != hashLowFeeTx ) ;
    }

    // Test that packages above the min relay fee do get included, even if one
    // of the transactions is below the min relay fee
    // Remove the low fee transaction and replace with a higher fee transaction
    mempool.removeRecursive(tx);
    tx.vout[0].nValue -= 2; // Now we should be just over the min relay fee
    hashLowFeeTx = tx.GetTxHash() ;
    mempool.addUnchecked(hashLowFeeTx, entry.Fee(feeToUse+2).FromTx(tx));
    pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey, true);
    BOOST_CHECK( pblocktemplate->block.vtx[ 4 ]->GetTxHash() == hashFreeTx ) ;
    BOOST_CHECK( pblocktemplate->block.vtx[ 5 ]->GetTxHash() == hashLowFeeTx ) ;

    // Test that transaction selection properly updates ancestor fee
    // calculations as ancestor transactions get included in a block.
    // Add a 0-fee transaction that has 2 outputs
    tx.vin[0].prevout.hash = txFirst[ 2 ]->GetTxHash() ;
    tx.vout.resize(2);
    tx.vout[0].nValue = 5000000000LL - 100000000 ;
    tx.vout[1].nValue = 100000000 ; // 1 DOGE output
    uint256 hashFreeTx2 = tx.GetTxHash() ;
    mempool.addUnchecked( hashFreeTx2, entry.Fee( 0 ).SpendsCoinbase( true ).FromTx( tx ) ) ;

    // This tx can't be mined by itself
    tx.vin[0].prevout.hash = hashFreeTx2;
    tx.vout.resize(1);
    feeToUse = blockMinFeeRate.GetFeePerBytes( freeTxSize ) ;
    tx.vout[0].nValue = 5000000000LL - 100000000 - feeToUse ;
    uint256 hashLowFeeTx2 = tx.GetTxHash() ;
    mempool.addUnchecked(hashLowFeeTx2, entry.Fee(feeToUse).SpendsCoinbase(false).FromTx(tx));
    pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey, true);

    // Verify that this tx isn't selected
    for ( size_t i = 0 ; i < pblocktemplate->block.vtx.size() ; ++ i ) {
        BOOST_CHECK( pblocktemplate->block.vtx[ i ]->GetTxHash() != hashFreeTx2 ) ;
        BOOST_CHECK( pblocktemplate->block.vtx[ i ]->GetTxHash() != hashLowFeeTx2 ) ;
    }

    // This tx will be mineable, and should cause hashLowFeeTx2 to be selected
    // as well
    tx.vin[0].prevout.n = 1;
    tx.vout[0].nValue = 100000000 - 10000 ; // 10k satoshi fee
    mempool.addUnchecked( tx.GetTxHash(), entry.Fee( 10000 ).FromTx( tx ) ) ;
    pblocktemplate = BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ) ;
    BOOST_CHECK( pblocktemplate->block.vtx[ 8 ]->GetTxHash() == hashLowFeeTx2 ) ;
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation
BOOST_AUTO_TEST_CASE(CreateNewBlock_validity)
{
    // Note that by default, these tests run with size accounting enabled
    const CChainParams & chainparams = ParamsFor( "main" ) ;
    BOOST_CHECK( chainparams.GetConsensus( 0 ).hashGenesisBlock ==
                    uint256S( "0x1a91e3dace36e2be3bf030a65679fe821aa1d6ef92e7c9902eb318182c355691" ) ) ;

    // dogecoin genesis pubkey script
    CScript scriptPubKey = CScript() << ParseHex("040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9") << OP_CHECKSIG;
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    CMutableTransaction tx,tx2;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.dPriority = 111.0;
    entry.nHeight = 11;

    LOCK(cs_main);
    fCheckpointsEnabled = false;

    // We can't make transactions until we have inputs
    // Therefore, make some blocks :)
    int baseheight = 0 ;
    std::vector< CTransactionRef > txFirst ;
    size_t number_of_blocks_to_premine = chainparams.GetConsensus( 0 ).nCoinbaseMaturity + 3 /* sizeof( blockinfo ) / sizeof( *blockinfo ) */ ;
    for ( unsigned int i = 0 ; i < number_of_blocks_to_premine ; ++ i )
    {
        // create new block candidate for each block
        // it is needed because each new block's subsidy is random derived from previous block's hash
        BOOST_CHECK( pblocktemplate = BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ) ) ;

        CBlock *pblock = &pblocktemplate->block ; // pointer for convenience
        pblock->nVersion = 1;
        pblock->nTime = chainActive.Tip()->GetBlockTime() + /* 1 minute and 5 seconds */ 65 ;
        CMutableTransaction txCoinbase( *pblock->vtx[0] ) ;
        txCoinbase.nVersion = 1 ;
        txCoinbase.vin[0].scriptSig = CScript() ;
        txCoinbase.vin[0].scriptSig.push_back( blockinfo[ i ].extranonce ) ;
        txCoinbase.vin[0].scriptSig.push_back( chainActive.Height() ) ;
        txCoinbase.vout.resize( 1 ); // ignore the (optional) segwit commitment added by CreateNewBlock
        txCoinbase.vout[0].scriptPubKey = CScript() ;
        pblock->vtx[0] = MakeTransactionRef( std::move( txCoinbase ) ) ;

        if ( txFirst.size() == 0 )
            baseheight = chainActive.Height() ;
        if ( txFirst.size() < 4 )
            txFirst.push_back( pblock->vtx[0] ) ;

        pblock->hashMerkleRoot = BlockMerkleRoot( *pblock ) ;
        pblock->nNonce = blockinfo[ i ].nonce ;
        /* while ( ! CheckProofOfWork( *pblock, pblock->nBits, chainparams.GetConsensus( 0 ) ) ) {
            pblock->nNonce -- ;
            if ( pblock->nNonce == 0 ) pblock->nNonce = 0xfffffffe ;
        }
        LogPrintf( "miner_tests: blockinfo[ %d ].nonce = 0x%x, extraNonce = %d\n", i, pblock->nNonce, blockinfo[ i ].extranonce ) ; */
        std::shared_ptr< const CBlock > shared_pblock = std::make_shared< const CBlock >( *pblock ) ;
        BOOST_CHECK( ProcessNewBlock( chainparams, shared_pblock, true, NULL ) ) ;
        //* pblock->hashPrevBlock = pblock->GetSha256Hash() ; // not needed when the new block candidate is created on every iteration
    }

    // Just to make sure we can still make simple blocks
    BOOST_CHECK( pblocktemplate = BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ) ) ;

    const CAmount LOWFEE = E6COIN ;
    static const int number_of_transactions = 1001 ; // 1000 CHECKMULTISIG + 1
    const CAmount BLOCKSUBSIDY = number_of_transactions * LOWFEE ;

    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize( 1 ) ;
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1 ;
    tx.vin[0].prevout.hash = txFirst[ 0 ]->GetTxHash() ;
    tx.vin[0].prevout.n = 0 ;
    tx.vout.resize( 1 ) ;
    tx.vout[0].nValue = BLOCKSUBSIDY ;
    for ( unsigned int i = 0 ; i < number_of_transactions ; ++ i )
    {
        tx.vout[0].nValue -= LOWFEE ;
        hash = tx.GetTxHash() ;
        bool spendsCoinbase = ( i == 0 ) ? true : false ; // only first tx spends coinbase
        // If we don't set the # of sig ops in the CTxMemPoolEntry, template creation fails
        mempool.addUnchecked( hash, entry.Fee( LOWFEE ).Time( GetTime() ).SpendsCoinbase( spendsCoinbase ).FromTx( tx ) ) ;
        tx.vin[0].prevout.hash = hash ;
    }
    BOOST_CHECK_THROW( BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ), std::runtime_error ) ;
    mempool.clear() ;

    tx.vin[0].prevout.hash = txFirst[0]->GetTxHash() ;
    tx.vout[0].nValue = BLOCKSUBSIDY ;
    for ( unsigned int i = 0 ; i < number_of_transactions ; ++ i )
    {
        tx.vout[0].nValue -= LOWFEE ;
        hash = tx.GetTxHash() ;
        bool spendsCoinbase = ( i == 0 ) ? true : false ; // only first tx spends coinbase
        // If we do set the # of sig ops in the CTxMemPoolEntry, template creation passes
        mempool.addUnchecked( hash, entry.Fee( LOWFEE ).Time( GetTime() ).SpendsCoinbase( spendsCoinbase ).SigOpsCost( 80 ).FromTx( tx ) ) ;
        tx.vin[0].prevout.hash = hash ;
    }
    BOOST_CHECK( pblocktemplate = BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ) ) ;
    mempool.clear() ;

    // block size > limit
    tx.vin[0].scriptSig = CScript() ;
    // 18 * (520char + DROP) + OP_1 = 9433 bytes
    std::vector< unsigned char > vchData( 520 ) ;
    for ( unsigned int i = 0 ; i < 18 ; ++ i )
        tx.vin[0].scriptSig << vchData << OP_DROP ;
    tx.vin[0].scriptSig << OP_1 ;
    tx.vin[0].prevout.hash = txFirst[0]->GetTxHash() ;
    tx.vout[0].nValue = BLOCKSUBSIDY ;
    for ( unsigned int i = 0 ; i < 128 ; ++ i )
    {
        tx.vout[0].nValue -= LOWFEE ;
        hash = tx.GetTxHash() ;
        bool spendsCoinbase = ( i == 0 ) ? true : false ; // only the first tx spends coinbase
        mempool.addUnchecked( hash, entry.Fee( LOWFEE ).Time( GetTime() ).SpendsCoinbase( spendsCoinbase ).FromTx( tx ) ) ;
        tx.vin[0].prevout.hash = hash ;
    }
    BOOST_CHECK( pblocktemplate = BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ) ) ;
    mempool.clear() ;

    // orphan in mempool, template creation fails
    hash = tx.GetTxHash() ;
    mempool.addUnchecked( hash, entry.Fee( LOWFEE ).Time( GetTime() ).FromTx( tx ) ) ;
    BOOST_CHECK_THROW( BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ), std::runtime_error ) ;
    mempool.clear() ;

    const CAmount HIGHFEE = 20 * LOWFEE ;
    const CAmount HIGHERFEE = 5 * HIGHFEE ;

    // child with higher priority than parent
    tx.vin[0].scriptSig = CScript() << OP_1 ;
    tx.vin[0].prevout.hash = txFirst[1]->GetTxHash() ;
    tx.vout[0].nValue = BLOCKSUBSIDY - HIGHFEE ;
    hash = tx.GetTxHash() ;
    mempool.addUnchecked( hash, entry.Fee( HIGHFEE ).Time( GetTime() ).SpendsCoinbase( true ).FromTx( tx ) ) ;
    tx.vin[0].prevout.hash = hash ;
    tx.vin.resize( 2 ) ;
    tx.vin[1].scriptSig = CScript() << OP_1 ;
    tx.vin[1].prevout.hash = txFirst[0]->GetTxHash() ;
    tx.vin[1].prevout.n = 0 ;
    tx.vout[0].nValue = tx.vout[0].nValue + BLOCKSUBSIDY - HIGHERFEE ; // first txn output + fresh coinbase - new txn fee
    hash = tx.GetTxHash() ;
    mempool.addUnchecked( hash, entry.Fee( HIGHERFEE ).Time( GetTime() ).SpendsCoinbase( true ).FromTx( tx ) ) ;
    BOOST_CHECK( pblocktemplate = BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ) ) ;
    mempool.clear() ;

    // coinbase in mempool, template creation fails
    tx.vin.resize( 1 ) ;
    tx.vin[0].prevout.SetNull() ;
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1 ;
    tx.vout[0].nValue = 0 ;
    hash = tx.GetTxHash() ;
    // no need to give any fee to be mined
    //* mempool.addUnchecked( hash, entry.Fee( LOWFEE ).Time( GetTime() ).SpendsCoinbase( false ).FromTx( tx ) ) ; *//
    mempool.addUnchecked( hash, entry.Fee( 0 ).Time( GetTime() ).SpendsCoinbase( false ).FromTx( tx ) ) ;
    BOOST_CHECK_THROW( BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ), std::runtime_error ) ;
    mempool.clear() ;

    // invalid (pre-p2sh) txn in mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetTxHash() ;
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-LOWFEE;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(CScriptID(script));
    hash = tx.GetTxHash() ;
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
    tx.vout[0].nValue -= LOWFEE;
    hash = tx.GetTxHash() ;
    mempool.addUnchecked(hash, entry.Fee(LOWFEE).Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    BOOST_CHECK_THROW(BlockAssembler(chainparams).CreateNewBlock(scriptPubKey, true), std::runtime_error);
    mempool.clear();

    // double spend txn pair in mempool, template creation fails
    tx.vin[0].prevout.hash = txFirst[0]->GetTxHash() ;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    hash = tx.GetTxHash() ;
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetTxHash() ;
    mempool.addUnchecked(hash, entry.Fee(HIGHFEE).Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    BOOST_CHECK_THROW(BlockAssembler(chainparams).CreateNewBlock(scriptPubKey, true), std::runtime_error);
    mempool.clear();

    // subsidy changing, Bitcoin block subsidy halves every 210 000 blocks
    {
    int nHeight = chainActive.Height() ;
    std::vector< uint256 > hashes ;
    hashes.reserve( 210000 - nHeight ) ;
    // Create an actual 209999-long block chain (without valid blocks)
    while (chainActive.Tip()->nHeight < 209999) {
        CBlockIndex* prev = chainActive.Tip();
        CBlockIndex* next = new CBlockIndex();
        hashes.push_back( uint256( GetRandHash() ) ) ;
        next->SetBlockSha256Hash( hashes.back() );
        pcoinsTip->SetBestBlockBySha256( next->GetBlockSha256Hash() ) ;
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        chainActive.SetTip( next ) ;
    }
    BOOST_CHECK( pblocktemplate = BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ) ) ;
    // Extend to a 210000-long block chain
    while (chainActive.Tip()->nHeight < 210000) {
        CBlockIndex* prev = chainActive.Tip();
        CBlockIndex* next = new CBlockIndex();
        hashes.push_back( uint256( GetRandHash() ) ) ;
        next->SetBlockSha256Hash( hashes.back() ) ;
        pcoinsTip->SetBestBlockBySha256( next->GetBlockSha256Hash() ) ;
        next->pprev = prev;
        next->nHeight = prev->nHeight + 1;
        next->BuildSkip();
        chainActive.SetTip( next ) ;
    }
    BOOST_CHECK( pblocktemplate = BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey, true ) ) ;
    // Delete these dummy blocks
    while (chainActive.Tip()->nHeight > nHeight) {
        CBlockIndex* del = chainActive.Tip();
        chainActive.SetTip( del->pprev ) ;
        pcoinsTip->SetBestBlockBySha256( del->pprev->GetBlockSha256Hash() ) ;
        delete del ;
    }
    hashes.clear() ;
    }

    // non-final txs in mempool
    // changed to 60 second block interval for consistency
    SetMockTime(chainActive.Tip()->GetBlockTime()+60);
    int flags = LOCKTIME_VERIFY_SEQUENCE|LOCKTIME_MEDIAN_TIME_PAST;
    // height map
    std::vector<int> prevheights;

    // relative height locked
    tx.nVersion = 2;
    tx.vin.resize(1);
    prevheights.resize(1);
    tx.vin[0].prevout.hash = txFirst[0]->GetTxHash() ; // only 1 transaction
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = chainActive.Tip()->nHeight + 1; // txFirst[0] is the 2nd block
    prevheights[0] = baseheight + 1;
    tx.vout.resize(1);
    tx.vout[0].nValue = BLOCKSUBSIDY-HIGHFEE;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = 0;
    hash = tx.GetTxHash() ;
    mempool.addUnchecked( hash, entry.Fee( HIGHFEE ).Time( GetTime() ).SpendsCoinbase( true ).FromTx( tx ) ) ;
    BOOST_CHECK( CheckFinalTx( tx, flags ) ) ; // Locktime passes
    BOOST_CHECK( ! TestSequenceLocks( tx, flags ) ) ; // Sequence locks fail

    {
        CBlockIndex blockIndex ;
        blockIndex.nHeight = chainActive.Tip()->nHeight + 2 ;
        blockIndex.pprev = chainActive.Tip() ;
        BOOST_CHECK( SequenceLocks( tx, flags, &prevheights, blockIndex ) ) ; // Sequence locks pass on 2nd block
    }

    // relative time locked
    tx.vin[0].prevout.hash = txFirst[1]->GetTxHash() ;
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | (((chainActive.Tip()->GetMedianTimePast()+1-chainActive[1]->GetMedianTimePast()) >> CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) + 1); // txFirst[1] is the 3rd block
    prevheights[0] = baseheight + 2;
    hash = tx.GetTxHash() ;
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    BOOST_CHECK( CheckFinalTx( tx, flags ) ) ; // Locktime passes
    BOOST_CHECK( ! TestSequenceLocks(tx, flags ) ) ; // Sequence locks fail

    static const int nMedianTimeSpan = 11 ;

    for ( int i = 0 ; i < nMedianTimeSpan ; i ++ )
        chainActive.Tip()->GetAncestor( chainActive.Tip()->nHeight - i )->nTime += 512 ; // Trick the MedianTimePast

    {
        CBlockIndex blockIndex ;
        blockIndex.nHeight = chainActive.Tip()->nHeight + 1 ;
        blockIndex.pprev = chainActive.Tip() ;
        BOOST_CHECK( SequenceLocks( tx, flags, &prevheights, blockIndex ) ) ; // Sequence locks pass 512 seconds later
    }

    for ( int i = 0 ; i < nMedianTimeSpan ; i ++ )
        chainActive.Tip()->GetAncestor( chainActive.Tip()->nHeight - i )->nTime -= 512 ; //undo tricked MedianTimePast

    // absolute height locked
    tx.vin[0].prevout.hash = txFirst[2]->GetTxHash() ;
    tx.vin[0].nSequence = CTxIn::SEQUENCE_FINAL - 1;
    prevheights[0] = baseheight + 3;
    tx.nLockTime = chainActive.Tip()->nHeight + 1;
    hash = tx.GetTxHash() ;
    mempool.addUnchecked( hash, entry.Time( GetTime() ).FromTx( tx ) ) ;
    BOOST_CHECK(!CheckFinalTx(tx, flags)); // Locktime fails
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    BOOST_CHECK(IsFinalTx(tx, chainActive.Tip()->nHeight + 2, chainActive.Tip()->GetMedianTimePast())); // Locktime passes on 2nd block

    // absolute time locked
    tx.vin[0].prevout.hash = txFirst[3]->GetTxHash() ;
    tx.nLockTime = chainActive.Tip()->GetMedianTimePast();
    prevheights.resize(1);
    prevheights[0] = baseheight + 4;
    hash = tx.GetTxHash() ;
    mempool.addUnchecked( hash, entry.Time( GetTime() ).FromTx( tx ) ) ;
    BOOST_CHECK( ! CheckFinalTx( tx, flags ) ) ; // Locktime fails
    BOOST_CHECK( TestSequenceLocks( tx, flags ) ) ; // Sequence locks pass
    BOOST_CHECK( IsFinalTx( tx, chainActive.Tip()->nHeight + 2, chainActive.Tip()->GetMedianTimePast() + 1 ) ) ; // Locktime passes 1 second later

    // mempool-dependent transactions (not added)
    tx.vin[0].prevout.hash = hash;
    prevheights[0] = chainActive.Tip()->nHeight + 1;
    tx.nLockTime = 0;
    tx.vin[0].nSequence = 0;
    BOOST_CHECK(CheckFinalTx(tx, flags)); // Locktime passes
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = 1;
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG;
    BOOST_CHECK(TestSequenceLocks(tx, flags)); // Sequence locks pass
    tx.vin[0].nSequence = CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG | 1;
    BOOST_CHECK(!TestSequenceLocks(tx, flags)); // Sequence locks fail

    BOOST_CHECK(pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey, true));

    // None of the of the absolute height/time locked tx should have made
    // it into the template because we still check IsFinalTx in CreateNewBlock,
    // but relative locked txs will if inconsistently added to mempool.
    // For now these will still generate a valid template until BIP68 soft fork
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 3);
    // However if we advance height by 1 and time by 512, all of them should be mined
    for ( int i = 0 ; i < nMedianTimeSpan ; i ++ )
        chainActive.Tip()->GetAncestor(chainActive.Tip()->nHeight - i)->nTime += 512; //Trick the MedianTimePast
    chainActive.Tip()->nHeight++;
    // changed to 60 second block interval for consistency
    SetMockTime(chainActive.Tip()->GetBlockTime() + 60);

    BOOST_CHECK(pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey, true));
    BOOST_CHECK_EQUAL(pblocktemplate->block.vtx.size(), 5);

    chainActive.Tip()->nHeight--;
    SetMockTime(0);
    mempool.clear();

    // Dogecoin: Package selection doesn't work that way because our fees are fundamentally
    //           different. Need to rationalise in a later release.
    // TestPackageSelection(chainparams, scriptPubKey, txFirst);

    fCheckpointsEnabled = true;
}

BOOST_AUTO_TEST_SUITE_END()
