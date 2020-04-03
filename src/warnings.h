// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_WARNINGS_H
#define DOGECOIN_WARNINGS_H

#include <stdlib.h>
#include <string>

void SetMiscWarning( const std::string & warning ) ; // like out of disk space

void SetHighForkFound( bool f ) ;
bool GetHighForkFound() ;

void SetHighInvalidChainFound( bool f ) ;
bool GetHighInvalidChainFound() ;

std::string GetWarnings( const std::string & strFor ) ;

static const bool DEFAULT_TESTSAFEMODE = false ;

#endif
