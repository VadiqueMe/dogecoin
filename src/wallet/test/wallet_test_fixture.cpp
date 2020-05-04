// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "wallet/test/wallet_test_fixture.h"

#include "rpc/server.h"
#include "wallet/db.h"
#include "wallet/wallet.h"

WalletTestingSetup::WalletTestingSetup( const std::string & chainName )
    : TestingSetup(chainName)
{
    walletdb.MakeMockDB() ;

    pwalletMain = new CWallet( "wallet_test.dat" ) ;
    bool fFirstRun ;
    pwalletMain->LoadWallet( fFirstRun ) ;
    RegisterValidationInterface( pwalletMain ) ;

    RegisterWalletRPCCommands( tableRPC ) ;
}

WalletTestingSetup::~WalletTestingSetup()
{
    UnregisterValidationInterface( pwalletMain ) ;
    delete pwalletMain ;
    pwalletMain = nullptr ;

    walletdb.Flush( true ) ;
    walletdb.Reset() ;
}
