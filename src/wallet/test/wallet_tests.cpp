// Copyright (c) 2012-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "txmempool.h"
#include "wallet/wallet.h"

#include <set>
#include <stdint.h>
#include <utility>
#include <vector>

#include "rpc/server.h"
#include "test/test_dogecoin.h"
#include "validation.h"
#include "wallet/test/wallet_test_fixture.h"

#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>
#include <univalue.h>

extern UniValue importmulti(const JSONRPCRequest& request);
extern UniValue dumpwallet(const JSONRPCRequest& request);
extern UniValue importwallet(const JSONRPCRequest& request);

// how many times to run all the tests to have a chance to catch errors that only show up with particular random shuffles
#define RUN_TESTS 100

// some tests fail 1% of the time due to bad luck.
// we repeat those tests this many times and only complain if all iterations of the test fail
#define RANDOM_REPEATS 5

std::vector<std::unique_ptr<CWalletTx>> wtxn;

typedef std::set< std::pair< const CWalletTx *, unsigned int > > CoinSet ;

BOOST_FIXTURE_TEST_SUITE(wallet_tests, WalletTestingSetup)

static const CWallet wallet ;
static std::vector< COutput > vCoins ;

static void add_coin(const CAmount& nValue, int nAge = 6*24, bool fIsFromMe = false, int nInput=0)
{
    static int nextLockTime = 0;
    CMutableTransaction tx;
    tx.nLockTime = nextLockTime++;        // so all transactions get different hashes
    tx.vout.resize(nInput+1);
    tx.vout[nInput].nValue = nValue;
    if (fIsFromMe) {
        // IsFromMe() returns (GetDebit() > 0), and GetDebit() is 0 if vin.empty(),
        // so stop vin being empty, and cache a non-zero Debit to fake out IsFromMe()
        tx.vin.resize(1);
    }
    std::unique_ptr<CWalletTx> wtx(new CWalletTx(&wallet, MakeTransactionRef(std::move(tx))));
    if (fIsFromMe)
    {
        wtx->fDebitCached = true;
        wtx->nDebitCached = 1;
    }
    COutput output(wtx.get(), nInput, nAge, true, true);
    vCoins.push_back(output);
    wtxn.emplace_back(std::move(wtx));
}

static void empty_wallet(void)
{
    vCoins.clear();
    wtxn.clear();
}

static bool equal_sets(CoinSet a, CoinSet b)
{
    std::pair< CoinSet::iterator, CoinSet::iterator > ret = mismatch( a.begin(), a.end(), b.begin() ) ;
    return ret.first == a.end() && ret.second == b.end() ;
}

