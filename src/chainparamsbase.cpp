// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "chainparamsbase.h"

#include <memory>

#include <assert.h>

/**
 * Main network
 */
class CBaseMainParams : public CBaseChainParams
{
public:
    CBaseMainParams() : CBaseChainParams( "main", "main", 22556, 22555 ) {}
} ;
static std::unique_ptr< CBaseMainParams > mainBaseParams ( nullptr ) ;

/**
 * Inu chain
 */
class CBaseInuParams : public CBaseChainParams
{
public:
    CBaseInuParams() : CBaseChainParams( "inu", "inuchain", 55336, 55334 ) {}
} ;
static std::unique_ptr< CBaseInuParams > inuBaseParams ( nullptr ) ;

/**
 * Testnet (v3)
 */
class CBaseTestNetParams : public CBaseChainParams
{
public:
    CBaseTestNetParams() : CBaseChainParams( "test", "testnet3", 44556, 44555 ) {}
} ;
static std::unique_ptr< CBaseTestNetParams > testNetBaseParams ( nullptr ) ;

/*
 * Regression test
 */
class CBaseRegTestParams : public CBaseChainParams
{
public:
    CBaseRegTestParams() : CBaseChainParams( "regtest", "regtest", 18444, 18332 ) {}
} ;
static std::unique_ptr< CBaseRegTestParams > regTestBaseParams ( nullptr ) ;

static CBaseChainParams * pCurrentBaseParams = nullptr ;

const CBaseChainParams & BaseParams()
{
    assert( pCurrentBaseParams != nullptr ) ;
    return *pCurrentBaseParams ;
}

#include "tinyformat.h" // strprintf

CBaseChainParams & BaseParamsFor( const std::string & chain )
{
    if ( chain == "main" ) {
        if ( mainBaseParams == nullptr ) mainBaseParams.reset( new CBaseMainParams() ) ;
        return *mainBaseParams.get() ;
    } else if ( chain == "inu" ) {
        if ( inuBaseParams == nullptr ) inuBaseParams.reset( new CBaseInuParams() ) ;
        return *inuBaseParams.get() ;
    } else if ( chain == "test" ) {
        if ( testNetBaseParams == nullptr ) testNetBaseParams.reset( new CBaseTestNetParams() ) ;
        return *testNetBaseParams.get() ;
    } else if ( chain == "regtest" ) {
        if ( regTestBaseParams == nullptr ) regTestBaseParams.reset( new CBaseRegTestParams() ) ;
        return *regTestBaseParams.get() ;
    } else
        throw std::runtime_error( strprintf( "%s: unknown chain %s", __func__, chain ) ) ;
}

void SelectBaseParams( const std::string & chain )
{
    pCurrentBaseParams = &BaseParamsFor( chain ) ;
}

const std::string & NameOfChain()
{
    return BaseParams().NameOfNetwork() ;
}
