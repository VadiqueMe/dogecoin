// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "base58.h"
#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "validation.h"
#include "miner.h"
#include "net.h"
#include "pow.h"
#include "rpc/server.h"
#include "txmempool.h"
#include "util.h"
#include "utilstrencodings.h"
#include "validationinterface.h"

#include <random>
#include <memory>
#include <stdint.h>

#include <univalue.h>

UniValue getgenerate( const JSONRPCRequest & request )
{
    if ( request.fHelp || request.params.size() != 0 )
        throw std::runtime_error(
            "getgenerate\n"
            "\nReturn if the node is set to generate coins or not\n"
            "It is set with the command line argument -gen (or " + std::string( DOGECOIN_CONF_FILENAME ) + " setting gen)\n"
            "It can also be set with the setgenerate call\n"
            "\nResult\n"
            "true|false      (boolean) If the node is set to generate coins or not\n"
            "\nExamples:\n"
            + HelpExampleCli( "getgenerate", "" )
            + HelpExampleRpc( "getgenerate", "" )
        );

    LOCK( cs_main ) ;
    return HowManyMiningThreads() > 0 ;
}

UniValue setgenerate( const JSONRPCRequest & request )
{
    if ( request.fHelp || request.params.size() < 1 || request.params.size() > 2 )
        throw std::runtime_error(
            "setgenerate generate ( genthreads )\n"
            "\nSet 'generate' true or false to turn generation of blocks on or off\n"
            "Set 'genthreads' to the number of generating threads\n"
            "Use getgenerate call to get the current setting\n"
            "\nArguments:\n"
            "1. generate   (boolean, required) Set to true to turn on generation, false to turn it off\n"
            "2. genthreads (numeric, optional) Set the number of generating threads, -1 for the number of physical processors/cores\n"
            "\nExamples:\n"
            "\nSet the generation on using one thread\n"
            + HelpExampleCli( "setgenerate", "true 1" ) +
            "\nCheck the setting\n"
            + HelpExampleCli( "getgenerate", "" ) +
            "\nTurn off generation\n"
            + HelpExampleCli( "setgenerate", "false" ) +
            "\nUsing json rpc\n"
            + HelpExampleRpc( "setgenerate", "true, 1" )
        );

    if ( Params().MineBlocksOnDemand() )
        throw JSONRPCError( RPC_METHOD_NOT_FOUND, "Use 'generate' instead of 'setgenerate' for \"" + NameOfChain() + "\" network" ) ;

    bool fGenerate = true ;
    if ( request.params.size() > 0 )
        fGenerate = request.params[ 0 ].get_bool() ;

    int generatingThreads = GetArg( "-genthreads", DEFAULT_GENERATE_THREADS ) ;
    if ( request.params.size() > 1 )
    {
        generatingThreads = request.params[ 1 ].get_int() ;
        if ( generatingThreads == 0 ) fGenerate = false ;
    }

    GenerateCoins( fGenerate, generatingThreads, Params() ) ;

    return NullUniValue ;
}

