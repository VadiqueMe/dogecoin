// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_WALLETMODELTRANSACTION_H
#define DOGECOIN_QT_WALLETMODELTRANSACTION_H

#include "walletmodel.h"

#include <QObject>

class SendCoinsRecipient ;

class CReserveKey ;
class CWallet ;
class CWalletTx ;

/** Data model for a walletmodel transaction */
class WalletModelTransaction
{
public:
    explicit WalletModelTransaction( const QList< SendCoinsRecipient > & listOfRecipients ) ;
    ~WalletModelTransaction() ;

    QList< SendCoinsRecipient > getRecipients() const {  return recipients ;  }

    CWalletTx * getTransaction() ;
    unsigned int getTransactionSize() ;

    void setTransactionFee( const CAmount & newFee ) {  fee = newFee ;  }
    CAmount getTransactionFee() const {  return fee ;  }

    CAmount getTotalTransactionAmount() const ;

    void newPossibleKeyChange( CWallet * wallet ) ;
    CReserveKey * getPossibleKeyChange() ;

    void reassignAmounts( int nChangePosRet ) ; // needed for the subtract-fee-from-amount feature

private:
    QList< SendCoinsRecipient > recipients ;
    CWalletTx * walletTransaction ;
    CReserveKey * keyChange ;
    CAmount fee ;
} ;

#endif
