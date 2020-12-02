// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_TRANSACTIONDESC_H
#define DOGECOIN_QT_TRANSACTIONDESC_H

#include <QObject>
#include <QString>

#include "unitsofcoin.h"

class TransactionRecord;

class CWallet ;
class CWalletTx ;

/* Provide a human-readable extended HTML description of a transaction
 * as well as raw hex of it
 */
class TransactionDesc: public QObject
{
    Q_OBJECT

public:
    static QString toHTML( CWallet * wallet, CWalletTx & wtx, TransactionRecord * rec, unitofcoin unit ) ;
    static QString getTxHex( TransactionRecord * rec, CWallet * wallet ) ;

private:
    TransactionDesc() {}

    static QString FormatTxStatus( const CWalletTx & wtx ) ;

} ;

#endif
