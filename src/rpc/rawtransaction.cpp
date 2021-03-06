// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "base58.h"
#include "chain.h"
#include "coins.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "keystore.h"
#include "validation.h"
#include "merkleblock.h"
#include "net.h"
#include "policy/policy.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "txmempool.h"
#include "uint256.h"
#include "utilstrencodings.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <stdint.h>

#include <univalue.h>

void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex)
{
    txnouttype type ;
    std::vector< CTxDestination > addresses ;

    out.pushKV( "asm", ScriptToAsmStr( scriptPubKey ) ) ;
    if ( fIncludeHex )
        out.pushKV( "hex", HexStr( scriptPubKey.begin(), scriptPubKey.end() ) ) ;

    int nRequired ;
    if ( ! ExtractDestinations( scriptPubKey, type, addresses, nRequired ) ) {
        out.pushKV( "type", GetTxnOutputType( type ) ) ;
        return ;
    }

    out.pushKV( "reqSigs", nRequired ) ;
    out.pushKV( "type", GetTxnOutputType( type ) ) ;

    UniValue a( UniValue::VARR ) ;
    for ( const CTxDestination & addr : addresses )
        a.push_back( CBase58Address( addr ).ToString() ) ;
    out.pushKV( "addresses", a ) ;
}

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry)
{
    entry.pushKV( "txid", tx.GetTxHash().GetHex() ) ;
    entry.pushKV( "hash", tx.GetWitnessHash().GetHex() ) ;
    entry.pushKV( "size", (int)::GetSerializeSize( tx, SER_NETWORK, PROTOCOL_VERSION ) ) ;
    entry.pushKV( "vsize", (int)::GetVirtualTransactionSize( tx ) ) ;
    entry.pushKV( "version", tx.nVersion ) ;
    entry.pushKV( "locktime", (int64_t)tx.nLockTime ) ;

    UniValue vin(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const CTxIn& txin = tx.vin[i];
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.pushKV( "coinbase", HexStr( txin.scriptSig.begin(), txin.scriptSig.end() ) ) ;
        else {
            in.pushKV( "txid", txin.prevout.hash.GetHex() ) ;
            in.pushKV( "vout", (int64_t)txin.prevout.n ) ;
            UniValue o( UniValue::VOBJ ) ;
            o.pushKV( "asm", ScriptToAsmStr( txin.scriptSig, true ) ) ;
            o.pushKV( "hex", HexStr( txin.scriptSig.begin(), txin.scriptSig.end() ) ) ;
            in.pushKV( "scriptSig", o ) ;
        }
        if (tx.HasWitness()) {
                UniValue txinwitness(UniValue::VARR);
                for (unsigned int j = 0; j < tx.vin[i].scriptWitness.stack.size(); j++) {
                    std::vector<unsigned char> item = tx.vin[i].scriptWitness.stack[j];
                    txinwitness.push_back(HexStr(item.begin(), item.end()));
                }
                in.pushKV( "txinwitness", txinwitness ) ;
        }
        in.pushKV( "sequence", (int64_t)txin.nSequence ) ;
        vin.push_back( in ) ;
    }
    entry.pushKV( "vin", vin ) ;
    UniValue vout( UniValue::VARR ) ;
    for ( unsigned int i = 0 ; i < tx.vout.size() ; i ++ ) {
        const CTxOut & txout = tx.vout[ i ] ;
        UniValue out( UniValue::VOBJ ) ;
        out.pushKV( "value", ValueFromAmount( txout.nValue ) ) ;
        out.pushKV( "n", (int64_t)i ) ;
        UniValue o( UniValue::VOBJ ) ;
        ScriptPubKeyToJSON( txout.scriptPubKey, o, true ) ;
        out.pushKV( "scriptPubKey", o ) ;
        vout.push_back( out ) ;
    }
    entry.pushKV( "vout", vout ) ;

    if ( ! hashBlock.IsNull() ) {
        entry.pushKV( "blockhash", hashBlock.GetHex() ) ;
        BlockMap::iterator mi = mapBlockIndex.find( hashBlock ) ;
        if ( mi != mapBlockIndex.end() && ( *mi ).second ) {
            CBlockIndex * pindex = ( *mi ).second ;
            if ( chainActive.Contains( pindex ) ) {
                entry.pushKV( "confirmations", 1 + chainActive.Height() - pindex->nHeight ) ;
                entry.pushKV( "time", pindex->GetBlockTime() ) ;
                entry.pushKV( "blocktime", pindex->GetBlockTime() ) ;
            }
            else
                entry.pushKV( "confirmations", 0 ) ;
        }
    }
}

