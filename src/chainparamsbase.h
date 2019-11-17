// Copyright (c) 2014-2015 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_CHAINPARAMSBASE_H
#define DOGECOIN_CHAINPARAMSBASE_H

#include <string>
#include <vector>

/**
 * Defines the base parameters of a given instance of the Dogecoin system
 */

class CBaseChainParams
{

public:

    const std::string & DataDir() const {  return dataDir ;  }
    int RPCPort() const {  return nRPCPort ;  }

protected:

    CBaseChainParams() {}

    int nRPCPort ;
    std::string dataDir ;

} ;

/**
 * Append the help messages for the chainparams options to the parameter string
 */
void AppendParamsHelpMessages( std::string & strUsage, bool debugHelp = true ) ;

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests
 */
const CBaseChainParams & BaseParams() ;

CBaseChainParams & BaseParamsFor( const std::string & chain ) ;

/** Sets the params returned by Params() to those for the given network */
void SelectBaseParams( const std::string & chain ) ;

// returns "main" by default
std::string ChainNameFromArguments() ;

#endif
