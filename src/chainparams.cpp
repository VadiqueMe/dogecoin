// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "chainparams.h"
#include "consensus/merkle.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include "arith_uint256.h"
#include "base58.h"

#include <iomanip> // std::get_time

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"

static CBlock CreateGenesisBlock( const char * pszTimestamp, const CScript & genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount & genesisReward )
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis ;
    genesis.nTime    = nTime ;
    genesis.nBits    = nBits ;
    genesis.nNonce   = nNonce ;
    genesis.nVersion = nVersion ;
    genesis.vtx.push_back( MakeTransactionRef( std::move( txNew ) ) ) ;
    genesis.hashPrevBlock.SetNull() ;
    genesis.hashMerkleRoot = BlockMerkleRoot( genesis ) ;
    return genesis ;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database
 */
static CBlock CreateGenesisBlock( uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount & genesisReward )
{
    const char* pszTimestamp = "Nintondo" ;
    const CScript genesisOutputScript = CScript() << ParseHex( "040184710fa689ad5023690c80f3a49c8f13f8d45b8c857fbcbc8bc4a8e4d3eb4b10f4d4604fa08dce601aaf0f470216fe1b51850b4acf21b179c45070ac7b03a9" ) << OP_CHECKSIG ;
    return CreateGenesisBlock( pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward ) ;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
private:
    Consensus::Params digishieldConsensus;
    Consensus::Params auxpowConsensus;
public:
    CMainParams()
    {
        // Blocks 0 - 144999 are conventional difficulty calculation
        consensus.nSubsidyHalvingInterval = 100000;
        consensus.nMajorityEnforceBlockUpgrade = 1500;
        consensus.nMajorityRejectBlockOutdated = 1900;
        consensus.nMajorityWindow = 2000;
        // BIP34 is never enforced in Dogecoin v2 blocks, so we enforce from v3
        consensus.BIP34Height = 1034383;
        consensus.BIP34Hash = uint256S("0x80d1364201e5df97e696c03bdd24dc885e8617b9de51e453c10a4f629b1e797a");
        // consensus.BIP65Height = 1032483; // Not enabled in Doge yet
        consensus.BIP66Height = 1034383; // 80d1364201e5df97e696c03bdd24dc885e8617b9de51e453c10a4f629b1e797a - this is the last block that could be v2, 1900 blocks past the last v2 block
        consensus.powLimit = uint256S("0x00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 20;
        consensus.nPowTargetTimespan = 4 * 60 * 60; // pre-digishield: 4 hours
        consensus.nPowTargetSpacing = 60; // 1 minute
        consensus.fDigishieldDifficultyCalculation = false;
        consensus.nCoinbaseMaturity = 30 ;
        consensus.fSimplifiedRewards = false ;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 9576; // 95% of 10,080
        consensus.nMinerConfirmationWindow = 10080; // 60 * 24 * 7 = 10,080 blocks, or one week
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        // XXX: BIP heights and hashes all need to be updated to Dogecoin values
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1462060800; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1479168000; // November 15th, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 0; // Disabled

        // The best chain has at least this much work
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000141a39e783aad4f660f");

        consensus.nAuxpowChainId = 0x0062 ; // 98 - Josh Wise!
        consensus.fStrictChainId = true ;
        consensus.fAllowLegacyBlocks = true ;
        consensus.nHeightEffective = 0 ;

        // Blocks 145000 - 371336 are Digishield without AuxPoW
        digishieldConsensus = consensus;
        digishieldConsensus.nHeightEffective = 145000;
        digishieldConsensus.fSimplifiedRewards = true;
        digishieldConsensus.fDigishieldDifficultyCalculation = true;
        digishieldConsensus.nPowTargetTimespan = 60; // post-digishield: 1 minute
        digishieldConsensus.nCoinbaseMaturity = 240;

        // Blocks 371337+ are AuxPoW
        auxpowConsensus = digishieldConsensus;
        auxpowConsensus.nHeightEffective = 371337;
        auxpowConsensus.fAllowLegacyBlocks = false;

        // Assemble the binary search tree of consensus parameters
        pConsensusRoot = &digishieldConsensus;
        digishieldConsensus.pLeft = &consensus;
        digishieldConsensus.pRight = &auxpowConsensus;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment
         */
        pchMessageStart[0] = 0xc0;
        pchMessageStart[1] = 0xc0;
        pchMessageStart[2] = 0xc0;
        pchMessageStart[3] = 0xc0;
        vAlertPubKey = ParseHex("04d4da7a5dae4db797d9b0644d57a5cd50e05a70f36091cd62e2fc41c98ded06340be5a43a35e185690cd9cde5d72da8f6d065b499b06f51dcfba14aad859f443a");
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock( 1386325540, 99943, 0x1e0ffff0, 1, 88 * E8COIN ) ;

        consensus.hashGenesisBlock = genesis.GetSha256Hash() ;
        digishieldConsensus.hashGenesisBlock = consensus.hashGenesisBlock;
        auxpowConsensus.hashGenesisBlock = consensus.hashGenesisBlock;
        assert(consensus.hashGenesisBlock == uint256S("0x1a91e3dace36e2be3bf030a65679fe821aa1d6ef92e7c9902eb318182c355691"));
        assert(genesis.hashMerkleRoot == uint256S("0x5b2a3f53f605d62c53e62932dac6925e3d74afa5a4b459745c36d42d0ed26a69"));

        // Note that of those with the service bits flag, most only support a subset of possible options
        vSeeds.push_back(CDNSSeedData("multidoge.org", "seed.multidoge.org", true));
        vSeeds.push_back(CDNSSeedData("multidoge.org", "seed2.multidoge.org"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,30);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,22);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,158);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x02)(0xfa)(0xca)(0xfd).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x02)(0xfa)(0xc3)(0x98).convert_to_container<std::vector<unsigned char> >();

        //TODO: fix this for dogecoin -- plddr
        //vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));
        vFixedSeeds.clear();

        fMiningRequiresPeers = true ;
        fDefaultConsistencyChecks = false ;
        fRequireStandardTxs = true ;
        fMineBlocksOnDemand = false ;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (       0, uint256S("0x1a91e3dace36e2be3bf030a65679fe821aa1d6ef92e7c9902eb318182c355691") )
            (  104679, uint256S("0x35eb87ae90d44b98898fec8c39577b76cb1eb08e1261cfc10706c8ce9a1d01cf") )
            (  145000, uint256S("0xcc47cae70d7c5c92828d3214a266331dde59087d4a39071fa76ddfff9b7bde72") )
            (  371337, uint256S("0x60323982f9c5ff1b5a954eac9dc1269352835f47c2c5222691d80f0d50dcf053") )
            (  450000, uint256S("0xd279277f8f846a224d776450aa04da3cf978991a182c6f3075db4c48b173bbd7") )
            (  771275, uint256S("0x1b7d789ed82cbdc640952e7e7a54966c6488a32eaad54fc39dff83f310dbaaed") )
            ( 1000000, uint256S("0x6aae55bea74235f0c80bd066349d4440c31f2d0f27d54265ecd484d8c1d11b47") )
            ( 1250000, uint256S("0x00c7a442055c1a990e11eea5371ca5c1c02a0677b33cc88ec728c45edc4ec060") )
            ( 1500000, uint256S("0xf1d32d6920de7b617d51e74bdf4e58adccaa582ffdc8657464454f16a952fca6") )
            ( 1750000, uint256S("0x5c8e7327984f0d6f59447d89d143e5f6eafc524c82ad95d176c5cec082ae2001") )
            ( 2000000, uint256S("0x9914f0e82e39bbf21950792e8816620d71b9965bdbbc14e72a95e3ab9618fea8") )
            ( 2031142, uint256S("0x893297d89afb7599a3c571ca31a3b80e8353f4cf39872400ad0f57d26c4c5d42") )
            ( 2510150, uint256S("0x77e3f4a4bcb4a2c15e8015525e3d15b466f6c022f6ca82698f329edef7d9777e") )
        } ;

        chainTxData = ChainTxData {
            // Data as of block 77e3f4a4bcb4a2c15e8015525e3d15b466f6c022f6ca82698f329edef7d9777e (height 2510150)
            // Tx estimate based on average of year 2018 (~27k transactions per day)
            1544484077, // * UNIX timestamp of last checkpoint block
            42797508,   // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug log lines)
            0.3125      // * estimated number of transactions per second after checkpoint
        } ;
    }
};
static std::unique_ptr< CMainParams > mainParams( nullptr ) ;