UniValue generateBlocks( std::shared_ptr < CReserveScript > coinbaseScript, int nGenerate, uint64_t nMaxTries, bool keepScript )
{
    static const int nInnerLoopCount = 0x10000 ;

    // Dogecoin: Never mine witness tx
    const bool fMineWitnessTx = false ;

    int nHeightStart = 0 ;
    {   // don't keep cs_main locked
        LOCK( cs_main ) ;
        nHeightStart = chainActive.Height() ;
    }
    int nHeight = nHeightStart ;
    int nHeightEnd = nHeightStart + nGenerate ;

    unsigned int nExtraNonce = 0 ;
    UniValue blockHashes( UniValue::VARR ) ;

    std::random_device randomDevice ;
    std::mt19937 randomNumber( randomDevice() ) ;

    while ( nHeight < nHeightEnd && nMaxTries > 0 )
    {
        std::unique_ptr< CBlockTemplate > blockCandidate(
                BlockAssembler( Params() ).CreateNewBlock( coinbaseScript->reserveScript, fMineWitnessTx )
        ) ;
        if ( blockCandidate == nullptr )
            throw JSONRPCError( RPC_INTERNAL_ERROR, strprintf( "%s: couldn't create new block", __func__ ) ) ;

        CBlock *pblock = &blockCandidate->block ;
        {
            LOCK( cs_main ) ;
            IncrementExtraNonce( pblock, chainActive.Tip(), nExtraNonce ) ;
        }

        pblock->nNonce = randomNumber() ;

        // Dogecoin: don't mine auxpow blocks here
        //CAuxPow::initAuxPow( *pblock ) ;

        bool found = false ;
        int loop = 0 ;
        while ( nMaxTries > 0 && loop < nInnerLoopCount )
        {
            if ( CheckProofOfWork( blockCandidate->block, blockCandidate->block.nBits, Params().GetConsensus( nHeight ) ) ) {
                // found a solution
                found = true ;
                break ;
            }

            ++ pblock->nNonce ;
            ++ loop ;
            -- nMaxTries ;
        }

        if ( found )
        {
            if ( ! ProcessNewBlock( Params(), std::make_shared< const CBlock >( *pblock ), true, NULL ) )
               throw JSONRPCError( RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted" ) ;

            ++nHeight ;
            blockHashes.push_back( pblock->GetSha256Hash().GetHex() ) ;

            // keep the script because it was used at least for one coinbase output if the script came from the wallet
            if ( keepScript )
                coinbaseScript->KeepScript() ;
        }
    }

    return blockHashes ;
}

UniValue generate( const JSONRPCRequest& request )
{
    if ( request.fHelp || request.params.size() < 1 || request.params.size() > 2 )
        throw std::runtime_error(
            "generate nblocks ( maxtries )\n"
            "\nMine up to nblocks blocks immediately (before the RPC call returns)\n"
            "\nArguments:\n"
            "1. nblocks    (numeric, required) How many blocks to generate\n"
            "2. maxtries   (numeric, optional) How many iterations to try (default = 1000000)\n"
            "\nResult:\n"
            "[ blockhashes ]   (array) hashes of blocks generated\n"
            "\nExamples:\n"
            "\nGenerate 11 blocks\n"
            + HelpExampleCli( "generate", "11" )
        ) ;

    if ( NameOfChain() == "inu" )
        throw JSONRPCError( RPC_MISC_ERROR, "Use 'setgenerate' instead of 'generate' for \"" + NameOfChain() + "\" network" ) ;

    int nGenerate = request.params[ 0 ].get_int() ;
    uint64_t nMaxTries = 1000000 ;
    if ( request.params.size() > 1 ) {
        nMaxTries = request.params[ 1 ].get_int() ;
    }

    std::shared_ptr< CReserveScript > coinbaseScript ;
    GetMainSignals().ScriptForMining( coinbaseScript ) ;

    // If no script is returned at all, the keypool is exhausted
    if ( coinbaseScript == nullptr )
        throw JSONRPCError( RPC_WALLET_KEYPOOL_RAN_OUT, "Keypool ran out, please invoke keypoolrefill" ) ;

    if ( coinbaseScript->reserveScript.empty() ) // no script was provided
        throw JSONRPCError( RPC_INTERNAL_ERROR, "No coinbase script available (mining needs a wallet)" ) ;

    return generateBlocks( coinbaseScript, nGenerate, nMaxTries, true ) ;
}

UniValue generatetoaddress( const JSONRPCRequest & request )
{
    if ( request.fHelp || request.params.size() < 2 || request.params.size() > 3 )
        throw std::runtime_error(
            "generatetoaddress nblocks address (maxtries)\n"
            "\nMine blocks immediately to a specified address (before the RPC call returns)\n"
            "\nArguments:\n"
            "1. nblocks    (numeric, required) How many blocks to generate\n"
            "2. address    (string, required) The address to send the newly generated dogecoin to\n"
            "3. maxtries   (numeric, optional) How many iterations to try (default = 1000000)\n"
            "\nResult:\n"
            "[ blockhashes ]   (array) hashes of blocks generated\n"
            "\nExamples:\n"
            "\nGenerate 11 blocks to " + CDogecoinAddress::DummyDogecoinAddress( Params() ) + "\n"
            + HelpExampleCli( "generatetoaddress", "11 \"" + CDogecoinAddress::DummyDogecoinAddress( Params() ) + "\"" )
        ) ;

    if ( NameOfChain() == "inu" )
        throw JSONRPCError( RPC_MISC_ERROR, "Use 'setgenerate' instead of 'generatetoaddress' for \"" + NameOfChain() + "\" network" ) ;

    int nGenerate = request.params[ 0 ].get_int() ;
    uint64_t nMaxTries = 1000000 ;
    if ( request.params.size() > 2 ) {
        nMaxTries = request.params[ 2 ].get_int() ;
    }

    CDogecoinAddress address( request.params[ 1 ].get_str() ) ;
    if ( ! address.IsValid() )
        throw JSONRPCError( RPC_INVALID_ADDRESS_OR_KEY, "Error: Invalid address" ) ;

    std::shared_ptr< CReserveScript > coinbaseScript( new CReserveScript() ) ;
    coinbaseScript->reserveScript = GetScriptForDestination( address.Get() ) ;

    return generateBlocks( coinbaseScript, nGenerate, nMaxTries, false ) ;
}

UniValue getmininginfo( const JSONRPCRequest& request )
{
    if ( request.fHelp || request.params.size() != 0 )
        throw std::runtime_error(
            "getmininginfo\n"
            "\nReturns a json object containing mining-related information"
            "\nResult:\n"
            "{\n"
            "  \"blocks\": nnn,             (numeric) The current block\n"
            "  \"currentblocksize\": nnn,   (numeric) The last block size\n"
            "  \"currentblockweight\": nnn, (numeric) The last block weight\n"
            "  \"currentblocktx\": nnn,     (numeric) The last block transaction\n"
            "  \"errors\": \"...\"            (string) Current errors\n"
            "  \"networkhashps\": nnn,      (numeric) The network hashes per second\n"
            "  \"generate\": true|false     (boolean) If the generation is on or off (see getgenerate or setgenerate)\n"
            "  \"genthreads\": n            (numeric) Number of threads running for generation (see getgenerate or setgenerate)\n"
            "  \"pooledtx\": n              (numeric) The size of the mempool\n"
            "  \"chain\": \"xxxx\",           (string) Current network name (main, inu, test, regtest)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli( "getmininginfo", "" )
            + HelpExampleRpc( "getmininginfo", "" )
        ) ;

    LOCK( cs_main ) ;

    UniValue obj( UniValue::VOBJ ) ;
    obj.push_back(Pair( "blocks",             (int)chainActive.Height() )) ;
    obj.push_back(Pair( "currentblocksize",   (uint64_t)nLastBlockSize )) ;
    obj.push_back(Pair( "currentblockweight", (uint64_t)nLastBlockWeight )) ;
    obj.push_back(Pair( "currentblocktx",     (uint64_t)nLastBlockTx )) ;
    obj.push_back(Pair( "errors",             GetWarnings( "statusbar" ) )) ;
    obj.push_back(Pair( "generate",           getgenerate( request ) )) ;
    obj.push_back(Pair( "genthreads",         HowManyMiningThreads() )) ;
    obj.push_back(Pair( "pooledtx",           (uint64_t)mempool.size() ));
    obj.push_back(Pair( "chain",              NameOfChain() )) ;
    return obj ;
}


// NOTE: Unlike wallet RPC (which use BTC values), mining RPCs follow GBT (BIP 22) in using satoshi amounts
UniValue prioritisetransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            "prioritisetransaction <txid> <priority delta> <fee delta>\n"
            "Accepts the transaction into mined blocks at a higher (or lower) priority\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id.\n"
            "2. priority_delta (numeric, required) The priority to add or subtract.\n"
            "                  The transaction selection algorithm considers the tx as it would have a higher priority.\n"
            "                  (priority of a transaction is calculated: coinage * value_in_satoshis / txsize) \n"
            "3. fee_delta      (numeric, required) The fee value (in satoshis) to add (or subtract, if negative).\n"
            "                  The fee is not actually paid, only the algorithm for selecting transactions into a block\n"
            "                  considers the transaction as it would have paid a higher (or lower) fee.\n"
            "\nResult:\n"
            "true              (boolean) Returns true\n"
            "\nExamples:\n"
            + HelpExampleCli("prioritisetransaction", "\"txid\" 0.0 10000")
            + HelpExampleRpc("prioritisetransaction", "\"txid\", 0.0, 10000")
        );

    LOCK(cs_main);

    uint256 hash = ParseHashStr(request.params[0].get_str(), "txid");
    CAmount nAmount = request.params[2].get_int64();

    mempool.PrioritiseTransaction(hash, request.params[0].get_str(), request.params[1].get_real(), nAmount);
    return true;
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const CValidationState& state)
{
    if (state.IsValid())
        return NullUniValue;

    std::string strRejectReason = state.GetRejectReason();
    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, strRejectReason);
    if (state.IsInvalid())
    {
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

std::string gbt_vb_name(const Consensus::DeploymentPos pos) {
    const struct BIP9DeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
    std::string s = vbinfo.name;
    if (!vbinfo.gbt_force) {
        s.insert(s.begin(), '!');
    }
    return s;
}

UniValue getblocktemplate( const JSONRPCRequest & request )
{
    // Dogecoin: Never mine witness tx
    const bool fMineWitnessTx = false ;
    if ( request.fHelp || request.params.size() > 1 )
        throw std::runtime_error(
            "getblocktemplate ( TemplateRequest )\n"
            "\nIf the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.\n"
            "It returns data needed to construct a block to work on.\n"
            "For full specification, see BIPs 22, 23, 9, and 145:\n"
            "    https://github.com/bitcoin/bips/blob/master/bip-0022.mediawiki\n"
            "    https://github.com/bitcoin/bips/blob/master/bip-0023.mediawiki\n"
            "    https://github.com/bitcoin/bips/blob/master/bip-0009.mediawiki#getblocktemplate_changes\n"
            "    https://github.com/bitcoin/bips/blob/master/bip-0145.mediawiki\n"

            "\nArguments:\n"
            "1. template_request         (json object, optional) A json object in the following spec\n"
            "     {\n"
            "       \"mode\":\"template\"    (string, optional) This must be set to \"template\", \"proposal\" (see BIP 23), or omitted\n"
            "       \"capabilities\":[     (array, optional) A list of strings\n"
            "           \"support\"          (string) client side supported feature, 'longpoll', 'coinbasetxn', 'coinbasevalue', 'proposal', 'serverlist', 'workid'\n"
            "           ,...\n"
            "       ],\n"
            "       \"rules\":[            (array, optional) A list of strings\n"
            "           \"support\"          (string) client side supported softfork deployment\n"
            "           ,...\n"
            "       ]\n"
            "     }\n"
            "\n"

            "\nResult:\n"
            "{\n"
            "  \"version\" : n,                    (numeric) The preferred block version\n"
            "  \"rules\" : [ \"rulename\", ... ],    (array of strings) specific block rules that are to be enforced\n"
            "  \"vbavailable\" : {                 (json object) set of pending, supported versionbit (BIP 9) softfork deployments\n"
            "      \"rulename\" : bitnumber          (numeric) identifies the bit number as indicating acceptance and readiness for the named softfork rule\n"
            "      ,...\n"
            "  },\n"
            "  \"vbrequired\" : n,                 (numeric) bit mask of versionbits the server requires set in submissions\n"
            "  \"previousblockhash\" : \"xxxx\",     (string) The hash of current highest block\n"
            "  \"transactions\" : [                (array) contents of non-coinbase transactions that should be included in the next block\n"
            "      {\n"
            "         \"data\" : \"xxxx\",             (string) transaction data encoded in hexadecimal (byte-for-byte)\n"
            "         \"txid\" : \"xxxx\",             (string) transaction id encoded in little-endian hexadecimal\n"
            "         \"hash\" : \"xxxx\",             (string) hash encoded in little-endian hexadecimal (including witness data)\n"
            "         \"depends\" : [                (array) array of numbers \n"
            "             n                          (numeric) transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is\n"
            "             ,...\n"
            "         ],\n"
            "         \"fee\": n,                    (numeric) difference in value between transaction inputs and outputs (in atomary coin units); for coinbase transactions, this is a negative number of the total collected block fees (not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT assume there isn't one\n"
            "         \"sigops\" : n,                (numeric) total SigOps cost, as counted for purposes of block limits; if key is not present, sigop cost is unknown and clients MUST NOT assume it is zero\n"
            "         \"weight\" : n,                (numeric) total transaction weight, as counted for purposes of block limits\n"
            "         \"required\" : true|false      (boolean) if provided and true, this transaction must be in the final block\n"
            "      }\n"
            "      ,...\n"
            "  ],\n"
            "  \"coinbaseaux\" : {                 (json object) data that should be included in the coinbase's scriptSig content\n"
            "      \"flags\" : \"xx\"                  (string) key name is to be ignored, and value included in scriptSig\n"
            "  },\n"
            "  \"coinbasevalue\" : n,              (numeric) maximum allowable input to coinbase transaction, including the generation award and transaction fees (in Satoshis)\n"
            "  \"coinbasetxn\" : { ... },          (json object) information for coinbase transaction\n"
            "  \"target\" : \"xxxx\",                (string) The hash target\n"
            "  \"mintime\" : xxx,                  (numeric) The minimum timestamp appropriate for next block time in seconds since Jan 1 1970 GMT\n"
            "  \"mutable\" : [                     (array of string) list of ways the block template may be changed \n"
            "     \"value\"                          (string) A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'\n"
            "     ,...\n"
            "  ],\n"
            "  \"noncerange\" : \"00000000ffffffff\",(string) A range of valid nonces\n"
            "  \"sigoplimit\" : n,                 (numeric) limit of sigops in blocks\n"
            "  \"sizelimit\" : n,                  (numeric) limit of block size\n"
            "  \"weightlimit\" : n,                (numeric) limit of block weight\n"
            "  \"curtime\" : ttt,                  (numeric) current timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"bits\" : \"xxxxxxxx\",              (string) compressed target of next block\n"
            "  \"height\" : n                      (numeric) The height of the next block\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getblocktemplate", "")
            + HelpExampleRpc("getblocktemplate", "")
         );

    LOCK(cs_main);

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    std::set<std::string> setClientRules;
    int64_t nMaxVersionPreVB = -1;
    if (request.params.size() > 0)
    {
        const UniValue& oparam = request.params[0].get_obj();
        const UniValue& modeval = find_value(oparam, "mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull())
        {
            /* Do nothing */
        }
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = find_value(oparam, "longpollid");

        if (strMode == "proposal")
        {
            const UniValue& dataval = find_value(oparam, "data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            uint256 hash = block.GetSha256Hash() ;
            BlockMap::iterator mi = mapBlockIndex.find( hash ) ;
            if (mi != mapBlockIndex.end()) {
                CBlockIndex *pindex = mi->second;
                if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                    return "duplicate";
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return "duplicate-invalid";
                return "duplicate-inconclusive";
            }

            CBlockIndex* const pindexPrev = chainActive.Tip();
            // TestBlockValidity only supports blocks built on the current Tip
            if (block.hashPrevBlock != pindexPrev->GetBlockSha256Hash())
                return "inconclusive-not-best-prevblk";
            CValidationState state;
            TestBlockValidity(state, Params(), block, pindexPrev, false, true);
            return BIP22ValidationResult(state);
        }

        const UniValue& aClientRules = find_value(oparam, "rules");
        if (aClientRules.isArray()) {
            for (unsigned int i = 0; i < aClientRules.size(); ++i) {
                const UniValue& v = aClientRules[i];
                setClientRules.insert(v.get_str());
            }
        } else {
            // NOTE: It is important that this NOT be read if versionbits is supported
            const UniValue& uvMaxVersion = find_value(oparam, "maxversion");
            if (uvMaxVersion.isNum()) {
                nMaxVersionPreVB = uvMaxVersion.get_int64();
            }
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    if ( ! g_connman )
        throw JSONRPCError( RPC_CLIENT_P2P_DISABLED, "Peer-to-peer functionality is absent" ) ;

    if ( g_connman->GetNodeCount( CConnman::CONNECTIONS_ALL ) == 0 )
        throw JSONRPCError( RPC_CLIENT_NOT_CONNECTED, "Dogecoin is not connected!" ) ;

    if ( IsInitialBlockDownload() )
        throw JSONRPCError( RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Dogecoin is downloading blocks..." ) ;

    static unsigned int nTransactionsUpdatedLast;

    if (!lpval.isNull())
    {
        // Wait to respond until either the best block changes, OR a minute has passed and there are more transactions
        uint256 hashWatchedChain;
        boost::system_time checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr())
        {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            std::string lpstr = lpval.get_str();

            hashWatchedChain.SetHex(lpstr.substr(0, 64));
            nTransactionsUpdatedLastLP = atoi64(lpstr.substr(64));
        }
        else
        {
            // NOTE: Spec does not specify behaviour for non-string longpollid, but this makes testing easier
            hashWatchedChain = chainActive.Tip()->GetBlockSha256Hash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        // Release the wallet and main lock while waiting
        LEAVE_CRITICAL_SECTION(cs_main);
        {
            checktxtime = boost::get_system_time() + boost::posix_time::minutes(1);

            boost::unique_lock<boost::mutex> lock(csBestBlock);
            while (chainActive.Tip()->GetBlockSha256Hash() == hashWatchedChain && IsRPCRunning())
            {
                if (!cvBlockChange.timed_wait(lock, checktxtime))
                {
                    // Timeout: Check transactions for update
                    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLastLP)
                        break;
                    checktxtime += boost::posix_time::seconds(10);
                }
            }
        }
        ENTER_CRITICAL_SECTION(cs_main);

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // TODO: Maybe recheck connections/IBD and (if something wrong) send an expires-immediately template to stop miners?
    }

    const struct BIP9DeploymentInfo& segwit_info = VersionBitsDeploymentInfo[Consensus::DEPLOYMENT_SEGWIT];
    // If the caller is indicating segwit support, then allow CreateNewBlock()
    // to select witness transactions, after segwit activates (otherwise don't)
    bool fSupportsSegwit = setClientRules.find(segwit_info.name) != setClientRules.end();

    // Update block
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static std::unique_ptr< CBlockTemplate > pblocktemplate ;
    // Cache whether the last invocation was with segwit support, to avoid returning
    // a segwit-block to a non-segwit caller
    static bool fLastTemplateSupportsSegwit = true;
    if (pindexPrev != chainActive.Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5) ||
        fLastTemplateSupportsSegwit != fSupportsSegwit)
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr ;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = chainActive.Tip();
        nStart = GetTime();
        fLastTemplateSupportsSegwit = fSupportsSegwit;

        // Create new block
        CScript scriptDummy = CScript() << OP_TRUE ;
        pblocktemplate = BlockAssembler( Params() ).CreateNewBlock( scriptDummy, fMineWitnessTx ) ;
        if ( pblocktemplate == nullptr )
            throw JSONRPCError( RPC_MISC_ERROR, "Can't create new block"
                                    + std::string( ( NameOfChain() == "inu" ) ? " (not in time?)" : " (out of memory?)" ) ) ;

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew ;
    }
    CBlock* pblock = &pblocktemplate->block; // pointer for convenience
    const Consensus::Params& consensusParams = Params().GetConsensus(pindexPrev->nHeight + 1);

    // Update nTime
    UpdateTime(pblock, consensusParams, pindexPrev);
    pblock->nNonce = 0;

    // NOTE: If at some point we support pre-segwit miners post-segwit-activation, this needs to take segwit support into consideration
    const bool fPreSegWit = (THRESHOLD_ACTIVE != VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_SEGWIT, versionbitscache));

    UniValue aCaps(UniValue::VARR); aCaps.push_back("proposal");

    UniValue transactions(UniValue::VARR);
    std::map< uint256, int64_t > setTxIndex ;
    int i = 0;
    for (const auto& it : pblock->vtx) {
        const CTransaction & tx = *it ;
        uint256 txHash = tx.GetTxHash() ;
        setTxIndex[ txHash ] = i ++ ;

        if (tx.IsCoinBase())
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.push_back(Pair("data", EncodeHexTx(tx)));
        entry.push_back(Pair("txid", txHash.GetHex()));
        entry.push_back(Pair("hash", tx.GetWitnessHash().GetHex()));

        UniValue deps(UniValue::VARR);
        BOOST_FOREACH (const CTxIn &in, tx.vin)
        {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.push_back(Pair("depends", deps));

        int index_in_template = i - 1;
        entry.push_back(Pair("fee", pblocktemplate->vTxFees[index_in_template]));
        int64_t nTxSigOps = pblocktemplate->vTxSigOpsCost[index_in_template];
        if (fPreSegWit) {
            assert(nTxSigOps % WITNESS_SCALE_FACTOR == 0);
            nTxSigOps /= WITNESS_SCALE_FACTOR;
        }
        entry.push_back(Pair("sigops", nTxSigOps));
        entry.push_back(Pair("weight", GetTransactionWeight(tx)));

        transactions.push_back(entry);
    }

    UniValue aux(UniValue::VOBJ);
    aux.push_back(Pair("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end())));

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    UniValue aMutable(UniValue::VARR);
    aMutable.push_back("time");
    aMutable.push_back("transactions");
    aMutable.push_back("prevblock");

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("capabilities", aCaps));

    UniValue aRules(UniValue::VARR);
    UniValue vbavailable(UniValue::VOBJ);
    for (int j = 0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
        Consensus::DeploymentPos pos = Consensus::DeploymentPos(j);
        ThresholdState state = VersionBitsState(pindexPrev, consensusParams, pos, versionbitscache);
        switch (state) {
            case THRESHOLD_DEFINED:
            case THRESHOLD_FAILED:
                // Not exposed to GBT at all
                break;
            case THRESHOLD_LOCKED_IN:
                // Ensure bit is set in block version
                pblock->nVersion |= VersionBitsMask(consensusParams, pos);
                // FALL THROUGH to get vbavailable set...
            case THRESHOLD_STARTED:
            {
                const struct BIP9DeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
                vbavailable.push_back(Pair(gbt_vb_name(pos), consensusParams.vDeployments[pos].bit));
                if (setClientRules.find(vbinfo.name) == setClientRules.end()) {
                    if (!vbinfo.gbt_force) {
                        // If the client doesn't support this, don't indicate it in the [default] version
                        pblock->nVersion &= ~VersionBitsMask(consensusParams, pos);
                    }
                }
                break;
            }
            case THRESHOLD_ACTIVE:
            {
                // Add to rules only
                const struct BIP9DeploymentInfo& vbinfo = VersionBitsDeploymentInfo[pos];
                aRules.push_back(gbt_vb_name(pos));
                if (setClientRules.find(vbinfo.name) == setClientRules.end()) {
                    // Not supported by the client; make sure it's safe to proceed
                    if (!vbinfo.gbt_force) {
                        // If we do anything other than throw an exception here, be sure version/force isn't sent to old clients
                        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Support for '%s' rule requires explicit client support", vbinfo.name));
                    }
                }
                break;
            }
        }
    }
    result.push_back(Pair("version", pblock->nVersion));
    result.push_back(Pair("rules", aRules));
    result.push_back(Pair("vbavailable", vbavailable));
    result.push_back(Pair("vbrequired", int(0)));

    if (nMaxVersionPreVB >= 2) {
        // If VB is supported by the client, nMaxVersionPreVB is -1, so we won't get here
        // Because BIP 34 changed how the generation transaction is serialized, we can only use version/force back to v2 blocks
        // This is safe to do [otherwise-]unconditionally only because we are throwing an exception above if a non-force deployment gets activated
        // Note that this can probably also be removed entirely after the first BIP9 non-force deployment (ie, probably segwit) gets activated
        aMutable.push_back("version/force");
    }

    result.push_back(Pair("previousblockhash", pblock->hashPrevBlock.GetHex()));
    result.push_back(Pair("transactions", transactions));
    result.push_back(Pair("coinbaseaux", aux));
    result.push_back(Pair("coinbasevalue", (int64_t)pblock->vtx[0]->vout[0].nValue));
    result.push_back(Pair("longpollid", chainActive.Tip()->GetBlockSha256Hash().GetHex() + i64tostr(nTransactionsUpdatedLast)));
    result.push_back(Pair("target", hashTarget.GetHex()));
    result.push_back( Pair("mintime", ( ! Params().UseMedianTimePast() ?
                                            (int64_t)pindexPrev->GetBlockTime() + 1 :
                                                (int64_t)pindexPrev->GetMedianTimePast() + 1 )) ) ;
    result.push_back(Pair("mutable", aMutable));
    result.push_back(Pair("noncerange", "00000000ffffffff"));
    int64_t nSigOpLimit = MAX_BLOCK_SIGOPS_COST;
    if (fPreSegWit) {
        assert(nSigOpLimit % WITNESS_SCALE_FACTOR == 0);
        nSigOpLimit /= WITNESS_SCALE_FACTOR;
    }
    result.push_back(Pair("sigoplimit", nSigOpLimit));
    if (fPreSegWit) {
        result.push_back(Pair("sizelimit", (int64_t)MAX_BLOCK_BASE_SIZE));
    } else {
        result.push_back(Pair("sizelimit", (int64_t)MAX_BLOCK_SERIALIZED_SIZE));
        result.push_back(Pair("weightlimit", (int64_t)MAX_BLOCK_WEIGHT));
    }
    result.push_back(Pair("curtime", pblock->GetBlockTime()));
    result.push_back(Pair("bits", strprintf("%08x", pblock->nBits)));
    result.push_back(Pair("height", (int64_t)(pindexPrev->nHeight+1)));

    if (!pblocktemplate->vchCoinbaseCommitment.empty() && fSupportsSegwit) {
        result.push_back(Pair("default_witness_commitment", HexStr(pblocktemplate->vchCoinbaseCommitment.begin(), pblocktemplate->vchCoinbaseCommitment.end())));
    }

    return result;
}

class submitblock_StateCatcher : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    submitblock_StateCatcher(const uint256 &hashIn) : hash(hashIn), found(false), state() {}

protected:
    virtual void BlockChecked( const CBlock & block, const CValidationState & stateIn ) {
        if ( block.GetSha256Hash() != hash ) return ;
        found = true ;
        state = stateIn ;
    }
} ;

UniValue submitblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "submitblock \"hexdata\" ( \"jsonparametersobject\" )\n"
            "\nAttempts to submit new block to network.\n"
            "The 'jsonparametersobject' parameter is currently ignored.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments\n"
            "1. \"hexdata\"        (string, required) the hex-encoded block data to submit\n"
            "2. \"parameters\"     (string, optional) object of optional parameters\n"
            "    {\n"
            "      \"workid\" : \"id\"    (string, optional) if the server provided a workid, it MUST be included with submissions\n"
            "    }\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("submitblock", "\"mydata\"")
            + HelpExampleRpc("submitblock", "\"mydata\"")
        );

    std::shared_ptr<CBlock> blockptr = std::make_shared<CBlock>();
    CBlock& block = *blockptr;
    if (!DecodeHexBlk(block, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block does not start with a coinbase");
    }

    uint256 hash = block.GetSha256Hash() ;
    bool fBlockPresent = false;
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end()) {
            CBlockIndex *pindex = mi->second;
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                return "duplicate";
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return "duplicate-invalid";
            // Otherwise, we might only have the header - process the block before returning
            fBlockPresent = true;
        }
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end()) {
            int nHeight = chainActive.Height() + 1;
            UpdateUncommittedBlockStructures(block, mi->second, Params().GetConsensus(nHeight));
        }
    }

    submitblock_StateCatcher sc( block.GetSha256Hash() ) ;
    RegisterValidationInterface(&sc);
    bool fAccepted = ProcessNewBlock(Params(), blockptr, true, NULL);
    UnregisterValidationInterface(&sc);
    if (fBlockPresent)
    {
        if (fAccepted && !sc.found)
            return "duplicate-inconclusive";
        return "duplicate";
    }
    if (!sc.found)
        return "inconclusive";
    return BIP22ValidationResult(sc.state);
}

