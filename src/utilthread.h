// Copyright (c) 2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_UTILTHREAD_H
#define DOGECOIN_UTILTHREAD_H

#include "utillog.h"

#include <vector>
#include <thread>

void RenameThread( const std::string & name ) ;

/**
 * Wait for all threads to finish
 */
void JoinAll( std::vector< std::thread > & threads ) ;

/**
 * Return the number of physical cores available on the current system
 */
inline int GetNumCores()
{
    return std::thread::hardware_concurrency() ;
}

/*
 * Wrapper that just calls func once
 */
template < typename Callable >
void TraceThread( const char * name,  Callable func )
{
    RenameThread( std::string( name ) ) ;
    try
    {
        LogPrintf( "%s thread start\n", name ) ;
        func() ;
        LogPrintf( "%s thread exit\n", name ) ;
    }
    catch ( const std::string & s ) {
        if ( s == "stopthread" ) LogPrintf( "%s thread stop\n", name ) ;
        throw ;
    }
    catch ( const std::exception & e ) {
        PrintExceptionContinue( &e, name ) ;
        throw ;
    }
    catch ( ... ) {
        PrintExceptionContinue( nullptr, name ) ;
        throw ;
    }
}

bool ShutdownRequested() ;
void RequestShutdown() ;
void RejectShutdown() ;

#endif