/**
 * Inu network
 */

class CInuParams : public CChainParams {

public:
    CInuParams()
    {
        consensus.powLimit = ArithToUint256( ( ~ arith_uint256() ) >> 6 ) ;
        //printf( "powLimit = 0x%s\n", consensus.powLimit.GetHex().c_str() ) ;
        //printf( "compact powLimit for nBits = 0x%08x\n", UintToArith256( consensus.powLimit ).GetCompact() ) ;

        consensus.nPowTargetTimespan = 60 ; // 1 minute
        consensus.nPowTargetSpacing = 60 ; // 1 minute
        consensus.nCoinbaseMaturity = 60 ; // number of blocks above before generated coins will be spendable
        consensus.fDigishieldDifficultyCalculation = true ;
        consensus.nSubsidyHalvingInterval = 1000000 ; // not used for inu chain
        consensus.fSimplifiedRewards = false ; // not used for inu chain
        consensus.fPowAllowMinDifficultyBlocks = false ; // not used for inu chain
        consensus.fPowNoRetargeting = false ;

        consensus.nRuleChangeActivationThreshold = 9576 ; // 95% of 10 080
        consensus.nMinerConfirmationWindow = 10080 ; // 60 * 24 * 7 = 10 080 blocks, or one week

        consensus.nMajorityEnforceBlockUpgrade = 9800 ;
        consensus.nMajorityRejectBlockOutdated = 9900 ;
        consensus.nMajorityWindow = 10000 ;

        consensus.BIP34Height = 1 ;
        consensus.BIP34Hash = uint256S( "0x00" ) ;
        consensus.BIP66Height = 1 ;

        struct std::tm startTime ;
        {
            std::istringstream ss( "2019-11-11 11:00:11" ) ;
            ss >> std::get_time( &startTime, "%Y-%m-%d %H:%M:%S" ) ;
        }
        struct std::tm timeout ;
        {
            std::istringstream ss( "2019-12-13 00:11:00" ) ;
            ss >> std::get_time( &timeout, "%Y-%m-%d %H:%M:%S" ) ;
        }

        // Deployment of BIP68, BIP112, and BIP113
        consensus.vDeployments[ Consensus::DEPLOYMENT_CSV ].bit = 0 ;
        consensus.vDeployments[ Consensus::DEPLOYMENT_CSV ].nStartTime = mktime( &startTime ) ;
        consensus.vDeployments[ Consensus::DEPLOYMENT_CSV ].nTimeout = mktime( &timeout ) ;

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[ Consensus::DEPLOYMENT_SEGWIT ].bit = 1 ;
        consensus.vDeployments[ Consensus::DEPLOYMENT_SEGWIT ].nStartTime = mktime( &startTime ) ;
        consensus.vDeployments[ Consensus::DEPLOYMENT_SEGWIT ].nTimeout = 0 ; // disabled

        // The best chain has at least this much work
        consensus.nMinimumChainWork = uint256S( "0x00001f80" ) ;

        consensus.nAuxpowChainId = 0x62 ; // 98 Josh Wise
        consensus.fStrictChainId = false ;
        consensus.fAllowLegacyBlocks = false ;
        consensus.nHeightEffective = 0 ; // parameters apply since this block

        pConsensusRoot = &consensus ;

        /* the message start string is designed to be unlikely to occur in common texts */
        /* it is a large 32-bit integer with any sequence of its bytes */
        pchMessageStart[ 0 ] = 0xd0 ;
        pchMessageStart[ 1 ] = 0xc0 ;
        pchMessageStart[ 2 ] = 0x9e ;
        pchMessageStart[ 3 ] = 0xbe ;

        /* one who holds the private key corresponding to vAlertPubKey public key
           can send a message to every node on the network */
        /* vAlertPubKey = uint256S( "0x00" ) ; */

        nPruneAfterHeight = 10000 ;

/*      struct std::tm genesisTimeTm ;
        {
            std::istringstream ss( "2019-12-25 00:01:22" ) ;
            ss >> std::get_time( &genesisTimeTm, "%Y-%m-%d %H:%M:%S" ) ;
        }
        printf( "genesis nTime=%u\n", mktime( &genesisTimeTm ) ) ; */

        int32_t genesisTime = 1577221282 ; // 2019-12-25 00:01:22

/*
        arith_uint256 maxScryptHashGenesis = UintToArith256( consensus.powLimit ) >> 3 ;
        arith_uint256 maxSha256HashGenesis = UintToArith256( consensus.powLimit ) >> 2 ;
        arith_uint256 maxLyra2re2HashGenesis = UintToArith256( consensus.powLimit ) >> 3 ;
        uint32_t nonce = 0xbe000000 ;
        while ( nonce >= 0x2be )
        {
            genesis = CreateGenesisBlock(
                genesisTime,
                nonce,
                UintToArith256( consensus.powLimit ).GetCompact(), // bits
                0x620004, // version
                1 // genesis block's subsidy, cannot be spent anyway
            ) ;

            if ( UintToArith256( genesis.GetScryptHash() ) <= maxScryptHashGenesis &&
                    UintToArith256( genesis.GetSha256Hash() ) <= maxSha256HashGenesis &&
                        UintToArith256( genesis.GetLyra2Re2Hash() ) <= maxLyra2re2HashGenesis )
                break ;

            -- nonce ;
            if ( 0 == ( nonce & 0xffff ) )
                printf( "scanned for generation of genesis block till nonce 0x%08x\n", nonce ) ;
        }

        std::cout << "genesis block's time is " << std::put_time( &genesisTime, "%Y-%m-%d %H:%M:%S" ) << std::endl ;
        printf( "genesis block's nonce is 0x%08x\n", genesis.nNonce ) ;
        printf( "genesis block's hash-merkle-root is 0x%s\n", genesis.hashMerkleRoot.GetHex().c_str() );
        printf( "genesis block's scrypt hash is 0x%s\n", genesis.GetScryptHash().GetHex().c_str() ) ;
        printf( "genesis block's sha256 hash is 0x%s\n", genesis.GetSha256Hash().GetHex().c_str() ) ;
        printf( "genesis block's lyra2re2 hash is 0x%s\n", genesis.GetLyra2Re2Hash().GetHex().c_str() ) ;
*/

        const uint32_t genesisNonce = 0xbced8c00 ;
        // genesis block's scrypt hash is 0x004e4ce8dac526e67b4245c9d6d1df30c6dc2a8e4a106c80f4619fb11f77dcee
        // genesis block's sha256 hash is 0x0068ddbf5d570f2589248a9b42ec8480e6fd40d681108a63729e563f0988a6ef
        // genesis block's lyra2re2 hash is 0x00299a9cc20dd3b9c7b5b57f267c9d35b467ca5aa5d182b11505c9a4e6284765

        genesis = CreateGenesisBlock( genesisTime , genesisNonce, /* bits */ UintToArith256( consensus.powLimit ).GetCompact(), /* version */ 0x620004, /* subsidy */ 1 ) ;

        consensus.hashGenesisBlock = genesis.GetSha256Hash() ;

        uint256 expectedSha256HashOfGenesis = uint256S( "0x0068ddbf5d570f2589248a9b42ec8480e6fd40d681108a63729e563f0988a6ef" ) ;
        uint256 expectedMerkleRootOfGenesis = uint256S( "0xcde1e3166a191d8e661382004982ade4892ade250dbf7190413ac7a26079b261" ) ;

        if ( consensus.hashGenesisBlock != expectedSha256HashOfGenesis ) {
            std::cout << "consensus.hashGenesisBlock = " << consensus.hashGenesisBlock.GetHex() << std::endl
                       << "expectedSha256HashOfGenesis = " << expectedSha256HashOfGenesis.GetHex() << std::endl ;
            std::cout << "genesis " << genesis.ToString() << std::endl ;
        }

        assert( consensus.hashGenesisBlock == expectedSha256HashOfGenesis ) ;
        assert( genesis.hashMerkleRoot == expectedMerkleRootOfGenesis ) ;

        base58Prefixes[ PUBKEY_ADDRESS ] = std::vector< unsigned char >( 1, 0x67 ) ;
        base58Prefixes[ SCRIPT_ADDRESS ] = std::vector< unsigned char >( 1, 0xbe ) ;
        base58Prefixes[ SECRET_KEY ]   =   std::vector< unsigned char >( 1, 0x77 ) ;
        base58Prefixes[ EXT_PUBLIC_KEY ] = boost::assign::list_of( 0x0a )( 0xbc )( 0x20 )( 0x88 ).convert_to_container< std::vector< unsigned char > >() ;
        base58Prefixes[ EXT_SECRET_KEY ] = boost::assign::list_of( 0x0a )( 0xbd )( 0x81 )( 0xd9 ).convert_to_container< std::vector< unsigned char > >() ;

/*
        unsigned char pubkeyFirst = 0x90 ;
        char networkPrefix = ' ' ;
        while ( networkPrefix != 'i' )
        {
            base58Prefixes[ PUBKEY_ADDRESS ] = std::vector< unsigned char >( 1, pubkeyFirst ) ;

            std::string addressExample = CDogecoinAddress::DummyDogecoinAddress(
                    Base58Prefix( CChainParams::PUBKEY_ADDRESS ),
                    Base58Prefix( CChainParams::SCRIPT_ADDRESS )
                ) ;
            assert( addressExample.length() > 0 ) ;
            networkPrefix = addressExample[ 0 ] ;
            printf( "network address example %s, network prefix '%c' with 0x%02x\n",
                    addressExample.c_str(), networkPrefix, pubkeyFirst ) ;
            -- pubkeyFirst ;
        }
*/
/*
        unsigned char privkeyFirst = 0x77 ;
        char privkeyPrefix = ' ' ;
        while ( privkeyPrefix != 'J' )
        {
            base58Prefixes[ SECRET_KEY ] = std::vector< unsigned char >( 1, privkeyFirst ) ;

            CKey privateKey ;
            privateKey.MakeNewKey( true ) ;
            CDogecoinSecret encodedPrivkey ;
            encodedPrivkey.SetKey( privateKey, Base58Prefix( CChainParams::SECRET_KEY ) ) ;
            std::string privateKeyExample = encodedPrivkey.ToString() ;
            printf( "private key example %s with 0x%02x\n", privateKeyExample.c_str(), privkeyFirst ) ;
            assert( privateKeyExample.length() > 0 ) ;
            privkeyPrefix = privateKeyExample[ 0 ] ;
            -- privkeyFirst ;
        }
*/

        vSeeds.clear() ;
        vFixedSeeds.clear() ;

        fMiningRequiresPeers = true ;
        fDefaultConsistencyChecks = false ;
        fRequireStandardTxs = true ;
        fMineBlocksOnDemand = false ;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (       0, uint256S( "0x0068ddbf5d570f2589248a9b42ec8480e6fd40d681108a63729e563f0988a6ef" ) )
            (     125, uint256S( "0x05878a2427b5201be66db0900cb47ab36fb85950771e35f70a1a3da6e34a4278" ) )
        } ;

