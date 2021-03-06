// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "policy/policy.h"
#include "txmempool.h"
#include "util.h"

#include "test/test_dogecoin.h"

#include <boost/test/unit_test.hpp>
#include <list>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(mempool_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(MempoolRemoveTest)
{
    // Test CTxMemPool::remove functionality

    TestMemPoolEntryHelper entry;
    // Parent transaction with three children,
    // and three grand-children:
    CMutableTransaction txParent;
    txParent.vin.resize(1);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    txParent.vout.resize(3);
    for (int i = 0; i < 3; i++)
    {
        txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.vout[i].nValue = 33000LL;
    }
    CMutableTransaction txChild[3];
    for (int i = 0; i < 3; i++)
    {
        txChild[i].vin.resize(1);
        txChild[i].vin[0].scriptSig = CScript() << OP_11;
        txChild[ i ].vin[0].prevout.hash = txParent.GetTxHash() ;
        txChild[i].vin[0].prevout.n = i;
        txChild[i].vout.resize(1);
        txChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txChild[i].vout[0].nValue = 11000LL;
    }
    CMutableTransaction txGrandChild[3];
    for (int i = 0; i < 3; i++)
    {
        txGrandChild[i].vin.resize(1);
        txGrandChild[i].vin[0].scriptSig = CScript() << OP_11;
        txGrandChild[ i ].vin[0].prevout.hash = txChild[ i ].GetTxHash() ;
        txGrandChild[i].vin[0].prevout.n = 0;
        txGrandChild[i].vout.resize(1);
        txGrandChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txGrandChild[i].vout[0].nValue = 11000LL;
    }


    CTxMemPool testPool ;

    // Nothing in pool, remove should do nothing
    unsigned int poolSize = testPool.size();
    testPool.removeRecursive(txParent);
    BOOST_CHECK_EQUAL(testPool.size(), poolSize);

    // Just the parent
    testPool.addUnchecked( txParent.GetTxHash(), entry.FromTx( txParent ) ) ;
    poolSize = testPool.size();
    testPool.removeRecursive(txParent);
    BOOST_CHECK_EQUAL(testPool.size(), poolSize - 1);

    // Parent, children, grandchildren
    testPool.addUnchecked( txParent.GetTxHash(), entry.FromTx( txParent ) ) ;
    for ( int i = 0 ; i < 3 ; i ++ )
    {
        testPool.addUnchecked( txChild[ i ].GetTxHash(), entry.FromTx( txChild[ i ] )  ) ;
        testPool.addUnchecked( txGrandChild[ i ].GetTxHash(), entry.FromTx( txGrandChild[ i ] ) ) ;
    }
    // Remove Child[0], GrandChild[0] should be removed
    poolSize = testPool.size();
    testPool.removeRecursive(txChild[0]);
    BOOST_CHECK_EQUAL(testPool.size(), poolSize - 2);
    // ... make sure grandchild and child are gone
    poolSize = testPool.size();
    testPool.removeRecursive(txGrandChild[0]);
    BOOST_CHECK_EQUAL(testPool.size(), poolSize);
    poolSize = testPool.size();
    testPool.removeRecursive(txChild[0]);
    BOOST_CHECK_EQUAL(testPool.size(), poolSize);
    // Remove parent, all children/grandchildren should go
    poolSize = testPool.size();
    testPool.removeRecursive(txParent);
    BOOST_CHECK_EQUAL(testPool.size(), poolSize - 5);
    BOOST_CHECK_EQUAL(testPool.size(), 0);

    // Add children and grandchildren, but NOT the parent (simulate the parent being in a block)
    for ( int i = 0 ; i < 3 ; i ++ )
    {
        testPool.addUnchecked( txChild[ i ].GetTxHash(), entry.FromTx( txChild[ i ] ) ) ;
        testPool.addUnchecked( txGrandChild[ i ].GetTxHash(), entry.FromTx( txGrandChild[ i ] ) ) ;
    }
    // Now remove the parent, as might happen if a block-re-org occurs but the parent cannot be
    // put into the mempool (maybe because it is non-standard)
    poolSize = testPool.size();
    testPool.removeRecursive(txParent);
    BOOST_CHECK_EQUAL(testPool.size(), poolSize - 6);
    BOOST_CHECK_EQUAL(testPool.size(), 0);
}

