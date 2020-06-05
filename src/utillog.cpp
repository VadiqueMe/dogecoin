// Copyright (c) 2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "utillog.h"

#include <memory>
#include <mutex>
#include <set>

#include <boost/filesystem.hpp>

#include "util.h"
#include "utiltime.h"

const std::string LOG_FILE_NAME = "debug.log" ;

std::atomic < bool > fReopenDebugLog( false ) ;

bool fLogTimestamps = DEFAULT_LOGTIMESTAMPS ;
bool fLogTimeMicros = DEFAULT_LOGTIMEMICROS ;
bool fLogIPs = DEFAULT_LOGIPS ;

static bool printToConsole = false ;
static bool printToDebugLog = true ;

void PickPrintToConsole()
{
    printToConsole = true ;
    printToDebugLog = false ;
}

void PickPrintToDebugLog()
{
    printToDebugLog = true ;
    printToConsole = false ;
}

static int FileWriteStr( const std::string & str, const std::unique_ptr< FILE > & fp )
{
    return fwrite( str.data(), 1, str.size(), fp.get() ) ;
}

static std::unique_ptr< std::mutex > mutexDebugLog( nullptr );

static void DebugPrintInit()
{
    if ( mutexDebugLog == nullptr )
        mutexDebugLog.reset( new std::mutex() ) ;
}

static std::list< std::string > messagesBeforeOpenLog ;

static std::unique_ptr< FILE > fileout( nullptr ) ;

static std::once_flag debugPrintInitFlag ;

void OpenDebugLog()
{
    if ( ! printToDebugLog ) return ;

    std::call_once( debugPrintInitFlag, &DebugPrintInit ) ;
    std::unique_lock< std::mutex > lock( *mutexDebugLog ) ;

    assert( fileout == nullptr ) ;
    boost::filesystem::path pathToDebugLog = GetDirForData() / LOG_FILE_NAME ;
    fileout.reset( fopen( pathToDebugLog.string().c_str (), "a" ) ) ;
    if ( fileout != nullptr ) {
        setbuf( fileout.get(), NULL ) ; // unbuffered
        // dump buffered messages from before we opened the log
        while ( ! messagesBeforeOpenLog.empty() ) {
            FileWriteStr( messagesBeforeOpenLog.front(), fileout ) ;
            messagesBeforeOpenLog.pop_front() ;
        }
    }
}

bool LogAcceptsCategory( const std::string & category )
{
    if ( ! category.empty() ) // if ( false )
    {
        if ( ! fDebug ) return false ;

        // Give each thread quick access to -debug settings.
        // This helps prevent issues debugging global destructors,
        // where mapMultiArgs might be deleted before another
        // global destructor calls LogPrint()
        static thread_local std::unique_ptr< std::set< std::string > > ptrCategory ;
        if ( ptrCategory == nullptr )
        {
            if ( mapMultiArgs.count( "-debug" ) ) {
                const std::vector< std::string > & categories = mapMultiArgs.at( "-debug" ) ;
                ptrCategory.reset( new std::set< std::string >( categories.begin(), categories.end() ) ) ;
                // thread_local unique_ptr automatically deletes the set when the thread ends
            } else
                ptrCategory.reset( new std::set< std::string >() ) ;
        }
        const std::set< std::string > & setCategories = *ptrCategory.get() ;

        // if not debugging everything and not debugging specific category, LogPrint does nothing
        if ( setCategories.count( std::string( "" ) ) == 0 &&
                setCategories.count( std::string( "1" ) ) == 0 &&
                setCategories.count( std::string( category ) ) == 0 )
            return false ;
    }
    return true ;
}

/**
 * fStartedNewLine is a state variable held by the calling context that will
 * suppress printing of the timestamp when multiple calls are made that don't
 * end in a newline. Initialize it to true, and hold it, in the calling context
 */