        struct std::tm lastCheckpointTime ;
        {
            std::istringstream ss( "2020-01-06 09:17:54" ) ;
            ss >> std::get_time( &lastCheckpointTime, "%Y-%m-%d %H:%M:%S" ) ;
        }
        chainTxData = ChainTxData {
            // data for block 05878a2427b5201be66db0900cb47ab36fb85950771e35f70a1a3da6e34a4278 at height 125
            mktime( &lastCheckpointTime ),
            127, // number of all transactions in all blocks at last checkpoint
            0.01 // estimated number of transactions per second after checkpoint
        } ;
    }
} ;
static std::unique_ptr< CInuParams > inuParams( nullptr ) ;


/**
 * Testnet (v3)
 */

class CTestNetParams : public CChainParams {
private:
    Consensus::Params digishieldConsensus;
    Consensus::Params auxpowConsensus;
    Consensus::Params minDifficultyConsensus;
public:
    CTestNetParams()
    {
        // Blocks 0 - 144999 are pre-Digishield
        consensus.nHeightEffective = 0;
        consensus.nPowTargetTimespan = 4 * 60 * 60; // pre-digishield: 4 hours
        consensus.fDigishieldDifficultyCalculation = false;
        consensus.nCoinbaseMaturity = 30 ;
        consensus.fSimplifiedRewards = false ;
        consensus.fPowAllowMinDifficultyBlocks = true ;
        consensus.nSubsidyHalvingInterval = 100000;
        consensus.nMajorityEnforceBlockUpgrade = 501;
        consensus.nMajorityRejectBlockOutdated = 750;
        consensus.nMajorityWindow = 1000;
        // BIP34 is never enforced in Dogecoin v2 blocks, so we enforce from v3
        consensus.BIP34Height = 708658;
        consensus.BIP34Hash = uint256S("0x21b8b97dcdb94caa67c7f8f6dbf22e61e0cfe0e46e1fff3528b22864659e9b38");
        // consensus.BIP65Height = 581885; // 00000000007f6655f22f98e72ed80d8b06dc761d5da09df0fa1dc4be4f861eb6
        consensus.BIP66Height = 708658; // 21b8b97dcdb94caa67c7f8f6dbf22e61e0cfe0e46e1fff3528b22864659e9b38 - this is the last block that could be v2, 1900 blocks past the last v2 block
        consensus.powLimit = uint256S("0x00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 20;
        consensus.nPowTargetTimespan = 4 * 60 * 60; // pre-digishield: 4 hours
        consensus.nPowTargetSpacing = 60; // 1 minute
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 2880; // 2 days (note this is significantly lower than Bitcoin standard)
        consensus.nMinerConfirmationWindow = 10080; // 60 * 24 * 7 = 10,080 blocks, or one week
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        // XXX: BIP heights and hashes all need to be updated to Dogecoin values
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1462060800; // May 1st 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 0; // Disabled

        // The best chain has at least this much work
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000001030d1382ade");

        consensus.nAuxpowChainId = 0x0062 ; // 98 - Josh Wise!
        consensus.fStrictChainId = false ;
        consensus.nHeightEffective = 0 ;
        consensus.fAllowLegacyBlocks = true ;

        // Blocks 145000 - 157499 are Digishield without minimum difficulty on all blocks
        digishieldConsensus = consensus;
        digishieldConsensus.nHeightEffective = 145000;
        digishieldConsensus.nPowTargetTimespan = 60; // post-digishield: 1 minute
        digishieldConsensus.fDigishieldDifficultyCalculation = true;
        digishieldConsensus.fSimplifiedRewards = true;
        digishieldConsensus.fPowAllowMinDifficultyBlocks = false;
        digishieldConsensus.nCoinbaseMaturity = 240;

        // Blocks 157500 - 158099 are Digishield with minimum difficulty on all blocks
        minDifficultyConsensus = digishieldConsensus;
        minDifficultyConsensus.nHeightEffective = 157500;
        minDifficultyConsensus.fPowAllowMinDifficultyBlocks = true;

        // Enable AuxPoW at 158100
        auxpowConsensus = minDifficultyConsensus;
        auxpowConsensus.nHeightEffective = 158100;
        auxpowConsensus.fAllowLegacyBlocks = false;

        // Assemble the binary search tree of parameters
        pConsensusRoot = &digishieldConsensus;
        digishieldConsensus.pLeft = &consensus;
        digishieldConsensus.pRight = &minDifficultyConsensus;
        minDifficultyConsensus.pRight = &auxpowConsensus;

        pchMessageStart[0] = 0xfc;
        pchMessageStart[1] = 0xc1;
        pchMessageStart[2] = 0xb7;
        pchMessageStart[3] = 0xdc;
        vAlertPubKey = ParseHex("042756726da3c7ef515d89212ee1705023d14be389e25fe15611585661b9a20021908b2b80a3c7200a0139dd2b26946606aab0eef9aa7689a6dc2c7eee237fa834");
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock( 1391503289, 997879, 0x1e0ffff0, 1, 88 * E8COIN ) ;
        consensus.hashGenesisBlock = genesis.GetSha256Hash() ;
        digishieldConsensus.hashGenesisBlock = consensus.hashGenesisBlock;
        minDifficultyConsensus.hashGenesisBlock = consensus.hashGenesisBlock;
        auxpowConsensus.hashGenesisBlock = consensus.hashGenesisBlock;
        assert(consensus.hashGenesisBlock == uint256S("0xbb0a78264637406b6360aad926284d544d7049f45189db5664f3c4d07350559e"));
        assert(genesis.hashMerkleRoot == uint256S("0x5b2a3f53f605d62c53e62932dac6925e3d74afa5a4b459745c36d42d0ed26a69"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.push_back(CDNSSeedData("jrn.me.uk", "testseed.jrn.me.uk"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,113); // 0x71
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196); // 0xc4
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,241); // 0xf1
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xcf).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        //TODO: fix this for dogecoin -- plddr
        //vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fMiningRequiresPeers = true ;
        fDefaultConsistencyChecks = false ;
        fRequireStandardTxs = false ;
        fMineBlocksOnDemand = false ;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (       0, uint256S("0xbb0a78264637406b6360aad926284d544d7049f45189db5664f3c4d07350559e") )
            (  483173, uint256S("0xa804201ca0aceb7e937ef7a3c613a9b7589245b10cc095148c4ce4965b0b73b5") )
            (  591117, uint256S("0x5f6b93b2c28cedf32467d900369b8be6700f0649388a7dbfd3ebd4a01b1ffad8") )
            (  658924, uint256S("0xed6c8324d9a77195ee080f225a0fca6346495e08ded99bcda47a8eea5a8a620b") )
            (  703635, uint256S("0x839fa54617adcd582d53030a37455c14a87a806f6615aa8213f13e196230ff7f") )
            ( 1000000, uint256S("0x1fe4d44ea4d1edb031f52f0d7c635db8190dc871a190654c41d2450086b8ef0e") )
            ( 1202214, uint256S("0xa2179767a87ee4e95944703976fee63578ec04fa3ac2fc1c9c2c83587d096977") )
        } ;

        chainTxData = ChainTxData {
            // Data as of block a2179767a87ee4e95944703976fee63578ec04fa3ac2fc1c9c2c83587d096977 (height 1202214)
            1514565123, // * UNIX timestamp of last checkpoint block
            2005610,    // * total number of transactions between genesis and last checkpoint
            1000 // * estimated number of transactions per second after checkpoint
        } ;

    }
};
static std::unique_ptr< CTestNetParams > testNetParams( nullptr ) ;


