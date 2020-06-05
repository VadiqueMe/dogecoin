// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "db.h"

#include "addrman.h"
#include "hash.h"
#include "protocol.h"
#include "util.h"
#include "utiltime.h"
#include "utilstrencodings.h"

#include <stdint.h>

#ifndef WIN32
#include <sys/stat.h>
#endif

#include <boost/filesystem.hpp>


//
// CDB
//

CDBEnv walletdb ;

void CDBEnv::EnvShutdown()
{
    if ( ! fDbEnvInitOnce ) return ;

    fDbEnvInitOnce = false ;
    int ret = dbenv->close(0);
    if (ret != 0)
        LogPrintf("CDBEnv::EnvShutdown: Error %d shutting down database environment: %s\n", ret, DbEnv::strerror(ret));
    if ( ! isMockDb )
        DbEnv((u_int32_t)0).remove(strPath.c_str(), 0);
}

void CDBEnv::Reset()
{
    delete dbenv ;
    dbenv = new DbEnv( DB_CXX_NO_EXCEPTIONS ) ;
    fDbEnvInitOnce = false ;
    fDbEnvFinished = false ;
    isMockDb = false ;
}

CDBEnv::CDBEnv() : dbenv( nullptr )
{
    Reset() ;
}

CDBEnv::~CDBEnv()
{
    EnvShutdown() ;
    delete dbenv ;
    dbenv = nullptr ;
}

void CDBEnv::Close()
{
    EnvShutdown() ;
}

bool CDBEnv::Open( const boost::filesystem::path & pathIn )
{
    if ( fDbEnvInitOnce ) return true ;

    if ( fDbEnvFinished ) {
        LogPrintf( "CDBEnv::%s( \"%s\" ): stopping\n", __func__, pathIn.string() ) ;
        throw new std::string( "stopthread" ) ;
    }

    strPath = pathIn.string() ;
    boost::filesystem::path pathToLogDir = pathIn / "database" ;
    TryToCreateDirectory( pathToLogDir ) ;
    boost::filesystem::path pathToErrorFile = pathIn / "db.errfile" ;
    LogPrintf( "%s: LogDir=%s ErrorFile=%s\n", __func__, pathToLogDir.string(), pathToErrorFile.string() ) ;

    unsigned int nEnvFlags = 0 ;
    if ( GetBoolArg( "-privdb", DEFAULT_WALLET_PRIVDB ) )
        nEnvFlags |= DB_PRIVATE ;

    dbenv->set_lg_dir( pathToLogDir.string().c_str() ) ;
    dbenv->set_cachesize( 0, 0x100000, 1 ) ; // 1 MiB is expected to be enough for just the wallet
    dbenv->set_lg_bsize( 0x10000 ) ;
    dbenv->set_lg_max( 1048576 ) ;
    dbenv->set_lk_max_locks( 40000 ) ;
    dbenv->set_lk_max_objects( 40000 ) ;
    dbenv->set_errfile( fopen( pathToErrorFile.string().c_str(), "a" ) ) ;
    dbenv->set_flags( DB_AUTO_COMMIT, 1 ) ;
    dbenv->set_flags( DB_TXN_WRITE_NOSYNC, 1 ) ;
    dbenv->log_set_config( DB_LOG_AUTO_REMOVE, 1 ) ;
    int ret = dbenv->open( strPath.c_str(),
                             DB_CREATE |
                             DB_INIT_LOCK |
                             DB_INIT_LOG |
                             DB_INIT_MPOOL |
                             DB_INIT_TXN |
                             DB_THREAD |
                             DB_RECOVER |
                             nEnvFlags,
                         S_IRUSR | S_IWUSR ) ;
    if ( ret != 0 )
        return error( "%s: Error %d opening database environment: %s\n", __func__, ret, DbEnv::strerror( ret ) ) ;

    fDbEnvInitOnce = true ;
    isMockDb = false ;
    return true ;
}

void CDBEnv::MakeMockDB()
{
    if ( fDbEnvInitOnce )
        throw std::runtime_error( strprintf( "%s: this CDBEnv is already initialized", __func__ ) ) ;

    if ( fDbEnvFinished ) {
        LogPrintf( "CDBEnv::%s(): stopping\n", __func__ ) ;
        throw new std::string( "stopthread" ) ;
    }

    LogPrintf( strprintf( "CDBEnv::%s\n", __func__ ) ) ;

    dbenv->set_cachesize(1, 0, 1);
    dbenv->set_lg_bsize(10485760 * 4);
    dbenv->set_lg_max(10485760);
    dbenv->set_lk_max_locks(10000);
    dbenv->set_lk_max_objects(10000);
    dbenv->set_flags(DB_AUTO_COMMIT, 1);
    dbenv->log_set_config(DB_LOG_IN_MEMORY, 1);
    int ret = dbenv->open(NULL,
                         DB_CREATE |
                             DB_INIT_LOCK |
                             DB_INIT_LOG |
                             DB_INIT_MPOOL |
                             DB_INIT_TXN |
                             DB_THREAD |
                             DB_PRIVATE,
                         S_IRUSR | S_IWUSR);
    if ( ret > 0 )
        throw std::runtime_error( strprintf( "%s: Error %d opening database environment", __func__, ret ) ) ;

    fDbEnvInitOnce = true ;
    isMockDb = true ;
}

