// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_UTIL_H
#define DOGECOIN_UTIL_H

/**
 * Utilities: argument handling, config file parsing
 */

#include "compat.h"

#include <atomic>
#include <exception>
#include <map>
#include <stdint.h>
#include <string>
#include <vector>

#include <boost/filesystem/path.hpp>

extern const std::map< std::string, std::vector< std::string > > & mapMultiArgs ;

extern bool fDebug ;

extern const std::string DOGECOIN_CONF_FILENAME ;
extern const std::string BITCOIN_PID_FILENAME ;

void SetupEnvironment() ;
bool SetupNetworking() ;

void ParseParameters( int argc, const char * const argv[] ) ;
void FileCommit(FILE *file);
bool TruncateFile(FILE *file, unsigned int length);
int RaiseFileDescriptorLimit(int nMinFD);
void AllocateFileRange(FILE *file, unsigned int offset, unsigned int length);
bool RenameOver( const boost::filesystem::path & src, const boost::filesystem::path & dest ) ;
bool TryToCreateDirectory( const boost::filesystem::path & p ) ;
boost::filesystem::path GetDefaultDataDir() ;
const boost::filesystem::path & GetDirForData( bool fNetSpecific = true ) ;
void ClearDatadirCache();
boost::filesystem::path GetConfigFile(const std::string& confPath);
#ifndef WIN32
boost::filesystem::path GetPidFile();
void CreatePidFile(const boost::filesystem::path &path, pid_t pid);
#endif
void ReadConfigFile(const std::string& confPath);
#ifdef WIN32
boost::filesystem::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif

void runCommand( const std::string & strCommand ) ;

std::string FormatBytes( uint64_t bytes ) ;

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

/**
 * Return true if the given argument has been manually set
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @return true if the argument has been set
 */
bool IsArgSet(const std::string& strArg);

/**
 * Return string argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. "1")
 * @return command-line argument or default value
 */
std::string GetArg(const std::string& strArg, const std::string& strDefault);

/**
 * Return integer argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. 1)
 * @return command-line argument (0 if invalid number) or default value
 */
int64_t GetArg(const std::string& strArg, int64_t nDefault);

/**
 * Return boolean argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (true or false)
 * @return command-line argument or default value
 */
bool GetBoolArg(const std::string& strArg, bool fDefault);

/**
 * Set an argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param strValue Value (e.g. "1")
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetArg(const std::string& strArg, const std::string& strValue);

/**
 * Set a boolean argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param fValue Value (e.g. false)
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetBoolArg(const std::string& strArg, bool fValue);

// Forces a arg setting, used only in testing
void ForceSetArg(const std::string& strArg, const std::string& strValue);

std::string CopyrightHolders( const std::string & prefix ) ;

#endif
