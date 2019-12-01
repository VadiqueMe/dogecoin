// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "amount.h"
#include "test/test_dogecoin.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(amount_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(GetFeeTest)
{
    CFeeRate feeRate ;

    feeRate = CFeeRate( 0 ) ;
    // Expected to always return 0
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 0 ), 0 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 1e5 ), 0 ) ;

    feeRate = CFeeRate( 1000 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 0 ), 0 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 1 ), 1000 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 121 ), 1000 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 999 ), 1000 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 1e3 ), 1000 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 9e3 ), 9000 ) ;

    feeRate = CFeeRate( -1000 ) ; // negative fees uhm
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 0 ), 0 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 1 ), -1000 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 121 ), -1000 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 999 ), -1000 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 1e3 ), -1000 ) ;
    BOOST_CHECK_EQUAL( feeRate.GetFeePerBytes( 9e3 ), -9000 ) ;

    // Check full constructor
    // default value
    BOOST_CHECK(CFeeRate(CAmount(-1), 1000) == CFeeRate(-1));
    BOOST_CHECK(CFeeRate(CAmount(0), 1000) == CFeeRate(0));
    BOOST_CHECK(CFeeRate(CAmount(1), 1000) == CFeeRate(1));
    // lost precision (can only resolve satoshis per kB)
    BOOST_CHECK(CFeeRate(CAmount(1), 1001) == CFeeRate(0));
    BOOST_CHECK(CFeeRate(CAmount(2), 1001) == CFeeRate(1));
    // some more integer checks
    BOOST_CHECK(CFeeRate(CAmount(26), 789) == CFeeRate(32));
    BOOST_CHECK(CFeeRate(CAmount(27), 789) == CFeeRate(34));
    // Maximum size in bytes, should not crash
    CFeeRate( MAX_MONEY, std::numeric_limits< size_t >::max() >> 1 ).GetFeePerKiloByte() ;
}

BOOST_AUTO_TEST_SUITE_END()
