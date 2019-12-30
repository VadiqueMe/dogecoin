// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "utilmoneystr.h"

#include "primitives/transaction.h"
#include "tinyformat.h"
#include "utilstrencodings.h"

std::string FormatMoney( const CAmount & n )
{
    // not using straight sprintf here because we do NOT want
    // localized number formatting
    int64_t n_abs = ( n > 0 ? n : -n ) ;
    int64_t quotient = n_abs / E8COIN ;
    int64_t remainder = n_abs % E8COIN ;
    std::string str = strprintf( "%d.%08d", quotient, remainder ) ;

    // Right-trim excess zeros before the decimal point:
    size_t nTrim = 0 ;
    for ( int i = str.size() - 1 ; i >= 2 && str[ i ] == '0' && isdigit( str[ i-2 ] ) ; -- i )
        ++ nTrim ;
    if ( nTrim > 0 )
        str.erase( str.size() - nTrim, nTrim ) ;

    if ( n < 0 )
        str.insert( 0, 1, '-' ) ;

    return str ;
}

bool ParseMoney( const std::string & str, CAmount & nRet )
{
    return ParseMoney( str.c_str(), nRet ) ;
}

bool ParseMoney( const char * pszIn, CAmount & nRet )
{
    const char * p = pszIn ;
    while ( isspace( *p ) ) p ++ ;
    std::string strWhole;
    int64_t nUnits = 0 ;
    for ( ; *p ; p ++ )
    {
        if ( *p == '.' )
        {
            p ++ ;
            int64_t nMult = E8CENT * 10 ;
            while (isdigit(*p) && (nMult > 0))
            {
                nUnits += nMult * (*p++ - '0');
                nMult /= 10;
            }
            break;
        }
        if (isspace(*p))
            break;
        if (!isdigit(*p))
            return false;
        strWhole.insert(strWhole.end(), *p);
    }
    for (; *p; p++)
        if (!isspace(*p))
            return false;
    if (strWhole.size() > 10) // guard against 63 bit overflow
        return false;
    if ( nUnits < 0 || nUnits > E8COIN )
        return false ;

    int64_t nWhole = atoi64( strWhole ) ;
    CAmount nValue = nWhole * E8COIN + nUnits ;

    nRet = nValue ;
    return true ;
}
