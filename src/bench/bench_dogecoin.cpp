// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "bench.h"

#include "key.h"
#include "validation.h"
#include "utillog.h"

void SetupEnvironment() ;

int
main( int argc, char ** argv )
{
    ECC_Start() ;
    SetupEnvironment() ;
    PickPrintToConsole() ; // don't write to debug log file

    benchmark::BenchRunner::RunAll() ;

    ECC_Stop() ;
}
