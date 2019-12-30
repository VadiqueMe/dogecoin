// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_AMOUNT_H
#define DOGECOIN_AMOUNT_H

#include <stdlib.h>
#include <string>

/** amount in atomary coin units, can be negative */
typedef int64_t CAmount ;

static const CAmount E12COIN = 1000000000000 ;
static const CAmount E8COIN = 100000000 ;
static const CAmount E6COIN = 1000000 ;
static const CAmount E8CENT = E6COIN ;

extern const std::string NAME_OF_CURRENCY ;

static const CAmount MAX_MONEY = 1000000 * E12COIN ; // max transaction 1 000 000 0000 0000 0000
inline bool MoneyRange( const CAmount & nValue ) {  return ( nValue >= 0 && nValue <= MAX_MONEY ) ;  }

#endif
