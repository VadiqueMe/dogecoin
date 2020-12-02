// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "wallet/wallettx.h"

#include "script/ismine.h"
#include "chainparams.h"
#include "net.h"
#include "txmempool.h"
#include "validation.h"
#include "wallet/wallet.h"

// constant used in hashBlock to indicate tx has been abandoned
const uint256 CWalletTx::ABANDON_HASH( uint256S( "0000000000000000000000000000000000000000000000000000000000000001" ) ) ;

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart ;
    return ( n != 0 ) ? n : nTimeReceived ;
}

int CWalletTx::GetRequestCount() const
{
    // Returns -1 if it wasn't being tracked
    int nRequests = -1 ;
    {
        LOCK( pwallet->cs_wallet ) ;
        if ( IsCoinBase() )
        {
            // Generated block
            if ( ! hashUnset() )
            {
                std::map< uint256, int >::const_iterator mi = pwallet->mapRequestCount.find( hashBlock ) ;
                if ( mi != pwallet->mapRequestCount.end() )
                    nRequests = ( *mi ).second ;
            }
        }
        else
        {
            // Did anyone request this transaction?
            std::map< uint256, int >::const_iterator mi = pwallet->mapRequestCount.find( GetTxHash() ) ;
            if ( mi != pwallet->mapRequestCount.end() )
            {
                nRequests = ( *mi ).second ;

                // How about the block it's in?
                if (nRequests == 0 && !hashUnset())
                {
                    std::map< uint256, int >::const_iterator _mi = pwallet->mapRequestCount.find( hashBlock ) ;
                    if ( _mi != pwallet->mapRequestCount.end() )
                        nRequests = ( *_mi ).second ;
                    else
                        nRequests = 1 ; // if it's in someone else's block it must have got out
                }
            }
        }
    }
    return nRequests;
}

void CWalletTx::GetAmounts( std::list< COutputEntry > & listReceived,
                            std::list< COutputEntry > & listSent,
                            CAmount & nFee, std::string & strSentAccount, const isminefilter & filter ) const
{
    nFee = 0 ;
    listReceived.clear() ;
    listSent.clear() ;
    strSentAccount = strFromAccount ;

    // Compute fee
    CAmount nDebit = GetDebit( filter ) ;
    if ( nDebit > 0 ) // debit > 0 means we signed/sent this transaction
    {
        CAmount nValueOut = tx->GetValueOut() ;
        nFee = nDebit - nValueOut ;
    }

    // Sent/received
    for (unsigned int i = 0; i < tx->vout.size(); ++i)
    {
        const CTxOut& txout = tx->vout[i];
        isminetype fIsMine = pwallet->IsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
                continue;
        }
        else if (!(fIsMine & filter))
            continue;

        // In either case, we need to get the destination address
        CTxDestination address;

        if (!ExtractDestination(txout.scriptPubKey, address) && !txout.scriptPubKey.IsUnspendable())
        {
            LogPrintf( "CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",
                       this->GetTxHash().ToString() ) ;
            address = CNoDestination();
        }

        COutputEntry output = {address, txout.nValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine & filter)
            listReceived.push_back(output);
    }

}

void CWalletTx::GetAccountAmounts( const std::string & strAccount, CAmount & nReceived,
                                   CAmount & nSent, CAmount & nFee, const isminefilter & filter ) const
{
    nReceived = nSent = nFee = 0 ;

    CAmount allFee ;
    std::string strSentAccount ;
    std::list< COutputEntry > listReceived ;
    std::list< COutputEntry > listSent ;
    GetAmounts( listReceived, listSent, allFee, strSentAccount, filter ) ;

    if (strAccount == strSentAccount)
    {
        for ( const COutputEntry & s : listSent )
            nSent += s.amount;
        nFee = allFee;
    }
    {
        LOCK(pwallet->cs_wallet);
        for ( const COutputEntry & r : listReceived )
        {
            if ( pwallet->mapAddressBook.count( r.destination ) )
            {
                std::map< CTxDestination, CAddressBookData >::const_iterator mi = pwallet->mapAddressBook.find( r.destination ) ;
                if ( mi != pwallet->mapAddressBook.end() && ( *mi ).second.name == strAccount )
                    nReceived += r.amount ;
            }
            else if ( strAccount.empty() )
            {
                nReceived += r.amount ;
            }
        }
    }
}