CDBEnv::VerifyResult CDBEnv::Verify( const std::string & dbFile, bool ( *recoverFunc )( CDBEnv & dbenv, const std::string & strFile ) )
{
    LOCK( cs_db ) ;
    assert( mapFileUseCount.count( dbFile ) == 0 ) ;

    Db db( dbenv, 0 ) ;
    int result = db.verify( dbFile.c_str(), NULL, NULL, 0 ) ;
    if ( result == 0 )
        return VERIFY_OK ;
    else if ( recoverFunc == NULL )
        return RECOVER_FAIL ;

    // Try to recover
    bool fRecovered = ( *recoverFunc )( *this, dbFile ) ;
    return ( fRecovered ? RECOVER_OK : RECOVER_FAIL ) ;
}

/* End of headers, beginning of key/value data */
static const char * HEADER_END = "HEADER=END" ;
/* End of key/value data */
static const char * DATA_END = "DATA=END" ;

bool CDBEnv::Salvage( const std::string & strFile, std::vector< CDBEnv::KeyValuePair > & vResult, bool fAggressive )
{
    LOCK( cs_db ) ;
    assert( mapFileUseCount.count( strFile ) == 0 ) ;

    u_int32_t flags = DB_SALVAGE ;
    if ( fAggressive )
        flags |= DB_AGGRESSIVE ;

    std::stringstream strDump ;

    Db db( dbenv, 0 ) ;
    int result = db.verify( strFile.c_str(), nullptr, &strDump, flags ) ;
    if ( result == DB_VERIFY_BAD ) {
        LogPrintf( "%s: Database salvage found errors, all data may not be recoverable\n", __func__ ) ;
        if ( ! fAggressive ) {
            LogPrintf( "%s: Rerun with aggressive=true to ignore errors and continue\n", __func__ ) ;
            return false ;
        }
    }
    if ( result != 0 && result != DB_VERIFY_BAD ) {
        LogPrintf( "%s: Database salvage failed with result %d\n", __func__, result ) ;
        return false ;
    }

    // Format of bdb dump is ascii lines:
    // header lines...
    // HEADER=END
    //  hexadecimal key
    //  hexadecimal value
    //  ... repeated
    // DATA=END

    std::string strLine ;
    while ( ! strDump.eof() && strLine != HEADER_END ) { // Skip past header
        std::getline( strDump, strLine ) ;
        LogPrintf( "%s: got header line \"%s\"\n", __func__, strLine ) ;
    }

    std::string keyHex, valueHex ;
    while ( ! strDump.eof() && keyHex != DATA_END ) {
        std::getline( strDump, keyHex ) ;
        LogPrintf( "%s: got key \"%s\"\n", __func__, keyHex ) ;
        if ( keyHex != DATA_END ) {
            if ( strDump.eof() )
                break ;
            std::getline( strDump, valueHex ) ;
            LogPrintf( "%s: got value \"%s\"\n", __func__, valueHex ) ;
            if ( valueHex == DATA_END ) {
                LogPrintf( "%s: Number of keys in data does not match number of values\n", __func__ ) ;
                break ;
            }
            vResult.push_back( std::make_pair( ParseHex( keyHex ), ParseHex( valueHex ) ) ) ;
        }
    }

    if ( keyHex != DATA_END ) {
        LogPrintf( "%s: Unexpected end of file while reading salvage output\n", __func__ ) ;
        return false ;
    }

    return ( result == 0 ) ;
}


void CDBEnv::CheckpointLSN( const std::string & strFile )
{
    dbenv->txn_checkpoint( 0, 0, 0 ) ;
    if ( isMockDb ) return ;
    dbenv->lsn_reset( strFile.c_str(), 0 ) ;
}


