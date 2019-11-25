// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "peerversion.h"

#include "tinyformat.h"

#include <string>

/**
 * Name of peer sent via the 'version' message
 */
const std::string PEER_NAME( "Inutoshi" ) ;

/**
 * Peer version number
 */
#define PEER_VERSION_SUFFIX ""

/**
 * The following part of the code composes Full_Version_Of_Peer
 */

#ifdef HAVE_BUILD_INFO
#include "build.h"
#endif

//! git will put "#define GIT_ARCHIVE 1" on the next line inside archives. $Format:%n#define GIT_ARCHIVE 1$
#ifdef GIT_ARCHIVE
#define GIT_COMMIT_ID "$Format:%h$"
#define GIT_COMMIT_DATE "$Format:%cD$"
#endif

#define BUILD_DESC_WITH_SUFFIX(maj, min, rev, build, suffix) \
    "v" DO_STRINGIZE(maj) "." DO_STRINGIZE(min) "." DO_STRINGIZE(rev) "." DO_STRINGIZE(build) "-" DO_STRINGIZE(suffix)

#define BUILD_DESC_FROM_COMMIT(maj, min, rev, build, commit) \
    "v" DO_STRINGIZE(maj) "." DO_STRINGIZE(min) "." DO_STRINGIZE(rev) "." DO_STRINGIZE(build) "-g" commit

#define BUILD_DESC_FROM_UNKNOWN(maj, min, rev, build) \
    "v" DO_STRINGIZE(maj) "." DO_STRINGIZE(min) "." DO_STRINGIZE(rev) "." DO_STRINGIZE(build) "-unk"

#ifndef BUILD_DESC
#ifdef BUILD_SUFFIX
#define BUILD_DESC BUILD_DESC_WITH_SUFFIX(PEER_VERSION_MAJOR, PEER_VERSION_MINOR, PEER_VERSION_REVISION, PEER_VERSION_BUILD, BUILD_SUFFIX)
#elif defined(GIT_COMMIT_ID)
#define BUILD_DESC BUILD_DESC_FROM_COMMIT(PEER_VERSION_MAJOR, PEER_VERSION_MINOR, PEER_VERSION_REVISION, PEER_VERSION_BUILD, GIT_COMMIT_ID)
#else
#define BUILD_DESC BUILD_DESC_FROM_UNKNOWN(PEER_VERSION_MAJOR, PEER_VERSION_MINOR, PEER_VERSION_REVISION, PEER_VERSION_BUILD)
#endif
#endif

static const std::string Full_Version_Of_Peer( BUILD_DESC PEER_VERSION_SUFFIX ) ;

static std::string FormatVersion( int nVersion )
{
    if ( nVersion % 100 == 0 && (nVersion / 100) % 10 == 0 )
        return strprintf( "%d.%d", nVersion / 1000000, (nVersion / 10000) % 100 ) ;
    else if ( nVersion % 100 == 0 )
        return strprintf( "%d.%d.%d", nVersion / 1000000, (nVersion / 10000) % 100, (nVersion / 100) % 100 ) ;
    else
        return strprintf( "%d.%d.%d.%d", nVersion / 1000000, (nVersion / 10000) % 100, (nVersion / 100) % 100, nVersion % 100 ) ;
}

std::string FormatFullVersion()
{
    return Full_Version_Of_Peer ;
}

/**
 * Format the subversion field according to BIP 14 spec (https://github.com/bitcoin/bips/blob/master/bip-0014.mediawiki)
 */
std::string FormatSubVersion( const std::string& name, int nPeerVersion, const std::vector<std::string>& comments )
{
    std::ostringstream ss;
    ss << "/";
    ss << name << ":" << FormatVersion( nPeerVersion ) ;
    if (!comments.empty())
    {
        std::vector<std::string>::const_iterator it(comments.begin());
        ss << "(" << *it;
        for(++it; it != comments.end(); ++it)
            ss << "; " << *it;
        ss << ")";
    }
    ss << "/";
    return ss.str();
}
