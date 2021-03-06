// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_COINS_H
#define DOGECOIN_COINS_H

#include "compressor.h"
#include "core_memusage.h"
#include "hash.h"
#include "memusage.h"
#include "serialize.h"
#include "uint256.h"
#include "random.h"

#include <assert.h>
#include <stdint.h>

#include <unordered_map>

/**
 * Pruned version of CTransaction: only retains metadata and unspent transaction outputs
 *
 * Serialized format:
 * - VARINT(nVersion)
 * - VARINT(nCode)
 * - unspentness bitvector, for vout[2] and further; least significant byte first
 * - the non-spent CTxOuts (via CTxOutCompressor)
 * - VARINT(nHeight)
 *
 * The nCode value consists of:
 * - bit 0: IsCoinBase()
 * - bit 1: vout[0] is not spent
 * - bit 2: vout[1] is not spent
 * - The higher bits encode N, the number of non-zero bytes in the following bitvector.
 *   - In case both bit 1 and bit 2 are unset, they encode N-1, as there must be at
 *     least one non-spent output).
 *
 * Example: 0104835800816115944e077fe7c803cfa57f29b36bf87c1d358bb85e
 *          <><><--------------------------------------------><---->
 *          |  \                  |                             /
 *    version   code             vout[1]                  height
 *
 *    - version = 1
 *    - code = 4 (vout[1] is not spent, and 0 non-zero bytes of bitvector follow)
 *    - unspentness bitvector: as 0 non-zero bytes follow, it has length 0
 *    - vout[1]: 835800816115944e077fe7c803cfa57f29b36bf87c1d35
 *               * 8358: compact amount representation for 60000000000 (600.00000000 DOGE)
 *               * 00: special txout type pay-to-pubkey-hash
 *               * 816115944e077fe7c803cfa57f29b36bf87c1d35: address uint160
 *    - height = 203998
 *
 *
 * Example: 0109044086ef97d5790061b01caab50f1b8e9c50a5057eb43c2d9563a4eebbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa486af3b
 *          <><><--><--------------------------------------------------><----------------------------------------------><---->
 *         /  \   \                     |                                                           |                     /
 *  version  code  unspentness       vout[4]                                                     vout[16]           height
 *
 *  - version = 1
 *  - code = 9 (coinbase, neither vout[0] or vout[1] are unspent,
 *                2 (1, +1 because both bit 1 and bit 2 are unset) non-zero bitvector bytes follow)
 *  - unspentness bitvector: bits 2 (0x04) and 14 (0x4000) are set, so vout[2+2] and vout[14+2] are unspent
 *  - vout[4]: 86ef97d5790061b01caab50f1b8e9c50a5057eb43c2d9563a4ee
 *             * 86ef97d579: compact amount representation for 234925952 (2.34925952 DOGE)
 *             * 00: special txout type pay-to-pubkey-hash
 *             * 61b01caab50f1b8e9c50a5057eb43c2d9563a4ee: address uint160
 *  - vout[16]: bbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa4
 *              * bbd123: compact amount representation for 110397 (0.00110397 DOGE)
 *              * 00: special txout type pay-to-pubkey-hash
 *              * 8c988f1a4a4de2161e0f50aac7f17e7f9555caa4: address uint160
 *  - height = 120891
 */
class CCoins
{
public:
    //! whether transaction is a coinbase
    bool fCoinBase;

    //! unspent transaction outputs; spent outputs are .IsNull(); spent outputs at the end of the array are dropped
    std::vector<CTxOut> vout;

    //! at which height this transaction was included in the active block chain
    int nHeight;

    //! version of the CTransaction; accesses to this value should probably check for nHeight as well,
    //! as new tx version will probably only be introduced at certain heights
    int nVersion;

    void FromTx(const CTransaction &tx, int nHeightIn) {
        fCoinBase = tx.IsCoinBase();
        vout = tx.vout;
        nHeight = nHeightIn;
        nVersion = tx.nVersion;
        ClearUnspendable();
    }

    //! construct a CCoins from a CTransaction, at a given height
    CCoins(const CTransaction &tx, int nHeightIn) {
        FromTx(tx, nHeightIn);
    }

    void Clear() {
        fCoinBase = false;
        std::vector<CTxOut>().swap(vout);
        nHeight = 0;
        nVersion = 0;
    }

    //! empty constructor
    CCoins() : fCoinBase(false), vout(0), nHeight(0), nVersion(0) { }

    //!remove spent outputs at the end of vout
    void Cleanup() {
        while (vout.size() > 0 && vout.back().IsNull())
            vout.pop_back();
        if (vout.empty())
            std::vector<CTxOut>().swap(vout);
    }