CDB::CDB( const std::string & strFilename, const char * mode, bool flushOnClose )
    : pdb( nullptr )
    , activeTxn( nullptr )
    , fFlushOnClose( flushOnClose )
{
    fReadOnly = ( ! strchr( mode, '+' ) && ! strchr( mode, 'w' ) ) ;

    if ( strFilename.empty() ) return ;

    bool fCreate = strchr( mode, 'c' ) != NULL ;
    unsigned int nFlags = DB_THREAD;
    if (fCreate)
        nFlags |= DB_CREATE;

    {
        LOCK( walletdb.cs_db ) ;
        if ( ! walletdb.Open( GetDirForData() ) )
            throw std::runtime_error( "CDB: Failed to open database environment" ) ;

        strFile = strFilename ;
        ++ walletdb.mapFileUseCount[ strFile ] ;
        pdb = walletdb.mapDb[ strFile ] ;
        if ( pdb == nullptr ) {
            pdb = new Db( walletdb.dbenv, 0 ) ;
            int ret ;

            bool fMockDb = walletdb.IsMockDB() ;
            if ( fMockDb ) {
                DbMpoolFile * mpf = pdb->get_mpf() ;
                ret = mpf->set_flags( DB_MPOOL_NOFILE, 1 ) ;
                if ( ret != 0 )
                    throw std::runtime_error( strprintf( "CDB: Failed to configure for no temp file backing for database %s", strFile ) ) ;
            }

            ret = pdb->open(NULL,                               // Txn pointer
                            fMockDb ? NULL : strFile.c_str(),   // Filename
                            fMockDb ? strFile.c_str() : "main", // Logical db name
                            DB_BTREE,                           // Database type
                            nFlags,                             // Flags
                            0);

            if ( ret != 0 ) {
                delete pdb ;
                pdb = nullptr ;
                -- walletdb.mapFileUseCount[ strFile ] ;
                strFile = "" ;
                throw std::runtime_error( strprintf( "CDB: Error %d, can't open database %s", ret, strFilename ) ) ;
            }

            if ( fCreate && ! Exists( std::string( "version" ) ) ) {
                bool fTmp = fReadOnly ;
                fReadOnly = false ;
                WriteVersion( PEER_VERSION ) ;
                fReadOnly = fTmp ;
            }

            walletdb.mapDb[ strFile ] = pdb ;
        }
    }
}

void CDB::Flush()
{
    if (activeTxn)
        return;

    // Flush database activity from memory pool to disk log
    unsigned int nMinutes = 0;
    if (fReadOnly)
        nMinutes = 1;

    walletdb.dbenv->txn_checkpoint( nMinutes ? GetArg( "-dblogsize", DEFAULT_WALLET_DBLOGSIZE ) * 1024 : 0, nMinutes, 0 ) ;
}

void CDB::Close()
{
    if (!pdb)
        return;
    if (activeTxn)
        activeTxn->abort();
    activeTxn = NULL;
    pdb = NULL;

    if (fFlushOnClose)
        Flush();

    {
        LOCK( walletdb.cs_db ) ;
        -- walletdb.mapFileUseCount[ strFile ] ;
    }
}

void CDBEnv::CloseDb( const std::string & strFile )
{
    LOCK( cs_db ) ;
    if ( mapDb[ strFile ] != nullptr ) {
        // Close the database handle
        Db* pdb = mapDb[ strFile ] ;
        pdb->close( 0 ) ;
        delete pdb ;
        mapDb[ strFile ] = nullptr ;
    }
}

bool CDBEnv::RemoveDb( const std::string & strFile )
{
    this->CloseDb( strFile ) ;

    LOCK(cs_db);
    int rc = dbenv->dbremove(NULL, strFile.c_str(), NULL, DB_AUTO_COMMIT);
    return (rc == 0);
}

