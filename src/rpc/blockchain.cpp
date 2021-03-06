// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "coins.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "validation.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "streams.h"
#include "sync.h"
#include "txmempool.h"
#include "util.h"
#include "utillog.h"
#include "utilstrencodings.h"
#include "hash.h"

#include <stdint.h>

#include <univalue.h>

#include <mutex>
#include <condition_variable>

struct CUpdatedBlock
{
    uint256 hash;
    int height;
};

static std::mutex cs_blockchange;
static std::condition_variable cond_blockchange;
static CUpdatedBlock latestblock;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);

UniValue AuxpowToJSON(const CAuxPow& auxpow)
{
    UniValue result(UniValue::VOBJ);

    {
        UniValue tx( UniValue::VOBJ ) ;
        tx.pushKV( "hex", EncodeHexTx(auxpow) ) ;
        TxToJSON( auxpow, auxpow.parentBlock.GetSha256Hash(), tx ) ;
        result.pushKV( "tx", tx ) ;
    }

    result.pushKV( "index", auxpow.nIndex ) ;
    result.pushKV( "chainindex", auxpow.nChainIndex ) ;

    {
        UniValue branch( UniValue::VARR ) ;
        for ( const uint256 & node : auxpow.vMerkleBranch )
            branch.push_back( node.GetHex() ) ;
        result.pushKV( "merklebranch", branch ) ;
    }

    {
        UniValue branch( UniValue::VARR ) ;
        for ( const uint256 & node : auxpow.vChainMerkleBranch )
            branch.push_back( node.GetHex() ) ;
        result.pushKV( "chainmerklebranch", branch ) ;
    }

    CDataStream ssParent(SER_NETWORK, PROTOCOL_VERSION);
    ssParent << auxpow.parentBlock;
    const std::string strHex = HexStr( ssParent.begin(), ssParent.end() ) ;
    result.pushKV( "parentblock", strHex ) ;

    return result;
}

UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV( "hash", blockindex->GetBlockSha256Hash().GetHex() ) ;
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.pushKV( "confirmations", confirmations ) ;
    result.pushKV( "height", blockindex->nHeight ) ;
    result.pushKV( "version", blockindex->nVersion ) ;
    result.pushKV( "versionHex", strprintf( "%08x", blockindex->nVersion ) ) ;
    result.pushKV( "merkleroot", blockindex->hashMerkleRoot.GetHex() ) ;
    result.pushKV( "time", (int64_t)blockindex->nTime ) ;
    if ( Params().UseMedianTimePast() )
        result.pushKV( "mediantime", (int64_t)blockindex->GetMedianTimePast() ) ;
    result.pushKV( "nonce", (uint64_t)blockindex->nNonce ) ;
    result.pushKV( "bits", strprintf( "%08x", blockindex->nBits ) ) ;
    result.pushKV( "blocknewcoins", (int64_t)blockindex->nBlockNewCoins ) ;
    /* result.pushKV( "chaincoins", blockindex->nChainCoins.GetHex() ) ; */

    if ( blockindex->pprev != nullptr )
        result.pushKV( "previousblockhash", blockindex->pprev->GetBlockSha256Hash().GetHex() ) ;
    CBlockIndex * pnext = chainActive.Next( blockindex) ;
    if ( pnext != nullptr )
        result.pushKV( "nextblockhash", pnext->GetBlockSha256Hash().GetHex() ) ;

    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV( "hash", blockindex->GetBlockSha256Hash().GetHex() ) ;
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.pushKV( "confirmations", confirmations ) ;
    result.pushKV( "strippedsize", (int)::GetSerializeSize( block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS ) ) ;
    result.pushKV( "size", (int)::GetSerializeSize( block, SER_NETWORK, PROTOCOL_VERSION ) ) ;
    result.pushKV( "weight", (int)::GetBlockWeight( block ) ) ;
    result.pushKV( "height", blockindex->nHeight ) ;
    result.pushKV( "version", block.nVersion ) ;
    result.pushKV( "versionHex", strprintf( "%08x", block.nVersion ) ) ;
    result.pushKV( "merkleroot", block.hashMerkleRoot.GetHex() ) ;
    UniValue txs(UniValue::VARR);
    for(const auto& tx : block.vtx)
    {
        if(txDetails)
        {
            UniValue objTx(UniValue::VOBJ);
            TxToJSON(*tx, uint256(), objTx);
            txs.push_back(objTx);
        }
        else
            txs.push_back( tx->GetTxHash().GetHex() ) ;
    }
    result.pushKV( "tx", txs ) ;
    result.pushKV( "time", block.GetBlockTime() ) ;
    if ( Params().UseMedianTimePast() )
        result.pushKV( "mediantime", (int64_t)blockindex->GetMedianTimePast() ) ;
    result.pushKV( "nonce", (uint64_t)block.nNonce ) ;
    result.pushKV( "bits", strprintf( "%08x", block.nBits ) ) ;
    result.pushKV( "blocknewcoins", (int64_t)blockindex->nBlockNewCoins ) ;
    /* result.pushKV( "chaincoins", blockindex->nChainCoins.GetHex() ) ; */

    if ( block.auxpow != nullptr )
        result.pushKV( "auxpow", AuxpowToJSON( *block.auxpow ) ) ;

    if ( blockindex->pprev != nullptr )
        result.pushKV( "previousblockhash", blockindex->pprev->GetBlockSha256Hash().GetHex() ) ;
    CBlockIndex * pnext = chainActive.Next( blockindex ) ;
    if ( pnext = nullptr )
        result.pushKV( "nextblockhash", pnext->GetBlockSha256Hash().GetHex() ) ;

    return result ;
}

UniValue getblockcount(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getblockcount\n"
            "\nReturns the number of blocks in the longest blockchain\n"
            "\nResult:\n"
            "n    (numeric) The current block count\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockcount", "")
            + HelpExampleRpc("getblockcount", "")
        );

    LOCK(cs_main);
    return chainActive.Height();
}

UniValue getbestblockhash(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (tip) block in the longest blockchain.\n"
            "\nResult:\n"
            "\"hex\"      (string) the block hash hex encoded\n"
            "\nExamples:\n"
            + HelpExampleCli("getbestblockhash", "")
            + HelpExampleRpc("getbestblockhash", "")
        );

    LOCK( cs_main ) ;
    return chainActive.Tip()->GetBlockSha256Hash().GetHex() ;
}

