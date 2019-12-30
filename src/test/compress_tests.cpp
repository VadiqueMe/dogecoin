// Copyright (c) 2012-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "compressor.h"
#include "util.h"
#include "test/test_dogecoin.h"

#include <stdint.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(compress_tests, BasicTestingSetup)

bool static TestEncode(uint64_t in) {
    return in == CTxOutCompressor::DecompressAmount(CTxOutCompressor::CompressAmount(in));
}

bool static TestDecode(uint64_t in) {
    return in == CTxOutCompressor::CompressAmount(CTxOutCompressor::DecompressAmount(in));
}

bool static TestPair(uint64_t dec, uint64_t enc) {
    return CTxOutCompressor::CompressAmount(dec) == enc &&
           CTxOutCompressor::DecompressAmount(enc) == dec;
}

BOOST_AUTO_TEST_CASE(compress_amounts)
{
    BOOST_CHECK( TestPair(                 0,       0x0 ) ) ;
    BOOST_CHECK( TestPair(                 1,       0x1 ) ) ;
    BOOST_CHECK( TestPair(            E8CENT,       0x7 ) ) ;
    BOOST_CHECK( TestPair(            E8COIN,       0x9 ) ) ;
    BOOST_CHECK( TestPair(       50 * E8COIN,      0x32 ) ) ;
    BOOST_CHECK( TestPair( 21000000 * E8COIN, 0x1406f40 ) ) ;

    // amounts 0.00000001 .. 0.00100000
    for ( uint64_t i = 1 ; i <= 100000 ; i ++ )
        BOOST_CHECK( TestEncode( i ) ) ;

    // amounts 0.01 .. 100.00
    for ( uint64_t i = 1 ; i <= 10000 ; i ++ )
        BOOST_CHECK( TestEncode( i * E8CENT ) ) ;

    // amounts 1 .. 10000
    for ( uint64_t i = 1 ; i <= 10000 ; i ++ )
        BOOST_CHECK( TestEncode( i * E8COIN ) ) ;

    // amounts 50 .. 21000000
    for ( uint64_t i = 50 ; i <= 21000000 ; i += 50 )
        BOOST_CHECK( TestEncode( i * E8COIN ) ) ;

    for ( uint64_t i = 0 ; i < 100000 ; i ++ )
        BOOST_CHECK( TestDecode( i ) ) ;
}

BOOST_AUTO_TEST_SUITE_END()