    void ClearUnspendable() {
        for ( CTxOut & txout : vout ) {
            if ( txout.scriptPubKey.IsUnspendable() )
                txout.SetNull() ;
        }
        Cleanup();
    }

    void swap(CCoins &to) {
        std::swap(to.fCoinBase, fCoinBase);
        to.vout.swap(vout);
        std::swap(to.nHeight, nHeight);
        std::swap(to.nVersion, nVersion);
    }

    //! equality test
    friend bool operator==(const CCoins &a, const CCoins &b) {
         // Empty CCoins objects are always equal.
         if (a.IsPruned() && b.IsPruned())
             return true;
         return a.fCoinBase == b.fCoinBase &&
                a.nHeight == b.nHeight &&
                a.nVersion == b.nVersion &&
                a.vout == b.vout;
    }
    friend bool operator!=(const CCoins &a, const CCoins &b) {
        return !(a == b);
    }

    void CalcMaskSize(unsigned int &nBytes, unsigned int &nNonzeroBytes) const;

    bool IsCoinBase() const {
        return fCoinBase;
    }

    template<typename Stream>
    void Serialize(Stream &s) const {
        unsigned int nMaskSize = 0, nMaskCode = 0;
        CalcMaskSize(nMaskSize, nMaskCode);
        bool fFirst = vout.size() > 0 && !vout[0].IsNull();
        bool fSecond = vout.size() > 1 && !vout[1].IsNull();
        assert(fFirst || fSecond || nMaskCode);
        unsigned int nCode = 8*(nMaskCode - (fFirst || fSecond ? 0 : 1)) + (fCoinBase ? 1 : 0) + (fFirst ? 2 : 0) + (fSecond ? 4 : 0);
        // version
        ::Serialize(s, VARINT(this->nVersion));
        // header code
        ::Serialize(s, VARINT(nCode));
        // spentness bitmask
        for (unsigned int b = 0; b<nMaskSize; b++) {
            unsigned char chAvail = 0;
            for (unsigned int i = 0; i < 8 && 2+b*8+i < vout.size(); i++)
                if (!vout[2+b*8+i].IsNull())
                    chAvail |= (1 << i);
            ::Serialize(s, chAvail);
        }
        // txouts themself
        for (unsigned int i = 0; i < vout.size(); i++) {
            if (!vout[i].IsNull())
                ::Serialize(s, CTxOutCompressor(REF(vout[i])));
        }
        // coinbase height
        ::Serialize(s, VARINT(nHeight));
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        unsigned int nCode = 0;
        // version
        ::Unserialize(s, VARINT(this->nVersion));
        // header code
        ::Unserialize(s, VARINT(nCode));
        fCoinBase = nCode & 1;
        std::vector<bool> vAvail(2, false);
        vAvail[0] = (nCode & 2) != 0;
        vAvail[1] = (nCode & 4) != 0;
        unsigned int nMaskCode = (nCode / 8) + ((nCode & 6) != 0 ? 0 : 1);
        // spentness bitmask
        while (nMaskCode > 0) {
            unsigned char chAvail = 0;
            ::Unserialize(s, chAvail);
            for (unsigned int p = 0; p < 8; p++) {
                bool f = (chAvail & (1 << p)) != 0;
                vAvail.push_back(f);
            }
            if (chAvail != 0)
                nMaskCode--;
        }
        // txouts themself
        vout.assign(vAvail.size(), CTxOut());
        for (unsigned int i = 0; i < vAvail.size(); i++) {
            if (vAvail[i])
                ::Unserialize(s, REF(CTxOutCompressor(vout[i])));
        }
        // coinbase height
        ::Unserialize(s, VARINT(nHeight));
        Cleanup();
    }

    //! mark a vout spent
    bool Spend(uint32_t nPos);

    //! check whether a particular output is still available
    bool IsAvailable(unsigned int nPos) const {
        return (nPos < vout.size() && !vout[nPos].IsNull());
    }

    //! check whether the entire CCoins is spent
    //! note that only !IsPruned() CCoins can be serialized
    bool IsPruned() const {
        for ( const CTxOut & out : vout )
            if ( ! out.IsNull() )
                return false ;
        return true ;
    }

    size_t DynamicMemoryUsage() const {
        size_t ret = memusage::DynamicUsage(vout);
        for ( const CTxOut & out : vout ) {
            ret += RecursiveDynamicUsage( out.scriptPubKey ) ;
        }
        return ret;
    }
};