void RPCNotifyBlockChange(bool ibd, const CBlockIndex * pindex)
{
    if(pindex) {
        std::lock_guard<std::mutex> lock(cs_blockchange);
        latestblock.hash = pindex->GetBlockSha256Hash();
        latestblock.height = pindex->nHeight;
    }
	cond_blockchange.notify_all();
}

UniValue waitfornewblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "waitfornewblock (timeout)\n"
            "\nWaits for a specific new block and returns useful info about it\n"
            "\nReturns the current block on timeout or exit\n"
            "\nArguments:\n"
            "1. timeout (int, optional, default=0) time in milliseconds to wait for a response, 0 means no timeout\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitfornewblock", "1000")
            + HelpExampleRpc("waitfornewblock", "1000")
        );
    int timeout = 0;
    if (request.params.size() > 0)
        timeout = request.params[0].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        block = latestblock;
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        else
            cond_blockchange.wait(lock, [&block]{return latestblock.height != block.height || latestblock.hash != block.hash || !IsRPCRunning(); });
        block = latestblock;
    }

    UniValue ret( UniValue::VOBJ ) ;
    ret.pushKV( "hash", block.hash.GetHex() ) ;
    ret.pushKV( "height", block.height ) ;
    return ret ;
}

UniValue waitforblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "waitforblock <blockhash> (timeout)\n"
            "\nWaits for a specific new block and returns useful info about it\n"
            "\nReturns the current block on timeout or exit\n"
            "\nArguments:\n"
            "1. \"blockhash\" (required, string) block hash to wait for\n"
            "2. timeout       (int, optional, default=0) time in milliseconds to wait for a response, 0 means no timeout\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
            + HelpExampleRpc("waitforblock", "\"0000000000079f8ef3d2c688c244eb7a4570b24c9ed7b4a8c619eb02596f8862\", 1000")
        );
    int timeout = 0;

    uint256 hash = uint256S(request.params[0].get_str());

    if (request.params.size() > 1)
        timeout = request.params[1].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&hash]{return latestblock.hash == hash || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&hash]{return latestblock.hash == hash || !IsRPCRunning(); });
        block = latestblock;
    }

    UniValue ret( UniValue::VOBJ ) ;
    ret.pushKV( "hash", block.hash.GetHex() ) ;
    ret.pushKV( "height", block.height ) ;
    return ret ;
}

UniValue waitforblockheight(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "waitforblockheight <height> (timeout)\n"
            "\nWaits for (at least) block height and returns the height and hash\n"
            "of the current tip.\n"
            "\nReturns the current block on timeout or exit\n"
            "\nArguments:\n"
            "1. height  (required, int) block height to wait for (integer)\n"
            "2. timeout (int, optional, default=0) time in milliseconds to wait for a response, 0 means no timeout\n"
            "\nResult:\n"
            "{                           (json object)\n"
            "  \"hash\" : {       (string) The blockhash\n"
            "  \"height\" : {     (int) Block height\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("waitforblockheight", "\"100\", 1000")
            + HelpExampleRpc("waitforblockheight", "\"100\", 1000")
        );
    int timeout = 0;

    int height = request.params[0].get_int();

    if (request.params.size() > 1)
        timeout = request.params[1].get_int();

    CUpdatedBlock block;
    {
        std::unique_lock<std::mutex> lock(cs_blockchange);
        if(timeout)
            cond_blockchange.wait_for(lock, std::chrono::milliseconds(timeout), [&height]{return latestblock.height >= height || !IsRPCRunning();});
        else
            cond_blockchange.wait(lock, [&height]{return latestblock.height >= height || !IsRPCRunning(); });
        block = latestblock;
    }
    UniValue ret( UniValue::VOBJ ) ;
    ret.pushKV( "hash", block.hash.GetHex() ) ;
    ret.pushKV( "height", block.height ) ;
    return ret ;
}

std::string EntryDescriptionString()
{
    return "    \"size\" : n,             (numeric) virtual transaction size as defined in BIP 141. This is different from actual serialized size for witness transactions as witness data is discounted.\n"
           "    \"fee\" : n,              (numeric) transaction fee in " + NameOfE8Currency() + "\n"
           "    \"modifiedfee\" : n,      (numeric) transaction fee with fee deltas used for mining priority\n"
           "    \"time\" : n,             (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT\n"
           "    \"height\" : n,           (numeric) block height when transaction entered pool\n"
           "    \"startingpriority\" : n, (numeric) DEPRECATED. Priority when transaction entered pool\n"
           "    \"currentpriority\" : n,  (numeric) DEPRECATED. Transaction priority now\n"
           "    \"descendantcount\" : n,  (numeric) number of in-mempool descendant transactions (including this one)\n"
           "    \"descendantsize\" : n,   (numeric) virtual transaction size of in-mempool descendants (including this one)\n"
           "    \"descendantfees\" : n,   (numeric) modified fees (see above) of in-mempool descendants (including this one)\n"
           "    \"ancestorcount\" : n,    (numeric) number of in-mempool ancestor transactions (including this one)\n"
           "    \"ancestorsize\" : n,     (numeric) virtual transaction size of in-mempool ancestors (including this one)\n"
           "    \"ancestorfees\" : n,     (numeric) modified fees (see above) of in-mempool ancestors (including this one)\n"
           "    \"depends\" : [           (array) unconfirmed transactions used as inputs for this transaction\n"
           "        \"transactionid\",    (string) parent transaction id\n"
           "       ... ]\n";
}

void entryToJSON(UniValue &info, const CTxMemPoolEntry &e)
{
    AssertLockHeld(mempool.cs);

    info.pushKV( "size", (int)e.GetTxSize() ) ;
    info.pushKV( "fee", ValueFromAmount( e.GetFee() ) ) ;
    info.pushKV( "modifiedfee", ValueFromAmount( e.GetModifiedFee() ) ) ;
    info.pushKV( "time", e.GetTime() ) ;
    info.pushKV( "height", (int)e.GetHeight() ) ;
    info.pushKV( "startingpriority", e.GetPriority( e.GetHeight() ) ) ;
    info.pushKV( "currentpriority", e.GetPriority( chainActive.Height() ) ) ;
    info.pushKV( "descendantcount", e.GetCountWithDescendants() ) ;
    info.pushKV( "descendantsize", e.GetSizeWithDescendants() ) ;
    info.pushKV( "descendantfees", e.GetModFeesWithDescendants() ) ;
    info.pushKV( "ancestorcount", e.GetCountWithAncestors() ) ;
    info.pushKV( "ancestorsize", e.GetSizeWithAncestors() ) ;
    info.pushKV( "ancestorfees", e.GetModFeesWithAncestors() ) ;
    const CTransaction & tx = e.GetTx() ;
    std::set< std::string > setDepends ;
    for ( const CTxIn & txin : tx.vin )
    {
        if (mempool.exists(txin.prevout.hash))
            setDepends.insert(txin.prevout.hash.ToString());
    }

    UniValue depends( UniValue::VARR ) ;
    for ( const std::string & dep : setDepends )
        depends.push_back( dep ) ;

    info.pushKV( "depends", depends ) ;
}

