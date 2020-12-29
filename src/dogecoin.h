// Copyright (c) 2015 The Dogecoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "amount.h"
#include "chain.h"
#include "chainparams.h"

static const int HEIGHT_OF_FIRST_DigiShield_BLOCK = 157500 ;

bool AcceptDigishieldMinDifficultyForBlock( const CBlockIndex * pindexLast, const CBlockHeader * pblock, const Consensus::Params & params ) ;
CAmount GetDogecoinBlockSubsidy( int nHeight, const Consensus::Params & consensusParams, uint256 prevHash ) ;
uint32_t CalculateDogecoinNextWorkRequired( const CBlockIndex* pindexLast, int64_t nLastRetargetTime, const Consensus::Params & params, bool talkative = false ) ;

/**
 * Check proof-of-work of a block header, taking auxpow into account
 * @param block The block header
 * @param params Consensus parameters
 * @return True iff the PoW is correct
 */
bool CheckDogecoinProofOfWork( const CBlockHeader & block, const Consensus::Params & params ) ;