/**
 * Regression test
 */

class CRegTestParams : public CChainParams {
private:
    Consensus::Params digishieldConsensus;
    Consensus::Params auxpowConsensus;
public:
    CRegTestParams()
    {
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = 100000000; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        // consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.powLimit = uint256S("0x7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~uint256(0) >> 1;
        consensus.nPowTargetTimespan = 4 * 60 * 60; // pre-digishield: 4 hours
        consensus.nPowTargetSpacing = 1; // regtest: 1 second blocks
        consensus.fDigishieldDifficultyCalculation = false;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 540; // 75% for testchains
        consensus.nMinerConfirmationWindow = 720; // Faster than normal for regtest (2,520 instead of 10,080)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 999999999999ULL;

        // The best chain has at least this much work
        consensus.nMinimumChainWork = uint256S( "0x00" ) ;

        consensus.nAuxpowChainId = 0x0062 ; // 98 - Josh Wise!
        consensus.fStrictChainId = true ;
        consensus.fAllowLegacyBlocks = true ;

        // Dogecoin parameters
        consensus.fSimplifiedRewards = true ; // always on for regtest
        consensus.nCoinbaseMaturity = 60; // For easier testability in RPC tests

        digishieldConsensus = consensus;
        digishieldConsensus.nHeightEffective = 10;
        digishieldConsensus.nPowTargetTimespan = 1; // regtest: also retarget every second in digishield mode, for conformity
        digishieldConsensus.fDigishieldDifficultyCalculation = true;

        auxpowConsensus = digishieldConsensus;
        auxpowConsensus.fAllowLegacyBlocks = false;
        auxpowConsensus.nHeightEffective = 20;

        // Assemble the binary search tree of parameters
        digishieldConsensus.pLeft = &consensus;
        digishieldConsensus.pRight = &auxpowConsensus;
        pConsensusRoot = &digishieldConsensus;

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock( 1296688602, 2, 0x207fffff, 1, 88 * E8COIN ) ;
        consensus.hashGenesisBlock = genesis.GetSha256Hash() ;
        digishieldConsensus.hashGenesisBlock = consensus.hashGenesisBlock;
        auxpowConsensus.hashGenesisBlock = consensus.hashGenesisBlock;
        assert(consensus.hashGenesisBlock == uint256S("0x3d2160a3b5dc4a9d62e7e66a295f70313ac808440ef7400d6c0772171ce973a5"));
        assert(genesis.hashMerkleRoot == uint256S("0x5b2a3f53f605d62c53e62932dac6925e3d74afa5a4b459745c36d42d0ed26a69"));

        vFixedSeeds.clear() ; // regtest doesn't have any fixed seeds
        vSeeds.clear() ;      // regtest doesn't have any DNS seeds

        fMiningRequiresPeers = false ;
        fDefaultConsistencyChecks = true ;
        fRequireStandardTxs = false ;
        fMineBlocksOnDemand = true ;

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            ( 0, uint256S("0x3d2160a3b5dc4a9d62e7e66a295f70313ac808440ef7400d6c0772171ce973a5") )
        } ;