UniValue mempoolToJSON(bool fVerbose = false)
{
    if (fVerbose)
    {
        LOCK(mempool.cs);
        UniValue o( UniValue::VOBJ ) ;
        for ( const CTxMemPoolEntry & e : mempool.mapTx )
        {
            const uint256 & hash = e.GetTx().GetTxHash() ;
            UniValue info( UniValue::VOBJ ) ;
            entryToJSON( info, e ) ;
            o.pushKV( hash.ToString(), info ) ;
        }
        return o;
    }
    else
    {
        std::vector< uint256 > vtxid ;
        mempool.queryHashes( vtxid ) ;

        UniValue a( UniValue::VARR ) ;
        for ( const uint256 & hash : vtxid )
            a.push_back( hash.ToString() ) ;

        return a ;
    }
}

UniValue getrawmempool(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction ids in memory pool as a json array of string transaction ids.\n"
            "\nHint: use getmempoolentry to fetch a specific transaction from the mempool.\n"
            "\nArguments:\n"
            "1. verbose (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult: (for verbose = false):\n"
            "[                     (json array of string)\n"
            "  \"transactionid\"     (string) The transaction id\n"
            "  ,...\n"
            "]\n"
            "\nResult: (for verbose = true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getrawmempool", "true")
            + HelpExampleRpc("getrawmempool", "true")
        );

    bool fVerbose = false;
    if (request.params.size() > 0)
        fVerbose = request.params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getmempoolancestors(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempoolancestors txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool ancestors\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool ancestor transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolancestors", "\"mytxid\"")
            + HelpExampleRpc("getmempoolancestors", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (request.params.size() > 1)
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setAncestors;
    uint64_t noLimit = std::numeric_limits<uint64_t>::max();
    std::string dummy;
    mempool.CalculateMemPoolAncestors(*it, setAncestors, noLimit, noLimit, noLimit, noLimit, dummy, false);

    if (!fVerbose) {
        UniValue o(UniValue::VARR);
        for ( CTxMemPool::txiter ancestorIt : setAncestors ) {
            o.push_back( ancestorIt->GetTx().GetTxHash().ToString() ) ;
        }

        return o;
    } else {
        UniValue o(UniValue::VOBJ);
        for ( CTxMemPool::txiter ancestorIt : setAncestors ) {
            const CTxMemPoolEntry &e = *ancestorIt;
            const uint256 & _hash = e.GetTx().GetTxHash() ;
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.pushKV( _hash.ToString(), info ) ;
        }
        return o;
    }
}

UniValue getmempooldescendants(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2) {
        throw std::runtime_error(
            "getmempooldescendants txid (verbose)\n"
            "\nIf txid is in the mempool, returns all in-mempool descendants\n"
            "\nArguments:\n"
            "1. \"txid\"                 (string, required) The transaction id (must be in mempool)\n"
            "2. verbose                  (boolean, optional, default=false) True for a json object, false for array of transaction ids\n"
            "\nResult (for verbose=false):\n"
            "[                       (json array of strings)\n"
            "  \"transactionid\"           (string) The transaction id of an in-mempool descendant transaction\n"
            "  ,...\n"
            "]\n"
            "\nResult (for verbose=true):\n"
            "{                           (json object)\n"
            "  \"transactionid\" : {       (json object)\n"
            + EntryDescriptionString()
            + "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempooldescendants", "\"mytxid\"")
            + HelpExampleRpc("getmempooldescendants", "\"mytxid\"")
            );
    }

    bool fVerbose = false;
    if (request.params.size() > 1)
        fVerbose = request.params[1].get_bool();

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    CTxMemPool::setEntries setDescendants;
    mempool.CalculateDescendants(it, setDescendants);
    // CTxMemPool::CalculateDescendants will include the given tx
    setDescendants.erase(it);

    if (!fVerbose) {
        UniValue o( UniValue::VARR ) ;
        for ( CTxMemPool::txiter descendantIt : setDescendants ) {
            o.push_back( descendantIt->GetTx().GetTxHash().ToString() ) ;
        }

        return o;
    } else {
        UniValue o( UniValue::VOBJ ) ;
        for ( CTxMemPool::txiter descendantIt : setDescendants ) {
            const CTxMemPoolEntry &e = *descendantIt;
            const uint256 & _hash = e.GetTx().GetTxHash() ;
            UniValue info(UniValue::VOBJ);
            entryToJSON(info, e);
            o.pushKV( _hash.ToString(), info ) ;
        }
        return o;
    }
}

UniValue getmempoolentry(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getmempoolentry txid\n"
            "\nReturns mempool data for given transaction\n"
            "\nArguments:\n"
            "1. \"txid\"                   (string, required) The transaction id (must be in mempool)\n"
            "\nResult:\n"
            "{                           (json object)\n"
            + EntryDescriptionString()
            + "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolentry", "\"mytxid\"")
            + HelpExampleRpc("getmempoolentry", "\"mytxid\"")
        );
    }

    uint256 hash = ParseHashV(request.params[0], "parameter 1");

    LOCK(mempool.cs);

    CTxMemPool::txiter it = mempool.mapTx.find(hash);
    if (it == mempool.mapTx.end()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not in mempool");
    }

    const CTxMemPoolEntry &e = *it;
    UniValue info(UniValue::VOBJ);
    entryToJSON(info, e);
    return info;
}

UniValue getblockhash( const JSONRPCRequest & request )
{
    if ( request.fHelp || request.params.size() != 1 )
        throw std::runtime_error(
            "getblockhash height\n"
            "\nReturns hash of block in best-block-chain at height provided.\n"
            "\nArguments:\n"
            "1. height         (numeric, required) The height index\n"
            "\nResult:\n"
            "\"hash\"         (string) The block hash\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockhash", "1000")
            + HelpExampleRpc("getblockhash", "1000")
        );

    LOCK(cs_main);

    int nHeight = request.params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockSha256Hash().GetHex();
}

