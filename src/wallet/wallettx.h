// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_WALLET_WALLETTX_H
#define DOGECOIN_WALLET_WALLETTX_H

#include "merkletx.h"
#include "pubkey.h"
#include "consensus/validation.h"
#include "script/standard.h"
#include "script/ismine.h"
#include "tinyformat.h" // strprintf
#include "utilstrencodings.h" // atoi64, i64tostr

#include <list>
#include <map>
#include <string>
#include <vector>

class CConnman ;
class CWallet ;

struct COutputEntry
{
    CTxDestination destination ;
    CAmount amount ;
    int vout ;
} ;

static inline void ReadOrderPos( int64_t & nOrderPos, std::map< std::string, std::string > & mapValue )
{
    if ( mapValue.count( "n" ) == 0 ) {
        nOrderPos = -1 ; // TODO: calculate elsewhere
        return ;
    }
    nOrderPos = atoi64( mapValue[ "n" ].c_str() ) ;
}

static inline void WriteOrderPos( const int64_t & nOrderPos, std::map< std::string, std::string > & mapValue )
{
    if ( nOrderPos == -1 ) return ;
    mapValue[ "n" ] = i64tostr( nOrderPos ) ;
}

/**
 * A transaction with a bunch of additional info that only the owner cares about,
 * it includes any unrecorded transactions needed to link it back to the block chain
 */
class CWalletTx : public CMerkleTx
{

private:
    const CWallet * pwallet ;

    /** constant used in hashBlock to indicate tx has been abandoned */
    static const uint256 ABANDON_HASH ;

public:
    std::map< std::string, std::string > mapValue ;
    std::vector< std::pair< std::string, std::string > > vOrderForm ;
    unsigned int fTimeReceivedIsTxTime ;
    unsigned int nTimeReceived ; // time received by this node
    unsigned int nTimeSmart ;
    /**
     * From me flag is set to 1 for transactions that were created by the wallet
     * on this bitcoin node, and set to 0 for transactions that were created
     * externally and came in through the network or sendrawtransaction RPC
     */
    char fFromMe ;
    std::string strFromAccount ;
    int64_t nOrderPos ; // position in ordered transaction list

    // memory only
    mutable bool fDebitCached;
    mutable bool fCreditCached;
    mutable bool fImmatureCreditCached;
    mutable bool fAvailableCreditCached;
    mutable bool fWatchDebitCached;
    mutable bool fWatchCreditCached;
    mutable bool fImmatureWatchCreditCached;
    mutable bool fAvailableWatchCreditCached;
    mutable bool fChangeCached;
    mutable CAmount nDebitCached;
    mutable CAmount nCreditCached;
    mutable CAmount nImmatureCreditCached;
    mutable CAmount nAvailableCreditCached;
    mutable CAmount nWatchDebitCached;
    mutable CAmount nWatchCreditCached;
    mutable CAmount nImmatureWatchCreditCached;
    mutable CAmount nAvailableWatchCreditCached;
    mutable CAmount nChangeCached;

    CWalletTx()
    {
        Init( nullptr ) ;
    }

    CWalletTx( const CWallet * pwalletIn, CTransactionRef arg ) : CMerkleTx( std::move( arg ) )
    {
        Init( pwalletIn ) ;
    }

    void Init( const CWallet * pwalletIn )
    {
        pwallet = pwalletIn;
        mapValue.clear();
        vOrderForm.clear();
        fTimeReceivedIsTxTime = false;
        nTimeReceived = 0;
        nTimeSmart = 0;
        fFromMe = false ;
        strFromAccount.clear();
        fDebitCached = false;
        fCreditCached = false;
        fImmatureCreditCached = false;
        fAvailableCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fChangeCached = false;
        nDebitCached = 0;
        nCreditCached = 0;
        nImmatureCreditCached = 0;
        nAvailableCreditCached = 0;
        nWatchDebitCached = 0;
        nWatchCreditCached = 0;
        nAvailableWatchCreditCached = 0;
        nImmatureWatchCreditCached = 0;
        nChangeCached = 0;
        nOrderPos = -1;
    }

    ADD_SERIALIZE_METHODS ;

