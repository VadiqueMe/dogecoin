// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_WALLETMODELTRANSACTION_H
#define DOGECOIN_QT_WALLETMODELTRANSACTION_H

#include "wallet/wallet.h"
#include "walletmodel.h"

#include <vector>

class SendCoinsRecipient ;

class WalletModelTransaction
{
public:
    explicit WalletModelTransaction( const std::vector< SendCoinsRecipient > & listOfRecipients ) ;
    ~WalletModelTransaction() ;

    std::vector< SendCoinsRecipient > getRecipients() const {  return recipients ;  }

    CWalletTx & getWalletTransaction() {  return walletTransaction ;  }

    unsigned int getSizeOfTransaction() const ;

    void setTransactionFee( const CAmount & newFee ) {  fee = newFee ;  }
    CAmount getTransactionFee() const {  return fee ;  }

    CAmount getTotalTransactionAmount() const ;

    void newPossibleKeyChange( CWallet * wallet ) ;
    CReserveKey * getPossibleKeyChange() ;

    void reassignAmounts( int nChangePosRet ) ; // for the subtract-fee-from-amount feature

private:
    std::vector< SendCoinsRecipient > recipients ;
    CWalletTx walletTransaction ;
    CReserveKey * keyChange ;
    CAmount fee ;
} ;

#endif
