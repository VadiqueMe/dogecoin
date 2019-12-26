// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "pow.h"

#include "auxpow.h"
#include "arith_uint256.h"
#include "chain.h"
#include "dogecoin.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

unsigned int GetNextWorkRequired( const CBlockIndex * pindexLast, const CBlockHeader * pblock, const Consensus::Params & params )
{
    unsigned int nProofOfWorkLimit = UintToArith256( params.powLimit ).GetCompact() ;

    // Genesis block
    if ( pindexLast == nullptr )
        return nProofOfWorkLimit ;

    // Dogecoin: Special rules for minimum difficulty blocks with Digishield
    if ( AcceptDigishieldMinDifficultyForBlock( pindexLast, pblock, params ) )
    {
        // If the new block's timestamp is more than 2 * nPowTargetSpacing minutes for testnet
        // or more than 20 * nPowTargetSpacing minutes for inu chain
        // then allow mining of a min-difficulty block
        return nProofOfWorkLimit ;
    }

    // Only change once per difficulty adjustment interval
    bool fNewDifficultyProtocol = ( pindexLast->nHeight >= 145000 ) || ( NameOfChain() == "inu" ) ;
    const int64_t difficultyAdjustmentInterval = fNewDifficultyProtocol
                                                 ? 1
                                                 : params.DifficultyAdjustmentInterval() ;
    if ( ( pindexLast->nHeight + 1 ) % difficultyAdjustmentInterval != 0 )
    {
        if ( params.fPowAllowMinDifficultyBlocks )
        {
            // Special difficulty rule with fPowAllowMinDifficultyBlocks:
            // If the new block's timestamp is more than 2 * nPowTargetSpacing minutes
            // then allow mining of a min-difficulty block
            if ( pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2 )
                return nProofOfWorkLimit ;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex * pindex = pindexLast ;
                while ( pindex->pprev && pindex->nHeight % difficultyAdjustmentInterval != 0 && pindex->nBits == nProofOfWorkLimit )
                    pindex = pindex->pprev ;
                return pindex->nBits ;
            }
        }
        return pindexLast->nBits ;
    }

    // Litecoin: This fixes an issue where a 51% attack can change difficulty at will.
    // Go back the full period unless it's the first retarget after genesis. Code courtesy of Art Forz
    int blockstogoback = difficultyAdjustmentInterval - 1 ;
    if ( ( pindexLast->nHeight + 1 ) != difficultyAdjustmentInterval )
        blockstogoback = difficultyAdjustmentInterval ;

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - blockstogoback;
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateDogecoinNextWorkRequired( pindexLast, pindexFirst->GetBlockTime(), params ) ;
}

/* unsigned int CalculateNextWorkRequired( const CBlockIndex * pindexLast, int64_t nFirstBlockTime, const Consensus::Params & params )
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
} */

bool CheckProofOfWork( const CBlockHeader & block, unsigned int nBits, const Consensus::Params & params )
{
    bool fNegative ;
    bool fOverflow ;
    arith_uint256 solutionHash ;

    solutionHash.SetCompact( nBits, &fNegative, &fOverflow ) ;

    // Check range
    if ( fNegative || solutionHash == 0 || fOverflow || solutionHash > UintToArith256( params.powLimit ) )
        return false ;

    // Proof that block's hash is not bigger than solution

    if ( NameOfChain() == "inu" ) {
        return ( UintToArith256( block.GetScryptHash() ) <= solutionHash )
                    && ( UintToArith256( block.GetLyra2Re2Hash() ) <= solutionHash )
                        && ( UintToArith256( block.GetSha256Hash() ) <= ( solutionHash << 1 ) ) ;
    }

    return UintToArith256( block.GetScryptHash() ) <= solutionHash ;
}

bool CheckAuxProofOfWork( const CAuxPow & auxpow, unsigned int nBits, const Consensus::Params & params )
{
    if ( NameOfChain() == "inu" ) return false ; // auxpow isn't proof for inu chain

    bool fNegative ;
    bool fOverflow ;
    arith_uint256 solutionHash ;

    solutionHash.SetCompact( nBits, &fNegative, &fOverflow ) ;

    // Check range
    if ( fNegative || solutionHash == 0 || fOverflow || solutionHash > UintToArith256( params.powLimit ) )
        return false ;

    // Proof that hash of a block from another chain is not bigger than solution
    return UintToArith256( auxpow.getParentBlockScryptHash() ) <= solutionHash ;
}
