// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_INIT_H
#define DOGECOIN_INIT_H

#include <string>
#include <thread>
#include <vector>

class CScheduler ;
class CWallet ;

void StopAndJoinThreads( std::vector< std::thread > & threads ) ;
void Shutdown() ;
void InitLogging() ;
//!Parameter interaction: change parameters depending on other parameters
void InitParameterInteraction() ;

/** Initialize bitcoin core: Basic context setup
 *  @note This can be done before daemonization
 *  @pre Parameters should be parsed and config file should be read
 */
bool AppInitBasicSetup();
/**
 * Initialization: parameter interaction
 * @note This can be done before daemonization
 * @pre Parameters should be parsed and config file should be read, AppInitBasicSetup should have been called
 */
bool AppInitParameterInteraction();
/**
 * Initialization sanity checks: ecc init, sanity checks, dir lock
 * @note This can be done before daemonization
 * @pre Parameters should be parsed and config file should be read, AppInitParameterInteraction should have been called
 */
bool AppInitSanityChecks();
/**
 * Main initialization
 * @note This should only be done after daemonization
 * @pre Parameters should be parsed and config file should be read, AppInitSanityChecks should have been called
 */
bool AppInitMain( std::vector< std::thread > & threads, CScheduler & scheduler ) ;

/** Determines what help message to show */
enum WhatHelpMessage {
    HELP_MESSAGE_DOGECOIND,
    HELP_MESSAGE_DOGECOIN_QT
} ;

/** Help for options shared between UI and daemon (for -help) */
std::string HelpMessage( WhatHelpMessage what ) ;
/** Returns licensing information */
std::string LicenseInfo() ;

#endif