UniValue getrawtransaction( const JSONRPCRequest & request )
{
    if ( request.fHelp || request.params.size() < 1 || request.params.size() > 2 )
        throw std::runtime_error(
            "getrawtransaction \"txid\" ( verbose )\n"

            "\nReturn the raw transaction data\n"
            "\nIf verbose is 'true', returns a json object with information about 'txid'\n"
            "If verbose is 'false' or omitted, returns a string that is serialized, hex-encoded data for 'txid'\n"

            "\nNOTE: By default this function only works for mempool transactions. If the -txindex option is\n"
            "enabled, it also works for blockchain transactions\n"
            "DEPRECATED: for now, it also works for transactions with unspent outputs\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose       (bool, optional, default=false) If false, return a string, otherwise return a json object\n"

            "\nResult (if verbose is not set or set to false):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose is set to true):\n"
            "{\n"
            "  \"hex\" : \"data\",       (string) the serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"hash\",      (string) the transaction id (same as provided)\n"
            "  \"hash\" : \"hash\",      (string) the transaction hash (differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) the serialized transaction size\n"
            "  \"vsize\" : n,            (numeric) the virtual transaction size (differs from size for witness transactions)\n"
            "  \"version\" : n,          (numeric) the version\n"
            "  \"locktime\" : ttt,       (numeric) the lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) the transaction id\n"
            "       \"vout\": n,         (numeric) \n"
            "       \"scriptSig\": {     (json object) the script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) the script sequence number\n"
            "       \"txinwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
            "     }\n"
            "     , ...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) the value in " + NameOfE8Currency() + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) the required signatures\n"
            "         \"type\" : \"pubkeyhash\",  (string) the type, e.g. 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"address\"        (string) dogecoin address\n"
            "           , ...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     , ...\n"
            "  ],\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"confirmations\" : n,      (numeric) the confirmations\n"
            "  \"time\" : ttt,             (numeric) the transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) the block time in seconds since Jan 1 1970 GMT\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" true")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", true")
        );

    LOCK(cs_main);

    uint256 hash = ParseHashV( request.params[ 0 ], "0th parameter" ) ;

    // Accept either a bool (true) or a num (>=1) to indicate verbose output
    bool fVerbose = false;
    if (request.params.size() > 1) {
        if (request.params[1].isNum()) {
            if (request.params[1].get_int() != 0) {
                fVerbose = true;
            }
        }
        else if(request.params[1].isBool()) {
            if(request.params[1].isTrue()) {
                fVerbose = true;
            }
        }
        else {
            throw JSONRPCError( RPC_TYPE_ERROR, "Invalid type for a boolean parameter \'verbose\'" ) ;
        }
    }

    CTransactionRef tx;
    uint256 hashBlock;
    // Dogecoin: Is this the best value for consensus height?
    if ( ! GetTransaction( hash, tx, Params().GetConsensus( 0 ), hashBlock, true ) )
        throw JSONRPCError( RPC_INVALID_ADDRESS_OR_KEY, std::string( fTxIndex ? "No such mempool or blockchain transaction"
            : "No such mempool transaction. Use -txindex to enable blockchain transaction queries" ) +
            ". Use gettransaction for wallet transactions" ) ;

    std::string strHex = EncodeHexTx( *tx ) ;

    if ( ! fVerbose )
        return strHex ;

    UniValue result( UniValue::VOBJ ) ;
    result.pushKV( "hex", strHex ) ;
    TxToJSON( *tx, hashBlock, result ) ;
    return result ;
}

