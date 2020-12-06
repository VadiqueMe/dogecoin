// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 Vince Durham
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2016 Daniel Kraft
// Copyright (c) 2020 vadique
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php

#include "auxpow.h"

#include "compat/endian.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "script/script.h"
#include "base58.h"
#include "chainparams.h"
#include "validation.h"
#include "utillog.h" // for error(
#include "utilstrencodings.h"

#include <algorithm>

const std::map< Base58PrefixType, std::vector< unsigned char > > litecoin_main_base58Prefixes = {
    { Base58PrefixType::PUBKEY_ADDRESS, std::vector< unsigned char >( 1, 48 ) },
    { Base58PrefixType::SCRIPT_ADDRESS, std::vector< unsigned char >( 1, 5 ) },
    { Base58PrefixType::SECRET_KEY,     std::vector< unsigned char >( 1, 176 ) },
    { Base58PrefixType::EXT_PUBLIC_KEY, { 0x04, 0x88, 0xB2, 0x1E } },
    { Base58PrefixType::EXT_SECRET_KEY, { 0x04, 0x88, 0xAD, 0xE4 } }
} ;

std::string CAuxPow::ToString() const
{
    std::stringstream s ;
    s << "CAuxPow(" ;
      s << "::" << CMerkleTx::ToString() ;
      if ( tx->IsCoinBase() ) {
          CTxDestination destination ;
          if ( ExtractDestination( tx->vout[ 0 ].scriptPubKey, destination ) )
              s << "(tx->vout[0]: address_litecoin="
                << CBase58Address(
                       destination,
                       litecoin_main_base58Prefixes.at( Base58PrefixType::PUBKEY_ADDRESS ),
                       litecoin_main_base58Prefixes.at( Base58PrefixType::SCRIPT_ADDRESS )
                   ).ToString() << ")" ;
      }
      s << ", " ;
      s << "vChainMerkleBranch[" << vChainMerkleBranch.size() << "]={" ;
        bool first = true ;
        for ( const uint256 & entry : vChainMerkleBranch ) {
            if ( first ) first = false ;
            else s << "," ;
            s << entry.ToString() ;
        }
        s << "}, " ;
      s << "nChainIndex=" << nChainIndex << ", " ;
      s << "parentBlock=" << parentBlock.ToString() ;
    s << ")" ;

    return s.str() ;
}

bool CAuxPow::check( const uint256 & hashAuxBlock, int nChainId, const Consensus::Params & params ) const
{
    if ( nIndex != 0 )
        return error( "AuxPow is not a generate" ) ;

    if ( params.fStrictChainId && parentBlock.GetChainId () == nChainId )
        return error( "Aux PoW parent has our chain ID" ) ;

    if ( vChainMerkleBranch.size() > 30 )
        return error( "Aux PoW chain merkle branch too long" ) ;

    // Check that the chain merkle root is in the coinbase
    const uint256 nRootHash
      = CheckMerkleBranch(hashAuxBlock, vChainMerkleBranch, nChainIndex);
    std::vector<unsigned char> vchRootHash(nRootHash.begin(), nRootHash.end());
    std::reverse(vchRootHash.begin(), vchRootHash.end()); // correct endian

    // Check that we are in the parent block merkle tree
    if ( CheckMerkleBranch( GetTxHash(), vMerkleBranch, nIndex ) != parentBlock.hashMerkleRoot )
        return error( "Aux POW merkle root incorrect" ) ;

    const CScript script = tx->vin[ 0 ].scriptSig ;

    // Check that the same work is not submitted twice to the chain

    CScript::const_iterator pcHead =
        std::search( script.begin(), script.end(), UBEGIN(pchMergedMiningHeader), UEND(pchMergedMiningHeader) ) ;

    CScript::const_iterator pc =
        std::search( script.begin(), script.end(), vchRootHash.begin(), vchRootHash.end() ) ;

    if ( pc == script.end() )
        return error( "Aux PoW missing chain merkle root in parent coinbase" ) ;

    if ( pcHead != script.end() )
    {
        // Enforce only one chain merkle root by checking that a single instance of the merged
        // mining header exists just before
        if ( script.end() != std::search( pcHead + 1, script.end(), UBEGIN(pchMergedMiningHeader), UEND(pchMergedMiningHeader) ) )
            return error( "Multiple merged mining headers in coinbase" ) ;
        if ( pcHead + sizeof( pchMergedMiningHeader ) != pc )
            return error( "Merged mining header is not just before chain merkle root" ) ;
    }
    else
    {
        // For backward compatibility
        // Enforce only one chain merkle root by checking that it starts early in the coinbase
        // 8-12 bytes are enough to encode extraNonce and nBits
        if ( pc - script.begin() > 20 )
            return error( "Aux PoW chain merkle root must start in the first 20 bytes of the parent coinbase" ) ;
    }

    // Ensure we are at a deterministic point in the merkle leaves by hashing
    // a nonce and our chain ID and comparing to the index
    pc += vchRootHash.size() ;
    if ( script.end() - pc < 8 )
        return error( "Aux PoW missing chain merkle tree size and nonce in parent coinbase" ) ;

    uint32_t nSize;
    memcpy(&nSize, &pc[0], 4);
    nSize = le32toh(nSize);
    const unsigned merkleHeight = vChainMerkleBranch.size();
    if (nSize != (1u << merkleHeight))
        return error( "Aux PoW merkle branch size does not match parent coinbase" ) ;

    uint32_t nNonce;
    memcpy(&nNonce, &pc[4], 4);
    nNonce = le32toh (nNonce);
    if (nChainIndex != getExpectedIndex (nNonce, nChainId, merkleHeight))
        return error( "Aux PoW wrong index" ) ;

    return true;
}

