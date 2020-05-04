// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_WALLET_TEST_FIXTURE_H
#define DOGECOIN_WALLET_TEST_FIXTURE_H

#include "test/test_dogecoin.h"

/** Testing setup and teardown for wallet
 */
class WalletTestingSetup : public TestingSetup
{
public:
    WalletTestingSetup( const std::string & chainName = "main" ) ;
    ~WalletTestingSetup() ;
} ;

#endif