UniValue gettxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 1 && request.params.size() != 2))
        throw std::runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded proof that \"txid\" was included in a block\n"
            "\nNOTE: Without -txindex, this function only works sometimes, when there is\n"
            "an unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index (-txindex) or specify the block\n"
            "in which the transaction is included manually (by blockhash)\n"
            "\nArguments:\n"
            "1. \"txids\"       (string) A json array of txids to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      , ...\n"
            "    ]\n"
            "2. \"blockhash\"   (string, optional) If specified, looks for txid in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A string that is a serialized, hex-encoded data for the proof\n"
        );

    std::set< uint256 > setTxHashes ;
    uint256 oneTxHash ;
    UniValue txhashes = request.params[0].get_array() ;
    for ( unsigned int idx = 0 ; idx < txhashes.size() ; idx ++ ) {
        const UniValue & txhash = txhashes[ idx ] ;
        if ( txhash.get_str().length() != 64 || ! IsHex( txhash.get_str() ) )
            throw JSONRPCError( RPC_INVALID_PARAMETER, std::string("Invalid tx hash ") + txhash.get_str() ) ;
        uint256 hash( uint256S( txhash.get_str() ) ) ;
        if ( setTxHashes.count( hash ) > 0 )
            throw JSONRPCError( RPC_INVALID_PARAMETER, std::string("Duplicated tx hash ") + txhash.get_str() ) ;
       setTxHashes.insert( hash ) ;
       oneTxHash = hash ;
    }

    LOCK(cs_main);

    CBlockIndex* pblockindex = nullptr ;

    uint256 hashBlock ;
    if ( request.params.size() > 1 )
    {
        hashBlock = uint256S( request.params[ 1 ].get_str() ) ;
        if ( mapBlockIndex.count( hashBlock ) == 0 )
            throw JSONRPCError( RPC_INVALID_ADDRESS_OR_KEY, "Block not found" ) ;
        pblockindex = mapBlockIndex[ hashBlock ] ;
    } else {
        CCoins coins ;
        if ( pcoinsTip->GetCoins( oneTxHash, coins ) && coins.nHeight > 0 && coins.nHeight <= chainActive.Height() )
            pblockindex = chainActive[ coins.nHeight ] ;
    }

    if ( pblockindex == nullptr )
    {
        CTransactionRef tx ;
        if ( ! GetTransaction( oneTxHash, tx, Params().GetConsensus(0), hashBlock, false ) || hashBlock.IsNull() )
            throw JSONRPCError( RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block" ) ;
        if ( mapBlockIndex.count( hashBlock ) == 0 )
            throw JSONRPCError( RPC_INTERNAL_ERROR, "Transaction index corrupt" ) ;
        pblockindex = mapBlockIndex[ hashBlock ] ;
    }

    CBlock block ;
    if( ! ReadBlockFromDisk( block, pblockindex, Params().GetConsensus( pblockindex->nHeight ) ) )
        throw JSONRPCError( RPC_INTERNAL_ERROR, "Can't read block from disk" ) ;

    unsigned int ntxFound = 0 ;
    for ( const auto & tx : block.vtx )
        if ( setTxHashes.count( tx->GetTxHash() ) > 0 )
            ntxFound ++ ;
    if ( ntxFound != setTxHashes.size() )
        throw JSONRPCError( RPC_INVALID_ADDRESS_OR_KEY, "(Not all) transactions not found in specified block" ) ;

    CDataStream ssMB( SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS ) ;
    CMerkleBlock mb( block, setTxHashes ) ;
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

UniValue verifytxoutproof(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a proof points to a transaction in a block, returning the transaction it commits to\n"
            "and throwing an RPC error if the block is not in our best chain\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded proof generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The txid(s) which the proof commits to, or empty array if the proof is invalid\n"
        );

    CDataStream ssMB(ParseHexV(request.params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    std::vector< uint256 > vMatch ;
    std::vector< unsigned int > vIndex ;
    if (merkleBlock.txn.ExtractMatches(vMatch, vIndex) != merkleBlock.header.hashMerkleRoot)
        return res;

    LOCK( cs_main ) ;

    if ( ! mapBlockIndex.count( merkleBlock.header.GetSha256Hash() ) ||
            ! chainActive.Contains( mapBlockIndex[ merkleBlock.header.GetSha256Hash() ] ) )
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");

    for ( const uint256 & hash : vMatch )
        res.push_back( hash.GetHex() ) ;

    return res ;
}

UniValue createrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] {\"address\":amount,\"data\":\"hex\",...} ( locktime )\n"
            "\nCreate a transaction spending the given inputs and creating new outputs,\n"
            "outputs can be addresses or data (for OP_RETURN data carrier transaction)\n"
            "\nReturns hex-encoded raw transaction\n"
            "\nNote that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network\n"

            "\nArguments:\n"
            "1. \"inputs\"                (array, required) A json array of json objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",    (string, required) The transaction id\n"
            "         \"vout\":n,         (numeric, required) The output number\n"
            "         \"sequence\":n      (numeric, optional) The sequence number\n"
            "       } \n"
            "       , ...\n"
            "     ]\n"
            "2. \"outputs\"               (object, required) a json object with outputs\n"
            "    {\n"
            "      \"address\": x.xxx,    (numeric or string, required) The key is the dogecoin address, the numeric value (can be string) is the " + NameOfE8Currency() + " amount\n"
            "      \"data\": \"hex\"      (string, required) The key is \"data\", the value is hex encoded data\n"
            "      , ...\n"
            "    }\n"
            "3. locktime                  (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
            "\nResult:\n"
            "\"transaction\"              (string) hex string of the transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"data\\\":\\\"00010203\\\"}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"data\\\":\\\"00010203\\\"}\"")
        );

    RPCTypeCheck( request.params, { UniValue::VARR, UniValue::VOBJ, UniValue::VNUM }, true ) ;

    if (request.params[0].isNull() || request.params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = request.params[0].get_array();
    UniValue sendTo = request.params[1].get_obj();

    CMutableTransaction rawTx;

    if (request.params.size() > 2 && !request.params[2].isNull()) {
        int64_t nLockTime = request.params[2].get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = nLockTime;
    }

    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence = (rawTx.nLockTime ? std::numeric_limits<uint32_t>::max() - 1 : std::numeric_limits<uint32_t>::max());

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum()) {
            int64_t seqNr64 = sequenceObj.get_int64();
            if (seqNr64 < 0 || seqNr64 > std::numeric_limits<uint32_t>::max())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, sequence number is out of range");
            else
                nSequence = (uint32_t)seqNr64;
        }

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    std::set< CBase58Address > setAddress ;
    std::vector< std::string > addrList = sendTo.getKeys() ;
    for ( const std::string & name : addrList )
    {
        if ( name == "data" ) {
            std::vector< unsigned char > data = ParseHexV( sendTo[ name ].getValStr(), "Data" ) ;

            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            CBase58Address address( name ) ;
            if ( ! address.IsValid() )
                throw JSONRPCError( RPC_INVALID_ADDRESS_OR_KEY, std::string( "Invalid Dogecoin address: " ) + name ) ;

            if ( setAddress.count( address ) )
                throw JSONRPCError( RPC_INVALID_PARAMETER, std::string( "Invalid parameter, duplicated address: " ) + name ) ;
            setAddress.insert( address ) ;

            CScript scriptPubKey = GetScriptForDestination( address.Get() ) ;
            CAmount nAmount = AmountFromValue( sendTo[ name ] ) ;

            CTxOut out( nAmount, scriptPubKey ) ;
            rawTx.vout.push_back( out ) ;
        }
    }

    return EncodeHexTx(rawTx);
}