UniValue getblockheader( const JSONRPCRequest & request )
{
    if ( request.fHelp || request.params.size() < 1 || request.params.size() > 2 ) {
        throw std::runtime_error(
            /* "  \"chaincoins\" : \"xxxx\",  (string) Summary number of coins generated in blocks of the current chain, in hex\n" */
            "getblockheader \"hash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'.\n"
            "If verbose is true, returns a JSON object with information about blockheader <hash>\n"
            "\nArguments:\n"
            "1. \"hash\"          (string, required) the block hash\n"
            "2. verbose           (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) the number of confirmations, or -1 if the block is not on the current chain\n"
            "  \"height\" : n,          (numeric) the block height or index\n"
            "  \"version\" : n,         (numeric) the block version\n"
            "  \"versionHex\" : \"00000000\", (string) the block version in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) the merkle root\n"
            "  \"time\" : ttt,          (numeric) the block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) if is used by the chain, the median block time in seconds since Jan 1 1970 GMT\n"
            "  \"nonce\" : n,           (numeric) the nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) the bits\n"
            "  \"blocknewcoins\" : n,   (numeric) amount of coins generated by this block, -1 if not known\n"
            "  \"chainwork\" : \"xxxx\",   (string) maximum number of hashes to produce the current chain, in hex\n"
            "  \"previousblockhash\" : \"hash\",  (string) the hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\",      (string) the hash of the next block\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) a string that is serialized, hex-encoded data for block 'hash'\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        ) ;
    }

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (request.params.size() > 1)
        fVerbose = request.params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex * pblockindex = mapBlockIndex[ hash ] ;

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader(Params().GetConsensus(pblockindex->nHeight));
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON( pblockindex ) ;
}

UniValue getblock( const JSONRPCRequest & request )
{
    if ( request.fHelp || request.params.size() < 1 || request.params.size() > 2 ) {
        throw std::runtime_error(
            /* "  \"chaincoins\" : \"xxxx\",  (string) Summary number of coins generated in the chain up to this block, in hex\n" */
            "getblock \"blockhash\" ( verbose )\n"
            "\nIf verbose is false, returns a string that is serialized, hex-encoded data for block 'hash'.\n"
            "If verbose is true, returns a JSON object with information about block <hash>\n"
            "\nArguments:\n"
            "1. \"blockhash\"          (string, required) the block hash\n"
            "2. verbose                (boolean, optional, default=true) true for a json object, false for the hex encoded data\n"
            "\nResult (for verbose = true):\n"
            "{\n"
            "  \"hash\" : \"hash\",     (string) the hash (same as provided)\n"
            "  \"confirmations\" : n,   (numeric) the number of confirmations, or -1 if the block is not on the main chain\n"
            "  \"size\" : n,            (numeric) the block size\n"
            "  \"strippedsize\" : n,    (numeric) the block size excluding witness data\n"
            "  \"weight\" : n           (numeric) the block weight as defined in BIP 141\n"
            "  \"height\" : n,          (numeric) the block height or index\n"
            "  \"version\" : n,         (numeric) the block version\n"
            "  \"versionHex\" : \"00000000\", (string) the block version formatted in hexadecimal\n"
            "  \"merkleroot\" : \"xxxx\", (string) the merkle root\n"
            "  \"tx\" : [               (array of string) the transaction ids\n"
            "     \"transactionid\"     (string) the transaction id\n"
            "     ,...\n"
            "  ],\n"
            "  \"time\" : ttt,          (numeric) the block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mediantime\" : ttt,    (numeric) if is used by the chain, the median block time in seconds since Jan 1 1970 GMT\n"
            "  \"nonce\" : n,           (numeric) the nonce\n"
            "  \"bits\" : \"1d00ffff\", (string) the bits\n"
            "  \"blocknewcoins\" : n,   (numeric) amount of coins generated by this block, -1 if not known\n"
            "  \"chainwork\" : \"xxxx\",   (string) maximum number of hashes to produce the chain up to this block, in hex\n"
            "  \"previousblockhash\" : \"hash\",  (string) the hash of the previous block\n"
            "  \"nextblockhash\" : \"hash\"       (string) the hash of the next block\n"
            "}\n"
            "\nResult (for verbose=false):\n"
            "\"data\"             (string) a string that is serialized, hex-encoded data for block 'hash'\n"
            "\nExamples:\n"
            + HelpExampleCli("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblock", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        ) ;
    }

    LOCK(cs_main);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (request.params.size() > 1)
        fVerbose = request.params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block ;
    CBlockIndex * pblockindex = mapBlockIndex[ hash ] ;

    if ( fHavePruned && ! ( pblockindex->nStatus & BLOCK_DATA_EXISTS ) && pblockindex->nBlockTx > 0 )
        throw JSONRPCError( RPC_MISC_ERROR, "Block not available (pruned data)" ) ;

    if ( ! ReadBlockFromDisk( block, pblockindex, Params().GetConsensus( pblockindex->nHeight ) ) )
        // Block not found on disk. This could be because we have the block header
        // in our index but don't have the block (for example if a non-whitelisted
        // node sends us an unrequested long chain of valid blocks, we add the headers
        // to our index, but don't accept the block)
        throw JSONRPCError( RPC_MISC_ERROR, "Block not found on disk" ) ;

    if ( ! fVerbose )
    {
        CDataStream ssBlock( SER_NETWORK, PROTOCOL_VERSION ) ;
        ssBlock << block ;
        std::string strHex = HexStr( ssBlock.begin(), ssBlock.end() ) ;
        return strHex ;
    }

    return blockToJSON( block, pblockindex ) ;
}

struct CCoinsStats
{
    int nHeight;
    uint256 hashBlock;
    uint64_t nTransactions;
    uint64_t nTransactionOutputs;
    uint64_t nSerializedSize;
    uint256 hashSerialized;
    arith_uint256 nTotalAmount;

    CCoinsStats() : nHeight(0), nTransactions(0), nTransactionOutputs(0), nSerializedSize(0), nTotalAmount(0) {}
} ;

// Calculate statistics about the unspent transaction output set
static bool GetUTXOStats( AbstractCoinsView * view, CCoinsStats & stats )
{
    std::unique_ptr<CCoinsViewCursor> pcursor(view->Cursor());

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = pcursor->GetSha256HashOfBestBlock() ;
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    ss << stats.hashBlock;
    arith_uint256 nTotalAmount = 0;
    while ( pcursor->Valid() ) {
        if ( ! IsRPCRunning() ) return false ;
        uint256 key ;
        CCoins coins ;
        if ( pcursor->GetKey( key ) && pcursor->GetValue( coins ) ) {
            stats.nTransactions++;
            ss << key;
            for (unsigned int i=0; i<coins.vout.size(); i++) {
                const CTxOut &out = coins.vout[i];
                if (!out.IsNull()) {
                    stats.nTransactionOutputs++;
                    ss << VARINT(i+1);
                    ss << out;
                    nTotalAmount += out.nValue;
                }
            }
            stats.nSerializedSize += 32 + pcursor->GetValueSize();
            ss << VARINT(0);
        } else {
            return error("%s: unable to read value", __func__);
        }
        pcursor->Next();
    }
    stats.hashSerialized = ss.GetHash();
    stats.nTotalAmount = nTotalAmount;
    return true;
}

UniValue pruneblockchain(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "pruneblockchain\n"
            "\nArguments:\n"
            "1. \"height\"       (numeric, required) The block height to prune up to. May be set to a discrete height, or a unix timestamp\n"
            "                  to prune blocks whose block time is at least 2 hours older than the provided timestamp.\n"
            "\nResult:\n"
            "n    (numeric) Height of the last block pruned.\n"
            "\nExamples:\n"
            + HelpExampleCli("pruneblockchain", "1000")
            + HelpExampleRpc("pruneblockchain", "1000"));

    if (!fPruneMode)
        throw JSONRPCError(RPC_MISC_ERROR, "Cannot prune blocks because node is not in prune mode.");

    LOCK(cs_main);

    int heightParam = request.params[0].get_int();
    if (heightParam < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative block height.");

    // Height value more than a billion is too high to be a block height, and
    // too low to be a block time (corresponds to timestamp from Sep 2001).
    if (heightParam > 1000000000) {
        // Add a 2 hour buffer to include blocks which might have had old timestamps
        CBlockIndex* pindex = chainActive.FindEarliestAtLeast(heightParam - 7200);
        if (!pindex) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not find block with at least the specified timestamp.");
        }
        heightParam = pindex->nHeight;
    }

    unsigned int height = (unsigned int) heightParam;
    unsigned int chainHeight = (unsigned int) chainActive.Height();
    if (chainHeight < Params().PruneAfterHeight())
        throw JSONRPCError(RPC_MISC_ERROR, "Blockchain is too short for pruning.");
    else if (height > chainHeight)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Blockchain is shorter than the attempted prune height.");
    else if (height > chainHeight - MIN_BLOCKS_TO_KEEP) {
        LogPrint("rpc", "Attempt to prune blocks close to the tip.  Retaining the minimum number of blocks.");
        height = chainHeight - MIN_BLOCKS_TO_KEEP;
    }

    PruneBlockFilesManual(height);
    return uint64_t(height);
}

UniValue gettxoutsetinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns statistics about the unspent transaction output set (this may take some time)\n"
            "\nResult:\n"
            "{\n"
            "  \"height\":n,     (numeric) The current block height (index)\n"
            "  \"bestblock\": \"hex\",   (string) the best block hash hex\n"
            "  \"transactions\": n,      (numeric) The number of transactions\n"
            "  \"txouts\": n,            (numeric) The number of output transactions\n"
            "  \"bytes_serialized\": n,  (numeric) The serialized size\n"
            "  \"hash_serialized\": \"hash\",   (string) The serialized hash\n"
            "  \"total_amount\": x.xxx          (numeric) The total amount\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if ( GetUTXOStats( pcoinsTip, stats ) ) {
        ret.pushKV( "height", (int64_t)stats.nHeight ) ;
        ret.pushKV( "bestblock", stats.hashBlock.GetHex() ) ;
        ret.pushKV( "transactions", (int64_t)stats.nTransactions ) ;
        ret.pushKV( "txouts", (int64_t)stats.nTransactionOutputs ) ;
        ret.pushKV( "bytes_serialized", (int64_t)stats.nSerializedSize ) ;
        ret.pushKV( "hash_serialized", stats.hashSerialized.GetHex() ) ;
        ret.pushKV( "total_amount", ValueFromAmount( stats.nTotalAmount ) ) ;
    } else {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to read UTXO set");
    }
    return ret;
}

