// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "primitives/transaction.h"

#include "hash.h"
#include "tinyformat.h"
#include "utilstrencodings.h"

std::string COutPoint::ToString() const
{
    return "COutPoint(" + ( IsNull() ? "0" : hash.ToString() ) + ", " + ( ( n < 10 ) ? strprintf( "%u", n ) : strprintf( "0x%x", n ) ) + ")" ;
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str = "CTxIn(" ;
    str += prevout.ToString() + ", " ;
    str += ( prevout.IsNull() ? "coinbase " : "scriptSig=" ) + HexStr( scriptSig ) ;
    if ( nSequence != SEQUENCE_FINAL )
        str += ", " + strprintf( "nSequence=0x%x", nSequence ) ;
    str += ")" ;
    return str ;
}

CTxOut::CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}

std::string CTxOut::ToString() const
{
    return strprintf( "CTxOut(nValue=%ld, scriptPubKey=%s)", nValue, HexStr( scriptPubKey ) ) ;
}

CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::CURRENT_VERSION), nLockTime(0) {}
CMutableTransaction::CMutableTransaction(const CTransaction& tx) : nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime) {}

uint256 CMutableTransaction::GetTxHash() const
{
    return SerializeHash( *this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS ) ;
}

uint256 CTransaction::ComputeTxHash() const
{
    return SerializeHash( *this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS ) ;
}

uint256 CTransaction::GetWitnessHash() const
{
    if ( ! HasWitness() ) {
        return GetTxHash() ;
    }
    return SerializeHash( *this, SER_GETHASH, 0 ) ;
}

/* For backward compatibility, the hash is initialized to 0 */
CTransaction::CTransaction() : nVersion( CTransaction::CURRENT_VERSION ), vin(), vout(), nLockTime( 0 ), hash() { }
CTransaction::CTransaction( const CMutableTransaction & tx ) : nVersion( tx.nVersion ), vin( tx.vin ), vout( tx.vout ), nLockTime( tx.nLockTime ), hash( ComputeTxHash() ) { }
CTransaction::CTransaction( CMutableTransaction && tx ) : nVersion( tx.nVersion ), vin( std::move( tx.vin ) ), vout( std::move( tx.vout ) ), nLockTime( tx.nLockTime ), hash( ComputeTxHash() ) { }

CAmount CTransaction::GetValueOut() const
{
    CAmount nValueOut = 0 ;
    for ( std::vector< CTxOut >::const_iterator it( vout.begin() ) ; it != vout.end() ; ++ it )
    {
        if ( ! MoneyRange( it->nValue ) )
            throw std::runtime_error( std::string(__func__) + ": " + strprintf( "it->nValue=%ld is out of range", it->nValue ) ) ;

        nValueOut += it->nValue ;

        if ( ! MoneyRange( nValueOut ) )
            throw std::runtime_error( std::string(__func__) + ": " + strprintf( "nValueOut=%ld is out of range", nValueOut ) ) ;
    }
    return nValueOut ;
}

double CTransaction::ComputePriority(double dPriorityInputs, unsigned int nTxSize) const
{
    nTxSize = CalculateModifiedSize(nTxSize);
    if (nTxSize == 0) return 0.0;

    return dPriorityInputs / nTxSize;
}

unsigned int CTransaction::CalculateModifiedSize(unsigned int nTxSize) const
{
    // In order to avoid disincentivizing cleaning up the UTXO set we don't count
    // the constant overhead for each txin and up to 110 bytes of scriptSig (which
    // is enough to cover a compressed pubkey p2sh redemption) for priority.
    // Providing any more cleanup incentive than making additional inputs free would
    // risk encouraging people to create junk outputs to redeem later
    if (nTxSize == 0)
        nTxSize = (GetTransactionWeight(*this) + WITNESS_SCALE_FACTOR - 1) / WITNESS_SCALE_FACTOR;
    for (std::vector<CTxIn>::const_iterator it(vin.begin()); it != vin.end(); ++it)
    {
        unsigned int offset = 41U + std::min(110U, (unsigned int)it->scriptSig.size());
        if (nTxSize > offset)
            nTxSize -= offset;
    }
    return nTxSize;
}

unsigned int CTransaction::GetFullSize() const
{
    return ::GetSerializeSize( *this, SER_NETWORK, PROTOCOL_VERSION ) ;
}

std::string CTransaction::ToString() const
{
    std::string str ;
    str += strprintf( "CTransaction(hash=%s, version=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
        GetTxHash().ToString(),
        nVersion,
        vin.size(),
        vout.size(),
        nLockTime ) ;
    for ( unsigned int i = 0 ; i < vin.size() ; i ++ )
        str += strprintf( "    vin[%u]: ", i ) + vin[ i ].ToString() + "\n" ;
    for ( unsigned int i = 0 ; i < vin.size() ; i ++ )
        if ( ! vin[ i ].scriptWitness.IsNull() )
            str += strprintf( "    vin[%u].scriptWitness: ", i ) + vin[ i ].scriptWitness.ToString() + "\n" ;
    for ( unsigned int i = 0 ; i < vout.size() ; i ++ )
        str += strprintf( "    vout[%u]: ", i ) + vout[ i ].ToString() + "\n" ;
    return str ;
}

int64_t GetTransactionWeight( const CTransaction & tx )
{
    return ::GetSerializeSize( tx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS ) * ( WITNESS_SCALE_FACTOR - 1 ) + ::GetSerializeSize( tx, SER_NETWORK, PROTOCOL_VERSION ) ;
}