template<typename name>
void CheckSort(CTxMemPool &pool, std::vector<std::string> &sortedOrder)
{
    BOOST_CHECK_EQUAL(pool.size(), sortedOrder.size());
    typename CTxMemPool::indexed_transaction_set::index<name>::type::iterator it = pool.mapTx.get<name>().begin();
    int count = 0 ;
    for ( ; it != pool.mapTx.get< name >().end() ; ++ it , ++ count ) {
        BOOST_CHECK_EQUAL( it->GetTx().GetTxHash().ToString(), sortedOrder[ count ] ) ;
    }
}

BOOST_AUTO_TEST_CASE(MempoolIndexingTest)
{
    CTxMemPool pool ;
    TestMemPoolEntryHelper entry;

    /* 3rd highest fee */
    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx1.vout[0].nValue = 10 * E8COIN ;
    pool.addUnchecked( tx1.GetTxHash(), entry.Fee( 10000LL ).Priority( 10.0 ).FromTx( tx1 ) ) ;

    /* highest fee */
    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx2.vout[0].nValue = 2 * E8COIN ;
    pool.addUnchecked( tx2.GetTxHash(), entry.Fee( 20000LL ).Priority( 9.0 ).FromTx( tx2 ) ) ;

    /* lowest fee */
    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx3.vout[0].nValue = 5 * E8COIN ;
    pool.addUnchecked( tx3.GetTxHash(), entry.Fee( 0LL ).Priority( 100.0 ).FromTx( tx3 ) ) ;

    /* 2nd highest fee */
    CMutableTransaction tx4 = CMutableTransaction();
    tx4.vout.resize(1);
    tx4.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx4.vout[0].nValue = 6 * E8COIN ;
    pool.addUnchecked( tx4.GetTxHash(), entry.Fee( 15000LL ).Priority( 1.0 ).FromTx( tx4 ) ) ;

    /* equal fee rate to tx1, but newer */
    CMutableTransaction tx5 = CMutableTransaction();
    tx5.vout.resize(1);
    tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx5.vout[0].nValue = 11 * E8COIN ;
    entry.nTime = 1;
    entry.dPriority = 10.0;
    pool.addUnchecked( tx5.GetTxHash(), entry.Fee( 10000LL ).FromTx( tx5 ) ) ;
    BOOST_CHECK_EQUAL(pool.size(), 5);

    std::vector<std::string> sortedOrder;
    sortedOrder.resize(5);
    sortedOrder[ 0 ] = tx3.GetTxHash().ToString() ; // 0
    sortedOrder[ 1 ] = tx5.GetTxHash().ToString() ; // 10000
    sortedOrder[ 2 ] = tx1.GetTxHash().ToString() ; // 10000
    sortedOrder[ 3 ] = tx4.GetTxHash().ToString() ; // 15000
    sortedOrder[ 4 ] = tx2.GetTxHash().ToString() ; // 20000
    CheckSort<descendant_score>(pool, sortedOrder);

    /* low fee but with high fee child */
    /* tx6 -> tx7 -> tx8, tx9 -> tx10 */
    CMutableTransaction tx6 = CMutableTransaction();
    tx6.vout.resize(1);
    tx6.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx6.vout[0].nValue = 20 * E8COIN ;
    pool.addUnchecked( tx6.GetTxHash(), entry.Fee( 0LL ).FromTx( tx6 ) ) ;
    BOOST_CHECK_EQUAL(pool.size(), 6);
    // Check that at this point, tx6 is sorted low
    sortedOrder.insert( sortedOrder.begin(), tx6.GetTxHash().ToString() ) ;
    CheckSort<descendant_score>(pool, sortedOrder);

    CTxMemPool::setEntries setAncestors;
    setAncestors.insert( pool.mapTx.find( tx6.GetTxHash() ) ) ;
    CMutableTransaction tx7 = CMutableTransaction();
    tx7.vin.resize(1);
    tx7.vin[0].prevout = COutPoint( tx6.GetTxHash(), 0 ) ;
    tx7.vin[0].scriptSig = CScript() << OP_11;
    tx7.vout.resize(2);
    tx7.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx7.vout[0].nValue = 10 * E8COIN ;
    tx7.vout[1].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx7.vout[1].nValue = 1 * E8COIN ;

    CTxMemPool::setEntries setAncestorsCalculated;
    std::string dummy;
    BOOST_CHECK_EQUAL(pool.CalculateMemPoolAncestors(entry.Fee(2000000LL).FromTx(tx7), setAncestorsCalculated, 100, 1000000, 1000, 1000000, dummy), true);
    BOOST_CHECK(setAncestorsCalculated == setAncestors);

    pool.addUnchecked( tx7.GetTxHash(), entry.FromTx( tx7 ), setAncestors ) ;
    BOOST_CHECK_EQUAL(pool.size(), 7);

    // Now tx6 should be sorted higher (high fee child): tx7, tx6, tx2, ...
    sortedOrder.erase(sortedOrder.begin());
    sortedOrder.push_back( tx6.GetTxHash().ToString() ) ;
    sortedOrder.push_back( tx7.GetTxHash().ToString() ) ;
    CheckSort<descendant_score>(pool, sortedOrder);

    /* low fee child of tx7 */
    CMutableTransaction tx8 = CMutableTransaction();
    tx8.vin.resize(1);
    tx8.vin[0].prevout = COutPoint( tx7.GetTxHash(), 0 ) ;
    tx8.vin[0].scriptSig = CScript() << OP_11;
    tx8.vout.resize(1);
    tx8.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx8.vout[0].nValue = 10 * E8COIN ;
    setAncestors.insert( pool.mapTx.find( tx7.GetTxHash() ) ) ;
    pool.addUnchecked( tx8.GetTxHash(), entry.Fee( 0LL ).Time( 2 ).FromTx( tx8 ), setAncestors ) ;

    // Now tx8 should be sorted low, but tx6/tx both high
    sortedOrder.insert( sortedOrder.begin(), tx8.GetTxHash().ToString() ) ;
    CheckSort<descendant_score>(pool, sortedOrder);

    /* low fee child of tx7 */
    CMutableTransaction tx9 = CMutableTransaction();
    tx9.vin.resize(1);
    tx9.vin[0].prevout = COutPoint( tx7.GetTxHash(), 1 ) ;
    tx9.vin[0].scriptSig = CScript() << OP_11;
    tx9.vout.resize(1);
    tx9.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx9.vout[0].nValue = 1 * E8COIN ;
    pool.addUnchecked( tx9.GetTxHash(), entry.Fee( 0LL ).Time( 3 ).FromTx( tx9 ), setAncestors ) ;

    // tx9 should be sorted low
    BOOST_CHECK_EQUAL(pool.size(), 9);
    sortedOrder.insert( sortedOrder.begin(), tx9.GetTxHash().ToString() ) ;
    CheckSort<descendant_score>(pool, sortedOrder);

    std::vector<std::string> snapshotOrder = sortedOrder;

    setAncestors.insert( pool.mapTx.find( tx8.GetTxHash() ) ) ;
    setAncestors.insert( pool.mapTx.find( tx9.GetTxHash() ) ) ;
    /* tx10 depends on tx8 and tx9 and has a high fee*/
    CMutableTransaction tx10 = CMutableTransaction();
    tx10.vin.resize(2);
    tx10.vin[0].prevout = COutPoint( tx8.GetTxHash(), 0 ) ;
    tx10.vin[0].scriptSig = CScript() << OP_11;
    tx10.vin[1].prevout = COutPoint( tx9.GetTxHash(), 0 ) ;
    tx10.vin[1].scriptSig = CScript() << OP_11;
    tx10.vout.resize(1);
    tx10.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx10.vout[0].nValue = 10 * E8COIN ;

    setAncestorsCalculated.clear();
    BOOST_CHECK_EQUAL(pool.CalculateMemPoolAncestors(entry.Fee(200000LL).Time(4).FromTx(tx10), setAncestorsCalculated, 100, 1000000, 1000, 1000000, dummy), true);
    BOOST_CHECK(setAncestorsCalculated == setAncestors);

    pool.addUnchecked( tx10.GetTxHash(), entry.FromTx( tx10 ), setAncestors ) ;

    /**
     *  tx8 and tx9 should both now be sorted higher
     *  Final order after tx10 is added:
     *
     *  tx3 = 0 (1)
     *  tx5 = 10000 (1)
     *  tx1 = 10000 (1)
     *  tx4 = 15000 (1)
     *  tx2 = 20000 (1)
     *  tx9 = 200k (2 txs)
     *  tx8 = 200k (2 txs)
     *  tx10 = 200k (1 tx)
     *  tx6 = 2.2M (5 txs)
     *  tx7 = 2.2M (4 txs)
     */
    sortedOrder.erase( sortedOrder.begin(), sortedOrder.begin() + 2 ) ; // take out tx9, tx8 from the beginning
    sortedOrder.insert( sortedOrder.begin() + 5, tx9.GetTxHash().ToString() ) ;
    sortedOrder.insert( sortedOrder.begin() + 6, tx8.GetTxHash().ToString() ) ;
    sortedOrder.insert( sortedOrder.begin() + 7, tx10.GetTxHash().ToString() ) ; // tx10 is just before tx6
    CheckSort<descendant_score>(pool, sortedOrder);

    // there should be 10 transactions in the mempool
    BOOST_CHECK_EQUAL(pool.size(), 10);

    // Now try removing tx10 and verify the sort order returns to normal
    pool.removeRecursive( pool.mapTx.find( tx10.GetTxHash() )->GetTx() ) ;
    CheckSort<descendant_score>(pool, snapshotOrder);

    pool.removeRecursive( pool.mapTx.find( tx9.GetTxHash())->GetTx() ) ;
    pool.removeRecursive( pool.mapTx.find( tx8.GetTxHash())->GetTx() ) ;
    /* Now check the sort on the mining score index.
     * Final order should be:
     *
     * tx7 (2M)
     * tx2 (20k)
     * tx4 (15000)
     * tx1/tx5 (10000)
     * tx3/6 (0)
     * (Ties resolved by hash)
     */
    sortedOrder.clear() ;
    sortedOrder.push_back( tx7.GetTxHash().ToString() ) ;
    sortedOrder.push_back( tx2.GetTxHash().ToString() ) ;
    sortedOrder.push_back( tx4.GetTxHash().ToString() ) ;
    if ( tx1.GetTxHash() < tx5.GetTxHash() ) {
        sortedOrder.push_back( tx5.GetTxHash().ToString() ) ;
        sortedOrder.push_back( tx1.GetTxHash().ToString() ) ;
    } else {
        sortedOrder.push_back( tx1.GetTxHash().ToString() ) ;
        sortedOrder.push_back( tx5.GetTxHash().ToString() ) ;
    }
    if ( tx3.GetTxHash() < tx6.GetTxHash() ) {
        sortedOrder.push_back( tx6.GetTxHash().ToString() ) ;
        sortedOrder.push_back( tx3.GetTxHash().ToString() ) ;
    } else {
        sortedOrder.push_back( tx3.GetTxHash().ToString() ) ;
        sortedOrder.push_back( tx6.GetTxHash().ToString() ) ;
    }
    CheckSort<mining_score>(pool, sortedOrder);
}