/* ************************************************************************** */
/* Merge mining.  */

UniValue getauxblockbip22(const JSONRPCRequest& request)
{
    if (request.fHelp
          || (request.params.size() != 0 && request.params.size() != 2))
        throw std::runtime_error(
            "getauxblock (hash auxpow)\n"
            "\nCreate or submit a merge-mined block.\n"
            "\nWithout arguments, create a new block and return information\n"
            "required to merge-mine it.  With arguments, submit a solved\n"
            "auxpow for a previously returned block.\n"
            "\nArguments:\n"
            "1. hash      (string, optional) hash of the block to submit\n"
            "2. auxpow    (string, optional) serialised auxpow found\n"
            "\nResult (without arguments):\n"
            "{\n"
            "  \"hash\"               (string) hash of the created block\n"
            "  \"chainid\"            (numeric) chain ID for this block\n"
            "  \"previousblockhash\"  (string) hash of the previous block\n"
            "  \"coinbasevalue\"      (numeric) value of the block's coinbase\n"
            "  \"bits\"               (string) compressed target of the block\n"
            "  \"height\"             (numeric) height of the block\n"
            "  \"target\"            (string) target in reversed sequence of bytes\n"
            "}\n"
            "\nResult (with arguments):\n"
            "xxxxx        (boolean) whether the submitted block was correct\n"
            "\nExamples:\n"
            + HelpExampleCli("getauxblock", "")
            + HelpExampleCli("getauxblock", "\"hash\" \"serialised auxpow\"")
            + HelpExampleRpc("getauxblock", "")
            );

    std::shared_ptr< CReserveScript > coinbaseScript ;
    GetMainSignals().ScriptForMining( coinbaseScript ) ;

    // If the keypool is exhausted, no script is returned at all
    if ( ! coinbaseScript )
        throw JSONRPCError( RPC_WALLET_KEYPOOL_RAN_OUT, "Keypool ran out, please invoke keypoolrefill" ) ;

    //throw an error if no script was provided
    if ( ! coinbaseScript->reserveScript.size())
        throw JSONRPCError( RPC_INTERNAL_ERROR, "No coinbase script available (mining requires a wallet)" ) ;

    if ( ! g_connman )
        throw JSONRPCError( RPC_CLIENT_P2P_DISABLED, "Peer-to-peer functionality is missing or disabled" ) ;

    if ( ! g_connman->hasConnectedNodes() && ! Params().MineBlocksOnDemand() )
        throw JSONRPCError( RPC_CLIENT_NOT_CONNECTED, "Dogecoin is not connected!" ) ;

    if ( IsInitialBlockDownload() && ! Params().MineBlocksOnDemand() )
        throw JSONRPCError( RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Dogecoin is downloading blocks..." ) ;

    /* This should never fail, since the chain is already
       past the point of merge-mining start.  Check nevertheless.  */
    {
        LOCK(cs_main);
        if (Params().GetConsensus(chainActive.Height() + 1).fAllowLegacyBlocks)
            throw std::runtime_error("getauxblock method is not yet available");
    }

    /* The variables below are used to keep track of created and not yet
       submitted auxpow blocks.  Lock them to be sure even for multiple
       RPC threads running in parallel.  */
    static CCriticalSection cs_auxblockCache;
    LOCK(cs_auxblockCache);
    static std::map<uint256, CBlock*> mapNewBlock;
    static std::vector<std::unique_ptr<CBlockTemplate>> vNewBlockTemplate;

    /* Create a new block?  */
    if (request.params.size() == 0)
    {
        static unsigned nTransactionsUpdatedLast;
        static const CBlockIndex* pindexPrev = nullptr;
        static uint64_t nStart;
        static CBlock* pblock = nullptr;
        static unsigned nExtraNonce = 0;

        // Update block
        // Dogecoin: Never mine witness tx
        const bool fMineWitnessTx = false;
        {
        LOCK(cs_main);
        if (pindexPrev != chainActive.Tip()
            || (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast
                && GetTime() - nStart > 60))
        {
            if (pindexPrev != chainActive.Tip())
            {
                // Clear old blocks since they're obsolete now
                mapNewBlock.clear();
                vNewBlockTemplate.clear();
                pblock = nullptr;
            }

            // Create new block with nonce = 0 and extraNonce = 1
            std::unique_ptr<CBlockTemplate> newBlock(BlockAssembler(Params()).CreateNewBlock(coinbaseScript->reserveScript, fMineWitnessTx));
            if (!newBlock)
                throw JSONRPCError(RPC_OUT_OF_MEMORY, "out of memory");

            // Update state only when CreateNewBlock succeeded
            nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
            pindexPrev = chainActive.Tip();
            nStart = GetTime();

            // Finalise it by setting the version and building the merkle root
            IncrementExtraNonce( &newBlock->block, pindexPrev, nExtraNonce ) ;
            newBlock->block.SetAuxpowInVersion( true ) ;

            // Save
            pblock = &newBlock->block;
            mapNewBlock[ pblock->GetSha256Hash() ] = pblock ;
            vNewBlockTemplate.push_back(std::move(newBlock));
        }
        }

        arith_uint256 target;
        bool fNegative, fOverflow;
        target.SetCompact(pblock->nBits, &fNegative, &fOverflow);
        if (fNegative || fOverflow || target == 0)
            throw std::runtime_error("invalid difficulty bits in block");

        UniValue result(UniValue::VOBJ);
        result.push_back( Pair("hash", pblock->GetSha256Hash().GetHex()) ) ;
        result.push_back( Pair("chainid", pblock->GetChainId()) ) ;
        result.push_back( Pair("previousblockhash", pblock->hashPrevBlock.GetHex()) ) ;
        result.push_back( Pair("coinbasevalue", (int64_t)pblock->vtx[0]->vout[0].nValue) ) ;
        result.push_back( Pair("bits", strprintf( "%08x", pblock->nBits )) ) ;
        result.push_back( Pair("height", static_cast< int64_t >( pindexPrev->nHeight + 1 )) ) ;
        result.push_back( Pair("target", HexStr( BEGIN(target), END(target) )) ) ;

        return result;
    }

    /* Submit a block instead.  Note that this need not lock cs_main,
       since ProcessNewBlock below locks it  */

    assert(request.params.size() == 2);
    uint256 hash;
    hash.SetHex(request.params[0].get_str());

    const std::map<uint256, CBlock*>::iterator mit = mapNewBlock.find(hash);
    if (mit == mapNewBlock.end())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "block hash unknown");
    CBlock& block = *mit->second;

    const std::vector< unsigned char > vchAuxPow = ParseHex( request.params[ 1 ].get_str() ) ;
    CDataStream ss( vchAuxPow, SER_GETHASH, PROTOCOL_VERSION ) ;
    CAuxPow auxpow ;
    ss >> auxpow ;
    block.SetAuxpow( new CAuxPow( auxpow ) ) ;
    assert( block.GetSha256Hash() == hash ) ;

    submitblock_StateCatcher sc ( block.GetSha256Hash() ) ;
    RegisterValidationInterface(&sc);
    std::shared_ptr<const CBlock> shared_block
      = std::make_shared<const CBlock>(block);
    bool fAccepted = ProcessNewBlock(Params(), shared_block, true, nullptr);
    UnregisterValidationInterface(&sc);

    if (fAccepted)
        coinbaseScript->KeepScript();

    return BIP22ValidationResult(sc.state);
}

UniValue getauxblock(const JSONRPCRequest& request)
{
    const UniValue response = getauxblockbip22(request);

    // this is a request for a new blocktemplate: return response
    if (request.params.size() == 0)
        return response;

    // this is a new block submission: return bool
    return response.isNull();
}

/* ************************************************************************** */

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "mining",             "getmininginfo",          &getmininginfo,          true,  {} },
    { "mining",             "prioritisetransaction",  &prioritisetransaction,  true,  {"txid","priority_delta","fee_delta"} },
    { "mining",             "getblocktemplate",       &getblocktemplate,       true,  {"template_request"} },
    { "mining",             "submitblock",            &submitblock,            true,  {"hexdata","parameters"} },
    { "mining",             "getauxblock",            &getauxblock,            true,  {"hash", "auxpow"} },

    { "generating",         "generate",               &generate,               true,  {"nblocks","maxtries"} },
    { "generating",         "generatetoaddress",      &generatetoaddress,      true,  {"nblocks","address","maxtries"} },
    { "generating",         "getgenerate",            &getgenerate,            true,  {} },
    { "generating",         "setgenerate",            &setgenerate,            true,  {"generate","genthreads"} },
};

void RegisterMiningRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