UniValue gettxout(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "gettxout \"txid\" n ( include_mempool )\n"
            "\nReturns details about an unspent transaction output\n"
            "\nArguments:\n"
            "1. \"txid\"       (string, required) the transaction hash\n"
            "2. n              (numeric, required) vout number\n"
            "3. include_mempool  (boolean, optional) whether to include the mempool\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\" : \"hash\",    (string) the block hash\n"
            "  \"confirmations\" : n,       (numeric) the number of confirmations\n"
            "  \"value\" : x.xxx,           (numeric) the transaction value in " + NameOfE8Currency() + "\n"
            "  \"scriptPubKey\" : {         (json object)\n"
            "     \"asm\" : \"code\",       (string) \n"
            "     \"hex\" : \"hex\",        (string) \n"
            "     \"reqSigs\" : n,          (numeric) number of required signatures\n"
            "     \"type\" : \"pubkeyhash\", (string) the type, e.g. pubkeyhash\n"
            "     \"addresses\" : [          (array of string) array of dogecoin addresses\n"
            "        \"address\"     (string) dogecoin address\n"
            "        ,...\n"
            "     ]\n"
            "  },\n"
            "  \"version\" : n,            (numeric) the version\n"
            "  \"coinbase\" : true|false   (boolean) coinbase or not\n"
            "}\n"

            "\nExamples:\n"
            "\nGet unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nView the details\n"
            + HelpExampleCli("gettxout", "\"txid\" 1") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("gettxout", "\"txid\", 1")
        );

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = request.params[1].get_int();
    bool fMempool = true;
    if (request.params.size() > 2)
        fMempool = request.params[2].get_bool();

    CCoins coins;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip, mempool);
        if (!view.GetCoins(hash, coins))
            return NullUniValue;
        mempool.pruneSpent(hash, coins); // TODO: this should be done by the CCoinsViewMemPool
    } else {
        if (!pcoinsTip->GetCoins(hash, coins))
            return NullUniValue;
    }
    if (n<0 || (unsigned int)n>=coins.vout.size() || coins.vout[n].IsNull())
        return NullUniValue;

    BlockMap::iterator it = mapBlockIndex.find( pcoinsTip->GetSha256OfBestBlock() ) ;
    CBlockIndex *pindex = it->second;
    ret.pushKV( "bestblock", pindex->GetBlockSha256Hash().GetHex() ) ;
    if ((unsigned int)coins.nHeight == MEMPOOL_HEIGHT)
        ret.pushKV( "confirmations", 0 ) ;
    else
        ret.pushKV( "confirmations", pindex->nHeight - coins.nHeight + 1 ) ;
    ret.pushKV( "value", ValueFromAmount( coins.vout[ n ].nValue ) ) ;
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToJSON(coins.vout[n].scriptPubKey, o, true);
    ret.pushKV( "scriptPubKey", o ) ;
    ret.pushKV( "version", coins.nVersion ) ;
    ret.pushKV( "coinbase", coins.fCoinBase ) ;

    return ret;
}