BOOST_AUTO_TEST_CASE(MempoolAncestorIndexingTest)
{
    CTxMemPool pool ;
    TestMemPoolEntryHelper entry;

    /* 3rd highest fee */
    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx1.vout[0].nValue = 10 * E8COIN ;
    pool.addUnchecked( tx1.GetTxHash(), entry.Fee( 10000LL ).Priority( 10.0 ).FromTx( tx1 ) ) ;

    /* highest fee */
    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx2.vout[0].nValue = 2 * E8COIN ;
    pool.addUnchecked( tx2.GetTxHash(), entry.Fee( 20000LL ).Priority( 9.0 ).FromTx( tx2 ) ) ;
    uint64_t tx2Size = GetVirtualTransactionSize(tx2);

    /* lowest fee */
    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx3.vout[0].nValue = 5 * E8COIN ;
    pool.addUnchecked( tx3.GetTxHash(), entry.Fee( 0LL ).Priority( 100.0 ).FromTx( tx3 ) ) ;

    /* 2nd highest fee */
    CMutableTransaction tx4 = CMutableTransaction();
    tx4.vout.resize(1);
    tx4.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx4.vout[0].nValue = 6 * E8COIN ;
    pool.addUnchecked( tx4.GetTxHash(), entry.Fee( 15000LL ).Priority( 1.0 ).FromTx( tx4 ) ) ;

    /* equal fee rate to tx1, but newer */
    CMutableTransaction tx5 = CMutableTransaction();
    tx5.vout.resize(1);
    tx5.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx5.vout[0].nValue = 11 * E8COIN ;
    pool.addUnchecked( tx5.GetTxHash(), entry.Fee( 10000LL ).FromTx( tx5 ) ) ;
    BOOST_CHECK_EQUAL(pool.size(), 5);

    std::vector<std::string> sortedOrder;
    sortedOrder.resize(5);
    sortedOrder[0] = tx2.GetTxHash().ToString() ; // 20000
    sortedOrder[1] = tx4.GetTxHash().ToString() ; // 15000
    // tx1 and tx5 are both 10000
    // Ties are broken by hash, not timestamp, so determine which hash comes first
    if ( tx1.GetTxHash() < tx5.GetTxHash() ) {
        sortedOrder[2] = tx1.GetTxHash().ToString() ;
        sortedOrder[3] = tx5.GetTxHash().ToString() ;
    } else {
        sortedOrder[2] = tx5.GetTxHash().ToString() ;
        sortedOrder[3] = tx1.GetTxHash().ToString() ;
    }
    sortedOrder[4] = tx3.GetTxHash().ToString() ; // 0

    CheckSort<ancestor_score>(pool, sortedOrder);

    /* low fee parent with high fee child */
    /* tx6 (0) -> tx7 (high) */
    CMutableTransaction tx6 = CMutableTransaction();
    tx6.vout.resize(1);
    tx6.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx6.vout[0].nValue = 20 * E8COIN ;
    uint64_t tx6Size = GetVirtualTransactionSize(tx6);

    pool.addUnchecked( tx6.GetTxHash(), entry.Fee( 0LL ).FromTx( tx6 ) ) ;
    BOOST_CHECK_EQUAL(pool.size(), 6);
    // Ties are broken by hash
    if ( tx3.GetTxHash() < tx6.GetTxHash() )
        sortedOrder.push_back( tx6.GetTxHash().ToString() ) ;
    else
        sortedOrder.insert( sortedOrder.end() - 1, tx6.GetTxHash().ToString() ) ;

    CheckSort<ancestor_score>(pool, sortedOrder);

    CMutableTransaction tx7 = CMutableTransaction();
    tx7.vin.resize(1);
    tx7.vin[0].prevout = COutPoint( tx6.GetTxHash(), 0 ) ;
    tx7.vin[0].scriptSig = CScript() << OP_11;
    tx7.vout.resize(1);
    tx7.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
    tx7.vout[0].nValue = 10 * E8COIN ;
    uint64_t tx7Size = GetVirtualTransactionSize(tx7);

    /* set the fee to just below tx2's feerate when including ancestor */
    CAmount fee = (20000/tx2Size)*(tx7Size + tx6Size) - 1;

    //CTxMemPoolEntry entry7(tx7, fee, 2, 10.0, 1, true);
    pool.addUnchecked( tx7.GetTxHash(), entry.Fee( fee ).FromTx( tx7 ) ) ;
    BOOST_CHECK_EQUAL(pool.size(), 7);
    sortedOrder.insert( sortedOrder.begin() + 1, tx7.GetTxHash().ToString() ) ;
    CheckSort<ancestor_score>(pool, sortedOrder);

    /* after tx6 is mined, tx7 should move up in the sort */
    std::vector<CTransactionRef> vtx;
    vtx.push_back(MakeTransactionRef(tx6));
    pool.removeForBlock(vtx, 1);

    sortedOrder.erase( sortedOrder.begin() + 1 ) ;
    // Ties are broken by hash
    if ( tx3.GetTxHash() < tx6.GetTxHash() )
        sortedOrder.pop_back() ;
    else
        sortedOrder.erase( sortedOrder.end() - 2 ) ;
    sortedOrder.insert( sortedOrder.begin(), tx7.GetTxHash().ToString() ) ;
    CheckSort<ancestor_score>(pool, sortedOrder);
}