int
CAuxPow::getExpectedIndex( uint32_t nNonce, int nChainId, unsigned h )
{
  // Choose a pseudo-random slot in the chain merkle tree
  // but have it be fixed for a size/nonce/chain combination
  //
  // This prevents the same work from being used twice for the
  // same chain while reducing the chance that two chains clash
  // for the same slot

  /* This computation can overflow the uint32 used.  This is not an issue,
     though, since we take the mod against a power-of-two in the end anyway.
     This also ensures that the computation is, actually, consistent
     even if done in 64 bits as it was in the past on some systems

     Note that h is always <= 30 (enforced by the maximum allowed chain
     merkle branch length), so that 32 bits are enough for the computation */

  uint32_t rand = nNonce;
  rand = rand * 1103515245 + 12345;
  rand += nChainId;
  rand = rand * 1103515245 + 12345;

  return rand % (1 << h);
}

void
CAuxPow::initAuxPow( CBlockHeader & header )
{
  /* set auxpow bit in version now, since we take the block hash below */
  header.SetAuxpowInVersion( true ) ;

  /* build a minimal coinbase script input for merge-mining */
  const uint256 blockHash = header.GetSha256Hash () ;
  std::vector< unsigned char > inputData( blockHash.begin (), blockHash.end () ) ;
  std::reverse( inputData.begin (), inputData.end () ) ;
  inputData.push_back( 1 ) ;
  inputData.insert( inputData.end (), 7, 0 ) ;

  /* fake a parent-block coinbase with just the required input script and no outputs */
  CMutableTransaction coinbase ;
  coinbase.vin.resize( 1 ) ;
  coinbase.vin[ 0 ].prevout.SetNull() ;
  coinbase.vin[ 0 ].scriptSig = ( CScript () << inputData ) ;
  assert( coinbase.vout.empty() ) ;
  CTransactionRef coinbaseRef = MakeTransactionRef( coinbase ) ;

  /* build a fake parent block with the coinbase */
  CBlock parent ;
  parent.nVersion = 1 ;
  parent.vtx.resize( 1 ) ;
  parent.vtx[ 0 ] = coinbaseRef ;
  parent.hashMerkleRoot = BlockMerkleRoot( parent ) ;

  /* construct the auxpow object */
  header.SetAuxpow( new CAuxPow( coinbaseRef ) ) ;
  assert( header.auxpow->vChainMerkleBranch.empty() ) ;
  header.auxpow->nChainIndex = 0 ;
  assert( header.auxpow->vMerkleBranch.empty() ) ;
  header.auxpow->nIndex = 0 ;
  header.auxpow->parentBlock = parent ;
}