static std::string LogTimestampStr( const std::string & str, std::atomic_bool * fStartedNewLine )
{
    if ( ! fLogTimestamps ) return str ;

    std::string strStamped ;

    if ( *fStartedNewLine ) {
        int64_t nTimeMicros = GetLogTimeMicros() ;
        strStamped = DateTimeStrFormat( "%Y-%m-%d %H:%M:%S", nTimeMicros / 1000000 ) ;
        if ( fLogTimeMicros )
            strStamped += strprintf( ".%06d", nTimeMicros % 1000000 ) ;
        strStamped += ' ' + str ;
    } else
        strStamped = str ;

    if ( ! str.empty() && str[ str.size() - 1 ] == '\n' )
        *fStartedNewLine = true ;
    else
        *fStartedNewLine = false ;

    return strStamped ;
}

int LogPrintStr( const std::string & str )
{
    int ret = 0 ; // number of characters written
    static std::atomic_bool fStartedNewLine( true ) ;

    std::string strTimestamped = LogTimestampStr( str, &fStartedNewLine ) ;

    if ( printToConsole )
    {
        ret = fwrite( strTimestamped.data(), 1, strTimestamped.size(), stdout ) ;
        fflush( stdout ) ;
    }
    else if ( printToDebugLog )
    {
        std::call_once( debugPrintInitFlag, &DebugPrintInit ) ;
        std::unique_lock< std::mutex > lock( *mutexDebugLog ) ;

        // buffer if we haven't opened the log yet
        if ( fileout == nullptr ) {
            ret = strTimestamped.length() ;
            messagesBeforeOpenLog.push_back( strTimestamped ) ;
        }
        else
        {
            boost::filesystem::path pathToDebugLog = GetDirForData() / LOG_FILE_NAME ;
            if ( ! boost::filesystem::exists( pathToDebugLog ) ) // log file is absent
                fReopenDebugLog = true ;

            if ( fReopenDebugLog ) {
                fReopenDebugLog = false ;
                FILE * outfile = freopen( pathToDebugLog.string().c_str (), "a", fileout.release() ) ;
                if ( outfile != nullptr ) {
                    setbuf( outfile, NULL ) ; // unbuffered
                    fileout.reset( outfile );
                }
            }

            ret = FileWriteStr( strTimestamped, fileout ) ;
        }
    }
    return ret ;
}

static std::string FormatException( const std::exception * pex, const char * thread )
{
#ifdef WIN32
    char moduleName[ MAX_PATH ] = "" ;
    GetModuleFileNameA( NULL, moduleName, sizeof( moduleName ) ) ;
#else
    const char * moduleName = "dogecoin" ;
#endif
    if ( pex != nullptr )
        return strprintf(
            "EXCEPTION: %s       \n%s       \n%s in %s       \n", typeid( *pex ).name(), pex->what(), moduleName, thread ) ;
    else
        return strprintf(
            "UNKNOWN EXCEPTION       \n%s in %s       \n", moduleName, thread ) ;
}

void PrintExceptionContinue( const std::exception * pex, const char * thread )
{
    std::string message = FormatException( pex, thread ) ;
    LogPrintf( "\n\n************************\n%s\n", message ) ;
    fprintf( stderr, "\n\n************************\n%s\n", message.c_str() ) ;
}

void ShrinkLogFile()
{
    // Amount of log to save at end when shrinking
    constexpr size_t RECENT_DEBUG_HISTORY_SIZE = 10 * 1000000 ;
    // Scroll log if it's getting too big
    boost::filesystem::path pathLog = GetDirForData() / LOG_FILE_NAME ;
    FILE* file = fopen( pathLog.string().c_str(), "r" ) ;
    // If log file is more than 10% bigger the RECENT_DEBUG_HISTORY_SIZE
    // trim it down by saving only the last RECENT_DEBUG_HISTORY_SIZE bytes
    if ( file && boost::filesystem::file_size( pathLog ) > 11 * ( RECENT_DEBUG_HISTORY_SIZE / 10 ) )
    {
        // Restart the file with some of the end
        std::vector< char > vch( RECENT_DEBUG_HISTORY_SIZE, 0 ) ;
        fseek( file, - (long)vch.size(), SEEK_END ) ;
        int nBytes = fread( vch.data(), 1, vch.size(), file ) ;
        fclose( file ) ;

        file = fopen( pathLog.string().c_str(), "w" ) ;
        if ( file ) {
            fwrite( vch.data(), 1, nBytes, file ) ;
            fclose( file ) ;
        }
    }
    else if ( file != nullptr )
        fclose( file ) ;
}
