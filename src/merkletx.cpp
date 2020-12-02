// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "merkletx.h"

#include "primitives/block.h"
#include "chain.h"
#include "hash.h"
#include "consensus/merkle.h"
#include "utilstrencodings.h"

std::string CMerkleTx::ToString() const
{
    std::stringstream s ;
    s << "CMerkleTx(" ;
      s << "tx=" << tx->ToString( false ) << ", " ;
      s << "hashBlock=" << hashBlock.ToString() << ", " ;
      s << "vMerkleBranch[" << vMerkleBranch.size() << "]={" ;
        bool first = true ;
        for ( const uint256 & entry : vMerkleBranch ) {
            if ( first ) first = false ;
            else s << "," ;
            s << entry.ToString() ;
        }
        s << "}, " ;
      s << "nIndex=" << nIndex ;
    s << ")" ;

    return s.str() ;
}

void CMerkleTx::SetMerkleBranch( const CBlockIndex * pindex, int posInBlock )
{
    // update this tx's hashBlock
    hashBlock = pindex->GetBlockSha256Hash() ;

    // set the position of the transaction in the block
    nIndex = posInBlock ;
}

void CMerkleTx::InitMerkleBranch( const CBlock & block, int posInBlock )
{
    hashBlock = block.GetSha256Hash() ;
    nIndex = posInBlock ;
    vMerkleBranch = BlockMerkleBranch( block, nIndex ) ;
}

uint256
CMerkleTx::CheckMerkleBranch( uint256 hash,
                              const std::vector< uint256 > & vMerkleBranch,
                              int nIndex )
{
    if ( nIndex == -1 ) return uint256() ;

    for ( std::vector< uint256 >::const_iterator it( vMerkleBranch.begin () ) ; it != vMerkleBranch.end () ; ++ it )
    {
        if ( nIndex & 1 )
            hash = Hash ( BEGIN (*it), END (*it), BEGIN (hash), END (hash) ) ;
        else
            hash = Hash ( BEGIN (hash), END (hash), BEGIN (*it), END (*it) ) ;

        nIndex >>= 1 ;
    }
    return hash ;
}
