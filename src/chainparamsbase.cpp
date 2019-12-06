// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "chainparamsbase.h"

#include "tinyformat.h"
#include "util.h"

#include <assert.h>

void AppendParamsHelpMessages( std::string & strUsage, bool debugHelp )
{
    strUsage += HelpMessageGroup( _("Chain selection options:") ) ;
    strUsage += HelpMessageOpt( "-inu", "Use the inu chain" ) ;
    strUsage += HelpMessageOpt( "-testnet", "Use the test chain" ) ;
    if ( debugHelp )
        strUsage += HelpMessageOpt( "-regtest", "Enter regression testing, which uses a special chain in which blocks can be solved instantly. This is intended for testing tools and app development" ) ;
}

/**
 * Main network
 */
class CBaseMainParams : public CBaseChainParams
{
public:
    CBaseMainParams()
    {
        nRPCPort = 22555 ;
    }
} ;
static std::unique_ptr< CBaseMainParams > mainBaseParams ( nullptr ) ;

/**
 * Inu chain
 */
class CBaseInuParams : public CBaseChainParams
{
public:
    CBaseInuParams()
    {
        nRPCPort = 55334 ;
        dataDir = "inuchain" ;
    }
} ;
static std::unique_ptr< CBaseInuParams > inuBaseParams ( nullptr ) ;

/**
 * Testnet (v3)
 */
class CBaseTestNetParams : public CBaseChainParams
{
public:
    CBaseTestNetParams()
    {
        nRPCPort = 44555 ;
        dataDir = "testnet3" ;
    }
} ;
static std::unique_ptr< CBaseTestNetParams > testNetBaseParams ( nullptr ) ;

/*
 * Regression test
 */
class CBaseRegTestParams : public CBaseChainParams
{
public:
    CBaseRegTestParams()
    {
        nRPCPort = 18332 ;
        dataDir = "regtest" ;
    }
} ;
static std::unique_ptr< CBaseRegTestParams > regTestBaseParams ( nullptr ) ;

static CBaseChainParams * pCurrentBaseParams = nullptr ;

const CBaseChainParams & BaseParams()
{
    assert( pCurrentBaseParams != nullptr ) ;
    return *pCurrentBaseParams ;
}

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

std::string ChainNameFromArguments()
{
    bool inuChain = GetBoolArg( "-inu", false ) ;
    bool fTestNet = GetBoolArg( "-testnet", false ) ;
    bool fRegTest = GetBoolArg( "-regtest", false ) ;

    if ( fTestNet && fRegTest )
        throw std::runtime_error( "-regtest and -testnet together?" ) ;
    if ( inuChain && ( fTestNet || fRegTest ) )
        throw std::runtime_error( "-inu and -regtest/-testnet together?" ) ;

    std::string chain = "main" ;

    if ( fRegTest ) chain = "regtest" ;
    if ( fTestNet ) chain = "test" ;
    if ( inuChain ) chain = "inu" ;

    ///LogPrintf( "%s: \"%s\"\n", __func__, chain ) ;

    return chain ;
}
