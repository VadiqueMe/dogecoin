// Copyright (c) 2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_UTILLOG_H
#define DOGECOIN_UTILLOG_H

#include <atomic>
#include <string>

#include "tinyformat.h"

extern const std::string LOG_FILE_NAME ;

extern std::atomic< bool > fReopenDebugLog ;

extern bool fLogTimestamps ;
extern bool fLogTimeMicros ;
extern bool fLogIPs ;

static const bool DEFAULT_LOGTIMESTAMPS = true ;
static const bool DEFAULT_LOGTIMEMICROS = false ;
static const bool DEFAULT_LOGIPS        = true ;

void PickPrintToConsole() ;
void PickPrintToDebugLog() ;

void OpenDebugLog() ;

/** Return true if log accepts specified category */
bool LogAcceptsCategory( const std::string & category ) ;

/** Send a string to the log output */
int LogPrintStr( const std::string & str ) ;

template< typename... Args >
inline void LogPrint( const std::string & category, const Args &... args )
{
    if ( LogAcceptsCategory( category ) )
        LogPrintStr( tfm::format( args... ) ) ;
}

template< typename... Args >
inline void LogPrintf( const Args &... args )
{
    LogPrintStr( tfm::format( args... ) ) ;
}

template< typename... Args >
bool error( const char * fmt, const Args &... args )
{
    LogPrintStr( "ERROR: " + tfm::format( fmt, args... ) + "\n" ) ;
    return false ;
}

void PrintExceptionContinue( const std::exception * pex, const char * thread ) ;

void ShrinkLogFile() ;

#endif