class SaltedTxHasher
{
private:
    /** Salt */
    const uint64_t k0, k1 ;

public:
    SaltedTxHasher() :
        k0( GetRand( std::numeric_limits< uint64_t >::max() ) ),
        k1( GetRand( std::numeric_limits< uint64_t >::max() ) )
    { }

    /**
     * This *must* return size_t. With Boost 1.46 on 32-bit systems the
     * unordered_map will behave unpredictably if the custom hasher returns
     * a uint64_t, resulting in failures when syncing the chain (#4634)
     */
    size_t operator()( const uint256 & txhash ) const {
        return SipHashUint256( k0, k1, txhash ) ;
    }
};

struct CCoinsCacheEntry
{
    CCoins coins; // The actual cached data
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view
        FRESH = (1 << 1), // The parent view does not have this entry (or it is pruned)
        /* Note that FRESH is a performance optimization with which we can
         * erase coins that are fully spent if we know we do not need to
         * flush the changes to the parent cache.  It is always safe to
         * not mark FRESH if that condition is not guaranteed */
    };

    CCoinsCacheEntry() : coins(), flags(0) {}
};

typedef std::unordered_map< uint256, CCoinsCacheEntry, SaltedTxHasher > CCoinsMap ;

/** Cursor for iterating over CoinsView state */
class CCoinsViewCursor
{
public:
    CCoinsViewCursor( const uint256 & hashOfBlock ): sha256Block( hashOfBlock ) { }
    virtual ~CCoinsViewCursor() ;

    virtual bool GetKey( uint256 & key ) const = 0 ;
    virtual bool GetValue( CCoins & coins ) const = 0 ;
    /* Don't care about GetKeySize here */
    virtual unsigned int GetValueSize() const = 0 ;

    virtual bool Valid() const = 0 ;
    virtual void Next() = 0 ;

    // Get best block at the time this cursor was created
    const uint256 & GetSha256HashOfBestBlock() const {  return sha256Block ;  }

private:
    uint256 sha256Block ;
} ;

/** Abstract view on the open txout dataset */
class AbstractCoinsView
{
public:
    // Retrieve the CCoins (unspent transaction outputs) for a given txhash
    virtual bool GetCoins( const uint256 & txhash, CCoins & coins ) const {  return false ;  }

    // Just check whether we have data for a given txhash
    // This may (but cannot always) return true for fully spent transactions
    virtual bool HaveCoins( const uint256 & txhash ) const {  return false ;  }

    // Retrieve the block hash whose state this CoinsView currently represents
    virtual uint256 GetSha256OfBestBlock() const = 0 ;

    // Do a bulk modification (multiple CCoins changes + BestBlock change)
    // The passed mapCoins can be modified
    virtual bool BatchWrite( CCoinsMap & mapCoins, const uint256 & blockHash ) {  return false ;  }

    // Get a cursor to iterate over the whole state
    virtual CCoinsViewCursor * Cursor() const = 0 ;

    // As we use CoinsViews polymorphically, have a virtual destructor
    virtual ~AbstractCoinsView() { }
} ;

class TrivialCoinsView : public AbstractCoinsView
{
public:
    virtual uint256 GetSha256OfBestBlock() const override {  return uint256() ;  }
    virtual CCoinsViewCursor * Cursor() const override {  return nullptr ;  }
} ;

/** CoinsView backed by another CoinsView */
class CCoinsViewBacked : public AbstractCoinsView
{
protected:
    AbstractCoinsView * base ;

public:
    CCoinsViewBacked( AbstractCoinsView * in ) : base( in ) { }
    void SetBackend( AbstractCoinsView & backend ) {  base = &backend ;  }

    virtual bool GetCoins( const uint256 & txhash, CCoins & coins ) const override {
        return base->GetCoins( txhash, coins ) ;
    }
    virtual bool HaveCoins( const uint256 & txhash ) const override {
        return base->HaveCoins( txhash ) ;
    }
    virtual uint256 GetSha256OfBestBlock() const override {
        return base->GetSha256OfBestBlock() ;
    }
    virtual bool BatchWrite( CCoinsMap & mapCoins, const uint256 & blockHash ) override {
        return base->BatchWrite( mapCoins, blockHash ) ;
    }
    virtual CCoinsViewCursor * Cursor() const override {
        return base->Cursor() ;
    }
} ;

class CCoinsViewCache ;

/**
 * A reference to a mutable cache entry. Encapsulating it allows us to run
 *  cleanup code after the modification is finished, and keeping track of
 *  concurrent modifications
 */
