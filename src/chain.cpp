// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "chain.h"
#include "validation.h"

/* Moved here from the header due to the need of auxpow and more involved logic */
CBlockHeader CBlockIndex::GetBlockHeader( const Consensus::Params & consensusParams ) const
{
    CBlockHeader block ;

    block.nVersion       = nVersion ;

    /* The CBlockIndex object's block header doesn't include the auxpow.
       So if this is an auxpow block, read it from disk instead. Only
       read the actual *header*, not the full block */
    if ( block.IsAuxpowInVersion() )
    {
        ReadBlockHeaderFromDisk( block, this, consensusParams ) ;
        return block ;
    }

    if ( pprev )
        block.hashPrevBlock = pprev->GetBlockSha256Hash() ;

    block.hashMerkleRoot = hashMerkleRoot ;
    block.nTime          = nTime ;
    block.nBits          = nBits ;
    block.nNonce         = nNonce ;

    return block ;
}

/**
 * CChain implementation
 */
void CChain::SetTip( CBlockIndex * pindex ) {
    if ( pindex == nullptr ) {
        vChain.clear() ;
        return ;
    }
    vChain.resize( pindex->nHeight + 1 ) ;
    while ( pindex && vChain[ pindex->nHeight ] != pindex ) {
        vChain[ pindex->nHeight ] = pindex ;
        pindex = pindex->pprev ;
    }
}

CBlockLocator CChain::GetLocator(const CBlockIndex *pindex) const {
    int nStep = 1;
    std::vector<uint256> vHave;
    vHave.reserve(32);

    if (!pindex)
        pindex = Tip();
    while (pindex) {
        vHave.push_back( pindex->GetBlockSha256Hash() ) ;
        // Stop when we have added the genesis block
        if (pindex->nHeight == 0)
            break;
        // Exponentially larger steps back, plus the genesis block
        int nHeight = std::max(pindex->nHeight - nStep, 0);
        if (Contains(pindex)) {
            // Use O(1) CChain index if possible
            pindex = (*this)[nHeight];
        } else {
            // Otherwise, use O(log n) skiplist
            pindex = pindex->GetAncestor(nHeight);
        }
        if (vHave.size() > 10)
            nStep *= 2;
    }

    return CBlockLocator(vHave);
}

const CBlockIndex *CChain::FindFork(const CBlockIndex *pindex) const {
    if (pindex == NULL) {
        return NULL;
    }
    if (pindex->nHeight > Height())
        pindex = pindex->GetAncestor(Height());
    while (pindex && !Contains(pindex))
        pindex = pindex->pprev;
    return pindex;
}

CBlockIndex* CChain::FindEarliestAtLeast(int64_t nTime) const
{
    std::vector<CBlockIndex*>::const_iterator lower = std::lower_bound(vChain.begin(), vChain.end(), nTime,
        [](CBlockIndex* pBlock, const int64_t& time) -> bool { return pBlock->GetBlockTimeMax() < time; });
    return (lower == vChain.end() ? NULL : *lower);
}

/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height) {
    if (height < 2)
        return 0;

    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    if (height > nHeight || height < 0)
        return NULL;

    CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while (heightWalk > height) {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip != NULL &&
            (heightSkip == height ||
             (heightSkip > height && !(heightSkipPrev < heightSkip - 2 &&
                                       heightSkipPrev >= height)))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            assert(pindexWalk->pprev);
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    return const_cast<CBlockIndex*>(this)->GetAncestor(height);
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

arith_uint256 EstimateBlockProofMaxHashes( const CBlockIndex & block )
{
    arith_uint256 bitsExpanded ;
    bool fNegative ;
    bool fOverflow ;
    bitsExpanded.SetCompact( block.nBits, &fNegative, &fOverflow ) ;
    if ( fNegative || fOverflow || bitsExpanded == 0 )
        return 0 ;

    // We need to compute 2**256 / ( bitsExpanded + 1 ), but arith_uint256 can't represent 2**256
    // as it's too large. However, as 2**256 is at least as large as bitsExpanded + 1,
    // it is equal to ( ( 2**256 - bitsExpanded - 1 ) / ( bitsExpanded + 1 ) ) + 1,
    // or ~bitsExpanded / ( bitsExpanded + 1 ) plus 1
    return ( ~bitsExpanded / ( bitsExpanded + 1 ) ) + 1 ;
}

/* int64_t EstimateBlockProofRedoTimeInSeconds( const CBlockIndex & to, const CBlockIndex & from, const CBlockIndex & tip, const Consensus::Params & params )
{
    arith_uint256 r ;
    int sign = 1 ;
    if ( to.nChainWorkHashes > from.nChainWorkHashes ) {
        r = to.nChainWorkHashes - from.nChainWorkHashes ;
    } else {
        r = from.nChainWorkHashes - to.nChainWorkHashes ;
        sign = -1 ;
    }
    r = arith_uint256( params.nPowTargetSpacing ) * r / EstimateBlockProofMaxHashes( tip ) ;
    if ( r.bits() > 63 ) {
        return sign * std::numeric_limits< int64_t >::max() ;
    }
    return sign * r.GetLow64() ;
} */
