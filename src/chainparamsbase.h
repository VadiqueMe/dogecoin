// Copyright (c) 2014-2015 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_CHAINPARAMSBASE_H
#define DOGECOIN_CHAINPARAMSBASE_H

#include <string>

/**
 * Defines the base parameters of a given instance of the Dogecoin system
 */

class CBaseChainParams
{

public:
    /** the name of chain and network (main, inu, test, regtest) */
    const std::string & NameOfNetwork() const {  return networkName ;  }

    const std::string & DirForData() const {  return dataDir ;  }

    int GetDefaultPort() const {  return nDefaultPort ;  }

    int GetRPCPort() const {  return nRPCPort ;  }

protected:

    CBaseChainParams( const std::string & name, const std::string & dir, int port, int rpcPort )
        : networkName( name )
        , dataDir( dir )
        , nDefaultPort( port )
        , nRPCPort( rpcPort )
    {}

private:

    CBaseChainParams() {}

    std::string networkName ;
    std::string dataDir ;
    int nDefaultPort ;
    int nRPCPort ;

} ;

/** Return the name of the current chain (main, inu, test, regtest) */
const std::string & NameOfChain() ;

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests
 */
const CBaseChainParams & BaseParams() ;

CBaseChainParams & BaseParamsFor( const std::string & chain ) ;

/** Sets the params returned by BaseParams() to those for the given network */
void SelectBaseParams( const std::string & chain ) ;

#endif
