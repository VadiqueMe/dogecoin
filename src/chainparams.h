// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_CHAINPARAMS_H
#define DOGECOIN_CHAINPARAMS_H

#include "chainparamsbase.h"
#include "consensus/params.h"
#include "primitives/block.h"
#include "protocol.h"

#include <vector>

struct CDNSSeedData {
    std::string name, host;
    bool supportsServiceBitsFiltering;
    CDNSSeedData(const std::string &strName, const std::string &strHost, bool supportsServiceBitsFilteringIn = false) : name(strName), host(strHost), supportsServiceBitsFiltering(supportsServiceBitsFilteringIn) {}
};

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

typedef std::map < int, uint256 > MapCheckpoints ;

struct CCheckpointData {
    MapCheckpoints mapCheckpoints ;
} ;

struct ChainTxData {
    int64_t nTime;
    int64_t nTxCount;
    double dTxRate;
};

/**
 * CChainParams defines various tweakable parameters of a given instance
 */
class CChainParams
{
public:
    enum Base58Type {
        PUBKEY_ADDRESS,
        SCRIPT_ADDRESS,
        SECRET_KEY,
        EXT_PUBLIC_KEY,
        EXT_SECRET_KEY,

        MAX_BASE58_TYPES
    };

    const Consensus::Params& GetConsensus(uint32_t nTargetHeight) const {
        return *(pConsensusRoot -> GetConsensus(nTargetHeight));
    }

    const CMessageHeader::MessageStartChars& MessageStart() const { return pchMessageStart; }
    const std::vector<unsigned char>& AlertKey() const { return vAlertPubKey; }

    const CBlock & GenesisBlock() const {  return genesis ;  }

    /** Make miner wait to have peers to avoid wasting work */
    bool MiningRequiresPeers() const {  return fMiningRequiresPeers ;  }

    /** Default value for -checkmempool and -checkblockindex argument */
    bool DefaultConsistencyChecks() const {  return fDefaultConsistencyChecks ;  }
    /** Filter transactions that do not match well-defined patterns */
    bool OnlyStandardTransactions() const {  return fRequireStandardTxs ;  }
    uint64_t PruneAfterHeight() const { return nPruneAfterHeight; }
    /** Make miner stop after a block is found. In RPC, don't return until nGenProcLimit blocks are generated */
    bool MineBlocksOnDemand() const { return fMineBlocksOnDemand; }
    const std::vector<CDNSSeedData>& DNSSeeds() const { return vSeeds; }
    const std::vector<unsigned char>& Base58Prefix(Base58Type type) const { return base58Prefixes[type]; }
    const std::vector<SeedSpec6>& FixedSeeds() const { return vFixedSeeds; }
    const CCheckpointData& getCheckpoints() const { return checkpointData ; }
    const ChainTxData& TxData() const { return chainTxData ; }

protected:
    CChainParams() {}

    Consensus::Params consensus ;
    Consensus::Params * pConsensusRoot ; // binary search tree root
    CMessageHeader::MessageStartChars pchMessageStart ;
    std::vector<unsigned char> vAlertPubKey ; // raw pub key bytes for the broadcast alert signing key
    uint64_t nPruneAfterHeight ;
    std::vector< CDNSSeedData > vSeeds ;
    std::vector< unsigned char > base58Prefixes[ MAX_BASE58_TYPES ] ;
    CBlock genesis ;
    std::vector< SeedSpec6 > vFixedSeeds ;
    bool fMiningRequiresPeers ;
    bool fDefaultConsistencyChecks ;
    bool fRequireStandardTxs ;
    bool fMineBlocksOnDemand ;
    CCheckpointData checkpointData ;
    ChainTxData chainTxData ;
} ;

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests
 */
const CChainParams & Params() ;

/** Return CChainParams for the given name of chain */
CChainParams & ParamsFor( const std::string & chain ) ;

/**
 * Sets the params returned by Params() to those for the given name of chain
 * @throws std::runtime_error when the chain is not known
 */
void SelectParams( const std::string & chain ) ;

/**
 * Modify BIP9 parameters for the regtest chain
 */
void UpdateRegtestBIP9Parameters( Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout ) ;

#endif