int CWalletTx::GetBlocksToMaturity() const
{
    if ( ! IsCoinBase() ) return 0 ;

    int coinbaseMaturity = Params().GetConsensus( chainActive.Height() ).nCoinbaseMaturity ;
    return std::max( 0, 1 + coinbaseMaturity - GetDepthInMainChain() ) ;
}

int CWalletTx::GetDepthInMainChain( const CBlockIndex* & pindexRet ) const
{
    if ( hashUnset() ) return 0 ;

    AssertLockHeld( cs_main ) ;

    // Find the block it claims to be in
    BlockMap::iterator mi = mapBlockIndex.find( hashBlock ) ;
    if ( mi == mapBlockIndex.end() )
        return 0 ;
    CBlockIndex * pindex = ( *mi ).second ;
    if ( pindex == nullptr || ! chainActive.Contains( pindex ) )
        return 0 ;

    pindexRet = pindex ;
    return ( ( nIndex == -1 ) ? -1 : 1 ) * ( chainActive.Height() - pindex->nHeight + 1 ) ;
}

// add this transaction to the mempool, TODO: fail if absolute fee exceeds absurd fee
bool CWalletTx::AddToMemoryPool( const CAmount & nAbsurdFee, CValidationState & state )
{
    ( void ) nAbsurdFee ;
    return ::AcceptToMemoryPool( mempool, state, tx, true, NULL, NULL ) ;
}

bool CWalletTx::RelayWalletTransaction( CConnman * connman )
{
    assert( pwallet->GetBroadcastTransactions() ) ;
    if ( ! IsCoinBase() && ! isAbandoned() && GetDepthInMainChain() == 0 )
    {
        CValidationState state ;
        /* GetDepthInMainChain already catches known conflicts */
        if ( InMempool() || AddToMemoryPool( maxTxFee, state ) ) {
            LogPrintf( "Relaying wtx %s\n", GetTxHash().ToString() ) ;
            if ( connman != nullptr ) {
                CInv inv( MSG_TX, GetTxHash() ) ;
                connman->ForEachNode([ &inv ]( CNode* pnode )
                {
                    pnode->PushInventory( inv ) ;
                }) ;
                return true ;
            }
        }
    }
    return false ;
}

std::set< uint256 > CWalletTx::GetConflicts() const
{
    std::set< uint256 > result ;
    if ( pwallet != nullptr )
    {
        uint256 myHash = GetTxHash() ;
        result = pwallet->GetConflicts( myHash ) ;
        result.erase( myHash ) ;
    }
    return result ;
}

CAmount CWalletTx::GetDebit( const isminefilter & filter ) const
{
    if ( tx->vin.empty() ) return 0 ;

    CAmount debit = 0 ;
    if ( filter & ISMINE_SPENDABLE )
    {
        if ( fDebitCached )
            debit += nDebitCached ;
        else
        {
            nDebitCached = pwallet->GetDebit( *this, ISMINE_SPENDABLE ) ;
            fDebitCached = true ;
            debit += nDebitCached ;
        }
    }
    if ( filter & ISMINE_WATCH_ONLY )
    {
        if ( fWatchDebitCached )
            debit += nWatchDebitCached ;
        else
        {
            nWatchDebitCached = pwallet->GetDebit( *this, ISMINE_WATCH_ONLY ) ;
            fWatchDebitCached = true ;
            debit += nWatchDebitCached ;
        }
    }
    return debit ;
}

CAmount CWalletTx::GetCredit( const isminefilter & filter ) const
{
    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if ( IsCoinBase() && GetBlocksToMaturity() > 0 )
        return 0 ;

    CAmount credit = 0 ;
    if ( filter & ISMINE_SPENDABLE )
    {
        // GetBalance can assume transactions in mapWallet won't change
        if ( fCreditCached )
            credit += nCreditCached ;
        else
        {
            nCreditCached = pwallet->GetCredit( *this, ISMINE_SPENDABLE ) ;
            fCreditCached = true ;
            credit += nCreditCached ;
        }
    }
    if ( filter & ISMINE_WATCH_ONLY )
    {
        if ( fWatchCreditCached )
            credit += nWatchCreditCached ;
        else
        {
            nWatchCreditCached = pwallet->GetCredit( *this, ISMINE_WATCH_ONLY ) ;
            fWatchCreditCached = true ;
            credit += nWatchCreditCached ;
        }
    }
    return credit ;
}

