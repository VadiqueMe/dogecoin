// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_PEERVERSION_H
#define DOGECOIN_PEERVERSION_H

#if defined(HAVE_CONFIG_H)
#include "config/dogecoin-config.h"
#else

/**
 * peer versioning and copyright year
 */

//! These need to be macros, as peerversion.cpp's and dogecoin*-res.rc's voodoo requires it
#define PEER_VERSION_MAJOR 0
#define PEER_VERSION_MINOR 14
#define PEER_VERSION_REVISION 2
#define PEER_VERSION_BUILD 0

/**
 * Copyright year (2009-this)
 * Todo: update this when changing our copyright comments in the source
 */
#define COPYRIGHT_YEAR 2017

#endif //HAVE_CONFIG_H

/**
 * Converts the parameter X to a string after macro replacement on X has been performed.
 * Don't merge these into one macro!
 */
#define STRINGIZE(X) DO_STRINGIZE(X)
#define DO_STRINGIZE(X) #X

//! Copyright string used in Windows .rc files
#define COPYRIGHT_STR "2009-" STRINGIZE(COPYRIGHT_YEAR) " " COPYRIGHT_HOLDERS_FINAL

/**
 * dogecoind-res.rc includes this file, but it cannot cope with real c++ code.
 * WINDRES_PREPROC is defined to indicate that its pre-processor is running.
 * Anything other than a define should be guarded below
 */

#if !defined(WINDRES_PREPROC)

#include <string>
#include <vector>

static const int PEER_VERSION =
                           1000000 * PEER_VERSION_MAJOR
                         +   10000 * PEER_VERSION_MINOR
                         +     100 * PEER_VERSION_REVISION
                         +       1 * PEER_VERSION_BUILD ;

extern const std::string PEER_NAME ;

std::string FormatFullVersion() ;
std::string FormatSubVersion( const std::string & name, int nPeerVersion, const std::vector< std::string > & comments ) ;

#endif // #if !defined(WINDRES_PREPROC)

#endif