BOOST_AUTO_TEST_CASE(MempoolSizeLimitTest)
{
    CTxMemPool pool ;
    TestMemPoolEntryHelper entry;
    entry.dPriority = 10.0;

    CMutableTransaction tx1 = CMutableTransaction();
    tx1.vin.resize(1);
    tx1.vin[0].scriptSig = CScript() << OP_1;
    tx1.vout.resize(1);
    tx1.vout[0].scriptPubKey = CScript() << OP_1 << OP_EQUAL;
    tx1.vout[0].nValue = 10 * E8COIN ;
    pool.addUnchecked( tx1.GetTxHash(), entry.Fee( 10000LL ).FromTx( tx1, &pool ) ) ;

    CMutableTransaction tx2 = CMutableTransaction();
    tx2.vin.resize(1);
    tx2.vin[0].scriptSig = CScript() << OP_2;
    tx2.vout.resize(1);
    tx2.vout[0].scriptPubKey = CScript() << OP_2 << OP_EQUAL;
    tx2.vout[0].nValue = 10 * E8COIN ;
    pool.addUnchecked( tx2.GetTxHash(), entry.Fee( 5000LL ).FromTx( tx2, &pool ) ) ;

    pool.TrimToSize(pool.DynamicMemoryUsage()); // should do nothing
    BOOST_CHECK( pool.exists( tx1.GetTxHash() ) ) ;
    BOOST_CHECK( pool.exists( tx2.GetTxHash() ) ) ;

    pool.TrimToSize(pool.DynamicMemoryUsage() * 3 / 4); // should remove the lower-feerate transaction
    BOOST_CHECK( pool.exists( tx1.GetTxHash() ) ) ;
    BOOST_CHECK( ! pool.exists( tx2.GetTxHash() ) ) ;

    pool.addUnchecked( tx2.GetTxHash(), entry.FromTx( tx2, &pool ) ) ;
    CMutableTransaction tx3 = CMutableTransaction();
    tx3.vin.resize(1);
    tx3.vin[0].prevout = COutPoint( tx2.GetTxHash(), 0 ) ;
    tx3.vin[0].scriptSig = CScript() << OP_2;
    tx3.vout.resize(1);
    tx3.vout[0].scriptPubKey = CScript() << OP_3 << OP_EQUAL;
    tx3.vout[0].nValue = 10 * E8COIN ;
    pool.addUnchecked( tx3.GetTxHash(), entry.Fee( 20000LL ).FromTx( tx3, &pool ) ) ;

    pool.TrimToSize(pool.DynamicMemoryUsage() * 3 / 4); // tx3 should pay for tx2 (CPFP)
    BOOST_CHECK( ! pool.exists( tx1.GetTxHash() ) ) ;
    BOOST_CHECK( pool.exists( tx2.GetTxHash() ) ) ;
    BOOST_CHECK( pool.exists( tx3.GetTxHash() ) ) ;

    pool.TrimToSize(GetVirtualTransactionSize(tx1)); // mempool is limited to tx1's size in memory usage, so nothing fits
    BOOST_CHECK( ! pool.exists( tx1.GetTxHash() ) ) ;
    BOOST_CHECK( ! pool.exists( tx2.GetTxHash() ) ) ;
    BOOST_CHECK( ! pool.exists( tx3.GetTxHash() ) ) ;

    CMutableTransaction tx4 = CMutableTransaction();
    tx4.vin.resize(2);
    tx4.vin[0].prevout.SetNull();
    tx4.vin[0].scriptSig = CScript() << OP_4;
    tx4.vin[1].prevout.SetNull();
    tx4.vin[1].scriptSig = CScript() << OP_4;
    tx4.vout.resize(2);
    tx4.vout[0].scriptPubKey = CScript() << OP_4 << OP_EQUAL;
    tx4.vout[0].nValue = 10 * E8COIN ;
    tx4.vout[1].scriptPubKey = CScript() << OP_4 << OP_EQUAL;
    tx4.vout[1].nValue = 10 * E8COIN ;

    CMutableTransaction tx5 = CMutableTransaction();
    tx5.vin.resize(2);
    tx5.vin[0].prevout = COutPoint( tx4.GetTxHash(), 0 ) ;
    tx5.vin[0].scriptSig = CScript() << OP_4;
    tx5.vin[1].prevout.SetNull();
    tx5.vin[1].scriptSig = CScript() << OP_5;
    tx5.vout.resize(2);
    tx5.vout[0].scriptPubKey = CScript() << OP_5 << OP_EQUAL;
    tx5.vout[0].nValue = 10 * E8COIN ;
    tx5.vout[1].scriptPubKey = CScript() << OP_5 << OP_EQUAL;
    tx5.vout[1].nValue = 10 * E8COIN ;

    CMutableTransaction tx6 = CMutableTransaction();
    tx6.vin.resize(2);
    tx6.vin[0].prevout = COutPoint( tx4.GetTxHash(), 1 ) ;
    tx6.vin[0].scriptSig = CScript() << OP_4;
    tx6.vin[1].prevout.SetNull();
    tx6.vin[1].scriptSig = CScript() << OP_6;
    tx6.vout.resize(2);
    tx6.vout[0].scriptPubKey = CScript() << OP_6 << OP_EQUAL;
    tx6.vout[0].nValue = 10 * E8COIN ;
    tx6.vout[1].scriptPubKey = CScript() << OP_6 << OP_EQUAL;
    tx6.vout[1].nValue = 10 * E8COIN ;

    CMutableTransaction tx7 = CMutableTransaction();
    tx7.vin.resize(2);
    tx7.vin[0].prevout = COutPoint( tx5.GetTxHash(), 0 ) ;
    tx7.vin[0].scriptSig = CScript() << OP_5;
    tx7.vin[1].prevout = COutPoint( tx6.GetTxHash(), 0 ) ;
    tx7.vin[1].scriptSig = CScript() << OP_6;
    tx7.vout.resize(2);
    tx7.vout[0].scriptPubKey = CScript() << OP_7 << OP_EQUAL;
    tx7.vout[0].nValue = 10 * E8COIN ;
    tx7.vout[1].scriptPubKey = CScript() << OP_7 << OP_EQUAL;
    tx7.vout[1].nValue = 10 * E8COIN ;

    pool.addUnchecked( tx4.GetTxHash(), entry.Fee( 7000LL ).FromTx( tx4, &pool ) ) ;
    pool.addUnchecked( tx5.GetTxHash(), entry.Fee( 1000LL ).FromTx( tx5, &pool ) ) ;
    pool.addUnchecked( tx6.GetTxHash(), entry.Fee( 1100LL ).FromTx( tx6, &pool ) ) ;
    pool.addUnchecked( tx7.GetTxHash(), entry.Fee( 9000LL ).FromTx( tx7, &pool ) ) ;

    // we only require this remove, at max, 2 txn, because its not clear what we're really optimizing for aside from that
    pool.TrimToSize(pool.DynamicMemoryUsage() - 1);
    BOOST_CHECK( pool.exists( tx4.GetTxHash() ) ) ;
    BOOST_CHECK( pool.exists( tx6.GetTxHash() ) ) ;
    BOOST_CHECK( ! pool.exists( tx7.GetTxHash() ) ) ;

    if ( ! pool.exists( tx5.GetTxHash() ) )
        pool.addUnchecked( tx5.GetTxHash(), entry.Fee( 1000LL ).FromTx( tx5, &pool ) ) ;
    pool.addUnchecked( tx7.GetTxHash(), entry.Fee( 9000LL ).FromTx( tx7, &pool ) ) ;

    pool.TrimToSize( pool.DynamicMemoryUsage() / 2 ) ; // should maximize mempool size by only removing 5/7
    BOOST_CHECK( pool.exists( tx4.GetTxHash() ) ) ;
    BOOST_CHECK( ! pool.exists( tx5.GetTxHash() ) ) ;
    BOOST_CHECK( pool.exists( tx6.GetTxHash() ) ) ;
    BOOST_CHECK( ! pool.exists( tx7.GetTxHash() ) ) ;

    SetMockTime(0);
}

BOOST_AUTO_TEST_SUITE_END()
