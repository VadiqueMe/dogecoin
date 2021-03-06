// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "primitives/block.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"

void CBlockHeader::SetAuxpow( CAuxPow * newAuxpow )
{
    this->auxpow.reset( newAuxpow ) ;
    SetAuxpowInVersion( newAuxpow != nullptr ) ;
}

std::string CBlockHeader::ToString() const
{
    std::stringstream s ;
    s << "CBlockHeader(" ;
      s << "::" << CPureBlockHeader::ToString() ;
      if ( auxpow != nullptr && IsAuxpowInVersion() )
          s << strprintf( ", auxpow=%s", auxpow->ToString() ) ;
    s << ")" ;

    return s.str() ;
}

std::string CBlock::ToString() const
{
    std::stringstream s ;
    s << "CBlock(" ;
      s << "::" << CBlockHeader::ToString() ;
      s << ", " ;
      s << strprintf( "vtx.size=%u", vtx.size() ) ;
    s << ")" << std::endl ;
    for ( unsigned int i = 0 ; i < vtx.size() ; i++ )
    {
        s << "  " << vtx[ i ]->ToString() ;
    }
    return s.str() ;
}

int64_t GetBlockWeight( const CBlock & block )
{
    // This implements the weight = ( stripped_size * 4 ) + witness_size formula,
    // using only serialization with and without witness data. As witness_size
    // is equal to total_size - stripped_size, this formula is identical to:
    // weight = ( stripped_size * 3 ) + total_size
    return ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * (WITNESS_SCALE_FACTOR - 1) + ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION);
}