class CCoinsModifier
{
private:
    CCoinsViewCache& cache;
    CCoinsMap::iterator it;
    size_t cachedCoinUsage; // Cached memory usage of the CCoins object before modification
    CCoinsModifier(CCoinsViewCache& cache_, CCoinsMap::iterator it_, size_t usage);

public:
    CCoins* operator->() { return &it->second.coins; }
    CCoins& operator*() { return it->second.coins; }
    ~CCoinsModifier();
    friend class CCoinsViewCache;
};

/** CoinsView that adds a memory cache for transactions to another CoinsView */
class CCoinsViewCache : public CCoinsViewBacked
{
protected:
    /* Whether this cache has an active modifier */
    bool hasModifier ;

    /**
     * Make mutable so that we can "fill the cache" even from Get-methods
     * declared as "const"
     */
    mutable uint256 sha256Block ;
    mutable CCoinsMap cacheCoins ;

    /* Cached dynamic memory usage for the inner CCoins objects */
    mutable size_t cachedCoinsUsage ;

public:
    CCoinsViewCache( AbstractCoinsView * in )
        : CCoinsViewBacked( in )
        , hasModifier( false )
        , cachedCoinsUsage( 0 )
    { }

    ~CCoinsViewCache() {  assert( ! hasModifier ) ;  }

    // derived from AbstractCoinsView
    virtual bool GetCoins( const uint256 & txhash, CCoins & coins ) const override ;
    virtual bool HaveCoins( const uint256 & txhash ) const override ;
    virtual uint256 GetSha256OfBestBlock() const override ;
    virtual bool BatchWrite( CCoinsMap & mapCoins, const uint256 & blockHash ) override ;

    void SetBestBlockBySha256( const uint256 & hash ) {
        sha256Block = hash ;
    }

    /**
     * Check if we have the given tx already loaded in this cache.
     * The semantics are the same as HaveCoins(), but no calls to
     * the backing CoinsView are made
     */
    bool HaveCoinsInCache( const uint256 & txhash ) const ;

    /**
     * Return a pointer to CCoins in the cache, or NULL if not found. This is
     * more efficient than GetCoins. Modifications to other cache entries are
     * allowed while accessing the returned pointer
     */
    const CCoins * AccessCoins( const uint256 & txhash ) const ;

    /**
     * Return a modifiable reference to a CCoins. If no entry with the given
     * txhash exists, a new one is created. Simultaneous modifications are not
     * allowed
     */
    CCoinsModifier ModifyCoins( const uint256 & txhash ) ;

    /**
     * Return a modifiable reference to a CCoins. Assumes that no entry with the given
     * txhash exists and creates a new one. This saves a database access in the case where
     * the coins were to be wiped out by FromTx anyway.  This should not be called with
     * the 2 historical coinbase duplicate pairs because the new coins are marked fresh, and
     * in the event the duplicate coinbase was spent before a flush, the now pruned coins
     * would not properly overwrite the first coinbase of the pair. Simultaneous modifications
     * are not allowed
     */
    CCoinsModifier ModifyNewCoins( const uint256 & txhash, bool coinbase ) ;

    /**
     * Push the modifications applied to this cache to its base.
     * Failure to call this method before destruction will cause the changes to be forgotten.
     * If false is returned, the state of this cache (and its backing view) will be undefined
     */
    bool Flush() ;

    /**
     * Removes the transaction with the given hash from the cache, if it is not modified
     */
    void Uncache( const uint256 & txhash ) ;

    // Calculate the size of the cache (in number of transactions)
    unsigned int GetCacheSize() const ;

    // Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const ;

    /**
     * Amount of bitcoins coming in to a transaction
     * Note that lightweight clients may not know anything besides the hash of previous transactions,
     * so may not be able to calculate this
     *
     * @param[in] tx	transaction for which we are checking input total
     * @return	Sum of value of all inputs (scriptSigs)
     */
    CAmount GetValueIn( const CTransaction & tx ) const ;

    //! Check whether all prevouts of the transaction are present in the UTXO set represented by this view
    bool HaveInputs( const CTransaction & tx ) const ;

    /**
     * Return priority of tx at height nHeight. Also calculate the sum of the values of the inputs
     * that are already in the chain.  These are the inputs that will age and increase priority as
     * new blocks are added to the chain
     */
    double GetPriority( const CTransaction & tx, int nHeight, CAmount & inChainInputValue ) const ;

    const CTxOut & GetOutputFor( const CTxIn & input ) const ;

    friend class CCoinsModifier ;

private:
    CCoinsMap::const_iterator FetchCoins( const uint256 & txhash ) const ;

    // no copy constructor
    CCoinsViewCache( const CCoinsViewCache & ) = delete ;
} ;

#endif