bool CDB::Rewrite( const std::string & strFile, CDBEnv & dbenv, const char* pszSkip )
{
    while (true) {
        {
            LOCK( dbenv.cs_db ) ;
            if ( dbenv.mapFileUseCount.count( strFile ) == 0 || dbenv.mapFileUseCount[ strFile ] == 0 ) {
                // Flush data to the wallet file
                dbenv.CloseDb( strFile ) ;
                dbenv.CheckpointLSN( strFile ) ;
                dbenv.mapFileUseCount.erase( strFile ) ;

                std::string strFileRewrite = strFile + ".rewrite" ;
                LogPrintf( "%s: Rewriting %s as %s...\n", __func__, strFile, strFileRewrite ) ;
                bool fSuccess = true ;
                { // surround usage of db with extra { }
                    CDB db( strFile.c_str(), "r" ) ;
                    Db* pdbCopy = new Db( dbenv.dbenv, 0 ) ;

                    int ret = pdbCopy->open( nullptr,                // txn pointer
                                             strFileRewrite.c_str(), // filename
                                             "main",                 // logical db name
                                             DB_BTREE,               // database type
                                             DB_CREATE,              // flags
                                             0 ) ;
                    if ( ret > 0 ) {
                        LogPrintf( "%s: Can't create database file %s\n", __func__, strFileRewrite ) ;
                        fSuccess = false ;
                    }

                    Dbc* pcursor = db.GetCursor();
                    if ( pcursor != nullptr ) {
                        while (fSuccess) {
                            CDataStream ssKey( SER_DISK, PEER_VERSION ) ;
                            CDataStream ssValue( SER_DISK, PEER_VERSION ) ;
                            int ret1 = db.ReadAtCursor(pcursor, ssKey, ssValue);
                            if (ret1 == DB_NOTFOUND) {
                                pcursor->close();
                                break;
                            } else if (ret1 != 0) {
                                pcursor->close();
                                fSuccess = false;
                                break;
                            }
                            if (pszSkip &&
                                strncmp(ssKey.data(), pszSkip, std::min(ssKey.size(), strlen(pszSkip))) == 0)
                                continue;
                            if (strncmp(ssKey.data(), "\x07version", 8) == 0) {
                                // update version
                                ssValue.clear() ;
                                ssValue << PEER_VERSION ;
                            }
                            Dbt datKey( ssKey.data(), ssKey.size() ) ;
                            Dbt datValue( ssValue.data(), ssValue.size() ) ;
                            int ret2 = pdbCopy->put( nullptr, &datKey, &datValue, DB_NOOVERWRITE ) ;
                            if ( ret2 > 0 )
                                fSuccess = false ;
                        }
                    }
                    if ( fSuccess ) {
                        db.Close() ;
                        dbenv.CloseDb( strFile ) ;
                        if ( pdbCopy->close( 0 ) )
                            fSuccess = false ;
                        delete pdbCopy ;
                    }
                }
                if ( fSuccess ) {
                    Db dbA( dbenv.dbenv, 0 ) ;
                    if ( dbA.remove( strFile.c_str(), NULL, 0 ) )
                        fSuccess = false ;
                    Db dbB( dbenv.dbenv, 0 ) ;
                    if ( dbB.rename( strFileRewrite.c_str(), NULL, strFile.c_str(), 0 ) )
                        fSuccess = false ;
                }
                if ( ! fSuccess )
                    LogPrintf( "%s: Failed to rewrite wallet database file %s\n", __func__, strFileRewrite ) ;
                return fSuccess ;
            }
        }
        MilliSleep( 100 ) ;
    }
    return false ;
}


void CDBEnv::Flush( bool fShutdown )
{
    int64_t nStart = GetTimeMillis();
    // Flush log data to the actual data file on all files that are not in use
    LogPrint( "db", "CDBEnv::Flush( %s )%s\n", fShutdown ? "true" : "false", fDbEnvInitOnce ? "" : " database not started" ) ;
    if ( ! fDbEnvInitOnce ) return ;

    {
        LOCK( cs_db ) ;
        std::map< std::string, int >::iterator mi = mapFileUseCount.begin() ;
        while ( mi != mapFileUseCount.end() ) {
            std::string strFile = ( *mi ).first ;
            int nRefCount = ( *mi ).second ;
            LogPrint( "db", "CDBEnv::Flush: flushing %s (refcount = %d)...\n", strFile, nRefCount ) ;
            if ( nRefCount == 0 ) {
                // Flush data to the wallet file
                CloseDb( strFile ) ;
                LogPrint( "db", "CDBEnv::Flush: %s checkpoint\n", strFile ) ;
                dbenv->txn_checkpoint( 0, 0, 0 ) ;
                LogPrint( "db", "CDBEnv::Flush: %s detach\n", strFile ) ;
                if ( ! isMockDb )
                    dbenv->lsn_reset( strFile.c_str(), 0 ) ;
                LogPrint( "db", "CDBEnv::Flush: %s closed\n", strFile ) ;
                mapFileUseCount.erase( mi ++ ) ;
            } else
                mi ++ ;
        }
        LogPrint( "db", "CDBEnv::Flush( %s ) took %.3f s\n", fShutdown ? "true" : "false", 0.001 * ( GetTimeMillis() - nStart ) ) ;

        if ( fShutdown ) {
            char** listp ;
            if ( mapFileUseCount.empty() ) {
                dbenv->log_archive( &listp, DB_ARCH_REMOVE ) ;
                Close() ;
                if ( ! isMockDb )
                    boost::filesystem::remove_all( boost::filesystem::path( strPath ) / "database" ) ;
            }
        }
    }
}
