// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_POW_H
#define DOGECOIN_POW_H

#include "consensus/params.h"

#include <stdint.h>

class CBlockHeader ;
class CBlockIndex ;
class uint256 ;
class CAuxPow ;

uint32_t GetNextWorkRequired( const CBlockIndex * pindexLast, const CBlockHeader * pblock, const Consensus::Params & params, bool talkative = false ) ;
///uint32_t CalculateNextWorkRequired( const CBlockIndex * pindexLast, int64_t nFirstBlockTime, const Consensus::Params & ) ;

/** Check whether a block header satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork( const CBlockHeader & block, unsigned int nBits, const Consensus::Params & ) ;
bool CheckAuxProofOfWork( const CAuxPow & auxpow, unsigned int nBits, const Consensus::Params & ) ;

#endif