UniValue verifychain(const JSONRPCRequest& request)
{
    int nCheckLevel = GetArg("-checklevel", DEFAULT_CHECKLEVEL);
    int nCheckDepth = GetArg("-checkblocks", DEFAULT_CHECKBLOCKS);
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "verifychain ( checklevel nblocks )\n"
            "\nVerifies blockchain database.\n"
            "\nArguments:\n"
            "1. checklevel   (numeric, optional, 0-4, default=" + strprintf("%d", nCheckLevel) + ") How thorough the block verification is.\n"
            "2. nblocks      (numeric, optional, default=" + strprintf("%d", nCheckDepth) + ", 0=all) The number of blocks to check.\n"
            "\nResult:\n"
            "true|false       (boolean) Verified or not\n"
            "\nExamples:\n"
            + HelpExampleCli("verifychain", "")
            + HelpExampleRpc("verifychain", "")
        );

    LOCK(cs_main);

    if (request.params.size() > 0)
        nCheckLevel = request.params[0].get_int();
    if (request.params.size() > 1)
        nCheckDepth = request.params[1].get_int();

    return WVerifyDB().VerifyDB( Params(), pcoinsTip, nCheckLevel, nCheckDepth ) ;
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv( UniValue::VOBJ ) ;
    bool activated = false ;
    switch ( version )
    {
        case 2:
            activated = pindex->nHeight >= consensusParams.BIP34Height;
            break;
        case 3:
            activated = pindex->nHeight >= consensusParams.BIP66Height;
            break;
        case 4:
            int nFound = 0;
            int nRequired = consensusParams.nMajorityRejectBlockOutdated;
            CBlockIndex* pstart = pindex;
            for (int i = 0; i < consensusParams.nMajorityWindow && pstart != NULL; i++)
            {
                if ( pstart->GetBaseVersion() >= CPureBlockHeader::GetBaseVersion( version ) )
                    ++nFound ;
                pstart = pstart->pprev;
            }

            activated = nFound >= nRequired;
            rv.pushKV( "found", nFound ) ;
            rv.pushKV( "required", nRequired ) ;
            rv.pushKV( "window", consensusParams.nMajorityWindow ) ;
            break;
    }
    rv.pushKV( "status", activated ) ;
    return rv ;
}

static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv( UniValue::VOBJ ) ;
    rv.pushKV( "id", name ) ;
    rv.pushKV( "version", version ) ;
    rv.pushKV( "reject", SoftForkMajorityDesc( version, pindex, consensusParams ) ) ;
    return rv ;
}

static UniValue BIP9SoftForkDesc(const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    UniValue rv(UniValue::VOBJ);
    const ThresholdState thresholdState = VersionBitsTipState(consensusParams, id);
    switch (thresholdState) {
    case THRESHOLD_DEFINED: rv.pushKV( "status", "defined" ) ; break;
    case THRESHOLD_STARTED: rv.pushKV( "status", "started" ) ; break;
    case THRESHOLD_LOCKED_IN: rv.pushKV( "status", "locked_in" ) ; break;
    case THRESHOLD_ACTIVE: rv.pushKV( "status", "active" ) ; break;
    case THRESHOLD_FAILED: rv.pushKV( "status", "failed" ) ; break;
    }
    if (THRESHOLD_STARTED == thresholdState)
    {
        rv.pushKV( "bit", consensusParams.vDeployments[ id ].bit ) ;
    }
    rv.pushKV( "startTime", consensusParams.vDeployments[ id ].nStartTime ) ;
    rv.pushKV( "timeout", consensusParams.vDeployments[ id ].nTimeout ) ;
    rv.pushKV( "since", VersionBitsTipStateSinceHeight( consensusParams, id ) ) ;
    return rv;
}

void BIP9SoftForkDescPushBack(UniValue& bip9_softforks, const std::string &name, const Consensus::Params& consensusParams, Consensus::DeploymentPos id)
{
    // Deployments with timeout value of 0 (it guarantees a softfork will never be activated) are hidden
    // This is used when softfork codes are merged without specifying the deployment schedule
    if ( consensusParams.vDeployments[id].nTimeout > 0 )
        bip9_softforks.pushKV( name, BIP9SoftForkDesc( consensusParams, id ) ) ;
}