CAmount CWalletTx::GetImmatureCredit( bool fUseCache ) const
{
    if ( IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain() )
    {
        if ( fUseCache && fImmatureCreditCached )
            return nImmatureCreditCached ;
        nImmatureCreditCached = pwallet->GetCredit( *this, ISMINE_SPENDABLE ) ;
        fImmatureCreditCached = true ;
        return nImmatureCreditCached ;
    }

    return 0 ;
}

CAmount CWalletTx::GetAvailableCredit( bool fUseCache ) const
{
    if ( pwallet == nullptr ) return 0 ;

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if ( IsCoinBase() && GetBlocksToMaturity() > 0 )
        return 0 ;

    if (fUseCache && fAvailableCreditCached)
        return nAvailableCreditCached;

    CAmount nCredit = 0 ;
    uint256 hashTx = GetTxHash() ;
    for (unsigned int i = 0; i < tx->vout.size(); i++)
    {
        if (!pwallet->IsSpent(hashTx, i))
        {
            const CTxOut &txout = tx->vout[i];
            nCredit += pwallet->GetCredit(txout, ISMINE_SPENDABLE);
            if (!MoneyRange(nCredit))
                throw std::runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
        }
    }

    nAvailableCreditCached = nCredit ;
    fAvailableCreditCached = true ;
    return nCredit ;
}

CAmount CWalletTx::GetImmatureWatchOnlyCredit( const bool & fUseCache ) const
{
    if ( IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain() )
    {
        if (fUseCache && fImmatureWatchCreditCached)
            return nImmatureWatchCreditCached;
        nImmatureWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
        fImmatureWatchCreditCached = true;
        return nImmatureWatchCreditCached;
    }

    return 0;
}

CAmount CWalletTx::GetAvailableWatchOnlyCredit( const bool & fUseCache ) const
{
    if ( pwallet == nullptr ) return 0 ;

    // wait until coinbase is safely deep enough in the chain before valuing it
    if ( IsCoinBase() && GetBlocksToMaturity() > 0 )
        return 0 ;

    if (fUseCache && fAvailableWatchCreditCached)
        return nAvailableWatchCreditCached;

    CAmount nCredit = 0 ;
    for (unsigned int i = 0; i < tx->vout.size(); i++)
    {
        if ( ! pwallet->IsSpent( GetTxHash(), i ) )
        {
            const CTxOut &txout = tx->vout[i];
            nCredit += pwallet->GetCredit(txout, ISMINE_WATCH_ONLY);
            if (!MoneyRange(nCredit))
                throw std::runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
        }
    }

    nAvailableWatchCreditCached = nCredit ;
    fAvailableWatchCreditCached = true ;
    return nCredit ;
}

CAmount CWalletTx::GetChange() const
{
    if (fChangeCached)
        return nChangeCached;
    nChangeCached = pwallet->GetChange(*this);
    fChangeCached = true;
    return nChangeCached;
}

bool CWalletTx::InMempool() const
{
    LOCK( mempool.cs ) ;
    return mempool.exists( GetTxHash() ) ;
}

bool CWalletTx::IsTrusted() const
{
    // quick answer in most cases
    if ( ! CheckFinalTx( *this ) )
        return false ;

    int nDepth = GetDepthInMainChain();
    if (nDepth >= 1)
        return true;
    if (nDepth < 0)
        return false;
    if (!bSpendZeroConfChange || !IsFromMe(ISMINE_ALL)) // using wtx's cached debit
        return false;

    // Don't trust unconfirmed transactions from us unless they are in the mempool
    if ( ! InMempool() ) return false ;

    // Trusted if all inputs are from us and are in the mempool
    for ( const CTxIn & txin : tx->vin )
    {
        // Transactions not sent by us: not trusted
        const CWalletTx * parent = pwallet->GetWalletTx( txin.prevout.hash ) ;
        if ( parent == nullptr ) return false ;

        const CTxOut & parentOut = parent->tx->vout[ txin.prevout.n ] ;
        if ( pwallet->IsMine( parentOut ) != ISMINE_SPENDABLE )
            return false ;
    }
    return true ;
}

bool CWalletTx::IsEquivalentTo( const CWalletTx & wtx ) const
{
    CMutableTransaction tx1 = *this->tx ;
    CMutableTransaction tx2 = *wtx.tx ;
    for ( unsigned int i = 0 ; i < tx1.vin.size() ; i ++ ) tx1.vin[ i ].scriptSig = CScript() ;
    for ( unsigned int i = 0 ; i < tx2.vin.size() ; i ++ ) tx2.vin[ i ].scriptSig = CScript() ;
    return CTransaction( tx1 ) == CTransaction( tx2 ) ;
}