UniValue decoderawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction\n"

            "\nArguments:\n"
            "1. \"hexstring\"      (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"hash\" : \"id\",        (string) The transaction hash (differs from txid for witness transactions)\n"
            "  \"size\" : n,             (numeric) The transaction size\n"
            "  \"vsize\" : n,            (numeric) The virtual transaction size (differs from size for witness transactions)\n"
            "  \"version\" : n,          (numeric) The version\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"txinwitness\": [\"hex\", ...] (array of string) hex-encoded witness data (if any)\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     , ...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + NameOfE8Currency() + "\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"D731rRTrFydjJdZCKNzfB5go229p59GUGD\"   (string) dogecoin address\n"
            "           , ...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     , ...\n"
            "  ],\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("decoderawtransaction", "\"hexstring\"")
            + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
        );

    LOCK(cs_main);
    RPCTypeCheck( request.params, { UniValue::VSTR } ) ;

    CMutableTransaction mtx;

    if (!DecodeHexTx(mtx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    UniValue result(UniValue::VOBJ);
    TxToJSON(CTransaction(std::move(mtx)), uint256(), result);

    return result;
}

UniValue decodescript(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodescript \"hexstring\"\n"
            "\nDecode a hex-encoded script\n"
            "\nArguments:\n"
            "1. \"hexstring\"     (string) the hex encoded script\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script public key\n"
            "  \"hex\":\"hex\",   (string) hex encoded public key\n"
            "  \"type\":\"type\", (string) The output type\n"
            "  \"reqSigs\": n,    (numeric) The required signatures\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) dogecoin address\n"
            "     , ...\n"
            "  ],\n"
            "  \"p2sh\",\"address\" (string) address of P2SH script wrapping this redeem script (not returned if the script is already a P2SH)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("decodescript", "\"hexstring\"")
            + HelpExampleRpc("decodescript", "\"hexstring\"")
        );

    RPCTypeCheck( request.params, { UniValue::VSTR } ) ;

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (request.params[0].get_str().size() > 0){
        std::vector< unsigned char > scriptData( ParseHexV( request.params[0], "argument" ) ) ;
        script = CScript( scriptData.begin(), scriptData.end() ) ;
    } else {
        // empty scripts are ok
    }
    ScriptPubKeyToJSON(script, r, false);

    UniValue type;
    type = find_value(r, "type");

    if ( type.isStr() && type.get_str() != "scripthash" ) {
        // P2SH cannot be wrapped in a P2SH. If this script is already a P2SH,
        // don't return the address for a P2SH of the P2SH
        r.pushKV( "p2sh", CBase58Address( CScriptID( script ) ).ToString() ) ;
    }

    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet */
static void TxInErrorToJSON( const CTxIn & txin, UniValue & vErrorsRet, const std::string & strMessage )
{
    UniValue entry( UniValue::VOBJ ) ;
    entry.pushKV( "txid", txin.prevout.hash.ToString() ) ;
    entry.pushKV( "vout", (uint64_t)txin.prevout.n ) ;
    entry.pushKV( "scriptSig", HexStr( txin.scriptSig.begin(), txin.scriptSig.end() ) ) ;
    entry.pushKV( "sequence", (uint64_t)txin.nSequence ) ;
    entry.pushKV( "error", strMessage ) ;
    vErrorsRet.push_back( entry ) ;
}

UniValue signrawtransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "signrawtransaction \"hexstring\" sighashtype ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded)\n"
            "\nThe third optional argument (may be null) is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the block chain\n"
            "\nThe fourth optional argument (may be null) is an array of base58-encoded private keys\n"
            "that, if given, will be the only keys used to sign the transaction\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphraseWithNewline() +
#endif
            "Arguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string\n"
            "2. \"sighashtype\"   (string, optional, default=ALL) The signature hash type. One of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"
            "3. \"prevtxs\"       (string, optional) An json array of previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction hash\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) script key\n"
            "         \"redeemScript\": \"hex\",   (string, required for P2SH or P2WSH) redeem script\n"
            "         \"amount\": value            (numeric, required) The amount spent\n"
            "       }\n"
            "       , ...\n"
            "    ]\n"
            "4. \"privkeys\"     (string, optional) A json array of base58-encoded private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) private key in base58-encoding\n"
            "      , ...\n"
            "    ]\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",           (string) The hash of the referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence number\n"
            "      \"error\" : \"text\"           (string) Verification or signing error related to the input\n"
            "    }\n"
            "    , ...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"")
            + HelpExampleRpc("signrawtransaction", "\"myhex\"")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck( request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VARR, UniValue::VARR }, true ) ;

    std::vector< unsigned char > txData( ParseHexV( request.params[ 0 ], "0th argument" ) ) ;
    CDataStream ssData( txData, SER_NETWORK, PROTOCOL_VERSION ) ;
    std::vector< CMutableTransaction > txVariants ;
    while ( ! ssData.empty() ) {
        try {
            CMutableTransaction tx ;
            ssData >> tx ;
            txVariants.push_back( tx ) ;
        }
        catch ( const std::exception & ) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    // mergedTx will end up with all the signatures
    // it begins as a clone of the rawtx
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs)
    TrivialCoinsView viewDummy ;
    CCoinsViewCache view( &viewDummy ) ;
    {
        LOCK( mempool.cs ) ;
        CCoinsViewCache & viewChain = *pcoinsTip ;
        CCoinsViewMemPool viewMempool( &viewChain, mempool ) ;
        view.SetBackend( viewMempool ) ; // temporarily switch cache backend to db+mempool view

        for ( const CTxIn & txin : mergedTx.vin ) {
            const uint256& prevHash = txin.prevout.hash;
            CCoins coins;
            view.AccessCoins(prevHash); // this is certainly allowed to fail
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    bool fGivenKeys = false ;
    CBasicKeyStore tempKeystore ;
    if ( request.params.size() > 3 && ! request.params[ 3 ].isNull() ) {
        fGivenKeys = true ;
        UniValue keys = request.params[ 3 ].get_array() ;
        for ( unsigned int idx = 0 ; idx < keys.size() ; idx ++ ) {
            UniValue k = keys[ idx ] ;
            CBase58Secret vchSecret ;
            bool fGood = vchSecret.SetString( k.get_str(), Params() ) ;
            if ( ! fGood )
                throw JSONRPCError( RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key" ) ;
            CKey key = vchSecret.GetKey() ;
            if ( ! key.IsValid() )
                throw JSONRPCError( RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range" ) ;
            tempKeystore.AddKey( key ) ;
        }
    }
#ifdef ENABLE_WALLET
    else if ( pwalletMain != nullptr )
        EnsureWalletIsUnlocked() ;
#endif

    // Add previous txouts given in the RPC call:
    if ( request.params.size() > 2 && ! request.params[ 2 ].isNull() ) {
        UniValue prevTxs = request.params[ 2 ].get_array() ;
        for (unsigned int idx = 0; idx < prevTxs.size(); idx++) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut,
                {
                    {"txid", UniValueType(UniValue::VSTR)},
                    {"vout", UniValueType(UniValue::VNUM)},
                    {"scriptPubKey", UniValueType(UniValue::VSTR)},
                });

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            std::vector< unsigned char > pkData( ParseHexO( prevOut, "scriptPubKey" ) ) ;
            CScript scriptPubKey( pkData.begin(), pkData.end() ) ;

            {
                CCoinsModifier coins = view.ModifyCoins(txid);
                if (coins->IsAvailable(nOut) && coins->vout[nOut].scriptPubKey != scriptPubKey) {
                    std::string err( "Previous output scriptPubKey mismatch:\n" ) ;
                    err = err + ScriptToAsmStr(coins->vout[nOut].scriptPubKey) + "\nvs:\n"+
                        ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                if ((unsigned int)nOut >= coins->vout.size())
                    coins->vout.resize(nOut+1);
                coins->vout[nOut].scriptPubKey = scriptPubKey;
                coins->vout[nOut].nValue = 0;
                if (prevOut.exists("amount")) {
                    coins->vout[nOut].nValue = AmountFromValue(find_value(prevOut, "amount"));
                }
            }

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && (scriptPubKey.IsPayToScriptHash() || scriptPubKey.IsPayToWitnessScriptHash())) {
                RPCTypeCheckObj(prevOut,
                    {
                        {"txid", UniValueType(UniValue::VSTR)},
                        {"vout", UniValueType(UniValue::VNUM)},
                        {"scriptPubKey", UniValueType(UniValue::VSTR)},
                        {"redeemScript", UniValueType(UniValue::VSTR)},
                    });
                UniValue v = find_value(prevOut, "redeemScript");
                if ( ! v.isNull() ) {
                    std::vector< unsigned char > rsData( ParseHexV( v, "redeemScript" ) ) ;
                    CScript redeemScript( rsData.begin(), rsData.end() ) ;
                    tempKeystore.AddCScript( redeemScript ) ;
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ( ( fGivenKeys || pwalletMain == nullptr ) ? tempKeystore : *pwalletMain ) ;
#else
    const CKeyStore& keystore = tempKeystore ;
#endif

    int nHashType = SIGHASH_ALL ;
    if ( request.params.size() > 1 && ! request.params[ 1 ].isNull() ) {
        static std::map< std::string, int > mapSigHashValues = {
            { std::string( "ALL" ), int(SIGHASH_ALL) },
            { std::string( "ALL|ANYONECANPAY" ), int(SIGHASH_ALL|SIGHASH_ANYONECANPAY) },
            { std::string( "NONE" ), int(SIGHASH_NONE) },
            { std::string( "NONE|ANYONECANPAY" ), int(SIGHASH_NONE|SIGHASH_ANYONECANPAY) },
            { std::string( "SINGLE" ), int(SIGHASH_SINGLE) },
            { std::string( "SINGLE|ANYONECANPAY" ), int(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY) }
        } ;
        std::string strHashType = request.params[ 1 ].get_str() ;
        if ( mapSigHashValues.count( strHashType ) > 0 )
            nHashType = mapSigHashValues[ strHashType ] ;
        else
            throw JSONRPCError( RPC_INVALID_PARAMETER, "Invalid sighashtype parameter \"" + strHashType + "\"" ) ;
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the transaction to avoid rehashing
    const CTransaction txConst(mergedTx);
    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
        CTxIn& txin = mergedTx.vin[i];
        const CCoins* coins = view.AccessCoins(txin.prevout.hash);
        if (coins == NULL || !coins->IsAvailable(txin.prevout.n)) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript& prevPubKey = coins->vout[txin.prevout.n].scriptPubKey;
        const CAmount& amount = coins->vout[txin.prevout.n].nValue;

        SignatureData sigdata;
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            ProduceSignature(MutableTransactionSignatureCreator(&keystore, &mergedTx, i, amount, nHashType), prevPubKey, sigdata);

        // ... and merge in other signatures:
        for ( const CMutableTransaction & txv : txVariants ) {
            if (txv.vin.size() > i) {
                sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount), sigdata, DataFromTransaction(txv, i));
            }
        }

        UpdateTransaction(mergedTx, i, sigdata);

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, &txin.scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, amount), &serror)) {
            TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
        }
    }
    bool fComplete = vErrors.empty();

    UniValue result( UniValue::VOBJ ) ;
    result.pushKV( "hex", EncodeHexTx( mergedTx ) ) ;
    result.pushKV( "complete", fComplete ) ;
    if ( ! vErrors.empty() )
        result.pushKV( "errors", vErrors ) ;

    return result ;
}

