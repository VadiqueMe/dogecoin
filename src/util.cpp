// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#if defined(HAVE_CONFIG_H)
#include "config/dogecoin-config.h"
#endif

#include "util.h"

#include "chainparamsbase.h"
#include "random.h"
#include "serialize.h"
#include "sync.h"
#include "utilstrencodings.h"
#include "utiltime.h"

#include <stdarg.h>

#if (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
#include <pthread.h>
#include <pthread_np.h>
#endif

#ifndef WIN32
// for posix_fallocate
#ifdef __linux__

#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif

#define _POSIX_C_SOURCE 200112L

#endif // __linux__

#include <algorithm>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>

#else

#ifdef _MSC_VER
#pragma warning(disable:4786)
#pragma warning(disable:4804)
#pragma warning(disable:4805)
#pragma warning(disable:4717)
#endif

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501

#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501

#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <io.h> /* for _commit */
#include <shlobj.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#ifdef HAVE_MALLOPT_ARENA_MAX
#include <malloc.h>
#endif

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp> // for startswith() and endswith()
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options/detail/config_file.hpp>
#include <boost/thread.hpp>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/conf.h>

std::string FormatBytes( uint64_t bytes )
{
    if ( bytes < 1024 )
        return std::to_string( bytes ) + " B" ;
    if ( bytes < 1024 * 1024 )
        return std::to_string( bytes / 1024 ) + " KiB" ;
    if ( bytes < 1024 * 1024 * 1024 )
        return std::to_string( bytes / 1024 / 1024 ) + " MiB" ;

    return std::to_string( bytes / 1024 / 1024 / 1024 ) + " GiB" ;
}

const char * const DOGECOIN_CONF_FILENAME = "dogecoin.conf";
const char * const BITCOIN_PID_FILENAME = "dogecoind.pid";

const char * const LOG_FILE_NAME = "debug.log" ;

CCriticalSection cs_args ;
std::map< std::string, std::string > mapArgs ;
static std::map< std::string, std::vector< std::string > > _mapMultiArgs ;
const std::map< std::string, std::vector< std::string > > & mapMultiArgs = _mapMultiArgs ;

bool fDebug = false;
bool fPrintToConsole = false;
bool fPrintToDebugLog = true;

bool fLogTimestamps = DEFAULT_LOGTIMESTAMPS;
bool fLogTimeMicros = DEFAULT_LOGTIMEMICROS;
bool fLogIPs = DEFAULT_LOGIPS ;
std::atomic < bool > fReopenDebugLog( false ) ;
CTranslationInterface translationInterface;

/** Init OpenSSL library multithreading support */
static CCriticalSection** ppmutexOpenSSL;
void locking_callback(int mode, int i, const char* file, int line) NO_THREAD_SAFETY_ANALYSIS
{
    if (mode & CRYPTO_LOCK) {
        ENTER_CRITICAL_SECTION(*ppmutexOpenSSL[i]);
    } else {
        LEAVE_CRITICAL_SECTION(*ppmutexOpenSSL[i]);
    }
}

// Init
class CInit
{
public:
    CInit()
    {
        // Init OpenSSL library multithreading support
        ppmutexOpenSSL = (CCriticalSection**)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(CCriticalSection*));
        for (int i = 0; i < CRYPTO_num_locks(); i++)
            ppmutexOpenSSL[i] = new CCriticalSection();
        CRYPTO_set_locking_callback(locking_callback);

        // OpenSSL can optionally load a config file which lists optional loadable modules and engines.
        // We don't use them so we don't require the config. However some of our libs may call functions
        // which attempt to load the config file, possibly resulting in an exit() or crash if it is missing
        // or corrupt. Explicitly tell OpenSSL not to try to load the file. The result for our libs will be
        // that the config appears to have been loaded and there are no modules/engines available.
        OPENSSL_no_config();

#ifdef WIN32
        // Seed OpenSSL PRNG with current contents of the screen
        RAND_screen();
#endif

        // Seed OpenSSL PRNG with performance counter
        RandAddSeed();
    }
    ~CInit()
    {
        // Securely erase the memory used by the PRNG
        RAND_cleanup();
        // Shutdown OpenSSL library multithreading support
        CRYPTO_set_locking_callback(NULL);
        for (int i = 0; i < CRYPTO_num_locks(); i++)
            delete ppmutexOpenSSL[i];
        OPENSSL_free(ppmutexOpenSSL);
    }
}
instance_of_cinit;