UniValue getblockchaininfo( const JSONRPCRequest & request )
{
    if ( request.fHelp || request.params.size() != 0 ) {
        throw std::runtime_error(
            /* "  \"chaincoins\": \"xxxx\"    (string) summary amount of coins generated in the active chain, in hexadecimal\n" */
            "getblockchaininfo\n"
            "Returns an object containing various state info regarding blockchain processing\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"xxxx\",        (string) current network name (main, inu, test, regtest)\n"
            "  \"blocks\": xxxxxx,         (numeric) the current number of blocks processed in the server\n"
            "  \"headers\": xxxxxx,        (numeric) the current number of headers we have validated\n"
            "  \"bestblockhash\": \"...\", (string) the hash of the currently best block\n"
            "  \"blocktime\": xxxxxxx,   (numeric) time of the current best block\n"
            "  \"mediantime\": xxxxxxx,  (numeric) median time for the current best block, if it's used by the chain\n"
            "  \"verificationprogress\": xxxx, (numeric) estimate of verification progress [0..1]\n"
            "  \"initialblockdownload\": xxxx, (bool) estimate of whether this node does initial block download\n"
            "  \"chainwork\": \"xxxx\"     (string) maximum number of hashes to produce the current chain, in hexadecimal\n"
            "  \"pruned\": xx,             (boolean) if the blocks are subject to pruning\n"
            "  \"pruneheight\": xxxxxx,    (numeric) lowest-height complete block stored\n"
            "  \"softforks\": [            (array) status of softforks in progress\n"
            "     {\n"
            "        \"id\": \"xxxx\",        (string) name of softfork\n"
            "        \"version\": xx,         (numeric) block version\n"
            "        \"reject\": {            (object) progress toward rejecting pre-softfork blocks\n"
            "           \"status\": xx,       (boolean) true if threshold reached\n"
            "        },\n"
            "     }, ...\n"
            "  ],\n"
            "  \"bip9_softforks\": {          (object) status of BIP9 softforks in progress\n"
            "     \"xxxx\" : {                (string) name of the softfork\n"
            "        \"status\": \"xxxx\",    (string) one of \"defined\", \"started\", \"locked_in\", \"active\", \"failed\"\n"
            "        \"bit\": xx,             (numeric) the bit (0-28) in the block version field used to signal this softfork (only for \"started\" status)\n"
            "        \"startTime\": xx,       (numeric) the minimum median time past of a block at which the bit gains its meaning\n"
            "        \"timeout\": xx,         (numeric) the median time past of a block at which the deployment is considered failed if not yet locked in\n"
            "        \"since\": xx            (numeric) height of the first block to which the status applies\n"
            "     }\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getblockchaininfo", "")
            + HelpExampleRpc("getblockchaininfo", "")
        ) ;
    }

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV( "chain",                 NameOfChain() ) ;
    obj.pushKV( "blocks",                (int)chainActive.Height() ) ;
    obj.pushKV( "headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1 ) ;
    obj.pushKV( "bestblockhash",         chainActive.Tip()->GetBlockSha256Hash().GetHex() ) ;
    obj.pushKV( "blocktime",             (int64_t)chainActive.Tip()->GetBlockTime() ) ;
    if ( Params().UseMedianTimePast() )
        obj.pushKV( "mediantime",        (int64_t)chainActive.Tip()->GetMedianTimePast() ) ;
    obj.pushKV( "verificationprogress",  GuessVerificationProgress( Params().TxData(), chainActive.Tip() ) ) ;
    obj.pushKV( "initialblockdownload",  IsInitialBlockDownload() ) ;
    /* obj.pushKV( "chaincoins",            chainActive.Tip()->nChainCoins.GetHex() ) ; */
    obj.pushKV( "pruned",                fPruneMode ) ;

    const Consensus::Params & consensusParams = Params().GetConsensus( 0 ) ;

    CBlockIndex* tip = chainActive.Tip();
    UniValue softforks(UniValue::VARR);
    UniValue bip9_softforks(UniValue::VOBJ);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    BIP9SoftForkDescPushBack(bip9_softforks, "csv", consensusParams, Consensus::DEPLOYMENT_CSV);
    BIP9SoftForkDescPushBack(bip9_softforks, "segwit", consensusParams, Consensus::DEPLOYMENT_SEGWIT);
    obj.pushKV( "softforks", softforks ) ;
    obj.pushKV( "bip9_softforks", bip9_softforks ) ;

    if (fPruneMode)
    {
        CBlockIndex * block = chainActive.Tip() ;
        while ( block && block->pprev && ( block->pprev->nStatus & BLOCK_DATA_EXISTS ) )
            block = block->pprev ;

        obj.pushKV( "pruneheight", block->nHeight ) ;
    }
    return obj;
}

/** Comparison function for sorting the getchaintips heads */
struct CompareBlocksByHeight
{
    bool operator()( const CBlockIndex* a, const CBlockIndex* b ) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction */

        if ( a->nHeight != b->nHeight )
            return a->nHeight > b->nHeight ;

        return a < b ;
    }
};

UniValue getchaintips( const JSONRPCRequest & request )
{
    if ( request.fHelp || request.params.size() != 0 )
        throw std::runtime_error(
            "getchaintips\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nResult:\n"
            "{\n"
            "    \"height\": xxxx,         (numeric) height of the chain tip\n"
            "    \"hash\": \"xxxx\"          (string) sha256 hash of the block's header\n"
            "    \"powhash\": \"xxxx\"       (string) scrypt hash of the block's header\n"
            "    \"branchlen\": xxxx       (numeric) length of branch connecting the tip to the main chain, 0 for main chain\n"
            "    \"status\": \"xxxx\"        (string) status of the chain (active, valid-fork, valid-headers, headers-only, invalid)\n"
            "}\n"
            "Possible values for status:\n"
            "    \"invalid\"               This branch contains at least one invalid block\n"
            "    \"headers-only\"          Not all blocks for this branch are available, but the headers are valid\n"
            "    \"valid-headers\"         All blocks are available for this branch, but they were never fully validated\n"
            "    \"valid-fork\"            This branch is not part of the active chain, but is fully validated\n"
            "    \"active\"                This is the tip of the currently active chain, which is certainly valid\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintips", "")
            + HelpExampleRpc("getchaintips", "")
        );

    LOCK( cs_main ) ;

    /*
     * Idea:  the set of chain tips is chainActive.tip, plus orphan blocks which do not have another orphan building off of them
     * Algorithm:
     *  - Make one pass through mapBlockIndex, picking out the orphan blocks, and also storing a set of the orphan block's pprev pointers
     *  - Iterate through the orphan blocks. If the block isn't pointed to by another orphan, it is a chain tip
     *  - add chainActive.Tip()
     */
    std::set< const CBlockIndex*, CompareBlocksByHeight > setTips ;
    std::set< const CBlockIndex* > setOrphans ;
    std::set< const CBlockIndex* > setPrevs ;

    for ( const std::pair< const uint256, CBlockIndex* > & item : mapBlockIndex )
    {
        if ( ! chainActive.Contains( item.second ) ) {
            setOrphans.insert( item.second ) ;
            setPrevs.insert( item.second->pprev ) ;
        }
    }

    for ( std::set< const CBlockIndex* >::iterator it = setOrphans.begin() ; it != setOrphans.end() ; ++ it )
    {
        if ( setPrevs.erase( *it ) == 0 )
            setTips.insert( *it ) ;
    }

    // Always add the currently active tip
    setTips.insert( chainActive.Tip() ) ;

    /* Construct the output array */
    UniValue res( UniValue::VARR ) ;
    for ( const CBlockIndex * block : setTips )
    {
        UniValue obj( UniValue::VOBJ ) ;
        obj.pushKV( "height", block->nHeight ) ;
        obj.pushKV( "hash", block->GetBlockSha256Hash().GetHex() ) ;

        CBlockHeader blockHeader = block->GetBlockHeader( Params().GetConsensus( block->nHeight ) ) ;
        obj.pushKV( "powhash", blockHeader.GetScryptHash().GetHex() ) ;

        const int branchLen = block->nHeight - chainActive.FindFork( block )->nHeight ;
        obj.pushKV( "branchlen", branchLen ) ;

        std::string status ;
        if ( chainActive.Contains( block ) ) {
            // This block is part of the currently active chain
            status = "active" ;
        } else if ( block->nStatus & BLOCK_FAILED_MASK ) {
            // This block or one of its ancestors is invalid
            status = "invalid" ;
        } else if ( block->nChainTx == 0 ) {
            // This block cannot be connected because full block data for it or one of its parents is missing
            status = "headers-only" ;
        } else if ( block->IsValid( BLOCK_VALID_SCRIPTS ) ) {
            // This block is fully validated, but no longer part of the active chain. It was probably the active block once, but was reorganized
            status = "valid-fork" ;
        } else if (block->IsValid( BLOCK_VALID_TREE ) ) {
            // The headers for this block are valid, but it has not been validated. It was probably never part of the most-work chain
            status = "valid-headers" ;
        } else {
            // No clue
            status = "unknown" ;
        }
        obj.pushKV( "status", status ) ;

        res.push_back( obj ) ;
    }

    return res ;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret( UniValue::VOBJ ) ;
    ret.pushKV( "size", (int64_t) mempool.size() ) ;
    ret.pushKV( "bytes", (int64_t) mempool.GetTotalTxSize() ) ;
    ret.pushKV( "usage", (int64_t) mempool.DynamicMemoryUsage() ) ;
    size_t maxmempool = GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    ret.pushKV( "maxmempool", (int64_t) maxmempool ) ;
    ret.pushKV( "mempoolminfee", (int64_t) 0 ) ;

    return ret ;
}