UniValue sendrawtransaction(const JSONRPCRequest& request)
{
    if ( request.fHelp || request.params.size() != 1 )
        throw std::runtime_error(
            "sendrawtransaction \"hexstring\"\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network\n"
            "\nAlso see createrawtransaction and signrawtransaction calls\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction)\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n"
            + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendrawtransaction", "\"signedhex\"")
        );

    LOCK(cs_main);
    RPCTypeCheck( request.params, { UniValue::VSTR } ) ;

    // parse hex string from parameter
    CMutableTransaction mtx ;
    if ( ! DecodeHexTx( mtx, request.params[ 0 ].get_str() ) )
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    const uint256 & hashTx = tx->GetTxHash() ;

    bool fLimitFree = false ;

    CCoinsViewCache &view = *pcoinsTip;
    const CCoins* existingCoins = view.AccessCoins(hashTx);
    bool fHaveMempool = mempool.exists(hashTx);
    bool fHaveChain = existingCoins && existingCoins->nHeight < 1000000000;
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        CValidationState state;
        bool fMissingInputs;
        if ( ! AcceptToMemoryPool(mempool, state, std::move(tx), fLimitFree, &fMissingInputs, NULL ) ) {
            if (state.IsInvalid()) {
                throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            } else {
                if (fMissingInputs) {
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                }
                throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
            }
        }
    } else if (fHaveChain) {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    }
    if ( g_connman == nullptr )
        throw JSONRPCError( RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality is absent" ) ;

    CInv inv(MSG_TX, hashTx);
    g_connman->ForEachNode([&inv](CNode* pnode)
    {
        pnode->PushInventory(inv);
    });
    return hashTx.GetHex();
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "rawtransactions",    "getrawtransaction",      &getrawtransaction,      true,  {"txid","verbose"} },
    { "rawtransactions",    "createrawtransaction",   &createrawtransaction,   true,  {"inputs","outputs","locktime"} },
    { "rawtransactions",    "decoderawtransaction",   &decoderawtransaction,   true,  {"hexstring"} },
    { "rawtransactions",    "decodescript",           &decodescript,           true,  {"hexstring"} },
    { "rawtransactions",    "sendrawtransaction",     &sendrawtransaction,     false, {"hexstring"} },
    { "rawtransactions",    "signrawtransaction",     &signrawtransaction,     false, {"hexstring","sighashtype","prevtxs","privkeys"} }, /* uses wallet if enabled */

    { "blockchain",         "gettxoutproof",          &gettxoutproof,          true,  {"txids", "blockhash"} },
    { "blockchain",         "verifytxoutproof",       &verifytxoutproof,       true,  {"proof"} },
};

void RegisterRawTransactionRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
