// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_MERKLETX_H
#define DOGECOIN_MERKLETX_H

#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"

#include <vector>

class CBlock ;
class CBlockIndex ;

/** A transaction with a merkle branch linking it to the block chain */
class CMerkleTx
{
public:
    CTransactionRef tx ;
    uint256 hashBlock ;

    std::vector< uint256 > vMerkleBranch ;

    /* An nIndex == -1 means that hashBlock (in nonzero) refers to the earliest
     * block in the chain we know this or any in-wallet dependency conflicts
     * with. Older clients interpret nIndex == -1 as unconfirmed for backward
     * compatibility
     */
    int nIndex ;

    CMerkleTx()
    {
        SetTx( MakeTransactionRef() ) ;
        Init() ;
    }

    CMerkleTx( CTransactionRef arg )
    {
        SetTx( std::move( arg ) ) ;
        Init() ;
    }

    /** Helper conversion operator to allow passing CMerkleTx where CTransaction is expected
     *  TODO: adapt callers and remove this operator */
    operator const CTransaction&() const {  return *tx ;  }

    void Init()
    {
        hashBlock = uint256() ;
        nIndex = -1 ;
    }

    void SetTx( CTransactionRef arg )
    {
        tx = std::move( arg ) ;
    }

    std::string ToString() const ;

    ADD_SERIALIZE_METHODS ;

    template < typename Stream, typename Operation >
    inline void SerializationOp( Stream & s, Operation ser_action ) {
        READWRITE( tx ) ;
        READWRITE( hashBlock ) ;
        READWRITE( vMerkleBranch ) ;
        READWRITE( nIndex ) ;
    }

    /**
     * Actually compute the Merkle branch
     */
    void InitMerkleBranch( const CBlock & block, int posInBlock ) ;

    void SetMerkleBranch( const CBlockIndex * pindex, int posInBlock ) ;

    const uint256 & GetTxHash() const {  return tx->GetTxHash() ;  }
    bool IsCoinBase() const {  return tx->IsCoinBase() ;  }

    static uint256 CheckMerkleBranch( uint256 hash,
                                      const std::vector< uint256 > & vMerkleBranch,
                                      int nIndex ) ;
} ;

#endif
