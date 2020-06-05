// Copyright (c) 2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "chainparamsutil.h"

#include "util.h"
#include "utilstr.h"
#include "utilhelp.h"

void AppendChainParamsHelp( std::string & strUsage, bool debugHelp )
{
    strUsage += HelpMessageGroup( _("Chain selection options:") ) ;
    strUsage += HelpMessageOpt( "-main", "Use the main chain" ) ;
    strUsage += HelpMessageOpt( "-inu", "Use the inu chain" ) ;
    strUsage += HelpMessageOpt( "-testnet", "Use the test chain" ) ;
    if ( debugHelp )
        strUsage += HelpMessageOpt( "-regtest", "Enter regression testing, which uses a special chain in which blocks can be solved instantly. This is intended for testing tools and app development" ) ;
}

std::string ChainNameFromArguments()
{
    bool mainChain = GetBoolArg( "-main", false ) ;
    bool inuChain = GetBoolArg( "-inu", false ) ;
    bool fTestNet = GetBoolArg( "-testnet", false ) ;
    bool fRegTest = GetBoolArg( "-regtest", false ) ;

    if ( fTestNet && fRegTest )
        throw std::runtime_error( "-regtest and -testnet together?" ) ;
    if ( mainChain && inuChain )
        throw std::runtime_error( "-inu and -main together?" ) ;
    if ( mainChain && ( fTestNet || fRegTest ) )
        throw std::runtime_error( "-main and -regtest/-testnet together?" ) ;
    if ( inuChain && ( fTestNet || fRegTest ) )
        throw std::runtime_error( "-inu and -regtest/-testnet together?" ) ;

    std::string chain = "main" ;

    if ( mainChain ) chain = "main" ;
    if ( fRegTest ) chain = "regtest" ;
    if ( fTestNet ) chain = "test" ;
    if ( inuChain ) chain = "inu" ;

    return chain ;
}
