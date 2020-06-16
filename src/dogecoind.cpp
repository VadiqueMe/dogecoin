// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "chainparams.h"
#include "chainparamsutil.h"
#include "compat.h"
#include "peerversion.h"
#include "init.h"
#include "noui.h"
#include "scheduler.h"
#include "util.h"
#include "utilstr.h"
#include "utillog.h"
#include "utilthread.h"
#include "utiltime.h"
#include "utilstrencodings.h"
#include "httpserver.h"
#include "httprpc.h"
#include "rpc/server.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#include <stdio.h>

#if defined(HAVE_CONFIG_H)
#include "config/dogecoin-config.h"
#endif

void WaitForShutdown( std::vector< std::thread > * threadGroup )
{
    while ( ! ShutdownRequested() )
        MilliSleep( 200 ) ;

    if ( threadGroup != nullptr )
        StopAndJoinThreads( *threadGroup ) ;
}

//
// Begin
//
bool AppInit(int argc, char* argv[])
{
    //
    // Parameters
    //
    ParseParameters( argc, argv ) ;

    // Look for chain name parameter
    // Params() work only after this clause
    try {
        SelectParams( ChainNameFromArguments() ) ;
    } catch ( const std::exception & e ) {
        fprintf( stderr, "Error: %s\n", e.what() ) ;
        return false ;
    }

    // Process help and version before taking care about datadir
    if ( IsArgSet( "-?" ) || IsArgSet( "-h" ) ||  IsArgSet( "-help" ) || IsArgSet( "-version" ) )
    {
        std::string strUsage = strprintf(_("%s Daemon"), _(PACKAGE_NAME)) + " " + _("version") + " " + FormatFullVersion() + "\n";

        if (IsArgSet("-version"))
        {
            strUsage += FormatParagraph(LicenseInfo());
        }
        else
        {
            strUsage += "\n" + _("Usage:") + "\n" +
                  "  dogecoind [options]                     " + strprintf(_("Start %s Daemon"), _(PACKAGE_NAME)) + "\n";

            strUsage += "\n" + HelpMessage( HELP_MESSAGE_DOGECOIND ) ;
        }

        fprintf(stdout, "%s", strUsage.c_str());
        return true;
    }

    std::vector< std::thread > threadGroup ;
    bool initOk = false ;

    try
    {
        if ( ! boost::filesystem::is_directory( GetDirForData( false ) ) )
        {
            fprintf( stderr, "Error: Specified data directory \"%s\" does not exist\n", GetArg( "-datadir", "" ).c_str() ) ;
            return false ;
        }

        // do this early
        BeginLogging() ;

        try {
            ReadConfigFile( GetArg( "-conf", DOGECOIN_CONF_FILENAME ) ) ;
        } catch ( const std::exception & e ) {
            fprintf( stderr, "Error reading configuration file: %s\n", e.what() ) ;
            return false ;
        }

        // Command-line RPC
        bool fCommandLine = false ;
        for ( int i = 1 ; i < argc ; i ++ )
            if ( ! IsSwitchChar( argv[ i ][ 0 ] ) && ! boost::algorithm::istarts_with( argv[ i ], "dogecoin:" ) )
                fCommandLine = true ;

        if ( fCommandLine )
        {
            fprintf( stderr, "Error: There is no RPC client functionality in dogecoind anymore. Use the dogecoin-cli utility instead\n" ) ;
            exit( EXIT_FAILURE ) ;
        }
        // -server defaults to true for bitcoind but not for the GUI so do this here
        SoftSetBoolArg( "-server", true ) ;

        InitParameterInteraction() ;
        if ( ! AppInitBasicSetup() || ! AppInitParameterInteraction() || ! AppInitSanityChecks() )
        {
            // InitError will have been called with detailed error, which ends up on console
            exit( 1 ) ;
        }

        if ( GetBoolArg( "-daemon", false ) )
        {
#if HAVE_DECL_DAEMON
            fprintf(stdout, "Dogecoin server starting\n");

            // Daemonize
            if (daemon(1, 0)) { // don't chdir (1), do close FDs (0)
                fprintf(stderr, "Error: daemon() failed: %s\n", strerror(errno));
                return false;
            }
#else
            fprintf(stderr, "Error: -daemon is not supported on this operating system\n");
            return false;
#endif // HAVE_DECL_DAEMON
        }

        CScheduler scheduler ;
        initOk = AppInitMain( threadGroup, scheduler ) ;
    }
    catch ( const std::exception & e ) {
        PrintExceptionContinue( &e, "AppInit()" ) ;
    } catch ( ... ) {
        PrintExceptionContinue( nullptr, "AppInit()" ) ;
    }

    if ( ! initOk ) {
        StopAndJoinThreads( threadGroup ) ;
    } else {
        WaitForShutdown( &threadGroup ) ;
    }
    Shutdown() ;

    return initOk ;
}

int main(int argc, char* argv[])
{
    SetupEnvironment();

    // Connect bitcoind signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE);
}
