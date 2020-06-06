// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "alert.h"
#include "sync.h"
#include "uint256.h"
#include "util.h"
#include "utilstrencodings.h"
#include "warnings.h"

CCriticalSection cs_warnings ;
std::string strMiscWarning ;

bool fHighForkFound = false ;
bool fHighInvalidChainFound = false ;

void SetMiscWarning( const std::string & warning )
{
    LOCK( cs_warnings ) ;
    strMiscWarning = warning ;
}

void SetHighForkFound( bool f )
{
    LOCK( cs_warnings ) ;
    fHighForkFound = f ;
}

bool GetHighForkFound()
{
    LOCK( cs_warnings ) ;
    return fHighForkFound ;
}

void SetHighInvalidChainFound( bool f )
{
    LOCK( cs_warnings ) ;
    fHighInvalidChainFound = f ;
}

bool GetHighInvalidChainFound()
{
    LOCK( cs_warnings ) ;
    return fHighInvalidChainFound ;
}

std::string GetWarnings( const std::string & strFor )
{
    int nPriority = 0;
    std::string strStatusBar;
    std::string strRPC;
    std::string strGUI;
    const std::string uiAlertSeperator = "<hr />";

    LOCK(cs_warnings);

    if (GetBoolArg("-testsafemode", DEFAULT_TESTSAFEMODE))
        strStatusBar = strRPC = strGUI = "testsafemode enabled";

    // warnings like out of disk space or wrong clock
    if ( strMiscWarning.size() > 0 )
    {
        nPriority = 1000 ;
        strStatusBar = strMiscWarning ;
        strGUI += ( strGUI.empty() ? "" : uiAlertSeperator ) + strMiscWarning ;
    }

    if ( fHighForkFound )
    {
        nPriority = 2000 ;
        strStatusBar = strRPC = "Warning: The network does not appear to fully agree. Some miners appear to be experiencing issues";
        strGUI += ( strGUI.empty() ? "" : uiAlertSeperator ) + strRPC ;
    }
    else if ( fHighInvalidChainFound )
    {
        nPriority = 2000 ;
        strStatusBar = strRPC = "Warning: We do not appear to fully agree with other peers. You may need to upgrade, or other nodes may need to upgrade";
        strGUI += ( strGUI.empty() ? "" : uiAlertSeperator ) + strRPC ;
    }

    // Alerts
    {
        LOCK(cs_mapAlerts);
        for ( std::pair< const uint256, CAlert > & item : mapAlerts )
        {
            const CAlert& alert = item.second;
            if (alert.AppliesToMe() && alert.nPriority > nPriority)
            {
                nPriority = alert.nPriority;
                strStatusBar = strGUI = alert.strStatusBar;
            }
        }
    }

    if (strFor == "gui")
        return strGUI;
    else if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;

    assert(!"GetWarnings(): invalid parameter");
    return "error";
}