UniValue getmempoolinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx,               (numeric) Current tx count\n"
            "  \"bytes\": xxxxx,              (numeric) Sum of all virtual transaction sizes as defined in BIP 141. Differs from actual serialized size because witness data is discounted\n"
            "  \"usage\": xxxxx,              (numeric) Total memory usage for the mempool\n"
            "  \"maxmempool\": xxxxx,         (numeric) Maximum memory usage for the mempool\n"
            "  \"mempoolminfee\": xxxxx       (numeric) Minimum fee for tx to be accepted\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolinfo", "")
            + HelpExampleRpc("getmempoolinfo", "")
        );

    return mempoolInfoToJSON();
}

UniValue preciousblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "preciousblock \"blockhash\"\n"
            "\nTreats a block as if it were received before others with the same work\n"
            "\nA later preciousblock call can override the effect of an earlier one\n"
            "\nThe effects of preciousblock are not retained across restarts\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to mark as precious\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("preciousblock", "\"blockhash\"")
            + HelpExampleRpc("preciousblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CBlockIndex* pblockindex;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        pblockindex = mapBlockIndex[hash];
    }

    CValidationState state;
    PreciousBlock(state, Params(), pblockindex);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue invalidateblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "invalidateblock \"blockhash\"\n"
            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the hash of the block to mark as invalid\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("invalidateblock", "\"blockhash\"")
            + HelpExampleRpc("invalidateblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex * pblockindex = mapBlockIndex[ hash ] ;
        InvalidateBlock( state, Params(), pblockindex ) ;
    }

    if (state.IsValid()) {
        ActivateBestChain(state, Params());
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

UniValue reconsiderblock( const JSONRPCRequest & request )
{
    if ( request.fHelp || request.params.size() != 1 )
        throw std::runtime_error(
            "reconsiderblock \"blockhash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, required) the sha256 hash of the block to reconsider\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("reconsiderblock", "\"blockhash\"")
            + HelpExampleRpc("reconsiderblock", "\"blockhash\"")
        );

    std::string strHash = request.params[0].get_str() ;
    uint256 hash( uint256S( strHash ) ) ;

    {
        LOCK( cs_main ) ;
        if ( mapBlockIndex.count( hash ) == 0 )
            throw JSONRPCError( RPC_INVALID_ADDRESS_OR_KEY, "Block not found" ) ;

        CBlockIndex* pblockindex = mapBlockIndex[ hash ] ;
        ResetBlockFailureFlags( pblockindex ) ;
    }

    CValidationState state ;
    ActivateBestChain( state, Params() ) ;

    if ( ! state.IsValid() ) {
        throw JSONRPCError( RPC_DATABASE_ERROR, state.GetRejectReason() ) ;
    }

    return NullUniValue ;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafe argNames
  //  --------------------- ------------------------  -----------------------  ------ ----------
    { "blockchain",         "getblockchaininfo",      &getblockchaininfo,      true,  {} },
    { "blockchain",         "getbestblockhash",       &getbestblockhash,       true,  {} },
    { "blockchain",         "getblockcount",          &getblockcount,          true,  {} },
    { "blockchain",         "getblock",               &getblock,               true,  {"blockhash","verbose"} },
    { "blockchain",         "getblockhash",           &getblockhash,           true,  {"height"} },
    { "blockchain",         "getblockheader",         &getblockheader,         true,  {"blockhash","verbose"} },
    { "blockchain",         "getchaintips",           &getchaintips,           true,  {} },
    { "blockchain",         "getmempoolancestors",    &getmempoolancestors,    true,  {"txid","verbose"} },
    { "blockchain",         "getmempooldescendants",  &getmempooldescendants,  true,  {"txid","verbose"} },
    { "blockchain",         "getmempoolentry",        &getmempoolentry,        true,  {"txid"} },
    { "blockchain",         "getmempoolinfo",         &getmempoolinfo,         true,  {} },
    { "blockchain",         "getrawmempool",          &getrawmempool,          true,  {"verbose"} },
    { "blockchain",         "gettxout",               &gettxout,               true,  {"txid","n","include_mempool"} },
    { "blockchain",         "gettxoutsetinfo",        &gettxoutsetinfo,        true,  {} },
    { "blockchain",         "pruneblockchain",        &pruneblockchain,        true,  {"height"} },
    { "blockchain",         "verifychain",            &verifychain,            true,  {"checklevel","nblocks"} },

    { "blockchain",         "preciousblock",          &preciousblock,          true,  {"blockhash"} },

    /* Not shown in help */
    { "hidden",             "invalidateblock",        &invalidateblock,        true,  {"blockhash"} },
    { "hidden",             "reconsiderblock",        &reconsiderblock,        true,  {"blockhash"} },
    { "hidden",             "waitfornewblock",        &waitfornewblock,        true,  {"timeout"} },
    { "hidden",             "waitforblock",           &waitforblock,           true,  {"blockhash","timeout"} },
    { "hidden",             "waitforblockheight",     &waitforblockheight,     true,  {"height","timeout"} },
};

void RegisterBlockchainRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
