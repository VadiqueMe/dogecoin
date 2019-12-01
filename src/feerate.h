// Copyright (c) 2019 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_FEERATE_H
#define DOGECOIN_FEERATE_H

#include "amount.h"
#include "serialize.h"

/**
 * Fee rate in atomary coin units per kilobyte, CAmount / kB
 */
class CFeeRate
{

private:

    CAmount nCoinuPerK ; // unit is atomary-coin-units-per-1000-bytes

public:

    CFeeRate() : nCoinuPerK( 0 ) { }
    explicit CFeeRate( const CAmount & perK ) : nCoinuPerK( perK ) { }
    CFeeRate( const CAmount & nFeePaid, size_t bytes ) ;
    CFeeRate( const CFeeRate & other ) {  nCoinuPerK = other.nCoinuPerK ;  }

    CAmount GetFeePerBytes( size_t bytes ) const ;
    CAmount GetFeePerKiloByte() const {  return GetFeePerBytes( 1000 ) ;  }

    friend bool operator< ( const CFeeRate & a, const CFeeRate & b ) {  return a.nCoinuPerK < b.nCoinuPerK ;  }
    friend bool operator> ( const CFeeRate & a, const CFeeRate & b ) {  return a.nCoinuPerK > b.nCoinuPerK ;  }
    friend bool operator==( const CFeeRate & a, const CFeeRate & b ) {  return a.nCoinuPerK == b.nCoinuPerK ;  }
    friend bool operator<=( const CFeeRate & a, const CFeeRate & b ) {  return a.nCoinuPerK <= b.nCoinuPerK ;  }
    friend bool operator>=( const CFeeRate & a, const CFeeRate & b ) {  return a.nCoinuPerK >= b.nCoinuPerK ;  }
    CFeeRate & operator+= ( const CFeeRate & a ) {  nCoinuPerK += a.nCoinuPerK ; return *this ;  }
    std::string ToString() const ;

    ADD_SERIALIZE_METHODS ;

    template < typename Stream, typename Operation >
    inline void SerializationOp( Stream & s, Operation ser_action ) {
        READWRITE( nCoinuPerK ) ;
    }

} ;

#endif
