// Copyright (c) 2019 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "feerate.h"

CFeeRate::CFeeRate( const CAmount & nFeePaid, size_t bytes )
{
    nCoinuPerK = ( bytes == 0 ) ? 0 : nFeePaid * 1000 / static_cast< signed >( bytes ) ;
}

CAmount CFeeRate::GetFeePerBytes( size_t bytes ) const
{
    // Dogecoin: Round up to the nearest 1000 bytes to have round tx fees
    if ( bytes % 1000 > 0 )
        bytes = bytes - ( bytes % 1000 ) + 1000 ;

    CAmount nFee = nCoinuPerK * static_cast< signed >( bytes ) / 1000 ;
    return nFee ;
}

#include "tinyformat.h"

std::string CFeeRate::ToString() const
{
    return strprintf( "%d.%08d %s/kB", nCoinuPerK / COIN, nCoinuPerK % COIN, NAME_OF_CURRENCY ) ;
}