BOOST_AUTO_TEST_CASE(coin_selection_tests)
{
    CoinSet setCoinsRet, setCoinsRet2;
    CAmount nValueRet;

    LOCK(wallet.cs_wallet);

    // test multiple times to allow for differences in the shuffle order
    for (int i = 0; i < RUN_TESTS; i++)
    {
        empty_wallet() ;

        // with an empty wallet we can't even pay one cent
        BOOST_CHECK( ! wallet.SelectCoinsMinConf( 1 * E8CENT, 1, 6, 0, vCoins, setCoinsRet, nValueRet ) ) ;

        add_coin( 1 * E8CENT, 4 ) ; // add a new 1 cent coin

        // with a new 1 cent coin, we still can't find a mature 1 cent
        BOOST_CHECK( ! wallet.SelectCoinsMinConf( 1 * E8CENT, 1, 6, 0, vCoins, setCoinsRet, nValueRet ) ) ;

        // but we can find a new 1 cent
        BOOST_CHECK( wallet.SelectCoinsMinConf( 1 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 1 * E8CENT ) ;

        add_coin( 2 * E8CENT ) ; // add a mature 2 cent coin

        // we can't make 3 cents of mature coins
        BOOST_CHECK( ! wallet.SelectCoinsMinConf( 3 * E8CENT, 1, 6, 0, vCoins, setCoinsRet, nValueRet ) ) ;

        // we can make 3 cents of new  coins
        BOOST_CHECK( wallet.SelectCoinsMinConf( 3 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 3 * E8CENT ) ;

        add_coin( 5 * E8CENT ) ;           // add a mature 5 cent coin,
        add_coin( 10 * E8CENT, 3, true ) ; // a new 10 cent coin sent from one of our own addresses
        add_coin( 20 * E8CENT ) ;          // and a mature 20 cent coin

        // now we have new: 1+10=11 (of which 10 was self-sent), and mature: 2+5+20=27.  total = 38

        // we can't make 38 cents only if we disallow new coins
        BOOST_CHECK( ! wallet.SelectCoinsMinConf( 38 * E8CENT, 1, 6, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        // we can't even make 37 cents if we don't allow new coins even if they're from us
        BOOST_CHECK( ! wallet.SelectCoinsMinConf( 38 * E8CENT, 6, 6, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        // but we can make 37 cents if we accept new coins from ourself
        BOOST_CHECK( wallet.SelectCoinsMinConf( 37 * E8CENT, 1, 6, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 37 * E8CENT ) ;
        // and we can make 38 cents if we accept all new coins
        BOOST_CHECK( wallet.SelectCoinsMinConf( 38 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 38 * E8CENT ) ;

        // try making 34 cents from 1,2,5,10,20 - we can't do it exactly
        BOOST_CHECK( wallet.SelectCoinsMinConf( 34 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 35 * E8CENT ) ; // but 35 cents is closest
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 3U ) ; // the best is expected to be 20+10+5, it's very unlikely the 1 or 2 got included (but possible)

        // when we try making 7 cents, the smaller coins (1,2,5) are enough, we should see just 2+5
        BOOST_CHECK( wallet.SelectCoinsMinConf( 7 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 7 * E8CENT ) ;
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 2U ) ;

        // when we try making 8 cents, the smaller coins (1,2,5) are exactly enough
        BOOST_CHECK( wallet.SelectCoinsMinConf( 8 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK( nValueRet == 8 * E8CENT ) ;
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 3U ) ;

        // when we try making 9 cents, no subset of smaller coins is enough, and we get the next bigger coin, 10
        BOOST_CHECK( wallet.SelectCoinsMinConf( 9 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 10 * E8CENT ) ;
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 1U ) ;

        // now clear out the wallet and start again to test choosing between subsets of smaller coins and the next biggest coin
        empty_wallet() ;

        add_coin( 6 * E8CENT ) ;
        add_coin( 7 * E8CENT ) ;
        add_coin( 8 * E8CENT ) ;
        add_coin( 20 * E8CENT ) ;
        add_coin( 30 * E8CENT ) ; // now we have 6+7+8+20+30 = 71 cents total

        // check that we have 71 and not 72
        BOOST_CHECK( wallet.SelectCoinsMinConf( 71 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK( ! wallet.SelectCoinsMinConf( 72 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;

        // now try making 16 cents.  the best smaller coins can do is 6+7+8 = 21; not as good at the next biggest coin, 20
        BOOST_CHECK( wallet.SelectCoinsMinConf( 16 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 20 * E8CENT ) ; // we should get 20 in one coin
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 1U ) ;

        add_coin( 5 * E8CENT ) ; // now we have 5+6+7+8+20+30 = 75 cents total

        // now if we try making 16 cents again, the smaller coins can make 5+6+7 = 18 cents, better than the next biggest coin, 20
        BOOST_CHECK( wallet.SelectCoinsMinConf( 16 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 18 * E8CENT ) ; // we should get 18 in 3 coins
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 3U ) ;

        add_coin( 18 * E8CENT ) ; // now we have 5+6+7+8+18+20+30

        // and now if we try making 16 cents again, the smaller coins can make 5+6+7 = 18 cents, the same as the next biggest coin, 18
        BOOST_CHECK( wallet.SelectCoinsMinConf( 16 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 18 * E8CENT ) ; // we should get 18 cents in 1 coin
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 1U ) ; // because in the event of a tie, the biggest coin wins

        // now try making 11 cents, we should get 5+6
        BOOST_CHECK( wallet.SelectCoinsMinConf( 11 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 11 * E8CENT ) ;
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 2U ) ;

        // check that the smallest bigger coin is used
        add_coin( 1 * E8COIN ) ;
        add_coin( 2 * E8COIN ) ;
        add_coin( 3 * E8COIN ) ;
        add_coin( 4 * E8COIN ) ; // now we have 5+6+7+8+18+20+30+100+200+300+400 = 1094 cents
        BOOST_CHECK( wallet.SelectCoinsMinConf( 95 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 1 * E8COIN ) ; // 1 00000000 in 1 coin
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 1U ) ;

        BOOST_CHECK( wallet.SelectCoinsMinConf( 195 * E8CENT, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 2 * E8COIN ) ; // 2 00000000 in 1 coin
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 1U ) ;

        // empty the wallet to restart again
        empty_wallet() ;

        // run the 'mtgox' test (see http://blockexplorer.com/tx/29a3efd3ef04f9153d47a990bd7b048a4b2d213daaa5fb8ed670fb85f13bdbcf)
        // they tried to consolidate 10 50k coins into one 500k coin, and ended up with 50k in change
        empty_wallet();
        for ( int j = 0 ; j < 20 ; j ++ )
            add_coin( 5 * E12COIN ) ;

        BOOST_CHECK( wallet.SelectCoinsMinConf( 50 * E12COIN, 1, 1, 0, vCoins, setCoinsRet, nValueRet ) ) ;
        BOOST_CHECK_EQUAL( nValueRet, 50 * E12COIN ) ; // we should get the exact amount
        BOOST_CHECK_EQUAL( setCoinsRet.size(), 10U ) ; // in ten coins

        empty_wallet() ;

        // test randomness
        {
            empty_wallet();
            for ( int i2 = 0 ; i2 < 100 ; i2 ++ )
                add_coin( E8COIN ) ;

            // picking 50 from 100 coins doesn't depend on the shuffle,
            // but does depend on randomness in the stochastic approximation code
            BOOST_CHECK( wallet.SelectCoinsMinConf( 50 * E8COIN, 1, 6, 0, vCoins, setCoinsRet , nValueRet ) ) ;
            BOOST_CHECK( wallet.SelectCoinsMinConf( 50 * E8COIN, 1, 6, 0, vCoins, setCoinsRet2, nValueRet ) ) ;
            BOOST_CHECK( ! equal_sets( setCoinsRet, setCoinsRet2 ) ) ;

            int fails = 0;
            for (int j = 0; j < RANDOM_REPEATS; j++)
            {
                // selecting 1 from 100 identical coins depends on the shuffle; this test will fail 1% of the time
                // run the test RANDOM_REPEATS times and only complain if all of them fail
                BOOST_CHECK( wallet.SelectCoinsMinConf( E8COIN, 1, 6, 0, vCoins, setCoinsRet , nValueRet ) ) ;
                BOOST_CHECK( wallet.SelectCoinsMinConf( E8COIN, 1, 6, 0, vCoins, setCoinsRet2, nValueRet ) ) ;
                if (equal_sets(setCoinsRet, setCoinsRet2))
                    fails++;
            }
            BOOST_CHECK_NE(fails, RANDOM_REPEATS);

            // add 75 cents in small change.  not enough to make 90 cents,
            // then try making 90 cents.  there are multiple competing "smallest bigger" coins,
            // one of which should be picked at random
            add_coin( 5 * E8CENT ) ;
            add_coin( 10 * E8CENT ) ;
            add_coin( 15 * E8CENT ) ;
            add_coin( 20 * E8CENT ) ;
            add_coin( 25 * E8CENT ) ;

            fails = 0;
            for (int j = 0; j < RANDOM_REPEATS; j++)
            {
                // selecting 1 from 100 identical coins depends on the shuffle; this test will fail 1% of the time
                // run the test RANDOM_REPEATS times and only complain if all of them fail
                BOOST_CHECK( wallet.SelectCoinsMinConf( 90 * E8CENT, 1, 6, 0, vCoins, setCoinsRet , nValueRet ) ) ;
                BOOST_CHECK( wallet.SelectCoinsMinConf( 90 * E8CENT, 1, 6, 0, vCoins, setCoinsRet2, nValueRet ) ) ;
                if (equal_sets(setCoinsRet, setCoinsRet2))
                    fails++;
            }
            BOOST_CHECK_NE(fails, RANDOM_REPEATS);
        }
    }
    empty_wallet();
}

BOOST_AUTO_TEST_CASE(ApproximateBestSubset)
{
    CoinSet setCoinsRet;
    CAmount nValueRet;

    LOCK(wallet.cs_wallet);

    empty_wallet();

    // Test vValue sort order
    for ( int i = 0 ; i < 1000 ; i ++ )
        add_coin( 1000 * E8COIN ) ;
    add_coin( 3 * E8COIN ) ;

    BOOST_CHECK( wallet.SelectCoinsMinConf( 1003 * E8COIN, 1, 6, 0, vCoins, setCoinsRet, nValueRet ) ) ;
    BOOST_CHECK_EQUAL( nValueRet, 1003 * E8COIN ) ;
    BOOST_CHECK_EQUAL( setCoinsRet.size(), 2U ) ;

    empty_wallet();
}

BOOST_FIXTURE_TEST_CASE(rescan, TestChain240Setup)
{
    LOCK(cs_main);

    // Cap last block file size, and mine new block in a new block file
    CBlockIndex* oldTip = chainActive.Tip();
    GetBlockFileInfo(oldTip->GetBlockPos().nFile)->nSize = MAX_BLOCKFILE_SIZE;
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    CBlockIndex* newTip = chainActive.Tip();

    // Verify ScanForWalletTransactions picks up transactions in both the old
    // and new block files
    {
        CWallet wallet;
        LOCK(wallet.cs_wallet);
        wallet.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());
        BOOST_CHECK_EQUAL(oldTip, wallet.ScanForWalletTransactions(oldTip));
        BOOST_CHECK( wallet.GetImmatureBalance() < ( 24000 * E12COIN ) ) ;
    }

    // Prune the older block file
    PruneOneBlockFile(oldTip->GetBlockPos().nFile);
    UnlinkPrunedFiles({oldTip->GetBlockPos().nFile});

    // Verify ScanForWalletTransactions only picks transactions in the new block
    // file
    {
        CWallet wallet;
        LOCK(wallet.cs_wallet);
        wallet.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());
        BOOST_CHECK_EQUAL(newTip, wallet.ScanForWalletTransactions(oldTip));
        BOOST_CHECK( wallet.GetImmatureBalance() < ( 12000 * E12COIN ) ) ;
    }

    // Verify importmulti RPC returns failure for a key whose creation time is
    // before the missing block, and success for a key whose creation time is
    // after
    {
        CWallet wallet;
        CWallet *backup = ::pwalletMain;
        ::pwalletMain = &wallet;
        UniValue keys;
        keys.setArray();
        UniValue key;
        key.setObject();
        key.pushKV("scriptPubKey", HexStr(GetScriptForRawPubKey(coinbaseKey.GetPubKey())));
        key.pushKV("timestamp", 0);
        key.pushKV("internal", UniValue(true));
        keys.push_back(key);
        key.clear();
        key.setObject();
        CKey futureKey;
        futureKey.MakeNewKey(true);
        key.pushKV("scriptPubKey", HexStr(GetScriptForRawPubKey(futureKey.GetPubKey())));
        key.pushKV("timestamp", newTip->GetBlockTimeMax() + 7200);
        key.pushKV("internal", UniValue(true));
        keys.push_back(key);
        JSONRPCRequest request;
        request.params.setArray();
        request.params.push_back(keys);

        UniValue response = importmulti(request);
        BOOST_CHECK_EQUAL(response.write(), strprintf("[{\"success\":false,\"error\":{\"code\":-1,\"message\":\"Failed to rescan before time %d, transactions may be missing.\"}},{\"success\":true}]", newTip->GetBlockTimeMax()));
        ::pwalletMain = backup;
    }
}

// Verify importwallet RPC starts rescan at earliest block with timestamp
// greater or equal than key birthday. Previously there was a bug where
// importwallet RPC would start the scan at the latest block with timestamp less
// than or equal to key birthday
BOOST_FIXTURE_TEST_CASE(importwallet_rescan, TestChain240Setup)
{
    CWallet *pwalletMainBackup = ::pwalletMain;
    LOCK(cs_main);

    // Create two blocks with same timestamp to verify that importwallet rescan
    // will pick up both blocks, not just the first
    const int64_t BLOCK_TIME = chainActive.Tip()->GetBlockTimeMax() + 5;
    SetMockTime(BLOCK_TIME);
    coinbaseTxns.emplace_back(*CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);
    coinbaseTxns.emplace_back(*CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);

    // Set key birthday to block time increased by the timestamp window, so
    // rescan will start at the block time
    const int64_t KEY_TIME = BLOCK_TIME + 7200;
    SetMockTime(KEY_TIME);
    coinbaseTxns.emplace_back(*CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())).vtx[0]);

    // Import key into wallet and call dumpwallet to create backup file
    {
        CWallet wallet;
        LOCK(wallet.cs_wallet);
        wallet.mapKeyMetadata[coinbaseKey.GetPubKey().GetID()].nCreateTime = KEY_TIME;
        wallet.AddKeyPubKey(coinbaseKey, coinbaseKey.GetPubKey());

        JSONRPCRequest request;
        request.params.setArray();
        request.params.push_back("wallet.backup");
        ::pwalletMain = &wallet;
        ::dumpwallet(request);
    }

    // Call importwallet RPC and verify all blocks with timestamps >= BLOCK_TIME
    // were scanned, and no prior blocks were scanned
    {
        CWallet wallet;

        JSONRPCRequest request;
        request.params.setArray();
        request.params.push_back("wallet.backup");
        ::pwalletMain = &wallet;
        ::importwallet(request);

        BOOST_CHECK_EQUAL(wallet.mapWallet.size(), 3);
        BOOST_CHECK_EQUAL(coinbaseTxns.size(), 243);
        for ( size_t i = 0 ; i < coinbaseTxns.size() ; ++ i ) {
            bool found = wallet.GetWalletTx( coinbaseTxns[ i ].GetTxHash() ) ;
            bool expected = i >= 240;
            BOOST_CHECK_EQUAL(found, expected);
        }
    }

    SetMockTime(0);
    ::pwalletMain = pwalletMainBackup;
}

BOOST_AUTO_TEST_SUITE_END()
