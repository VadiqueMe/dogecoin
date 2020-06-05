// Copyright (c) 2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_UTILHELP_H
#define DOGECOIN_UTILHELP_H

#include <string>

/**
 * Format a string to be used as group of options in help messages
 *
 * @param message name of group (e.g. "RPC server options:")
 * @return the formatted string
 */
inline std::string HelpMessageGroup( const std::string & message )
{
    return message + "\n\n" ;
}

/**
 * Format a string to be used as option description in help messages
 *
 * @param option like "-nodebug" or "-rpcuser=<user>"
 * @param message option description (e.g. "Username for JSON-RPC connections")
 * @return the formatted string
 */
std::string HelpMessageOpt( const std::string & option, const std::string & message ) ;

#endif
