// Copyright (c) 2015 The Dogecoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include <boost/random/uniform_int.hpp>
#include <boost/random/mersenne_twister.hpp>

#include "policy/policy.h"
#include "arith_uint256.h"
#include "dogecoin.h"
#include "txmempool.h"
#include "util.h"
#include "validation.h"

int static generateMTRandom(unsigned int s, int range)
{
    boost::mt19937 gen(s);
    boost::uniform_int<> dist(1, range);
    return dist(gen);
}

// Dogecoin: Normally minimum difficulty blocks can only occur in between
// retarget blocks. However, once we introduce Digishield every block is
// a retarget, so we need to handle minimum difficulty on all blocks.
bool AllowDigishieldMinDifficultyForBlock(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    // check if the chain allows minimum difficulty blocks
    if (!params.fPowAllowMinDifficultyBlocks)
        return false;

    // check if the chain allows minimum difficulty blocks on recalc blocks
    if (pindexLast->nHeight < 157500)
    // if (!params.fPowAllowDigishieldMinDifficultyBlocks)
        return false;

    // Allow for a minimum block time if the elapsed time > 2*nTargetSpacing
    return (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2);
}

unsigned int CalculateDogecoinNextWorkRequired( const CBlockIndex * pindexLast, int64_t nFirstBlockTime, const Consensus::Params & params )
{
    int nHeight = pindexLast->nHeight + 1;
    const int64_t retargetTimespan = params.nPowTargetTimespan;
    const int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    int64_t nModulatedTimespan = nActualTimespan;
    int64_t nMaxTimespan;
    int64_t nMinTimespan;

    if (params.fDigishieldDifficultyCalculation) //DigiShield implementation - thanks to RealSolid & WDC for this code
    {
        // amplitude filter - thanks to daft27 for this code
        nModulatedTimespan = retargetTimespan + (nModulatedTimespan - retargetTimespan) / 8;

        nMinTimespan = retargetTimespan - (retargetTimespan / 4);
        nMaxTimespan = retargetTimespan + (retargetTimespan / 2);
    } else if (nHeight > 10000) {
        nMinTimespan = retargetTimespan / 4;
        nMaxTimespan = retargetTimespan * 4;
    } else if (nHeight > 5000) {
        nMinTimespan = retargetTimespan / 8;
        nMaxTimespan = retargetTimespan * 4;
    } else {
        nMinTimespan = retargetTimespan / 16;
        nMaxTimespan = retargetTimespan * 4;
    }

    // Limit adjustment step
    if (nModulatedTimespan < nMinTimespan)
        nModulatedTimespan = nMinTimespan;
    else if (nModulatedTimespan > nMaxTimespan)
        nModulatedTimespan = nMaxTimespan;

    // Retarget
    arith_uint256 bnOld ;
    bnOld.SetCompact( pindexLast->nBits ) ;
    arith_uint256 bnNew = bnOld ;
    bnNew *= nModulatedTimespan ;
    bnNew /= retargetTimespan ;

    const arith_uint256 lowerPowLimit = UintToArith256( params.powLimit ) ;
    if ( bnNew > lowerPowLimit ) bnNew = lowerPowLimit ;

    return bnNew.GetCompact() ;
}