    template < typename Stream, typename Operation >
    inline void SerializationOp( Stream & s, Operation ser_action )
    {
        if ( ser_action.ForRead() )
            Init( nullptr ) ;

        char fSpent = false ;

        if ( ! ser_action.ForRead() )
        {
            mapValue[ "fromaccount" ] = strFromAccount ;

            WriteOrderPos( nOrderPos, mapValue ) ;

            if ( nTimeSmart )
                mapValue[ "timesmart" ] = strprintf( "%u", nTimeSmart ) ;
        }

        READWRITE( *(CMerkleTx*)this ) ;
        std::vector< CMerkleTx > vUnused ; // used to be vtxPrev
        READWRITE( vUnused ) ;
        READWRITE( mapValue ) ;
        READWRITE( vOrderForm ) ;
        READWRITE( fTimeReceivedIsTxTime ) ;
        READWRITE( nTimeReceived ) ;
        READWRITE( fFromMe ) ;
        READWRITE( fSpent ) ;

        if ( ser_action.ForRead() )
        {
            strFromAccount = mapValue[ "fromaccount" ] ;

            ReadOrderPos( nOrderPos, mapValue ) ;

            nTimeSmart = mapValue.count( "timesmart" ) ? (unsigned int)atoi64( mapValue[ "timesmart" ] ) : 0 ;
        }

        mapValue.erase("fromaccount");
        mapValue.erase("version");
        mapValue.erase("spent");
        mapValue.erase("n");
        mapValue.erase("timesmart");
    }

    // make sure balances are recalculated
    void MarkDirty()
    {
        fCreditCached = false;
        fAvailableCreditCached = false;
        fImmatureCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fDebitCached = false;
        fChangeCached = false;
    }

    void BindWallet(CWallet *pwalletIn)
    {
        pwallet = pwalletIn;
        MarkDirty();
    }

    // filter decides which addresses will count towards the debit
    CAmount GetDebit( const isminefilter & filter ) const ;
    CAmount GetCredit( const isminefilter & filter ) const ;
    CAmount GetImmatureCredit( bool fUseCache = true ) const ;
    CAmount GetAvailableCredit( bool fUseCache = true ) const ;
    CAmount GetImmatureWatchOnlyCredit( const bool & fUseCache = true ) const ;
    CAmount GetAvailableWatchOnlyCredit( const bool & fUseCache = true ) const ;
    CAmount GetChange() const ;

    void GetAmounts( std::list< COutputEntry > & listReceived,
                     std::list< COutputEntry > & listSent,
                     CAmount & nFee, std::string & strSentAccount, const isminefilter & filter ) const ;

    void GetAccountAmounts( const std::string & strAccount, CAmount & nReceived,
                            CAmount & nSent, CAmount & nFee, const isminefilter & filter ) const ;

    bool IsFromMe( const isminefilter & filter ) const
    {
        return ( GetDebit( filter ) > 0 ) ;
    }

    // true if only scriptSigs are different
    bool IsEquivalentTo( const CWalletTx & wtx ) const ;

    bool InMempool() const ;
    bool IsTrusted() const ;

    int64_t GetTxTime() const ;
    int GetRequestCount() const ;

    bool hashUnset() const {  return hashBlock.IsNull() || hashBlock == ABANDON_HASH ;  }
    bool isAbandoned() const {  return hashBlock == ABANDON_HASH ;  }
    void setAbandoned() {  hashBlock = ABANDON_HASH ;  }

    int GetBlocksToMaturity() const ;

    /**
     * Return depth of transaction in blockchain:
     * <0  : conflicts with a transaction this deep in the blockchain
     *  0  : in memory pool, waiting to be included in a block
     * >=1 : this many blocks deep in the main chain
     */
    int GetDepthInMainChain( const CBlockIndex* & pindexRet ) const ;
    int GetDepthInMainChain() const {  const CBlockIndex * pindexRet ;  return GetDepthInMainChain( pindexRet ) ;  }
    bool IsInMainChain() const {  const CBlockIndex * pindexRet ;  return GetDepthInMainChain( pindexRet ) > 0 ;  }

    // add this transaction to the mempool, fails if absolute fee exceeds absurd fee
    bool AddToMemoryPool( const CAmount & nAbsurdFee, CValidationState & state ) ;

    bool RelayWalletTransaction( CConnman * connman ) ;

    std::set< uint256 > GetConflicts() const ;

} ;
#endif