static boost::once_flag debugPrintInitFlag = BOOST_ONCE_INIT ;

static std::unique_ptr< FILE > fileout( nullptr ) ;
static std::unique_ptr< boost::mutex > mutexDebugLog( nullptr );

static std::list< std::string > messagesBeforeOpenLog ;

static int FileWriteStr( const std::string & str, const std::unique_ptr< FILE > & fp )
{
    return fwrite( str.data(), 1, str.size(), fp.get() ) ;
}

static void DebugPrintInit()
{
    if ( mutexDebugLog == nullptr )
        mutexDebugLog.reset( new boost::mutex() ) ;
}

void OpenDebugLog()
{
    boost::call_once( &DebugPrintInit, debugPrintInitFlag ) ;
    boost::mutex::scoped_lock scoped_lock( *mutexDebugLog ) ;

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

bool LogAcceptCategory( const char * category )
{
    if ( category != nullptr ) // if ( false )
    {
        if (!fDebug)
            return false;

        // Give each thread quick access to -debug settings.
        // This helps prevent issues debugging global destructors,
        // where mapMultiArgs might be deleted before another
        // global destructor calls LogPrint()
        static boost::thread_specific_ptr< std::set< std::string > > ptrCategory ;
        if (ptrCategory.get() == NULL)
        {
            if ( mapMultiArgs.count( "-debug" ) ) {
                const std::vector< std::string > & categories = mapMultiArgs.at( "-debug" ) ;
                ptrCategory.reset( new std::set< std::string >( categories.begin(), categories.end() ) ) ;
                // thread_specific_ptr automatically deletes the set when the thread ends
            } else
                ptrCategory.reset( new std::set< std::string >() ) ;
        }
        const std::set< std::string > & setCategories = *ptrCategory.get() ;

        // if not debugging everything and not debugging specific category, LogPrint does nothing
        if (setCategories.count( std::string("") ) == 0 &&
            setCategories.count( std::string("1") ) == 0 &&
            setCategories.count( std::string( category ) ) == 0)
            return false;
    }
    return true;
}

/**
 * fStartedNewLine is a state variable held by the calling context that will
 * suppress printing of the timestamp when multiple calls are made that don't
 * end in a newline. Initialize it to true, and hold it, in the calling context
 */
static std::string LogTimestampStr( const std::string & str, std::atomic_bool * fStartedNewLine )
{
    std::string strStamped ;

    if (!fLogTimestamps)
        return str;

    if (*fStartedNewLine) {
        int64_t nTimeMicros = GetLogTimeMicros();
        strStamped = DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nTimeMicros/1000000);
        if (fLogTimeMicros)
            strStamped += strprintf(".%06d", nTimeMicros%1000000);
        strStamped += ' ' + str;
    } else
        strStamped = str;

    if (!str.empty() && str[str.size()-1] == '\n')
        *fStartedNewLine = true;
    else
        *fStartedNewLine = false;

    return strStamped;
}