bool CheckDogecoinProofOfWork( const CBlockHeader & block, const Consensus::Params & params )
{
    /* Except for legacy blocks with full version 1, ensure that
       the chain ID is correct. Legacy blocks are not accepted since
       the merge-mining start, which is checked in AcceptBlockHeader
       where the height is known */
    if ( ! block.IsLegacy() && params.fStrictChainId && block.GetChainId() != params.nAuxpowChainId )
        return error( "%s : block does not have our chain ID"
                      " (got %d, expected %d, full nVersion 0x%x)",
                      __func__,
                      block.GetChainId(), params.nAuxpowChainId,
                      block.nVersion ) ;

    /* If there is no auxpow, just check the block hash */
    if ( ! block.auxpow ) {
        if ( block.IsAuxpow() )
            return error( "%s : no auxpow on a block with auxpow in version", __func__ );

        if ( ! CheckProofOfWork( block, block.nBits, params ) )
            return error( "%s : non-aux proof of work failed with bits=%s and hashes scrypt=%s, sha256=%s", __func__,
                            arith_uint256().SetCompact( block.nBits ).GetHex(),
                            block.GetScryptHash().GetHex(), block.GetSha256Hash().GetHex() ) ;

        return true ;
    }

    // Block has auxpow, check it

    if ( ! block.IsAuxpow() )
        return error( "%s : auxpow on a block with non-auxpow version", __func__ ) ;

    if ( ! block.auxpow->check( block.GetSha256Hash(), block.GetChainId(), params ) )
        return error( "%s : auxpow is not valid", __func__ ) ;

    if ( ! CheckAuxProofOfWork( *block.auxpow.get(), block.nBits, params ) )
        return error( "%s : aux proof of work failed", __func__ ) ;

    return true ;
}

CAmount GetDogecoinBlockSubsidy( int nHeight, const Consensus::Params & consensusParams, uint256 prevHash )
{
    if ( NameOfChain() == "inu" )
    {   /*
           inu chain gives for each new block a random subsidy from 1 COIN to 10000
           the first 1234 blocks have a random subsidy from 1 COIN to 100
        */

        static const bool randomSubsidy = true ;
        if ( randomSubsidy )
        {
            int random = 0 ;
            {
                const std::string strseed = prevHash.ToString().substr( 16, 10 ) ;
                long seed = strtol( strseed.c_str(), nullptr, /* hex */ 16 ) ;
                const CAmount supremum = ( nHeight > 1234 ) ? 10000 : 100 ;
                random = generateMTRandom( seed, supremum - 1 ) ;
            }

            int randomtoshi = COIN - 1 ;
            if ( nHeight > 1234 ) {
                const std::string strseedoshi = prevHash.ToString().substr( 12, 12 ) ;
                long seed = strtol( strseedoshi.c_str(), nullptr, /* hex */ 16 ) ;
                randomtoshi = generateMTRandom( seed, COIN - 1 ) ;
            }

            return randomtoshi + ( random * COIN ) + 1 ;
        }
        else return 87654321 ;
    }

    if ( ! consensusParams.fSimplifiedRewards )
    {
        // Original rewards derived from the sha256 hash of previous block
        const std::string strseed = prevHash.ToString().substr( 7, 7 ) ;
        long seed = strtol( strseed.c_str(), nullptr, /* hex */ 16 ) ;

        int halvings = nHeight / consensusParams.nSubsidyHalvingInterval ;
        const CAmount maxReward = ( 1000000 >> halvings ) - 1 ;
        int rand = generateMTRandom( seed, maxReward ) ;

        CAmount reward = ( 1 + rand ) * COIN ;
        /* LogPrintf( "%s: at height %i with sha256 of previous block %s strseed=\"%s\" seed=%ld reward=%ld\n", __func__,
                   nHeight, prevHash.GetHex(), strseed, seed, reward ) ; */

        return reward ;
    }
    else if ( nHeight < ( 6 * consensusParams.nSubsidyHalvingInterval ) ) {
        // Mid-style constant rewards for each halving interval
        // six are for 50 0000, 25 0000, 12 5000, 6 2500, 3 1250, 1 5625
        int halvings = nHeight / consensusParams.nSubsidyHalvingInterval ;
        return ( 500000 * COIN ) >> halvings ;
    } else {
        // Constant inflation 1 0000 per every new block
        return 10000 * COIN ;
    }
}

CAmount GetDogecoinMinRelayFee( const CTransaction & tx, unsigned int nBytes )
{
    ( void ) tx ;
    ( void ) nBytes ;

    return 0 ;
}

