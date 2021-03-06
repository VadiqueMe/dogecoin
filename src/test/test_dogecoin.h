// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef TEST_DOGECOIN_H
#define TEST_DOGECOIN_H

#include "chainparamsbase.h"
#include "key.h"
#include "pubkey.h"
#include "txdb.h"
#include "txmempool.h"

#include <vector>
#include <thread>

#include <boost/filesystem/path.hpp>

/** Basic testing setup. This just configures logging and chain parameters */
struct BasicTestingSetup {
    ECCVerifyHandle globalVerifyHandle;

    BasicTestingSetup( const std::string & chainName = "main" ) ;
    ~BasicTestingSetup() ;
} ;

/** Testing setup that configures a complete environment.
 *  Included are data directory, coins database, script check threads setup */
class CConnman ;
struct TestingSetup: public BasicTestingSetup {
    CCoinsViewDB * pcoinsdbview ;
    boost::filesystem::path pathTemp ;
    std::vector< std::thread > scriptcheckThreads ;
    CConnman * connman ;

    TestingSetup( const std::string & chainName = "main" ) ;
    ~TestingSetup() ;
} ;

class CBlock;
struct CMutableTransaction;
class CScript;

//
// Testing fixture that pre-creates a 100-block regtest chain
//
struct TestChain240Setup : public TestingSetup {
    TestChain240Setup();

    // Create a new block with just given transactions, coinbase paying to
    // scriptPubKey, and try to add it to the current chain
    CBlock CreateAndProcessBlock(const std::vector<CMutableTransaction>& txns,
                                 const CScript& scriptPubKey);

    ~TestChain240Setup();

    std::vector<CTransaction> coinbaseTxns; // For convenience, coinbase transactions
    CKey coinbaseKey; // private/public key needed to spend coinbase transactions
};

class CTxMemPoolEntry;
class CTxMemPool;

struct TestMemPoolEntryHelper
{
    // Default values
    CAmount nFee;
    int64_t nTime;
    double dPriority;
    unsigned int nHeight;
    bool spendsCoinbase;
    unsigned int sigOpCost;
    LockPoints lp;

    TestMemPoolEntryHelper() :
        nFee(0), nTime(0), dPriority(0.0), nHeight(1),
        spendsCoinbase(false), sigOpCost(4) { }

    CTxMemPoolEntry FromTx(const CMutableTransaction &tx, CTxMemPool *pool = NULL);
    CTxMemPoolEntry FromTx(const CTransaction &tx, CTxMemPool *pool = NULL);

    // Change the default value
    TestMemPoolEntryHelper &Fee(CAmount _fee) { nFee = _fee; return *this; }
    TestMemPoolEntryHelper &Time(int64_t _time) { nTime = _time; return *this; }
    TestMemPoolEntryHelper &Priority(double _priority) { dPriority = _priority; return *this; }
    TestMemPoolEntryHelper &Height(unsigned int _height) { nHeight = _height; return *this; }
    TestMemPoolEntryHelper &SpendsCoinbase(bool _flag) { spendsCoinbase = _flag; return *this; }
    TestMemPoolEntryHelper &SigOpsCost(unsigned int _sigopsCost) { sigOpCost = _sigopsCost; return *this; }
};
#endif
