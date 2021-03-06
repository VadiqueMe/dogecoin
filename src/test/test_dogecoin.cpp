// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#define BOOST_TEST_MODULE Dogecoin Test Suite

#include "test_dogecoin.h"

#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "key.h"
#include "validation.h"
#include "miner.h"
#include "net_processing.h"
#include "pubkey.h"
#include "random.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"
#include "utilthread.h"
#include "rpc/server.h"
#include "rpc/register.h"
#include "script/sigcache.h"
#include "test/testutil.h"

#include <memory>

#include <boost/test/unit_test.hpp>

std::unique_ptr< CConnman > g_connman ;
FastRandomContext insecure_rand_ctx( true ) ;

extern void noui_connect() ;

BasicTestingSetup::BasicTestingSetup( const std::string & chainName )
{
        ECC_Start() ;
        SetupEnvironment() ;
        SetupNetworking() ;
        InitSignatureCache() ;
        PickPrintToConsole() ; // don't want to write to debug log file
        fCheckBlockIndex = true ;
        SelectParams( chainName ) ;
        noui_connect() ;
}

BasicTestingSetup::~BasicTestingSetup()
{
        ECC_Stop() ;
        g_connman.reset() ;
}

TestingSetup::TestingSetup(const std::string& chainName) : BasicTestingSetup(chainName)
{
    const CChainParams& chainparams = Params();
        // Ideally we'd move all the RPC tests to the functional testing framework
        // instead of unit tests, but for now we need these here

        RegisterAllCoreRPCCommands(tableRPC);
        ClearDatadirCache();
        pathTemp = GetTempPath() / strprintf( "test_dogecoin_%lu_%i", (unsigned long)GetTime(), (int)( GetRand(100000) ) ) ;
        boost::filesystem::create_directories(pathTemp);
        ForceSetArg("-datadir", pathTemp.string());
        mempool.setSanityCheck(1.0);
        pblocktree = new CBlockTreeDB(1 << 20, true);
        pcoinsdbview = new CCoinsViewDB(1 << 23, true);
        pcoinsTip = new CCoinsViewCache(pcoinsdbview);
        InitBlockIndex(chainparams);
        {
            CValidationState state;
            bool ok = ActivateBestChain(state, chainparams);
            BOOST_CHECK(ok);
        }
        nScriptCheckThreads = 3 ;
        for ( int i = 0 ; i < nScriptCheckThreads - 1 ; i ++ )
            scriptcheckThreads.push_back( std::thread( &ThreadScriptCheck ) ) ;
        g_connman = std::unique_ptr<CConnman>(new CConnman(0x1337, 0x1337)); // Deterministic randomness for tests
        connman = g_connman.get();
        RegisterNodeSignals(GetNodeSignals());
}

TestingSetup::~TestingSetup()
{
    UnregisterNodeSignals( GetNodeSignals() ) ;

    StopScriptChecking() ;
    JoinAll( scriptcheckThreads ) ;

    UnloadBlockIndex() ;
    delete pcoinsTip ;
    delete pcoinsdbview ;
    delete pblocktree ;
    boost::filesystem::remove_all( pathTemp ) ;
}

TestChain240Setup::TestChain240Setup() : TestingSetup( "regtest" )
{
    // Generate a 240-block chain:
    coinbaseKey.MakeNewKey(true);
    CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    const int manyBlocks = 60 * 4 ; // 4 hours of blocks
    for ( int i = 0; i < manyBlocks; i ++ )
    {
        std::vector<CMutableTransaction> noTxns;
        CBlock b = CreateAndProcessBlock(noTxns, scriptPubKey);
        coinbaseTxns.push_back(*b.vtx[0]);
    }
}

//
// Create a new block with just given transactions, coinbase paying to
// scriptPubKey, and try to add it to the current chain
//
CBlock
TestChain240Setup::CreateAndProcessBlock( const std::vector< CMutableTransaction > & txns, const CScript & scriptPubKey )
{
    const CChainParams & chainparams = Params() ;
    std::unique_ptr< CBlockTemplate > pblocktemplate = BlockAssembler( chainparams ).CreateNewBlock( scriptPubKey ) ;
    CBlock & block = pblocktemplate->block ;

    // Replace mempool-selected txns with just coinbase plus passed-in txns:
    block.vtx.resize(1);
    for ( const CMutableTransaction & tx : txns )
        block.vtx.push_back( MakeTransactionRef( tx ) ) ;

    // IncrementExtraNonce creates a valid coinbase and merkleRoot
    unsigned int extraNonce = 0;
    IncrementExtraNonce(&block, chainActive.Tip(), extraNonce);

    while ( ! CheckProofOfWork( block, block.nBits, chainparams.GetConsensus(0) ) ) ++ block.nNonce ;

    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(block);
    ProcessNewBlock(chainparams, shared_pblock, true, NULL);

    CBlock result = block;
    return result;
}

TestChain240Setup::~TestChain240Setup()
{
}


CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CMutableTransaction &tx, CTxMemPool *pool) {
    CTransaction txn(tx);
    return FromTx(txn, pool);
}

CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CTransaction &txn, CTxMemPool *pool) {
    // Hack to assume either it's completely dependent on other mempool txs or not at all
    CAmount inChainValue = pool && pool->HasNoInputsOf(txn) ? txn.GetValueOut() : 0;

    return CTxMemPoolEntry(MakeTransactionRef(txn), nFee, nTime, dPriority, nHeight,
                           inChainValue, spendsCoinbase, sigOpCost, lp);
}

void Shutdown( void* arg )
{
    exit( EXIT_SUCCESS ) ;
}
