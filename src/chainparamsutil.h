// Copyright (c) 2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_CHAINPARAMSUTIL_H
#define DOGECOIN_CHAINPARAMSUTIL_H

#include <string>

/**
 * Append messages about options for chain parameters to the help string
 */
void AppendChainParamsHelp( std::string & strUsage, bool debugHelp = true ) ;

// returns "main" by default
std::string ChainNameFromArguments() ;

#endif
