// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2016 Daniel Kraft
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_AUXPOW_H
#define DOGECOIN_AUXPOW_H

#include "merkletx.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "primitives/pureheader.h"
#include "uint256.h"

#include <map>
#include <string>
#include <vector>

class CBlockHeader ;

/** Header for merge-mining data in the coinbase */
static const unsigned char pchMergedMiningHeader[] = { 0xfa, 0xbe, 'm', 'm' } ;

enum class Base58PrefixType ;
extern const std::map< Base58PrefixType, std::vector< unsigned char > > litecoin_main_base58Prefixes ;

/**
 * Data for the merge-mining auxpow.  This is a merkle tx (the parent block's
 * coinbase tx) that can be verified to be in the parent block, and this
 * transaction's input (the coinbase script) contains the reference
 * to the actual merge-mined block
 */
class CAuxPow : public CMerkleTx
{

/* Public for the unit tests */
public:

  /** The merkle branch connecting the aux block to our coinbase */
  std::vector< uint256 > vChainMerkleBranch ;

  /** Merkle tree index of the aux block header in the coinbase */
  int nChainIndex ;

  /** Parent block header (on which the real PoW is done) */
  CPureBlockHeader parentBlock ;

public:

  inline explicit CAuxPow( CTransactionRef txIn ) : CMerkleTx( txIn ) {}

  inline CAuxPow() : CMerkleTx() {}

  std::string ToString() const ;

  ADD_SERIALIZE_METHODS ;

  template< typename Stream, typename Operation >
    inline void
    SerializationOp( Stream & s, Operation ser_action )
  {
      READWRITE (*static_cast< CMerkleTx* >( this ));
      READWRITE (vChainMerkleBranch);
      READWRITE (nChainIndex);
      READWRITE (parentBlock);
  }

  /**
   * Check the auxpow, given the merge-mined block's hash and our chain ID.
   * Note that this does not verify the actual PoW on the parent block!
   * It just confirms that all the merkle branches are valid
   * @param hashAuxBlock Hash of the merge-mined block
   * @param nChainId The auxpow chain ID of the block to check
   * @param params Consensus parameters
   * @return True if the auxpow is valid
   */
  bool check( const uint256 & hashAuxBlock, int nChainId, const Consensus::Params & params ) const ;

  /**
   * Get the parent block's hash. This is used to verify that it
   * satisfies the PoW requirement
   * @return The parent block's scrypt hash
   */
  inline uint256
  getParentBlockScryptHash() const
  {
      return parentBlock.GetScryptHash () ;
  }

  /**
   * @return The parent block header
   */
  inline const CPureBlockHeader &
  getParentBlockHeader() const
  {
      return parentBlock ;
  }

  /**
   * Calculate the expected index in the merkle tree. This is also used
   * for the test-suite
   * @param nNonce The coinbase's nonce value
   * @param nChainId The chain ID
   * @param h The merkle block height
   * @return The expected index for the aux hash
   */
  static int getExpectedIndex( uint32_t nNonce, int nChainId, unsigned h ) ;

  /**
   * Initialise the auxpow of the given block header.  This constructs
   * a minimal CAuxPow object with a minimal parent block and sets
   * it on the block header.  The auxpow is not necessarily valid, but
   * can be "mined" to make it valid
   * @param header The header to set the auxpow on
   */
  static void initAuxPow( CBlockHeader & header ) ;

};

#endif
