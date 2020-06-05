// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "util.h"

#include "chainparamsbase.h"
#include "random.h"
#include "serialize.h"
#include "sync.h"

#include "utillog.h"
#include "utilstr.h"
#include "utilstrencodings.h"

#ifndef WIN32
// for posix_fallocate
#ifdef __linux__

#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif

#define _POSIX_C_SOURCE 200112L

#endif // __linux__

#include <algorithm>
#include <mutex>
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

#if defined(HAVE_CONFIG_H)
#include "config/dogecoin-config.h"
#endif

#ifdef HAVE_MALLOPT_ARENA_MAX
#include <malloc.h>
#endif

#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp> // for starts_with() and ends_with()
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/program_options/detail/config_file.hpp>

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

const std::string DOGECOIN_CONF_FILENAME = "dogecoin.conf" ;
const std::string BITCOIN_PID_FILENAME = "dogecoind.pid" ;

CCriticalSection cs_args ;
std::map< std::string, std::string > mapArgs ;
static std::map< std::string, std::vector< std::string > > _mapMultiArgs ;
const std::map< std::string, std::vector< std::string > > & mapMultiArgs = _mapMultiArgs ;

bool fDebug = false ;

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
instance_of_cinit ;

/** Interpret string as boolean, for argument parsing */
static bool InterpretBool( const std::string & boolStr )
{
    if ( boolStr.empty() ) return true ;
    return atoi( boolStr ) != 0 ;
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

        // Interpret --foo as -foo
        // If both --foo and -foo are set, the last takes effect
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

bool RenameOver( const boost::filesystem::path & src, const boost::filesystem::path & dest )
{
#ifdef WIN32
    return MoveFileExA(src.string().c_str(), dest.string().c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
#else
    int rc = std::rename(src.string().c_str(), dest.string().c_str());
    return (rc == 0);
#endif
}

bool TryToCreateDirectory( const boost::filesystem::path & p )
{
    try {
        return boost::filesystem::create_directory( p ) ;
    } catch ( const boost::filesystem::filesystem_error & ) {
        if ( ! boost::filesystem::exists( p ) || ! boost::filesystem::is_directory( p ) )
            throw ;
    }

    // create_directory didn't create the directory, it had to have existed already
    return false ;
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

void runCommand( const std::string & strCommand )
{
    int nErr = ::system( strCommand.c_str() ) ;
    if ( nErr != 0 )
        LogPrintf( "runCommand error: system( %s ) returned %d\n", strCommand, nErr ) ;
}

void SetupEnvironment()
{
#ifdef HAVE_MALLOPT_ARENA_MAX
    // glibc-specific: On 32-bit systems set the number of arenas to 1.
    // By default, since glibc 2.10, the C library will create up to two heap
    // arenas per core. This is known to cause excessive virtual address space
    // usage in our case. Work around it by setting the maximum number of
    // arenas to 1
    if ( sizeof( void* ) == 4 ) {
        mallopt( M_ARENA_MAX, 1 ) ;
    }
#endif
    // On most POSIX systems (e.g. Linux, but not BSD) the environment's locale
    // may be invalid, in which case the "C" locale is used as fallback
#if !defined(WIN32) && !defined(MAC_OSX) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
    try {
        std::locale( "" ); // Raises a runtime error if current locale is invalid
    } catch ( const std::runtime_error & ) {
        setenv( "LC_ALL", "C", 1 ) ;
    }
#endif
    // The path locale is lazy initialized and to avoid deinitialization errors
    // in multithreading environments, it is set explicitly by the main thread.
    // A dummy locale is used to extract the internal default locale, used by
    // boost::filesystem::path, which is then used to explicitly imbue the path
    std::locale loc = boost::filesystem::path::imbue( std::locale::classic() ) ;
    boost::filesystem::path::imbue( loc ) ;
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

std::string CopyrightHolders( const std::string & prefix )
{
    std::string copyrightHolders = prefix + strprintf( _(COPYRIGHT_HOLDERS), _(COPYRIGHT_HOLDERS_SUBSTITUTION) ) ;

    // look for untranslated substitution to make sure Bitcoin Core & Dogecoin Core copyright is not removed by accident
    if ( strprintf( COPYRIGHT_HOLDERS, COPYRIGHT_HOLDERS_SUBSTITUTION ).find( "Bitcoin Core" ) == std::string::npos
            || strprintf( COPYRIGHT_HOLDERS, COPYRIGHT_HOLDERS_SUBSTITUTION ).find( "Dogecoin Core" ) == std::string::npos ) {
        copyrightHolders += "\n" + prefix + "The Bitcoin Core and Dogecoin Core developers" ;
    }

    return copyrightHolders ;
}
