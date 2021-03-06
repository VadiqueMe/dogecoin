// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

/**
 * Why base-58 instead of base-64 encoding?
 * - Don't want 0OIl characters that look the same in some fonts and
 *      could be used to create visually identical looking data
 * - A string with non-alphanumeric characters is not as easily accepted as input
 * - E-mail usually won't line-break if there's no punctuation to break at
 * - Double-clicking selects the whole string as one word if it's all alphanumeric
 */
#ifndef DOGECOIN_BASE58_H
#define DOGECOIN_BASE58_H

#include "chainparams.h"
#include "key.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/standard.h"
#include "support/allocators/zeroafterfree.h"

#include <string>
#include <vector>

/**
 * Encode a byte sequence as a base58-encoded string
 * pbegin and pend cannot be NULL, unless both are
 */
std::string EncodeBase58( const unsigned char * pbegin, const unsigned char * pend ) ;

/**
 * Encode a byte vector as a base58-encoded string
 */
std::string EncodeBase58( const std::vector< unsigned char > & vch ) ;

/**
 * Decode a base58-encoded string (str) into a byte vector (vchRet)
 * return true if decoding is successful
 */
bool DecodeBase58( const std::string & str, std::vector< unsigned char > & vchRet ) ;

/**
 * Encode a byte vector into a base58-encoded string, including checksum
 */
std::string EncodeBase58Check( const std::vector< unsigned char > & vchIn ) ;

/**
 * Decode a base58-encoded string (str) that includes a checksum into a byte
 * vector (vchRet), return true if decoding is successful
 */
inline bool DecodeBase58Check( const std::string & str, std::vector< unsigned char > & vchRet ) ;

/**
 * Base class for all base58-encoded data
 */
class CBase58Data
{
protected:
    // prefix byte(s)
    std::vector< unsigned char > vchPrefix ;

    // actually encoded data
    typedef std::vector< unsigned char, zero_after_free_allocator< unsigned char > > vector_uchar ;
    vector_uchar vchData ;

    CBase58Data() ;
    void SetData( const std::vector< unsigned char > & vchPrefixIn, const void * pdata, size_t nSize ) ;
    void SetData( const std::vector< unsigned char > & vchPrefixIn, const unsigned char * pbegin, const unsigned char * pend ) ;

public:
    bool SetString( const std::string & str, unsigned int nPrefixBytes = 1 ) ;
    std::string ToString() const ;
    int CompareTo( const CBase58Data & b58 ) const ;

    bool operator == ( const CBase58Data & b58 ) const {  return CompareTo( b58 ) == 0 ;  }
    bool operator != ( const CBase58Data & b58 ) const {  return CompareTo( b58 ) != 0 ;  }
    bool operator <= ( const CBase58Data & b58 ) const {  return CompareTo( b58 ) <= 0 ;  }
    bool operator >= ( const CBase58Data & b58 ) const {  return CompareTo( b58 ) >= 0 ;  }
    bool operator <  ( const CBase58Data & b58 ) const {  return CompareTo( b58 ) <  0 ;  }
    bool operator >  ( const CBase58Data & b58 ) const {  return CompareTo( b58 ) >  0 ;  }
} ;

/** base58-encoded coin addresses
 *
 * Public-key-hash-addresses
 *   The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key
 * Script-hash-addresses
 *   The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script
 */
class CBase58Address : public CBase58Data
{
public:
    bool Set( const CTxDestination & dest,
                const std::vector< unsigned char > & pubkeyPrefix,
                const std::vector< unsigned char > & scriptPrefix ) ;
    bool Set( const CTxDestination & dest, const CChainParams & params ) ;

    bool SetByKeyID( const CKeyID & id, const std::vector< unsigned char > & pubkeyPrefix ) ;
    bool SetByScriptID( const CScriptID & id, const std::vector< unsigned char > & scriptPrefix ) ;

    bool IsValid( const CChainParams & params = Params() ) const ;
    bool IsValid( const std::vector< unsigned char > & pubkeyPrefix,
                  const std::vector< unsigned char > & scriptPrefix ) const ;

    CBase58Address() {}

    CBase58Address( const CTxDestination & dest, const CChainParams & params = Params() )
    {
        Set( dest, params ) ;
    }

    CBase58Address( const CTxDestination & dest,
                      const std::vector< unsigned char > & pubkeyPrefix,
                      const std::vector< unsigned char > & scriptPrefix )
    {
        Set( dest, pubkeyPrefix, scriptPrefix ) ;
    }

    CBase58Address( const std::string & strAddress )
    {
        SetString( strAddress ) ;
    }

    CTxDestination Get( const CChainParams & params = Params() ) const ;
    bool GetKeyID( CKeyID & keyID, const CChainParams & params = Params() ) const ;
    bool IsScript( const CChainParams & params = Params() ) const ;

    static std::string DummyCoinAddress( const CChainParams & params ) ;
    static std::string DummyCoinAddress( const std::vector< unsigned char > & pubkeyPrefix,
                                         const std::vector< unsigned char > & scriptPrefix ) ;
} ;

/**
 * A base58-encoded secret key
 */
class CBase58Secret : public CBase58Data
{

public:
    CKey GetKey() ;
    void SetKey( const CKey & secretKey, const std::vector< unsigned char > & privkeyPrefix ) ;
    void SetKey( const CKey & secretKey, const CChainParams & params ) ;
    bool IsValid( const CChainParams & params = Params() ) const ;
    bool IsValidFor( const std::vector< unsigned char > & privkeyPrefix ) const ;
    bool SetString( const std::string & strSecret, const std::vector< unsigned char > & privkeyPrefix ) ;
    bool SetString( const std::string & strSecret, const CChainParams & params ) ;

    CBase58Secret( const CKey & secretKey, const CChainParams & params = Params() )
    {
        SetKey( secretKey, params ) ;
    }

    CBase58Secret() {}

} ;

template < typename K, int Size, Base58PrefixType Type >
class CDogecoinExtKeyBase : public CBase58Data
{
public:
    void SetKey( const K & key, const CChainParams & params = Params() ) {
        unsigned char vch[ Size ] ;
        key.Encode( vch ) ;
        SetData( params.Base58PrefixFor( Type ), vch, vch + Size ) ;
    }

    K GetKey() {
        K ret ;
        if ( vchData.size() == Size ) {
            // If base58 encoded data does not hold an ext key, return a ! IsValid() key
            ret.Decode( & vchData[ 0 ] ) ;
        }
        return ret ;
    }

    CDogecoinExtKeyBase( const K & key ) {
        SetKey( key ) ;
    }

    CDogecoinExtKeyBase( const std::string & strBase58c, const CChainParams & params = Params() ) {
        SetString( strBase58c.c_str(), params.Base58PrefixFor( Type ).size() ) ;
    }

    CDogecoinExtKeyBase() { }
} ;

typedef CDogecoinExtKeyBase< CExtKey, BIP32_EXTKEY_SIZE, Base58PrefixType::EXT_SECRET_KEY > CDogecoinExtKey ;
typedef CDogecoinExtKeyBase< CExtPubKey, BIP32_EXTKEY_SIZE, Base58PrefixType::EXT_PUBLIC_KEY > CDogecoinExtPubKey ;

#endif
