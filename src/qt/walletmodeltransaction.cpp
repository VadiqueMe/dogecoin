// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "walletmodeltransaction.h"

#include "policy/policy.h" // GetVirtualTransactionSize

WalletModelTransaction::WalletModelTransaction( const std::vector< SendCoinsRecipient > & listOfRecipients )
    : recipients( listOfRecipients )
    , walletTransaction()
    , keyChange( nullptr )
    , fee( 0 )
{}

WalletModelTransaction::~WalletModelTransaction()
{
    delete keyChange ; keyChange = nullptr ;
}

unsigned int WalletModelTransaction::getSizeOfTransaction() const
{
    return ::GetVirtualTransactionSize( walletTransaction ) ;
}

void WalletModelTransaction::reassignAmounts( int nChangePosRet )
{
    int i = 0 ;
    for ( SendCoinsRecipient & rcp : recipients )
    {
        if ( rcp.paymentRequest.IsInitialized() )
        {
            CAmount subtotal = 0 ;
            const payments::PaymentDetails & details = rcp.paymentRequest.getDetails() ;
            for ( int j = 0 ; j < details.outputs_size() ; j ++ )
            {
                const payments::Output & out = details.outputs( j ) ;
                if ( out.amount() <= 0 ) continue;
                if ( i == nChangePosRet ) i++ ;
                subtotal += walletTransaction.tx->vout[ i ].nValue ;
                i ++ ;
            }
            rcp.amount = subtotal ;
        }
        else // normal recipient (no payment request)
        {
            if ( i == nChangePosRet ) i++ ;
            rcp.amount = walletTransaction.tx->vout[ i ].nValue ;
            i ++ ;
        }
    }
}

CAmount WalletModelTransaction::getTotalTransactionAmount() const
{
    CAmount totalTransactionAmount = 0 ;
    for ( const SendCoinsRecipient & rcp : recipients ) {
        totalTransactionAmount += rcp.amount ;
    }
    return totalTransactionAmount ;
}

void WalletModelTransaction::newPossibleKeyChange( CWallet * wallet )
{
    keyChange = new CReserveKey( wallet ) ;
}

CReserveKey * WalletModelTransaction::getPossibleKeyChange()
{
    return keyChange ;
}
