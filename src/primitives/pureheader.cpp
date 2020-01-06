// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2019 vadique
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "primitives/pureheader.h"

#include "chainparams.h"
#include "crypto/scrypt.h"
#include "hash.h"
#include "utilstrencodings.h"
#include "algo/Lyra2RE.h"

#include <sstream>
#include <boost/format.hpp>

void CPureBlockHeader::SetBaseVersion( int32_t nBaseVersion, int32_t nChainId )
{
    assert( nBaseVersion >= 1 && nBaseVersion < VERSION_AUXPOW ) ;
    assert( ! IsAuxpowInVersion() ) ;
    nVersion = nBaseVersion | ( nChainId * VERSION_CHAIN_START ) ;
}

uint256 CPureBlockHeader::GetSha256Hash() const
{
    return SerializeHash( *this ) ;
}

uint256 CPureBlockHeader::GetScryptHash() const
{
    uint256 hash ;
    scrypt_1024_1_1_256( BEGIN(nVersion), BEGIN(hash) ) ;
    return hash ;
}

uint256 CPureBlockHeader::GetLyra2Re2Hash() const
{
    uint256 hash ;
    lyra2re2_hash( BEGIN(nVersion), BEGIN(hash) ) ;
    return hash ;
}

std::string CPureBlockHeader::ToString() const
{
    std::stringstream ss ;
    ss << boost::format( "CPureBlockHeader(version=0x%x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=0x%08x, sha256_hash=%s, scrypt_hash=%s)" )
        % nVersion
        % hashPrevBlock.ToString()
        % hashMerkleRoot.ToString()
        % nTime % nBits % nNonce
        % GetSha256Hash().ToString() % GetScryptHash().ToString()
    << std::endl ;
    return ss.str() ;
}
