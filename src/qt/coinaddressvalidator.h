// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_COINADDRESSVALIDATOR_H
#define DOGECOIN_QT_COINADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace
 */
class CoinAddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit CoinAddressEntryValidator( QObject * parent ) ;

    State validate( QString & input, int & pos ) const ;
} ;

/** Coin address widget validator, checks for a valid dogecoin address
 */
class CoinAddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit CoinAddressCheckValidator( QObject * parent ) ;

    State validate( QString & input, int & pos ) const ;
} ;

#endif