int LogPrintStr( const std::string & str )
{
    int ret = 0 ; // number of characters written
    static std::atomic_bool fStartedNewLine( true ) ;

    std::string strTimestamped = LogTimestampStr( str, &fStartedNewLine ) ;

    if ( fPrintToConsole )
    {
        ret = fwrite( strTimestamped.data(), 1, strTimestamped.size(), stdout ) ;
        fflush( stdout ) ;
    }
    else if ( fPrintToDebugLog )
    {
        boost::call_once( &DebugPrintInit, debugPrintInitFlag ) ;
        boost::mutex::scoped_lock scoped_lock( *mutexDebugLog ) ;

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

/** Interpret string as boolean, for argument parsing */
static bool InterpretBool(const std::string& strValue)
{
    if (strValue.empty())
        return true;
    return (atoi(strValue) != 0);
}

/** Turn -noX into -X=0 */
static void InterpretNegativeSetting(std::string& strKey, std::string& strValue)
{
    if (strKey.length()>3 && strKey[0]=='-' && strKey[1]=='n' && strKey[2]=='o')
    {
        strKey = "-" + strKey.substr(3);
        strValue = InterpretBool(strValue) ? "0" : "1";
    }
}

void ParseParameters(int argc, const char* const argv[])
{
    LOCK(cs_args);
    mapArgs.clear();
    _mapMultiArgs.clear();

    for (int i = 1; i < argc; i++)
    {
        std::string str(argv[i]);
        std::string strValue;
        size_t is_index = str.find('=');
        if (is_index != std::string::npos)
        {
            strValue = str.substr(is_index+1);
            str = str.substr(0, is_index);
        }
#ifdef WIN32
        boost::to_lower(str);
        if (boost::algorithm::starts_with(str, "/"))
            str = "-" + str.substr(1);
#endif

        if (str[0] != '-')
            break;

        // Interpret --foo as -foo.
        // If both --foo and -foo are set, the last takes effect.
        if (str.length() > 1 && str[1] == '-')
            str = str.substr(1);
        InterpretNegativeSetting(str, strValue);

        mapArgs[str] = strValue;
        _mapMultiArgs[str].push_back(strValue);
    }
}

bool IsArgSet(const std::string& strArg)
{
    LOCK(cs_args);
    return mapArgs.count(strArg);
}

std::string GetArg(const std::string& strArg, const std::string& strDefault)
{
    LOCK(cs_args);
    if (mapArgs.count(strArg))
        return mapArgs[strArg];
    return strDefault;
}

int64_t GetArg(const std::string& strArg, int64_t nDefault)
{
    LOCK(cs_args);
    if (mapArgs.count(strArg))
        return atoi64(mapArgs[strArg]);
    return nDefault;
}

bool GetBoolArg(const std::string& strArg, bool fDefault)
{
    LOCK(cs_args);
    if (mapArgs.count(strArg))
        return InterpretBool(mapArgs[strArg]);
    return fDefault;
}

bool SoftSetArg(const std::string& strArg, const std::string& strValue)
{
    LOCK(cs_args);
    if ( mapArgs.count(strArg) != 0 ) return false ;
    mapArgs[strArg] = strValue;
    return true ;
}

bool SoftSetBoolArg(const std::string& strArg, bool fValue)
{
    if (fValue)
        return SoftSetArg(strArg, std::string("1"));
    else
        return SoftSetArg(strArg, std::string("0"));
}

void ForceSetArg(const std::string& strArg, const std::string& strValue)
{
    LOCK(cs_args);
    mapArgs[strArg] = strValue;
}



static const int screenWidth = 79;
static const int optIndent = 2;
static const int msgIndent = 7;

std::string HelpMessageGroup(const std::string &message) {
    return std::string(message) + std::string("\n\n");
}

std::string HelpMessageOpt(const std::string &option, const std::string &message) {
    return std::string(optIndent,' ') + std::string(option) +
           std::string("\n") + std::string(msgIndent,' ') +
           FormatParagraph(message, screenWidth - msgIndent, msgIndent) +
           std::string("\n\n");
}

static std::string FormatException(const std::exception* pex, const char* pszThread)
{
#ifdef WIN32
    char pszModule[MAX_PATH] = "";
    GetModuleFileNameA(NULL, pszModule, sizeof(pszModule));
#else
    const char* pszModule = "dogecoin";
#endif
    if (pex)
        return strprintf(
            "EXCEPTION: %s       \n%s       \n%s in %s       \n", typeid(*pex).name(), pex->what(), pszModule, pszThread);
    else
        return strprintf(
            "UNKNOWN EXCEPTION       \n%s in %s       \n", pszModule, pszThread);
}

void PrintExceptionContinue(const std::exception* pex, const char* pszThread)
{
    std::string message = FormatException(pex, pszThread);
    LogPrintf("\n\n************************\n%s\n", message);
    fprintf(stderr, "\n\n************************\n%s\n", message.c_str());
}

boost::filesystem::path GetDefaultDataDir()
{
    // Windows: C:\Users\Username\AppData\Roaming\Dogecoin
    // Mac: ~/Library/Application Support/Dogecoin
    // Unix: ~/.dogecoin
#ifdef WIN32
    // Windows
    return GetSpecialFolderPath( CSIDL_APPDATA ) / "Dogecoin" ;
#else
    boost::filesystem::path pathRet ;
    char* pszHome = getenv( "HOME" ) ;
    if ( pszHome == NULL || strlen( pszHome ) == 0 )
        pathRet = boost::filesystem::path( "/" ) ;
    else
        pathRet = boost::filesystem::path( pszHome ) ;
#ifdef MAC_OSX
    // Mac
    return pathRet / "Library/Application Support/Dogecoin";
#else
    // Unix
    return pathRet / ".dogecoin";
#endif
#endif
}

static boost::filesystem::path pathCached ;
static boost::filesystem::path pathCachedNetSpecific ;
static CCriticalSection csPathCached ;

const boost::filesystem::path & GetDirForData( bool fNetSpecific )
{
    LOCK( csPathCached ) ;

    boost::filesystem::path & path = fNetSpecific ? pathCachedNetSpecific : pathCached ;

    // This can be called during exceptions by LogPrintf(), so we cache the
    // value so we don't have to do memory allocations after that
    if ( ! path.empty() )
        return path ;

    if ( IsArgSet( "-datadir" ) ) {
        path = boost::filesystem::system_complete( GetArg( "-datadir", "" ) ) ;
        if ( ! boost::filesystem::is_directory( path ) ) {
            path = "" ;
            return path ;
        }
    } else {
        path = GetDefaultDataDir() ;
    }
    if ( fNetSpecific )
        path /= BaseParams().DirForData() ;

    boost::filesystem::create_directories( path ) ;

    return path ;
}

void ClearDatadirCache()
{
    LOCK(csPathCached);

    pathCached = boost::filesystem::path();
    pathCachedNetSpecific = boost::filesystem::path();
}

boost::filesystem::path GetConfigFile( const std::string & confPath )
{
    boost::filesystem::path pathConfigFile( confPath ) ;
    if ( ! pathConfigFile.is_complete() )
        pathConfigFile = GetDirForData( false ) / pathConfigFile ;

    return pathConfigFile ;
}

void ReadConfigFile( const std::string & confPath )
{
    boost::filesystem::ifstream streamConfig( GetConfigFile( confPath ) ) ;
    if ( ! streamConfig.good() )
        return ; // no dogecoin.conf file

    {
        LOCK(cs_args);
        std::set< std::string > setOptions ;
        setOptions.insert("*");

        for ( boost::program_options::detail::config_file_iterator it( streamConfig, setOptions ), end ; it != end ; ++ it )
        {
            // Don't overwrite existing settings so command line settings override dogecoin.conf
            std::string strKey = std::string( "-" ) + it->string_key ;
            std::string strValue = it->value[ 0 ] ;
            InterpretNegativeSetting(strKey, strValue);
            if (mapArgs.count(strKey) == 0)
                mapArgs[strKey] = strValue;
            _mapMultiArgs[strKey].push_back(strValue);
        }
    }
    // If datadir is changed in .conf file:
    ClearDatadirCache();
}

#ifndef WIN32
boost::filesystem::path GetPidFile()
{
    boost::filesystem::path pathPidFile( GetArg( "-pid", BITCOIN_PID_FILENAME ) ) ;
    if ( ! pathPidFile.is_complete() ) pathPidFile = GetDirForData() / pathPidFile ;
    return pathPidFile ;
}

void CreatePidFile(const boost::filesystem::path &path, pid_t pid)
{
    FILE* file = fopen(path.string().c_str(), "w");
    if (file)
    {
        fprintf(file, "%d\n", pid);
        fclose(file);
    }
}
#endif

bool RenameOver(boost::filesystem::path src, boost::filesystem::path dest)
{
#ifdef WIN32
    return MoveFileExA(src.string().c_str(), dest.string().c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
#else
    int rc = std::rename(src.string().c_str(), dest.string().c_str());
    return (rc == 0);
#endif /* WIN32 */
}

/**
 * Ignores exceptions thrown by Boost's create_directory if the requested directory exists.
 * Specifically handles case where path p exists, but it wasn't possible for the user to
 * write to the parent directory.
 */
bool TryCreateDirectory(const boost::filesystem::path& p)
{
    try
    {
        return boost::filesystem::create_directory(p);
    } catch (const boost::filesystem::filesystem_error&) {
        if (!boost::filesystem::exists(p) || !boost::filesystem::is_directory(p))
            throw;
    }

    // create_directory didn't create the directory, it had to have existed already
    return false;
}

void FileCommit(FILE *file)
{
    fflush(file); // harmless if redundantly called
#ifdef WIN32
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(file));
    FlushFileBuffers(hFile);
#else
    #if defined(__linux__) || defined(__NetBSD__)
    fdatasync(fileno(file));
    #elif defined(__APPLE__) && defined(F_FULLFSYNC)
    fcntl(fileno(file), F_FULLFSYNC, 0);
    #else
    fsync(fileno(file));
    #endif
#endif
}

bool TruncateFile(FILE *file, unsigned int length) {
#if defined(WIN32)
    return _chsize(_fileno(file), length) == 0;
#else
    return ftruncate(fileno(file), length) == 0;
#endif
}

/**
 * this function tries to raise the file descriptor limit to the requested number.
 * It returns the actual file descriptor limit (which may be more or less than nMinFD)
 */
int RaiseFileDescriptorLimit(int nMinFD) {
#if defined(WIN32)
    return 2048;
#else
    struct rlimit limitFD;
    if (getrlimit(RLIMIT_NOFILE, &limitFD) != -1) {
        if (limitFD.rlim_cur < (rlim_t)nMinFD) {
            limitFD.rlim_cur = nMinFD;
            if (limitFD.rlim_cur > limitFD.rlim_max)
                limitFD.rlim_cur = limitFD.rlim_max;
            setrlimit(RLIMIT_NOFILE, &limitFD);
            getrlimit(RLIMIT_NOFILE, &limitFD);
        }
        return limitFD.rlim_cur;
    }
    return nMinFD; // getrlimit failed, assume it's fine
#endif
}

/**
 * this function tries to make a particular range of a file allocated (corresponding to disk space)
 * it is advisory, and the range specified in the arguments will never contain live data
 */
void AllocateFileRange(FILE *file, unsigned int offset, unsigned int length) {
#if defined(WIN32)
    // Windows-specific version
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(file));
    LARGE_INTEGER nFileSize;
    int64_t nEndPos = (int64_t)offset + length;
    nFileSize.u.LowPart = nEndPos & 0xFFFFFFFF;
    nFileSize.u.HighPart = nEndPos >> 32;
    SetFilePointerEx(hFile, nFileSize, 0, FILE_BEGIN);
    SetEndOfFile(hFile);
#elif defined(MAC_OSX)
    // OSX specific version
    fstore_t fst;
    fst.fst_flags = F_ALLOCATECONTIG;
    fst.fst_posmode = F_PEOFPOSMODE;
    fst.fst_offset = 0;
    fst.fst_length = (off_t)offset + length;
    fst.fst_bytesalloc = 0;
    if (fcntl(fileno(file), F_PREALLOCATE, &fst) == -1) {
        fst.fst_flags = F_ALLOCATEALL;
        fcntl(fileno(file), F_PREALLOCATE, &fst);
    }
    ftruncate(fileno(file), fst.fst_length);
#elif defined(__linux__)
    // Version using posix_fallocate
    off_t nEndPos = (off_t)offset + length;
    posix_fallocate(fileno(file), 0, nEndPos);
#else
    // Fallback version
    // TODO: just write one byte per block
    static const char buf[65536] = {};
    fseek(file, offset, SEEK_SET);
    while (length > 0) {
        unsigned int now = 65536;
        if (length < now)
            now = length;
        fwrite(buf, 1, now, file); // allowed to fail; this function is advisory anyway
        length -= now;
    }
#endif
}

void ShrinkLogFile()
{
    // Amount of log to save at end when shrinking
    constexpr size_t RECENT_DEBUG_HISTORY_SIZE = 10 * 1000000;
    // Scroll log if it's getting too big
    boost::filesystem::path pathLog = GetDirForData() / LOG_FILE_NAME ;
    FILE* file = fopen(pathLog.string().c_str(), "r");
    // If log file is more than 10% bigger the RECENT_DEBUG_HISTORY_SIZE
    // trim it down by saving only the last RECENT_DEBUG_HISTORY_SIZE bytes
    if (file && boost::filesystem::file_size(pathLog) > 11 * (RECENT_DEBUG_HISTORY_SIZE / 10))
    {
        // Restart the file with some of the end
        std::vector<char> vch(RECENT_DEBUG_HISTORY_SIZE, 0);
        fseek(file, -((long)vch.size()), SEEK_END);
        int nBytes = fread(vch.data(), 1, vch.size(), file);
        fclose(file);

        file = fopen(pathLog.string().c_str(), "w");
        if (file)
        {
            fwrite(vch.data(), 1, nBytes, file);
            fclose(file);
        }
    }
    else if (file != NULL)
        fclose(file);
}

#ifdef WIN32
boost::filesystem::path GetSpecialFolderPath( int nFolder, bool fCreate )
{
    char pszPath[ MAX_PATH ] = "" ;
    if ( SHGetSpecialFolderPathA( NULL, pszPath, nFolder, fCreate ) )
        return boost::filesystem::path( pszPath ) ;

    LogPrintf( "%s: SHGetSpecialFolderPathA() failed, could not obtain requested path\n", __func__ ) ;
    return boost::filesystem::path( "" ) ;
}
#endif

void runCommand(const std::string& strCommand)
{
    int nErr = ::system(strCommand.c_str());
    if (nErr)
        LogPrintf("runCommand error: system(%s) returned %d\n", strCommand, nErr);
}

void RenameThread( const std::string & name )
{
#if defined(PR_SET_NAME)
    // Only the first 15 characters are used (16 - NUL terminator)
    ::prctl( PR_SET_NAME, name.c_str(), 0, 0, 0 ) ;
#elif (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
    pthread_set_name_np( pthread_self(), name.c_str() ) ;
#elif defined(MAC_OSX)
    pthread_setname_np( name.c_str() ) ;
#else
    ( void ) name ; // not used
#endif
}

void SetupEnvironment()
{
#ifdef HAVE_MALLOPT_ARENA_MAX
    // glibc-specific: On 32-bit systems set the number of arenas to 1.
    // By default, since glibc 2.10, the C library will create up to two heap
    // arenas per core. This is known to cause excessive virtual address space
    // usage in our usage. Work around it by setting the maximum number of
    // arenas to 1.
    if (sizeof(void*) == 4) {
        mallopt(M_ARENA_MAX, 1);
    }
#endif
    // On most POSIX systems (e.g. Linux, but not BSD) the environment's locale
    // may be invalid, in which case the "C" locale is used as fallback.
#if !defined(WIN32) && !defined(MAC_OSX) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
    try {
        std::locale(""); // Raises a runtime error if current locale is invalid
    } catch (const std::runtime_error&) {
        setenv("LC_ALL", "C", 1);
    }
#endif
    // The path locale is lazy initialized and to avoid deinitialization errors
    // in multithreading environments, it is set explicitly by the main thread.
    // A dummy locale is used to extract the internal default locale, used by
    // boost::filesystem::path, which is then used to explicitly imbue the path.
    std::locale loc = boost::filesystem::path::imbue(std::locale::classic());
    boost::filesystem::path::imbue(loc);
}

bool SetupNetworking()
{
#ifdef WIN32
    // Initialize Windows Sockets
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (ret != NO_ERROR || LOBYTE(wsadata.wVersion ) != 2 || HIBYTE(wsadata.wVersion) != 2)
        return false;
#endif
    return true;
}

int GetNumCores()
{
    return boost::thread::physical_concurrency() ;
}

std::string CopyrightHolders(const std::string& strPrefix)
{
    std::string strCopyrightHolders = strPrefix + strprintf(_(COPYRIGHT_HOLDERS), _(COPYRIGHT_HOLDERS_SUBSTITUTION));

    // look for untranslated substitution to make sure Bitcoin Core & Dogecoin Core copyright is not removed by accident
    if ( strprintf(COPYRIGHT_HOLDERS, COPYRIGHT_HOLDERS_SUBSTITUTION).find("Bitcoin Core") == std::string::npos
            || strprintf(COPYRIGHT_HOLDERS, COPYRIGHT_HOLDERS_SUBSTITUTION).find("Dogecoin Core") == std::string::npos ) {
        strCopyrightHolders += "\n" + strPrefix + "The Bitcoin Core and Dogecoin Core developers";
    }

    return strCopyrightHolders;
}
