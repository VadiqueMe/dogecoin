// Copyright (c) 2010-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "ui_interface.h"

#include "tinyformat.h"
#include "utilstr.h"

CClientUserInterface uiInterface ;

bool InitError( const std::string & str )
{
    uiInterface.ThreadSafeMessageBox( str, "", CClientUserInterface::MSG_ERROR ) ;
    return false ;
}

void InitWarning( const std::string & str )
{
    uiInterface.ThreadSafeMessageBox( str, "", CClientUserInterface::MSG_WARNING ) ;
}