        chainTxData = ChainTxData {
            0,
            0,
            0
        } ;

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>( 1, 0x6f ) ;
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>( 1, 0xc4 ) ;
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>( 1, 0xef ) ;
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
    }

    void changeBIP9Parameters( Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout )
    {
        consensus.vDeployments[ d ].nStartTime = nStartTime ;
        consensus.vDeployments[ d ].nTimeout = nTimeout ;
    }
};
static std::unique_ptr< CRegTestParams > regTestParams( nullptr ) ;

static CChainParams * pCurrentParams = nullptr ;

const CChainParams & Params() {
    assert( pCurrentParams != nullptr ) ;
    return *pCurrentParams ;
}

const Consensus::Params * Consensus::Params::GetConsensus( uint32_t nTargetHeight ) const
{
    if ( nTargetHeight < this -> nHeightEffective && this -> pLeft != NULL ) {
        return this -> pLeft -> GetConsensus( nTargetHeight ) ;
    } else if ( nTargetHeight > this -> nHeightEffective && this -> pRight != NULL ) {
        const Consensus::Params * pCandidate = this -> pRight -> GetConsensus( nTargetHeight ) ;
        if ( pCandidate->nHeightEffective <= nTargetHeight ) {
            return pCandidate ;
        }
    }

    // No better match below the target height
    return this ;
}

CChainParams & ParamsFor( const std::string & chain )
{
    if ( chain == "main" ) {
            if ( mainParams == nullptr ) mainParams.reset( new CMainParams() ) ;
            return *mainParams.get() ;
    } else if ( chain == "inu" ) {
            if ( inuParams == nullptr ) inuParams.reset( new CInuParams() ) ;
            return *inuParams.get() ;
    } else if ( chain == "test" ) {
            if ( testNetParams == nullptr ) testNetParams.reset( new CTestNetParams() ) ;
            return *testNetParams.get() ;
    } else if ( chain == "regtest" ) {
            if ( regTestParams == nullptr ) regTestParams.reset( new CRegTestParams() ) ;
            return *regTestParams.get() ;
    } else
        throw std::runtime_error( strprintf( "%s: unknown chain %s", __func__, chain ) ) ;
}

void SelectParams( const std::string & network )
{
    SelectBaseParams( network ) ;
    pCurrentParams = &ParamsFor( network ) ;
}

void UpdateRegtestBIP9Parameters( Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout )
{
    if ( regTestParams != nullptr )
        regTestParams->changeBIP9Parameters( d, nStartTime, nTimeout ) ;
}
