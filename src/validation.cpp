// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "validation.h"

#include "alert.h"
#include "arith_uint256.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "checkqueue.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "dogecoin.h"
#include "hash.h"
#include "init.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/pureheader.h"
#include "primitives/transaction.h"
#include "random.h"
#include "script/script.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "timedata.h"
#include "tinyformat.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "utilthread.h"
#include "utilmoneystr.h"
#include "utilstr.h"
#include "utilstrencodings.h"
#include "validationinterface.h"
#include "versionbits.h"
#include "warnings.h"

#include <atomic>
#include <sstream>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/foreach.hpp> // BOOST_REVERSE_FOREACH
///#include <boost/math/distributions/poisson.hpp>

#if defined(NDEBUG)
# error "Dogecoin cannot be compiled without assertions"
#endif

/**
 * Global variables
 */

CCriticalSection cs_main ;

BlockMap mapBlockIndex ;
CChain chainActive ;
CBlockIndex * pindexBestHeader = nullptr ;
CWaitableCriticalSection csBestBlock ;
std::condition_variable cvBlockChange ;
int nScriptCheckThreads = 0;
std::atomic_bool fImporting(false);
bool fReindex = false;
bool fTxIndex = false;
bool fHavePruned = false;
bool fPruneMode = false;
bool fIsBareMultisigStd = DEFAULT_PERMIT_BAREMULTISIG;
bool acceptNonStandardTxs = false ;
bool fCheckBlockIndex = false;
size_t nCoinCacheUsage = 5000 * 300;
uint64_t nPruneTarget = 0;
bool fAlerts = DEFAULT_ALERTS;
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE ;
bool fEnableReplacement = DEFAULT_ENABLE_REPLACEMENT;

CTxMemPool mempool ;

static void CheckBlockIndex( const Consensus::Params & consensusParams ) ;

/** Constant stuff for coinbase transactions we create */
CScript COINBASE_FLAGS ;

const std::string strMessageMagic = "Dogecoin Signed Message:\n";

// Internal stuff
namespace {

    struct CBlockIndexComparator
    {
        bool operator()( CBlockIndex * pa, CBlockIndex * pb ) const
        {
            // First sort by most height, ...
            if ( pa->nHeight > pb->nHeight ) return false ;
            if ( pa->nHeight < pb->nHeight ) return true ;

            // ... then by earliest time received, ...
            if ( pa->nSequenceId < pb->nSequenceId ) return false ;
            if ( pa->nSequenceId > pb->nSequenceId ) return true ;

            // Use pointer address as tie breaker (only happens with blocks
            // loaded from disk, as those all have id 0)
            if ( pa < pb ) return false ;
            if ( pa > pb ) return true ;

            // Identical blocks
            return false ;
        }
    } ;

    CBlockIndex * pindexBestInvalid ;

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block
     */
    std::set< CBlockIndex*, CBlockIndexComparator > setOfBlockIndexCandidates ;

    /** All pairs A->B, where A (or one of its ancestors) misses transactions, but B has transactions.
     * Pruned nodes may have entries where B is missing data
     */
    std::multimap< CBlockIndex*, CBlockIndex* > mapBlocksUnlinked;

    CCriticalSection cs_LastBlockFile ;
    std::vector< CBlockFileInfo > vinfoBlockFile ;
    int nLastBlockFile = 0 ;

    /** Global flag to indicate we should check to see if there are
     *  block/undo files that should be deleted.  Set on startup
     *  or if we allocate more file space when we're in prune mode
     */
    bool fCheckForPruning = false ;

    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork
     */
    CCriticalSection cs_nBlockSequenceId ;
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1 */
    int32_t nBlockSequenceId = 1 ;
    /** Decreasing counter (used by subsequent preciousblock calls) */
    int32_t nBlockReverseSequenceId = -1 ;
    /** height for the last block that preciousblock has been applied to */
    int nLastPreciousHeight = -1 ;

    /** Dirty block index entries */
    std::set< CBlockIndex* > setOfDirtyBlockIndices ;

    /** Dirty block file entries */
    std::set< int > setOfDirtyBlockFiles ;
} // anon namespace

/* Use this class to start tracking transactions that are removed from the
 * mempool and pass all those transactions through SyncTransaction when the
 * object goes out of scope. This is currently only used to call SyncTransaction
 * on conflicts removed from the mempool during block connection.  Applied in
 * ActivateBestChain around ActivateBestStep which in turn calls:
 * ConnectTip->removeForBlock->removeConflicts
 */
class MemPoolConflictRemovalTracker
{
private:
    std::vector<CTransactionRef> conflictedTxs;
    CTxMemPool &pool;

public:
    MemPoolConflictRemovalTracker(CTxMemPool &_pool) : pool(_pool) {
        pool.NotifyEntryRemoved.connect(boost::bind(&MemPoolConflictRemovalTracker::NotifyEntryRemoved, this, _1, _2));
    }

    void NotifyEntryRemoved(CTransactionRef txRemoved, MemPoolRemovalReason reason) {
        if (reason == MemPoolRemovalReason::CONFLICT) {
            conflictedTxs.push_back(txRemoved);
        }
    }

    ~MemPoolConflictRemovalTracker() {
        pool.NotifyEntryRemoved.disconnect(boost::bind(&MemPoolConflictRemovalTracker::NotifyEntryRemoved, this, _1, _2));
        for (const auto& tx : conflictedTxs) {
            GetMainSignals().SyncTransaction(*tx, NULL, CMainSignals::SYNC_TRANSACTION_NOT_IN_BLOCK);
        }
        conflictedTxs.clear();
    }
};

CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    for ( const uint256 & hash : locator.vHave ) {
        BlockMap::iterator mi = mapBlockIndex.find( hash ) ;
        if ( mi != mapBlockIndex.end() )
        {
            CBlockIndex* pindex = (*mi).second;
            if (chain.Contains(pindex))
                return pindex;
            if (pindex->GetAncestor(chain.Height()) == chain.Tip()) {
                return chain.Tip();
            }
        }
    }
    return chain.Genesis();
}

CCoinsViewCache * pcoinsTip = nullptr ;
CBlockTreeDB * pblocktree = nullptr ;

enum FlushStateMode {
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

// see definition for documentation
bool static FlushStateToDisk( CValidationState & state, FlushStateMode mode, int nManualPruneHeight = 0 ) ;

void FindFilesToPruneManual(std::set<int>& setFilesToPrune, int nManualPruneHeight);

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    if ( tx.nLockTime == 0 )
        return true ;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    for (const auto& txin : tx.vin) {
        if (!(txin.nSequence == CTxIn::SEQUENCE_FINAL))
            return false;
    }
    return true;
}

bool CheckFinalTx( const CTransaction & tx, int flags )
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set
    flags = std::max( flags, 0 ) ;

    // CheckFinalTx() uses chainActive.Height() + 1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock()
    const int nBlockHeight = chainActive.Height() + 1 ;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set
    const int64_t nBlockTime = ( Params().UseMedianTimePast() && ( flags & LOCKTIME_MEDIAN_TIME_PAST ) )
                             ? chainActive.Tip()->GetMedianTimePast()
                             : GetAdjustedTime() ;

    return IsFinalTx( tx, nBlockHeight, nBlockTime ) ;
}

/**
 * Calculates the block height and previous block's median time past at
 * which the transaction will be considered final in the context of BIP 68.
 * Also removes from the vector of input heights any entries which did not
 * correspond to sequence locked inputs as they do not affect the calculation
 */
static std::pair< int, int64_t > CalculateSequenceLocks( const CTransaction & tx, int flags, std::vector< int > * prevHeights, const CBlockIndex & block )
{
    assert( prevHeights->size() == tx.vin.size() ) ;

    // Will be set to the equivalent height- and time-based nLockTime
    // values that would be necessary to satisfy all relative lock-
    // time constraints given our view of block chain history.
    // The semantics of nLockTime are the last invalid height/time, so
    // use -1 to have the effect of any height or time being valid
    int nMinHeight = -1 ;
    int64_t nMinTime = -1 ;

    bool fEnforceBIP68 = false ;
    if ( NameOfChain() != "inu" ) {
        // tx.nVersion is signed integer so requires cast to unsigned otherwise
        // we would be doing a signed comparison and half the range of nVersion
        // wouldn't support BIP 68
        fEnforceBIP68 = static_cast< uint32_t >( tx.nVersion ) >= 2
                              && ( flags & LOCKTIME_VERIFY_SEQUENCE ) ;
    }

    // If sequence numbers as a relative lock time are not enforced, it is done
    if ( ! fEnforceBIP68 ) {
        return std::make_pair( nMinHeight, nMinTime ) ;
    }

    for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
        const CTxIn& txin = tx.vin[txinIndex];

        // Sequence numbers with the most significant bit set are not
        // treated as relative lock-times, nor are they given any
        // consensus-enforced meaning at this point
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            // The height of this input is not relevant for sequence locks
            (*prevHeights)[txinIndex] = 0;
            continue;
        }

        int nCoinHeight = (*prevHeights)[txinIndex];

        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            int64_t nCoinTime = block.GetAncestor(std::max(nCoinHeight-1, 0))->GetMedianTimePast();
            // NOTE: Subtract 1 to maintain nLockTime semantics
            // BIP 68 relative lock times have the semantics of calculating
            // the first block or time at which the transaction would be
            // valid. When calculating the effective block time or height
            // for the entire transaction, we switch to using the
            // semantics of nLockTime which is the last invalid block
            // time or height.  Thus we subtract 1 from the calculated
            // time or height.

            // Time-based relative lock-times are measured from the
            // smallest allowed timestamp of the block containing the
            // txout being spent, which is the median time past of the
            // block prior.
            nMinTime = std::max(nMinTime, nCoinTime + (int64_t)((txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1);
        } else {
            nMinHeight = std::max(nMinHeight, nCoinHeight + (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1);
        }
    }

    return std::make_pair(nMinHeight, nMinTime);
}

static bool EvaluateSequenceLocks( const CBlockIndex & block, std::pair< int, int64_t > lockPair )
{
    assert( block.pprev != nullptr ) ;
    int64_t nBlockTime = ( ! Params().UseMedianTimePast() ) ? block.pprev->GetBlockTime() : block.pprev->GetMedianTimePast() ;
    return ( lockPair.first < block.nHeight && lockPair.second < nBlockTime ) ;
}

bool SequenceLocks( const CTransaction & tx, int flags, std::vector< int > * prevHeights, const CBlockIndex & block )
{
    return EvaluateSequenceLocks( block, CalculateSequenceLocks( tx, flags, prevHeights, block ) ) ;
}

bool TestLockPointValidity(const LockPoints* lp)
{
    AssertLockHeld(cs_main);
    assert(lp);
    // If there are relative lock times then the maxInputBlock will be set
    // If there are no relative lock times, the LockPoints don't depend on the chain
    if (lp->maxInputBlock) {
        // Check whether chainActive is an extension of the block at which the LockPoints
        // calculation was valid.  If not LockPoints are no longer valid
        if (!chainActive.Contains(lp->maxInputBlock)) {
            return false;
        }
    }

    // LockPoints still valid
    return true;
}

bool CheckSequenceLocks( const CTransaction & tx, int flags, LockPoints * lp, bool useExistingLockPoints )
{
    AssertLockHeld(cs_main);
    AssertLockHeld(mempool.cs);

    CBlockIndex* tip = chainActive.Tip();
    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses chainActive.Height() + 1 to evaluate
    // height based locks because when SequenceLocks() is called within
    // ConnectBlock(), the height of the block *being*
    // evaluated is what is used.
    // Thus if we want to know if a transaction can be part of the
    // *next* block, we need to use one more than chainActive.Height()
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints) {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    }
    else {
        // pcoinsTip contains the UTXO set for chainActive.Tip()
        CCoinsViewMemPool viewMemPool(pcoinsTip, mempool);
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
            const CTxIn& txin = tx.vin[txinIndex];
            CCoins coins;
            if (!viewMemPool.GetCoins(txin.prevout.hash, coins)) {
                return error("%s: Missing input", __func__);
            }
            if (coins.nHeight == MEMPOOL_HEIGHT) {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            } else {
                prevheights[txinIndex] = coins.nHeight;
            }
        }
        lockPair = CalculateSequenceLocks( tx, flags, &prevheights, index ) ;
        if (lp) {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of
            // all the blocks which have sequence locked prevouts.
            // This hash needs to still be on the chain
            // for these LockPoint calculations to be valid
            // Note: It is impossible to correctly calculate a maxInputBlock
            // if any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height
            // is 0, which is equivalent to no sequence lock. Since we assume
            // input height of tip+1 for mempool txs and test the resulting
            // lockPair from CalculateSequenceLocks against tip+1.  We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            for ( int height : prevheights ) {
                // Can ignore mempool inputs since we'll fail if they had non-zero locks
                if (height != tip->nHeight+1) {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return EvaluateSequenceLocks( index, lockPair ) ;
}


unsigned int GetLegacySigOpCount( const CTransaction & tx )
{
    unsigned int nSigOps = 0 ;
    for ( const auto & txin : tx.vin ) {
        nSigOps += txin.scriptSig.GetSigOpCount( false ) ;
    }
    for ( const auto & txout : tx.vout ) {
        nSigOps += txout.scriptPubKey.GetSigOpCount( false ) ;
    }
    return nSigOps ;
}

unsigned int GetP2SHSigOpCount( const CTransaction & tx, const CCoinsViewCache & inputs )
{
    if ( tx.IsCoinBase() ) return 0 ;

    unsigned int nSigOps = 0 ;
    for ( const CTxIn & txin : tx.vin ) {
        const CTxOut & prevout = inputs.GetOutputFor( txin ) ;
        if ( prevout.scriptPubKey.IsPayToScriptHash() )
            nSigOps += prevout.scriptPubKey.GetSigOpCount( txin.scriptSig ) ;
    }
    return nSigOps ;
}

size_t GetTransactionSigOpCost( const CTransaction & tx, const CCoinsViewCache & inputs, int flags )
{
    size_t nSigOps = GetLegacySigOpCount( tx ) * WITNESS_SCALE_FACTOR ;

    if ( tx.IsCoinBase() ) return nSigOps ;

    if ( flags & SCRIPT_VERIFY_P2SH ) {
        nSigOps += GetP2SHSigOpCount( tx, inputs ) * WITNESS_SCALE_FACTOR ;
    }

    size_t nSegWitSigOps = 0 ;
    for ( const CTxIn & txin : tx.vin ) {
        const CTxOut & prevout = inputs.GetOutputFor( txin ) ;
        nSegWitSigOps += CountSegregatedWitnessSigOps( txin.scriptSig, prevout.scriptPubKey, &txin.scriptWitness, flags ) ;
    }
    nSigOps += nSegWitSigOps ;

    return nSigOps ;
}


bool CheckTransaction(const CTransaction& tx, CValidationState &state, bool fCheckDuplicateInputs)
{
    // Basic checks that don't depend on any context
    if (tx.vin.empty())
        return state.DoS( 1, false, REJECT_INVALID, "bad-txns-vin-empty" ) ;
    if (tx.vout.empty())
        return state.DoS( 1, false, REJECT_INVALID, "bad-txns-vout-empty" ) ;
    // Size limits (this doesn't take the witness into account, as that hasn't been checked for malleability)
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) > MAX_BLOCK_BASE_SIZE)
        return state.DoS( 10, false, REJECT_INVALID, "bad-txns-oversize" ) ;

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    for (const auto& txout : tx.vout)
    {
        if (txout.nValue < 0)
            return state.DoS( 10, false, REJECT_INVALID, "bad-txns-vout-negative" ) ;
        if (txout.nValue > MAX_MONEY)
            return state.DoS( 10, false, REJECT_INVALID, "bad-txns-vout-toolarge" ) ;
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS( 10, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge" ) ;
    }

    // Check for duplicate inputs - note that this check is slow so we skip it in CheckBlock
    if (fCheckDuplicateInputs) {
        std::set<COutPoint> vInOutPoints;
        for (const auto& txin : tx.vin)
        {
            if (!vInOutPoints.insert(txin.prevout).second)
                return state.DoS( 10, false, REJECT_INVALID, "bad-txns-inputs-duplicate" ) ;
        }
    }

    if (tx.IsCoinBase())
    {
        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.DoS( 10, false, REJECT_INVALID, "bad-cb-length" ) ;
    }
    else
    {
        for (const auto& txin : tx.vin)
            if (txin.prevout.IsNull())
                return state.DoS( 1, false, REJECT_INVALID, "bad-txns-prevout-null" ) ;
    }

    return true;
}

void LimitMempoolSize(CTxMemPool& pool, size_t limit, unsigned long age) {
    int expired = pool.Expire(GetTime() - age);
    if (expired != 0)
        LogPrint("mempool", "Expired %i transactions from the memory pool\n", expired);

    std::vector< uint256 > vNoSpendsRemaining ;
    pool.TrimToSize( limit, &vNoSpendsRemaining ) ;
    for ( const uint256 & removed : vNoSpendsRemaining )
        pcoinsTip->Uncache( removed ) ;
}

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state)
{
    return strprintf("%s%s (code %i)",
        state.GetRejectReason(),
        state.GetDebugMessage().empty() ? "" : ", "+state.GetDebugMessage(),
        state.GetRejectCode());
}

bool AcceptToMemoryPoolWorker( CTxMemPool & pool, CValidationState & state, const CTransactionRef & ptx, bool fLimitFree,
                               bool * pfMissingInputs, int64_t nAcceptTime, std::list< CTransactionRef > * plTxnReplaced,
                               std::vector< uint256 > & vHashTxnToUncache )
{
    const CTransaction & tx = *ptx ;
    const uint256 hash = tx.GetTxHash() ;
    AssertLockHeld( cs_main ) ;
    if ( pfMissingInputs != nullptr )
        *pfMissingInputs = false ;

    if ( ! CheckTransaction( tx, state ) )
        return false ; // state filled in by CheckTransaction

    // Coinbase is only valid in a block, not as a loose transaction
    if ( tx.IsCoinBase() )
        return state.DoS( 50, false, REJECT_INVALID, "coinbase" ) ;

    // Reject transactions with witness before segregated witness activates
    bool witnessEnabled = IsWitnessEnabled( chainActive.Tip(), Params().GetConsensus( chainActive.Height() ) ) ;
    if ( tx.HasWitness() && ! witnessEnabled ) {
        return state.Invalid( false, REJECT_NONSTANDARD, "no-witness-yet" ) ;
    }

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    std::string reason;
    if ( ! acceptNonStandardTxs && ! IsStandardTx( tx, reason, witnessEnabled ) )
        return state.Invalid( false, REJECT_NONSTANDARD, reason ) ;

    // Only accept nLockTime-using transactions that can be mined in the next
    // block, don't fill mempool with transactions that can't be mined yet
    if ( ! CheckFinalTx( tx, STANDARD_LOCKTIME_VERIFY_FLAGS ) )
        return state.Invalid( false, REJECT_NONSTANDARD, "non-final" ) ;

    // is it already in the memory pool?
    if ( pool.exists( hash ) )
        return state.Invalid( false, REJECT_ALREADY_KNOWN, "txn-already-in-mempool" ) ;

    // Check for conflicts with in-memory transactions
    std::set<uint256> setConflicts;
    {
    LOCK(pool.cs); // protect pool.mapNextTx
    for ( const CTxIn & txin : tx.vin )
    {
        auto itConflicting = pool.mapNextTx.find(txin.prevout);
        if (itConflicting != pool.mapNextTx.end())
        {
            const CTransaction * ptxConflicting = itConflicting->second ;
            if ( ! setConflicts.count( ptxConflicting->GetTxHash() ) )
            {
                // Allow opt-out of transaction replacement by setting
                // nSequence >= maxint-1 on all inputs
                //
                // maxint-1 is picked to still allow use of nLockTime by
                // non-replaceable transactions. All inputs rather than just one
                // is for the sake of multi-party protocols, where we don't
                // want a single party to be able to disable replacement
                //
                // The opt-out ignores descendants as anyone relying on
                // first-seen mempool behavior should be checking all
                // unconfirmed ancestors anyway; doing otherwise is hopelessly
                // insecure
                bool fReplacementOptOut = true;
                if (fEnableReplacement)
                {
                    for ( const CTxIn & txin : ptxConflicting->vin )
                    {
                        if ( txin.nSequence < std::numeric_limits<unsigned int>::max() - 1 )
                        {
                            fReplacementOptOut = false ;
                            break ;
                        }
                    }
                }
                if (fReplacementOptOut)
                    return state.Invalid(false, REJECT_CONFLICT, "txn-mempool-conflict");

                setConflicts.insert( ptxConflicting->GetTxHash() ) ;
            }
        }
    }
    }

    {
        TrivialCoinsView dummy ;
        CCoinsViewCache view( &dummy ) ;

        CAmount nValueIn = 0;
        LockPoints lp;
    {
        LOCK( pool.cs ) ;

        CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
        view.SetBackend(viewMemPool);

        // do we already have it?
        bool fHadTxInCache = pcoinsTip->HaveCoinsInCache(hash);
        if (view.HaveCoins(hash)) {
            if (!fHadTxInCache)
                vHashTxnToUncache.push_back(hash);
            return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-known");
        }

        // do all inputs exist?
        // Note that this does not check for the presence of actual outputs (see the next check for that),
        // and only helps with filling in pfMissingInputs (to determine missing vs spent)
        for ( const CTxIn & txin : tx.vin ) {
            if ( ! pcoinsTip->HaveCoinsInCache( txin.prevout.hash ) )
                vHashTxnToUncache.push_back( txin.prevout.hash ) ;
            if ( ! view.HaveCoins( txin.prevout.hash ) ) {
                if ( pfMissingInputs != nullptr ) *pfMissingInputs = true ;
                return false ; // fMissingInputs and !state.IsInvalid() is used to detect this condition, don't set state.Invalid()
            }
        }

        // are the actual inputs available?
        if (!view.HaveInputs(tx))
            return state.Invalid(false, REJECT_DUPLICATE, "bad-txns-inputs-spent");

        // Bring the best block into scope
        /* const uint256 & unused = */ view.GetSha256OfBestBlock() ;

        nValueIn = view.GetValueIn(tx);

        // all inputs are cached now, thus switch back to dummy, so we don't need to keep lock on mempool
        view.SetBackend( dummy ) ;

        // Accept BIP68 sequence locked transactions that can be included in the next block,
        // don't fill up the mempool with transactions that can't be mined yet.
        // Keep pool.cs for this unless we change CheckSequenceLocks to take a CoinsViewCache
        // instead of creating its own
        if ( ! CheckSequenceLocks( tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &lp ) )
            return state.DoS( 0, false, REJECT_NONSTANDARD, "non-BIP68-final" ) ;
    }

        // Check for non-standard pay-to-script-hash in inputs
        if ( ! acceptNonStandardTxs && ! AreInputsStandard( tx, view ) )
            return state.Invalid( false, REJECT_NONSTANDARD, "bad-txns-nonstandard-inputs" ) ;

        // Check for non-standard witness in P2WSH
        if ( tx.HasWitness() && ! acceptNonStandardTxs && ! IsWitnessStandard( tx, view ) )
            return state.DoS( 0, false, REJECT_NONSTANDARD, "bad-witness-nonstandard", true ) ;

        int64_t nSigOpsCost = GetTransactionSigOpCost( tx, view, STANDARD_SCRIPT_VERIFY_FLAGS ) ;

        CAmount nValueOut = tx.GetValueOut() ;
        CAmount nFees = nValueIn - nValueOut ;

        // nModifiedFees includes any fee deltas from PrioritiseTransaction
        CAmount nModifiedFees = nFees ;
        double nPriorityDummy = 0 ;
        pool.ApplyDeltas( hash, nPriorityDummy, nModifiedFees ) ;

        CAmount inChainInputValue ;
        double dPriority = view.GetPriority( tx, chainActive.Height(), inChainInputValue ) ;

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure coinbase maturity is still met
        bool fSpendsCoinbase = false ;
        for ( const CTxIn & txin : tx.vin ) {
            const CCoins * coins = view.AccessCoins( txin.prevout.hash ) ;
            if ( coins->IsCoinBase() ) {
                fSpendsCoinbase = true ;
                break ;
            }
        }

        CTxMemPoolEntry entry( ptx, nFees, nAcceptTime, dPriority, chainActive.Height(),
                               inChainInputValue, fSpendsCoinbase, nSigOpsCost, lp ) ;
        unsigned int nSize = entry.GetTxSize() ;

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops, MAX_STANDARD_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction
        if ( nSigOpsCost > MAX_STANDARD_TX_SIGOPS_COST )
            return state.DoS( 0, false, REJECT_NONSTANDARD, "bad-txns-too-many-sigops", false,
                strprintf( "%d", nSigOpsCost ) ) ;

        // Continuously rate-limit free (really, very-low-fee) transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm
        static const CAmount VERY_LOW_FEE = 10000 ;
        if ( fLimitFree && nModifiedFees < VERY_LOW_FEE )
        {
            static CCriticalSection csFreeLimiter ;
            static double dFreeCount ;
            static int64_t nLastTime ;
            int64_t nNow = GetTime() ;

            LOCK( csFreeLimiter ) ;

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= pow( 1.0 - 1.0/600.0, (double)( nNow - nLastTime ) ) ;
            nLastTime = nNow ;
            // -limitfreerelay unit is thousand-bytes-per-minute
            if ( dFreeCount + nSize >= GetArg( "-limitfreerelay", DEFAULT_LIMITFREERELAY ) * 10 * 1000 )
                return false ; // rate limited free transaction
            LogPrintf( "%s: rate limit dFreeCount: %g => %g\n", __func__, dFreeCount, dFreeCount + nSize ) ;
            dFreeCount += nSize ;
        }

        // Calculate in-mempool ancestors, up to a limit
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
        std::string errString;
        if (!pool.CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize, nLimitDescendants, nLimitDescendantSize, errString)) {
            return state.DoS( 0, false, REJECT_NONSTANDARD, "too-long-mempool-chain", false, errString ) ;
        }

        // A transaction that spends outputs that would be replaced by it is invalid. Now
        // that we have the set of all ancestors we can detect this
        // pathological case by making sure setConflicts and setAncestors don't
        // intersect
        for ( CTxMemPool::txiter ancestorIt : setAncestors )
        {
            const uint256 & hashAncestor = ancestorIt->GetTx().GetTxHash() ;
            if (setConflicts.count(hashAncestor))
            {
                return state.DoS( 10, false,
                                  REJECT_INVALID, "bad-txns-spends-conflicting-tx", false,
                                  strprintf("%s spends conflicting transaction %s",
                                          hash.ToString(),
                                          hashAncestor.ToString()) ) ;
            }
        }

        // Check if it's economically rational to mine this transaction rather
        // than the ones it replaces
        CAmount nConflictingFees = 0;
        size_t nConflictingSize = 0;
        uint64_t nConflictingCount = 0;
        CTxMemPool::setEntries allConflicting;

        // If we don't hold the lock allConflicting might be incomplete; the
        // subsequent RemoveStaged() and addUnchecked() calls don't guarantee
        // mempool consistency for us
        LOCK(pool.cs);
        const bool fReplacementTransaction = setConflicts.size();
        if (fReplacementTransaction)
        {
            CFeeRate newFeeRate(nModifiedFees, nSize);
            std::set<uint256> setConflictsParents;
            const int maxDescendantsToVisit = 100;
            CTxMemPool::setEntries setIterConflicting;
            for ( const uint256 & hashConflicting : setConflicts )
            {
                CTxMemPool::txiter mi = pool.mapTx.find(hashConflicting);
                if (mi == pool.mapTx.end())
                    continue;

                // Save these to avoid repeated lookups
                setIterConflicting.insert(mi);

                for ( const CTxIn & txin : mi->GetTx().vin )
                {
                    setConflictsParents.insert( txin.prevout.hash ) ;
                }

                nConflictingCount += mi->GetCountWithDescendants();
            }
            // This potentially overestimates the number of actual descendants
            // but we just want to be conservative to avoid doing too much
            // work
            if (nConflictingCount <= maxDescendantsToVisit) {
                // If not too many to replace, then calculate the set of
                // transactions that would have to be evicted
                for ( CTxMemPool::txiter it : setIterConflicting ) {
                    pool.CalculateDescendants( it, allConflicting ) ;
                }
                for ( CTxMemPool::txiter it : allConflicting ) {
                    nConflictingFees += it->GetModifiedFee() ;
                    nConflictingSize += it->GetTxSize() ;
                }
            } else {
                return state.DoS( 0, false,
                        REJECT_NONSTANDARD, "too many potential replacements", false,
                        strprintf("rejecting replacement %s; too many potential replacements (%d > %d)\n",
                            hash.ToString(),
                            nConflictingCount,
                            maxDescendantsToVisit) ) ;
            }

            for (unsigned int j = 0; j < tx.vin.size(); j++)
            {
                // We don't want to accept replacements that require low
                // feerate junk to be mined first. Ideally we'd keep track of
                // the ancestor feerates and make the decision based on that,
                // but for now requiring all new inputs to be confirmed works
                if (!setConflictsParents.count(tx.vin[j].prevout.hash))
                {
                    // Rather than check the UTXO set - potentially expensive -
                    // it's cheaper to just check if the new input refers to a
                    // tx that's in the mempool
                    if (pool.mapTx.find(tx.vin[j].prevout.hash) != pool.mapTx.end())
                        return state.DoS(0, false,
                                         REJECT_NONSTANDARD, "replacement-adds-unconfirmed", false,
                                         strprintf("replacement %s adds unconfirmed input, idx %d",
                                                  hash.ToString(), j));
                }
            }
        }

        unsigned int scriptVerifyFlags = STANDARD_SCRIPT_VERIFY_FLAGS;
        if ( ! Params().OnlyStandardTransactions() ) {
            scriptVerifyFlags = GetArg("-promiscuousmempoolflags", scriptVerifyFlags);
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks
        PrecomputedTransactionData txdata(tx);
        if (!CheckInputs(tx, state, view, true, scriptVerifyFlags, true, txdata)) {
            // SCRIPT_VERIFY_CLEANSTACK requires SCRIPT_VERIFY_WITNESS, so we
            // need to turn both off, and compare against just turning off CLEANSTACK
            // to see if the failure is specifically due to witness validation
            CValidationState stateDummy; // Want reported failures to be from first CheckInputs
            if (!tx.HasWitness() && CheckInputs(tx, stateDummy, view, true, scriptVerifyFlags & ~(SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_CLEANSTACK), true, txdata) &&
                !CheckInputs(tx, stateDummy, view, true, scriptVerifyFlags & ~SCRIPT_VERIFY_CLEANSTACK, true, txdata)) {
                // Only the witness is missing, so the transaction itself may be fine
                state.SetCorruptionPossible();
            }
            return false; // state filled in by CheckInputs
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack
        if (!CheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true, txdata))
        {
            return error("%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s, %s",
                __func__, hash.ToString(), FormatStateMessage(state));
        }

        // Remove conflicting transactions from the mempool
        for ( const CTxMemPool::txiter it : allConflicting )
        {
            LogPrint( "mempool", "replacing tx %s with %s for %s DOGE additional fees, %d delta bytes\n",
                    it->GetTx().GetTxHash().ToString(),
                    hash.ToString(),
                    FormatMoney( nModifiedFees - nConflictingFees ),
                    (int)nSize - (int)nConflictingSize ) ;
            if ( plTxnReplaced != nullptr )
                plTxnReplaced->push_back( it->GetTxPtr() ) ;
        }
        pool.RemoveStaged(allConflicting, false, MemPoolRemovalReason::REPLACED);

        // Store transaction in memory
        pool.addUnchecked( hash, entry, setAncestors ) ;
    }

    GetMainSignals().SyncTransaction(tx, NULL, CMainSignals::SYNC_TRANSACTION_NOT_IN_BLOCK);

    return true;
}

bool AcceptToMemoryPoolWithTime( CTxMemPool& pool, CValidationState &state, const CTransactionRef &tx, bool fLimitFree,
                                 bool* pfMissingInputs, int64_t nAcceptTime, std::list<CTransactionRef>* plTxnReplaced )
{
    std::vector<uint256> vHashTxToUncache;
    bool res = AcceptToMemoryPoolWorker( pool, state, tx, fLimitFree, pfMissingInputs, nAcceptTime, plTxnReplaced, vHashTxToUncache ) ;
    if ( ! res ) {
        for ( const uint256 & hashTx : vHashTxToUncache )
            pcoinsTip->Uncache( hashTx ) ;
    }
    // After we've (potentially) uncached entries, ensure our coins cache is still within its size limits
    CValidationState dummyState ;
    FlushStateToDisk( dummyState, FLUSH_STATE_PERIODIC ) ;
    return res ;
}

bool AcceptToMemoryPool( CTxMemPool& pool, CValidationState &state, const CTransactionRef &tx, bool fLimitFree,
                         bool* pfMissingInputs, std::list<CTransactionRef>* plTxnReplaced )
{
    return AcceptToMemoryPoolWithTime( pool, state, tx, fLimitFree, pfMissingInputs, GetTime(), plTxnReplaced ) ;
}

/** Return transaction in txOut, and if it was found inside a block, its hash is placed in hashBlock */
bool GetTransaction(const uint256 &hash, CTransactionRef &txOut, const Consensus::Params& consensusParams, uint256 &hashBlock, bool fAllowSlow)
{
    LOCK(cs_main);

    CTransactionRef ptx = mempool.get(hash);
    if ( ptx != nullptr )
    {
        txOut = ptx ;
        return true ;
    }

    if ( fTxIndex ) {
        CDiskTxPos postx ;
        if ( pblocktree->ReadTxIndex( hash, postx ) )
        {
            CAutoFile file( OpenBlockFile( postx, true ), SER_DISK, PEER_VERSION ) ;
            if ( file.isNull() )
                return error( "%s: OpenBlockFile failed", __func__ ) ;
            CBlockHeader header ;
            try {
                file >> header ;
                fseek( file.get(), postx.nTxOffset, SEEK_CUR ) ;
                file >> txOut ;
            } catch ( const std::exception & e ) {
                return error("%s: Deserialize or I/O error - %s", __func__, e.what());
            }
            hashBlock = header.GetSha256Hash() ;
            if ( txOut->GetTxHash() != hash )
                return error( "%s: tx hash mismatch", __func__ ) ;
            return true ;
        }
    }

    CBlockIndex * pindexSlow = nullptr ;

    if ( fAllowSlow ) { // use coin database to locate block that contains transaction, and scan it
        int nHeight = -1 ;
        {
            const CCoinsViewCache& view = *pcoinsTip ;
            const CCoins* coins = view.AccessCoins( hash ) ;
            if ( coins != nullptr )
                nHeight = coins->nHeight ;
        }
        if ( nHeight > 0 )
            pindexSlow = chainActive[ nHeight ] ;
    }

    if ( pindexSlow != nullptr ) {
        CBlock block ;
        if ( ReadBlockFromDisk( block, pindexSlow, consensusParams ) ) {
            for ( const auto & tx : block.vtx ) {
                if ( tx->GetTxHash() == hash ) {
                    txOut = tx ;
                    hashBlock = pindexSlow->GetBlockSha256Hash() ;
                    return true ;
                }
            }
        }
    }

    return false ;
}



//
// CBlock and CBlockIndex
//

bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout( OpenBlockFile( pos ), SER_DISK, PEER_VERSION ) ;
    if ( fileout.isNull() )
        return error( "%s: OpenBlockFile failed", __func__ ) ;

    // Write index header
    unsigned int nSize = GetSerializeSize( fileout, block ) ;
    fileout << FLATDATA(messageStart) << nSize ;

    // Write block
    long fileOutPos = ftell( fileout.get() ) ;
    if ( fileOutPos < 0 )
        return error( "%s: ftell failed", __func__ ) ;
    pos.nPos = static_cast< unsigned int >( fileOutPos ) ;
    fileout << block ;

    return true ;
}

/* Generic implementation of block reading that can handle both a block and its header */

template<typename T>
static bool ReadBlockOrHeader(T& block, const CDiskBlockPos& pos, const Consensus::Params& consensusParams)
{
    block.SetNull() ;

    // Open history file to read
    CAutoFile filein( OpenBlockFile( pos, true ), SER_DISK, PEER_VERSION ) ;
    if ( filein.isNull() )
        return error( "%s: OpenBlockFile failed for %s", __func__, pos.ToString() ) ;

    // Read block
    try {
        filein >> block ;
    } catch ( const std::exception & e ) {
        return error( "%s: Deserialize or I/O error - %s at %s", __func__, e.what(), pos.ToString() ) ;
    }

    // Check the header
    if ( ! CheckDogecoinProofOfWork( block, consensusParams ) )
        return error( "%s: Errors in block header at %s", __func__, pos.ToString() ) ;

    return true ;
}

template<typename T>
static bool ReadBlockOrHeader(T& block, const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    if ( ! ReadBlockOrHeader( block, pindex->GetBlockPos(), consensusParams ) )
        return false;
    if ( block.GetSha256Hash() != pindex->GetBlockSha256Hash() )
        return error( "ReadBlockOrHeader: sha256 hash doesn't match index for %s at %s",
                pindex->ToString(), pindex->GetBlockPos().ToString() ) ;
    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, const Consensus::Params& consensusParams)
{
    return ReadBlockOrHeader( block, pos, consensusParams ) ;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    return ReadBlockOrHeader( block, pindex, consensusParams ) ;
}

bool ReadBlockHeaderFromDisk(CBlockHeader& block, const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    return ReadBlockOrHeader(block, pindex, consensusParams);
}

/*
CAmount GetBitcoinBlockSubsidy( int nHeight, const Consensus::Params & consensusParams )
{
    int halvings = nHeight / consensusParams.nSubsidyHalvingInterval;
    // Force block reward to zero when right shift is undefined
    if (halvings >= 64)
        return 0;

    CAmount nSubsidy = 50 * COIN;
    // Subsidy is cut in half every 210,000 blocks which will occur approximately every 4 years
    nSubsidy >>= halvings;
    return nSubsidy;
}
*/

bool IsInitialBlockDownload()
{
    const CChainParams& chainParams = Params() ;

    // Once this function has returned false, it remains false
    static std::atomic < bool > latchToFalse { false } ;
    // Optimization: pre-test latch before taking the lock
    if ( latchToFalse.load( std::memory_order_relaxed ) )
        return false ;

    LOCK( cs_main ) ;
    if ( fImporting || fReindex )
        return true ;
    if ( chainActive.Tip() == nullptr )
        return true ;
    if ( chainActive.Tip()->GetBlockTime() < ( GetTime() - nMaxTipAge ) )
        return true ;

    LogPrintf( "%s: initial downloading of blocks is done, returning false\n", __func__ ) ;
    latchToFalse.store( true, std::memory_order_relaxed ) ;
    return false ;
}

CBlockIndex *pindexBestForkTip = nullptr, *pindexBestForkBase = nullptr ;

static void AlertNotify(const std::string& strMessage)
{
    CAlert::Notify(strMessage);
}

void CheckForkWarningConditions()
{
    AssertLockHeld( cs_main ) ;

    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before finishing the sync)
    if ( IsInitialBlockDownload() )
        return ;

    // If our best fork is no longer within 360 blocks (+/- 6 hours if no one mines it)
    // of our head, drop it
    const int blocks_above_limit = 360 ;
    if ( pindexBestForkTip != nullptr && chainActive.Height() - pindexBestForkTip->nHeight >= blocks_above_limit )
        pindexBestForkTip = nullptr ;

    const int blocks_above = 30 ;
    if ( pindexBestForkTip != nullptr ||
            ( pindexBestInvalid != nullptr &&
                pindexBestInvalid->nHeight > chainActive.Tip()->nHeight + blocks_above ) )
    {
        if ( ! GetHighForkFound() && pindexBestForkBase != nullptr )
        {
            std::string warning = std::string( "'Warning: Higher fork found, forking after block " ) +
                pindexBestForkBase->GetBlockSha256Hash().ToString() + std::string( "'" ) ;
            AlertNotify( warning ) ;
        }
        if ( pindexBestForkTip != nullptr && pindexBestForkBase != nullptr )
        {
            LogPrintf( "%s: Warning: Higher valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s)\n(chain state database corruption likely)\n", __func__,
                   pindexBestForkBase->nHeight, pindexBestForkBase->GetBlockSha256Hash().ToString(),
                   pindexBestForkTip->nHeight, pindexBestForkTip->GetBlockSha256Hash().ToString() ) ;
            SetHighForkFound( true ) ;
        }
        else
        {
            LogPrintf( "%s: Warning: Found invalid chain at least ~%i blocks higher than the best chain\n(chain state database corruption likely)\n", __func__, blocks_above ) ;
            SetHighInvalidChainFound( true ) ;
        }
    }
    else
    {
        SetHighForkFound( false ) ;
        SetHighInvalidChainFound( false ) ;
    }
}

//
// If we are on a fork that is sufficiently large, set a warning flag
//
void CheckForkWarningConditionsOnNewFork( CBlockIndex * pindexNewForkTip )
{
    AssertLockHeld( cs_main ) ;

    CBlockIndex * pfork = pindexNewForkTip ;
    CBlockIndex * plonger = chainActive.Tip() ;
    while ( pfork && pfork != plonger )
    {
        while ( plonger && plonger->nHeight > pfork->nHeight )
            plonger = plonger->pprev ;
        if ( pfork == plonger )
            break ;
        pfork = pfork->pprev ;
    }

    // We define a condition where we should warn the user about as a fork of at least 30 blocks
    // with a tip within 360 blocks (+/- 6 hours if no one mines it) of ours
    const int blocks_above = 30 ;
    const int blocks_above_limit = 360 ;
    if ( pfork && ( ! pindexBestForkTip || ( pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight ) ) &&
            pindexNewForkTip->nHeight - pfork->nHeight > blocks_above &&
            chainActive.Height() - pindexNewForkTip->nHeight < blocks_above_limit )
    {
        pindexBestForkTip = pindexNewForkTip ;
        pindexBestForkBase = pfork ;
    }

    CheckForkWarningConditions() ;
}

void static SayAboutRejectedChain( CBlockIndex* pindexNew )
{
    if ( pindexBestInvalid == nullptr || pindexNew->nHeight > pindexBestInvalid->nHeight )
        pindexBestInvalid = pindexNew ;

    LogPrintf( "%s: rejected block height=%d sha256_hash=%s version=0x%x%s date=%s\n", __func__,
        pindexNew->nHeight, pindexNew->GetBlockSha256Hash().ToString(),
        pindexNew->nVersion, CPureBlockHeader::IsAuxpowInVersion( pindexNew->nVersion ) ? "(auxpow)" : "",
        DateTimeStrFormat( "%Y-%m-%d %H:%M:%S", pindexNew->GetBlockTime() ) ) ;

    CBlockIndex *tip = chainActive.Tip() ;
    assert ( tip != nullptr ) ;

    LogPrintf( "%s: current tip height=%d sha256_hash=%s version=0x%x%s date=%s\n", __func__,
        chainActive.Height(), tip->GetBlockSha256Hash().ToString(),
        tip->nVersion, CPureBlockHeader::IsAuxpowInVersion( tip->nVersion ) ? "(auxpow)" : "",
        DateTimeStrFormat( "%Y-%m-%d %H:%M:%S", tip->GetBlockTime() ) ) ;

    CheckForkWarningConditions() ;
}

void static InvalidBlockFound( CBlockIndex * pindex, const CValidationState & state )
{
    if ( ! state.CorruptionPossible() ) {
        pindex->nStatus |= BLOCK_FAILED_VALID ;
        setOfDirtyBlockIndices.insert( pindex ) ;
        setOfBlockIndexCandidates.erase( pindex ) ;
        SayAboutRejectedChain( pindex ) ;
    }
}

void UpdateCoins( const CTransaction & tx, CCoinsViewCache & inputs, CTxUndo & txundo, int nHeight )
{
    // mark inputs spent
    if ( ! tx.IsCoinBase() ) {
        txundo.vprevout.reserve( tx.vin.size() ) ;
        for ( const CTxIn & txin : tx.vin ) {
            CCoinsModifier coins = inputs.ModifyCoins( txin.prevout.hash ) ;
            unsigned nPos = txin.prevout.n ;

            if ( nPos >= coins->vout.size() || coins->vout[ nPos ].IsNull() )
                assert( false ) ;

            // mark an outpoint spent, and construct undo information
            txundo.vprevout.push_back( CTxInUndo( coins->vout[ nPos ] ) ) ;
            coins->Spend( nPos ) ;
            if ( coins->vout.size() == 0 ) {
                CTxInUndo& undo = txundo.vprevout.back() ;
                undo.nHeight = coins->nHeight ;
                undo.fCoinBase = coins->fCoinBase ;
                undo.nVersion = coins->nVersion ;
            }
        }
    }
    // add outputs
    inputs.ModifyNewCoins( tx.GetTxHash(), tx.IsCoinBase() )->FromTx( tx, nHeight ) ;
}

void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, int nHeight)
{
    CTxUndo txundo;
    UpdateCoins(tx, inputs, txundo, nHeight);
}

bool CScriptCheck::operator()() {
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    const CScriptWitness *witness = &ptxTo->vin[nIn].scriptWitness;
    if (!VerifyScript(scriptSig, scriptPubKey, witness, nFlags, CachingTransactionSignatureChecker(ptxTo, nIn, amount, cacheStore, *txdata), &error)) {
        return false;
    }
    return true;
}

int GetSpendHeight( const CCoinsViewCache & inputs )
{
    LOCK( cs_main ) ;
    BlockMap::iterator mi = mapBlockIndex.find( inputs.GetSha256OfBestBlock() ) ;
    assert( mi != mapBlockIndex.end() ) ;
    CBlockIndex* pindexPrev = ( *mi ).second ;
    assert( pindexPrev != nullptr ) ;
    return pindexPrev->nHeight + 1 ;
}

namespace Consensus {
bool CheckTxInputs(const CChainParams& params, const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight)
{
        // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
        // for an attacker to attempt to split the network.
        if (!inputs.HaveInputs(tx))
            return state.Invalid(false, 0, "", "Inputs unavailable");

        CAmount nValueIn = 0;
        CAmount nFees = 0;
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            const COutPoint &prevout = tx.vin[i].prevout;
            const CCoins *coins = inputs.AccessCoins(prevout.hash);
            assert(coins);

            // If prev is coinbase, check that it's matured
            if (coins->IsCoinBase()) {
                // Dogecoin: Switch maturity at depth 145,000
                int nCoinbaseMaturity = params.GetConsensus(coins->nHeight).nCoinbaseMaturity;
                if (nSpendHeight - coins->nHeight < nCoinbaseMaturity)
                    return state.Invalid(false,
                        REJECT_INVALID, "bad-txns-premature-spend-of-coinbase",
                        strprintf("tried to spend coinbase at depth %d", nSpendHeight - coins->nHeight));
            }

            // Check for negative or overflow input values
            nValueIn += coins->vout[ prevout.n ].nValue ;
            if ( ! MoneyRange( coins->vout[ prevout.n ].nValue ) || ! MoneyRange( nValueIn ) )
                return state.DoS( 10, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange" ) ;

        }

        if ( nValueIn < tx.GetValueOut() )
            return state.DoS( 10, false, REJECT_INVALID, "bad-txns-in-belowout", false,
                strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(tx.GetValueOut())) ) ;

        // Tally transaction fees
        CAmount nTxFee = nValueIn - tx.GetValueOut() ;
        if ( nTxFee < 0 )
            return state.DoS( 10, false, REJECT_INVALID, "bad-txns-fee-negative" ) ;
        nFees += nTxFee ;
        if ( ! MoneyRange( nFees ) )
            return state.DoS( 10, false, REJECT_INVALID, "bad-txns-fee-outofrange" ) ;
    return true;
}
}// namespace Consensus

bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheStore, PrecomputedTransactionData& txdata, std::vector<CScriptCheck> *pvChecks)
{
    if (!tx.IsCoinBase())
    {
        if (!Consensus::CheckTxInputs(Params(), tx, state, inputs, GetSpendHeight(inputs)))
            return false;

        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks

        // Skip script verification when connecting blocks under the
        // assumedvalid block. Assuming the assumedvalid block is valid this
        // is safe because block merkle hashes are still computed and checked,
        // Of course, if an assumed valid block is invalid due to false scriptSigs
        // this optimization would allow an invalid chain to be accepted
        if ( fScriptChecks ) {
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const COutPoint &prevout = tx.vin[i].prevout;
                const CCoins* coins = inputs.AccessCoins(prevout.hash);
                assert(coins);

                // Verify signature
                CScriptCheck check(*coins, tx, i, flags, cacheStore, &txdata);
                if (pvChecks) {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check()) {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes
                        CScriptCheck check2(*coins, tx, i,
                                flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheStore, &txdata);
                        if (check2())
                            return state.Invalid(false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after soft-fork
                    // super-majority signaling has occurred
                    return state.DoS( 10,false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())) ) ;
                }
            }
        }
    }

    return true;
}

namespace {

bool UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout( OpenUndoFile( pos ), SER_DISK, PEER_VERSION ) ;
    if ( fileout.isNull() )
        return error( "%s: OpenUndoFile failed", __func__ ) ;

    // Write index header
    unsigned int nSize = GetSerializeSize(fileout, blockundo);
    fileout << FLATDATA(messageStart) << nSize;

    // Write undo data
    long fileOutPos = ftell( fileout.get() ) ;
    if ( fileOutPos < 0 )
        return error( "%s: ftell failed", __func__ ) ;
    pos.nPos = static_cast< unsigned int >( fileOutPos ) ;
    fileout << blockundo ;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

bool UndoReadFromDisk(CBlockUndo& blockundo, const CDiskBlockPos& pos, const uint256& hashBlock)
{
    // Open history file to read
    CAutoFile filein( OpenUndoFile( pos, true ), SER_DISK, PEER_VERSION ) ;
    if ( filein.isNull() )
        return error( "%s: OpenUndoFile failed", __func__ ) ;

    // Read block
    uint256 hashChecksum;
    try {
        filein >> blockundo;
        filein >> hashChecksum;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    // Verify checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    if (hashChecksum != hasher.GetHash())
        return error("%s: Checksum mismatch", __func__);

    return true;
}

/** Abort with a message */
bool AbortNode( const std::string & strMessage, const std::string & userMessage = "" )
{
    SetMiscWarning(strMessage);
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occurred, see debug log for details") : userMessage,
        "", CClientUserInterface::MSG_ERROR ) ;
    RequestShutdown() ;
    return false ;
}

bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage="")
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

} // anon namespace

/**
 * Apply the undo operation of a CTxInUndo to the given chain state.
 * @param undo The undo object.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return True on success.
 */
bool ApplyTxInUndo(const CTxInUndo& undo, CCoinsViewCache& view, const COutPoint& out)
{
    bool fClean = true;

    CCoinsModifier coins = view.ModifyCoins(out.hash);
    if (undo.nHeight != 0) {
        // undo data contains height: this is the last output of the prevout tx being spent
        if (!coins->IsPruned())
            fClean = fClean && error("%s: undo data overwriting existing transaction", __func__);
        coins->Clear();
        coins->fCoinBase = undo.fCoinBase;
        coins->nHeight = undo.nHeight;
        coins->nVersion = undo.nVersion;
    } else {
        if (coins->IsPruned())
            fClean = fClean && error("%s: undo data adding output to missing transaction", __func__);
    }
    if (coins->IsAvailable(out.n))
        fClean = fClean && error("%s: undo data overwriting existing output", __func__);
    if (coins->vout.size() < out.n+1)
        coins->vout.resize(out.n+1);
    coins->vout[out.n] = undo.txout;

    return fClean;
}

bool DisconnectBlock(const CBlock& block, CValidationState& state, const CBlockIndex* pindex, CCoinsViewCache& view, bool* pfClean)
{
    assert( pindex->GetBlockSha256Hash() == view.GetSha256OfBestBlock() ) ;

    if (pfClean)
        *pfClean = false;

    bool fClean = true;

    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
        return error("DisconnectBlock(): no undo data available");
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockSha256Hash()))
        return error("DisconnectBlock(): failure reading undo data");

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
        return error("DisconnectBlock(): block and undo data inconsistent");

    // undo transactions in reverse order
    for (int i = block.vtx.size() - 1; i >= 0; i--) {
        const CTransaction &tx = *(block.vtx[i]);
        uint256 hash = tx.GetTxHash() ;

        // Check that all outputs are available and match the outputs in the block itself
        // exactly
        {
        CCoinsModifier outs = view.ModifyCoins(hash);
        outs->ClearUnspendable();

        CCoins outsBlock(tx, pindex->nHeight);
        // The CCoins serialization does not serialize negative numbers.
        // No network rules currently depend on the version here, so an inconsistency is harmless
        // but it must be corrected before txout nversion ever influences a network rule
        if (outsBlock.nVersion < 0)
            outs->nVersion = outsBlock.nVersion;
        if (*outs != outsBlock)
            fClean = fClean && error("DisconnectBlock(): added transaction mismatch? database corrupted");

        // remove outputs
        outs->Clear();
        }

        // restore inputs
        if (i > 0) { // not coinbases
            const CTxUndo &txundo = blockUndo.vtxundo[i-1];
            if (txundo.vprevout.size() != tx.vin.size())
                return error("DisconnectBlock(): transaction and undo data inconsistent");
            for (unsigned int j = tx.vin.size(); j-- > 0;) {
                const COutPoint &out = tx.vin[j].prevout;
                const CTxInUndo &undo = txundo.vprevout[j];
                if (!ApplyTxInUndo(undo, view, out))
                    fClean = false;
            }
        }
    }

    // move best block pointer to prevout block
    view.SetBestBlockBySha256( pindex->pprev->GetBlockSha256Hash() ) ;

    if (pfClean) {
        *pfClean = fClean;
        return true;
    }

    return fClean;
}

void static FlushBlockFile(bool fFinalize = false)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize);

static CCheckQueue< CScriptCheck > scriptcheckqueue( 128 ) ;

void ThreadScriptCheck()
{
    RenameThread( "scriptcheck" ) ;
    scriptcheckqueue.Loop() ;
}

void StopScriptChecking()
{
    LogPrintf( "%s()\n", __func__ ) ;
    scriptcheckqueue.Quit() ;
}

// Protected by cs_main
VersionBitsCache versionbitscache;

int32_t ComputeBlockVersion(const CBlockIndex* pindexPrev, const Consensus::Params& params)
{
    LOCK(cs_main);
    int32_t nVersion = VERSIONBITS_TOP_BITS;

    for (int i = 0; i < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; i++) {
        ThresholdState state = VersionBitsState(pindexPrev, params, (Consensus::DeploymentPos)i, versionbitscache);
        if (state == THRESHOLD_LOCKED_IN || state == THRESHOLD_STARTED) {
            nVersion |= VersionBitsMask(params, (Consensus::DeploymentPos)i);
        }
    }

    return nVersion;
}

/**
 * Threshold condition checker that triggers when unknown versionbits are seen on the network.
 */
class WarningBitsConditionChecker : public AbstractThresholdConditionChecker
{
private:
    int bit;

public:
    WarningBitsConditionChecker(int bitIn) : bit(bitIn) {}

    int64_t BeginTime(const Consensus::Params& params) const { return 0; }
    int64_t EndTime(const Consensus::Params& params) const { return std::numeric_limits<int64_t>::max(); }
    int Period(const Consensus::Params& params) const { return params.nMinerConfirmationWindow; }
    int Threshold(const Consensus::Params& params) const { return params.nRuleChangeActivationThreshold; }

    bool Condition(const CBlockIndex* pindex, const Consensus::Params& params) const
    {
        return ((pindex->nVersion & VERSIONBITS_TOP_MASK) == VERSIONBITS_TOP_BITS) &&
               ((pindex->nVersion >> bit) & 1) != 0 &&
               ((ComputeBlockVersion(pindex->pprev, params) >> bit) & 1) == 0;
    }
};

// Protected by cs_main
static ThresholdConditionCache warningcache[VERSIONBITS_NUM_BITS];

static int64_t nTimeCheck = 0;
static int64_t nTimeForks = 0;
static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;

/**
 * Returns true if there are nRequired or more blocks of minVersion or above
 * in the last consensusParams.nMajorityWindow blocks, starting at pstart and going backwards
 */
static bool IsSuperMajority( int minVersion, const CBlockIndex * pstart, unsigned nRequired, const Consensus::Params & consensusParams )
{
    unsigned int nFound = 0;
    for (int i = 0; i < consensusParams.nMajorityWindow && nFound < nRequired && pstart != NULL; i++)
    {
        if (pstart->GetBaseVersion() >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }
    return nFound >= nRequired ;
}

bool ConnectBlock( const CBlock & block, CValidationState & state, CBlockIndex * pindex,
                   CCoinsViewCache & view, const CChainParams & chainparams, bool justCheck )
{
    AssertLockHeld( cs_main ) ;

    const Consensus::Params & consensus = Params().GetConsensus( pindex->nHeight ) ;
    int64_t nTimeStart = GetTimeMicros() ;

    // Check it again in case a previous version let a bad block in
    if ( ! CheckBlock( block, state, ! justCheck, ! justCheck ) )
        return error("%s: Consensus::CheckBlock: %s", __func__, FormatStateMessage(state));

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = ( pindex->pprev == nullptr ) ? uint256() : pindex->pprev->GetBlockSha256Hash() ;
    assert( hashPrevBlock == view.GetSha256OfBestBlock() ) ;

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if ( block.GetSha256Hash() == Params().GetConsensus(0).hashGenesisBlock ) {
        if ( ! justCheck )
            view.SetBestBlockBySha256( pindex->GetBlockSha256Hash() ) ;
        return true ;
    }

    bool fScriptChecks = true ; // just true and no hashAssumeValid stuff

    int64_t nTime1 = GetTimeMicros(); nTimeCheck += nTime1 - nTimeStart;
    LogPrint("bench", "    - Sanity checks: %.2fms [%.2fs]\n", 0.001 * (nTime1 - nTimeStart), nTimeCheck * 0.000001);

    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    // If such overwrites are allowed, coinbases and transactions depending upon those
    // can be duplicated to remove the ability to spend the first instance -- even after
    // being sent to another address.
    // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
    // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
    // already refuses previously-known transaction hashes entirely.
    // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
    // two in the chain that violate it. This prevents exploiting the issue against nodes during their
    // initial block download.
    // Dogecoin: BIP30 has been active since inception
    bool fEnforceBIP30 = true;

    // Once BIP34 activated it was not possible to create new duplicate coinbases and thus other than starting
    // with the 2 existing duplicate coinbase pairs, not possible to create overwriting txs.  But by the
    // time BIP34 activated, in each of the existing pairs the duplicate coinbase had overwritten the first
    // before the first had been spent.  Since those coinbases are sufficiently buried its no longer possible to create further
    // duplicate transactions descending from the known pairs either.
    // If we're on the known chain at height greater than where BIP34 activated, we can save the db accesses needed for the BIP30 check
    CBlockIndex *pindexBIP34height = pindex->pprev->GetAncestor(chainparams.GetConsensus(0).BIP34Height);
    //Only continue to enforce if we're below BIP34 activation height or the block hash at that height doesn't correspond
    fEnforceBIP30 = fEnforceBIP30 && (!pindexBIP34height || !(pindexBIP34height->GetBlockSha256Hash() == chainparams.GetConsensus(0).BIP34Hash));

    if (fEnforceBIP30) {
        for ( const auto & tx : block.vtx ) {
            const CCoins * coins = view.AccessCoins( tx->GetTxHash() ) ;
            if ( coins && ! coins->IsPruned() )
                return state.DoS( 50, error("ConnectBlock(): tried to overwrite transaction"),
                                  REJECT_INVALID, "bad-txns-BIP30" ) ;
        }
    }

    // BIP16 didn't become active until Apr 1 2012
    // Dogecoin: BIP16 has been enabled since inception
    bool fStrictPayToScriptHash = true;

    unsigned int flags = fStrictPayToScriptHash ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE;

    // Start enforcing the DERSIG (BIP66) rule
    if (pindex->nHeight >= chainparams.GetConsensus(0).BIP66Height) {
        flags |= SCRIPT_VERIFY_DERSIG;
    }

    // Start enforcing CHECKLOCKTIMEVERIFY, (BIP65) for block.nVersion=4
    // blocks, when 75% of the network has upgraded:
    if (block.GetBaseVersion() >= 4 && IsSuperMajority(4, pindex->pprev, chainparams.GetConsensus(0).nMajorityEnforceBlockUpgrade, chainparams.GetConsensus(0))) {
        flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic
    int nLockTimeFlags = 0 ;
    if ( VersionBitsState( pindex->pprev, consensus, Consensus::DEPLOYMENT_CSV, versionbitscache ) == THRESHOLD_ACTIVE ) {
        flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY ;
        nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE ;
    }

    // Start enforcing WITNESS rules using versionbits logic
    if (IsWitnessEnabled(pindex->pprev, consensus)) {
        flags |= SCRIPT_VERIFY_WITNESS;
        flags |= SCRIPT_VERIFY_NULLDUMMY;
    }

    int64_t nTime2 = GetTimeMicros(); nTimeForks += nTime2 - nTime1;
    LogPrint("bench", "    - Fork checks: %.2fms [%.2fs]\n", 0.001 * (nTime2 - nTime1), nTimeForks * 0.000001);

    CBlockUndo blockundo;

    CCheckQueueControl< CScriptCheck > control( fScriptChecks && nScriptCheckThreads ? &scriptcheckqueue : NULL ) ;

    std::vector<int> prevheights;
    CAmount nFees = 0;
    int nInputs = 0;
    int64_t nSigOpsCost = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    std::vector<PrecomputedTransactionData> txdata;
    txdata.reserve(block.vtx.size()); // Required so that pointers to individual PrecomputedTransactionData don't get invalidated
    for ( unsigned int i = 0 ; i < block.vtx.size() ; i ++ )
    {
        const CTransaction & tx = *( block.vtx[ i ] ) ;

        nInputs += tx.vin.size() ;

        if ( ! tx.IsCoinBase() )
        {
            if ( ! view.HaveInputs( tx ) )
                return state.DoS( IsInitialBlockDownload() ? 100 : 50,
                                    error( "ConnectBlock(): inputs missing/spent" ),
                                        REJECT_INVALID, "bad-txns-inputs-missingorspent" ) ;

            // Check that transaction is BIP68 final
            // BIP68 lock checks (as opposed to nLockTime checks) must
            // be in ConnectBlock because they require the UTXO set
            prevheights.resize(tx.vin.size());
            for (size_t j = 0; j < tx.vin.size(); j++) {
                prevheights[j] = view.AccessCoins(tx.vin[j].prevout.hash)->nHeight;
            }

            if ( ! SequenceLocks( tx, nLockTimeFlags, &prevheights, *pindex ) ) {
                return state.DoS( 10, error("%s: contains a non-BIP68-final transaction", __func__),
                                  REJECT_INVALID, "bad-txns-nonfinal" ) ;
            }
        }

        // GetTransactionSigOpCost counts 3 types of sigops:
        // * legacy (always)
        // * p2sh (when P2SH enabled in flags and excludes coinbase)
        // * witness (when witness enabled in flags and excludes coinbase)
        nSigOpsCost += GetTransactionSigOpCost( tx, view, flags ) ;
        if ( nSigOpsCost > MAX_BLOCK_SIGOPS_COST )
            return state.DoS( 10, error("ConnectBlock(): too many signature check operations"),
                              REJECT_INVALID, "bad-blk-sigops" ) ;

        txdata.emplace_back( tx ) ;
        if ( ! tx.IsCoinBase() )
        {
            nFees += view.GetValueIn( tx ) - tx.GetValueOut() ;

            std::vector< CScriptCheck > vChecks ;
            bool cacheResults = justCheck ; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
            if ( ! CheckInputs( tx, state, view, fScriptChecks, flags, cacheResults, txdata[ i ], nScriptCheckThreads ? &vChecks : NULL ) )
                return error( "ConnectBlock(): CheckInputs on %s failed with %s",
                    tx.GetTxHash().ToString(), FormatStateMessage(state) ) ;
            control.Add(vChecks);
        }

        CTxUndo undoDummy;
        if (i > 0) {
            blockundo.vtxundo.push_back(CTxUndo());
        }
        UpdateCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

        vPos.push_back( std::make_pair(tx.GetTxHash(), pos) ) ;
        pos.nTxOffset += ::GetSerializeSize( tx, SER_DISK, PEER_VERSION ) ;
    }

    int64_t nTime3 = GetTimeMicros(); nTimeConnect += nTime3 - nTime2;
    LogPrint("bench", "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", (unsigned)block.vtx.size(), 0.001 * (nTime3 - nTime2), 0.001 * (nTime3 - nTime2) / block.vtx.size(), nInputs <= 1 ? 0 : 0.001 * (nTime3 - nTime2) / (nInputs-1), nTimeConnect * 0.000001);

    CAmount blockReward = nFees + GetDogecoinBlockSubsidy( pindex->nHeight, chainparams.GetConsensus( pindex->nHeight ), hashPrevBlock ) ;
    if ( block.vtx[0]->GetValueOut() > blockReward ) {
            return state.DoS( IsInitialBlockDownload() ? 100 : 50,
                          error( "ConnectBlock(): coinbase pays too much (actual=%d vs limit=%d)",
                                  block.vtx[0]->GetValueOut(), blockReward ),
                                      REJECT_INVALID, "bad-cb-amount" ) ;
    }

    // update nBlockNewCoins
    pindex->nBlockNewCoins = block.vtx[ 0 ]->GetValueOut() - nFees ;
    // update nChainCoins
    /* pindex->nChainCoins = ( pindex->pprev != nullptr ? pindex->pprev->nChainCoins : 0 ) + pindex->nBlockNewCoins ; */

    if (!control.Wait())
        return state.DoS( 50, false ) ;
    int64_t nTime4 = GetTimeMicros(); nTimeVerify += nTime4 - nTime2;
    LogPrint("bench", "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1, 0.001 * (nTime4 - nTime2), nInputs <= 1 ? 0 : 0.001 * (nTime4 - nTime2) / (nInputs-1), nTimeVerify * 0.000001);

    if ( justCheck ) return true ;

    // Write undo information to disk
    if ( pindex->GetUndoPos().IsNull() || ! pindex->IsValid( BLOCK_VALID_SCRIPTS ) )
    {
        if ( pindex->GetUndoPos().IsNull() ) {
            CDiskBlockPos _pos ;
            if ( ! FindUndoPos( state, pindex->nFile, _pos, ::GetSerializeSize( blockundo, SER_DISK, PEER_VERSION ) + 40 ) )
                return error( "ConnectBlock(): FindUndoPos failed" ) ;
            if ( ! UndoWriteToDisk( blockundo, _pos, pindex->pprev->GetBlockSha256Hash(), chainparams.MessageStart() ) )
                return AbortNode( state, "Failed to write undo data" ) ;

            // update nUndoPos in block index
            pindex->nUndoPos = _pos.nPos ;
            pindex->nStatus |= BLOCK_UNDO_EXISTS ;
        }

        pindex->RaiseValidity( BLOCK_VALID_SCRIPTS ) ;
        setOfDirtyBlockIndices.insert( pindex ) ;
    }

    if (fTxIndex)
        if (!pblocktree->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");

    // add this block to the view's block chain
    view.SetBestBlockBySha256( pindex->GetBlockSha256Hash() ) ;

    int64_t nTime5 = GetTimeMicros(); nTimeIndex += nTime5 - nTime4;
    LogPrint("bench", "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime5 - nTime4), nTimeIndex * 0.000001);

    // Watch for changes to the previous coinbase transaction
    static uint256 hashPrevBestCoinBase;
    GetMainSignals().UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = block.vtx[0]->GetTxHash() ;

    int64_t nTime6 = GetTimeMicros(); nTimeCallbacks += nTime6 - nTime5;
    LogPrint("bench", "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime6 - nTime5), nTimeCallbacks * 0.000001);

    return true;
}

/**
 * Update the on-disk chain state
 *
 * The caches and indexes are flushed depending on the mode we're called with
 * if they're too large, if it's been a while since the last write,
 * or always and in all cases if we're pruning and are deleting files
 */
bool static FlushStateToDisk( CValidationState & state, FlushStateMode mode, int nManualPruneHeight )
{
    int64_t beginMicros = GetTimeMicros() ;

    int64_t nMempoolUsage = mempool.DynamicMemoryUsage() ;
    const CChainParams & chainparams = Params() ;

    static int64_t nLastWrite = 0 ;
    static int64_t nLastFlush = 0 ;
    static int64_t nLastSetChain = 0 ;

    LOCK2( cs_main, cs_LastBlockFile ) ;

    std::set<int> setFilesToPrune;
    bool fFlushForPrune = false;
    try {
    if (fPruneMode && (fCheckForPruning || nManualPruneHeight > 0) && !fReindex) {
        if (nManualPruneHeight > 0) {
            FindFilesToPruneManual(setFilesToPrune, nManualPruneHeight);
        } else {
            FindFilesToPrune(setFilesToPrune, chainparams.PruneAfterHeight());
            fCheckForPruning = false;
        }
        if (!setFilesToPrune.empty()) {
            fFlushForPrune = true;
            if (!fHavePruned) {
                pblocktree->WriteFlag("prunedblockfiles", true);
                fHavePruned = true;
            }
        }
    }
    int64_t nNow = GetTimeMicros();
    // Avoid writing/flushing immediately after startup
    if (nLastWrite == 0) {
        nLastWrite = nNow;
    }
    if (nLastFlush == 0) {
        nLastFlush = nNow;
    }
    if (nLastSetChain == 0) {
        nLastSetChain = nNow;
    }
    int64_t nMempoolSizeMax = GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
    int64_t cacheSize = pcoinsTip->DynamicMemoryUsage() * DB_PEAK_USAGE_FACTOR;
    int64_t nTotalSpace = nCoinCacheUsage + std::max<int64_t>(nMempoolSizeMax - nMempoolUsage, 0);
    // The cache is large and we're within 10% and 200 MiB or 50% and 50MiB of the limit, but we have time now (not in the middle of a block processing)
    bool fCacheLarge = ( mode == FLUSH_STATE_PERIODIC &&
                            cacheSize > std::min(
                                    std::max( nTotalSpace / 2, nTotalSpace - MIN_BLOCK_COINSDB_USAGE * 1024 * 1024 ),
                                    std::max( ( 9 * nTotalSpace ) / 10, nTotalSpace - MAX_BLOCK_COINSDB_USAGE * 1024 * 1024 )
                            ) ) ;
    // The cache is over the limit, we have to write now
    bool fCacheCritical = mode == FLUSH_STATE_IF_NEEDED && cacheSize > nTotalSpace;
    // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload after a crash
    bool fPeriodicWrite = mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
    // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage
    bool fPeriodicFlush = mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
    // Combine all conditions that result in a full cache flush
    bool fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fCacheLarge || fCacheCritical || fPeriodicFlush || fFlushForPrune;
    // Write blocks and block index to disk
    if (fDoFullFlush || fPeriodicWrite) {
        // Depend on nMinDiskSpace to ensure we can write block index
        if (!CheckDiskSpace(0))
            return state.Error("out of disk space");
        // First make sure all block and undo data is flushed to disk
        FlushBlockFile();
        // Then update all block file information (which may refer to block and undo files)
        {
            std::vector< std::pair< int, const CBlockFileInfo * > > vFiles ;
            vFiles.reserve( setOfDirtyBlockFiles.size() ) ;
            for ( std::set< int >::iterator it = setOfDirtyBlockFiles.begin() ; it != setOfDirtyBlockFiles.end() ; ) {
                vFiles.push_back( std::make_pair( *it, &vinfoBlockFile[ *it ] ) ) ;
                setOfDirtyBlockFiles.erase( it ++ ) ;
            }
            std::vector< const CBlockIndex * > vBlocks ;
            vBlocks.reserve( setOfDirtyBlockIndices.size() ) ;
            for ( std::set< CBlockIndex* >::iterator it = setOfDirtyBlockIndices.begin() ; it != setOfDirtyBlockIndices.end() ; ) {
                vBlocks.push_back( *it ) ;
                setOfDirtyBlockIndices.erase( it ++ ) ;
            }
            if ( ! pblocktree->WriteBatchSync( vFiles, nLastBlockFile, vBlocks ) ) {
                return AbortNode( state, "Failed to write to block index database" ) ;
            }
        }
        // Finally remove any pruned files
        if (fFlushForPrune)
            UnlinkPrunedFiles(setFilesToPrune);
        nLastWrite = nNow;
    }
    // Flush best chain related state. This can only be done if the blocks / block index write was also done
    if (fDoFullFlush) {
        // Typical CCoins structures on disk are around 128 bytes in size.
        // Pushing a new one to the database can cause it to be written
        // twice (once in the log, and once in the tables). This is already
        // an overestimation, as most will delete an existing entry or
        // overwrite one. Still, use a conservative safety factor of 2
        if (!CheckDiskSpace(128 * 2 * 2 * pcoinsTip->GetCacheSize()))
            return state.Error("out of disk space");
        // Flush the chainstate (which may refer to block index entries)
        if (!pcoinsTip->Flush())
            return AbortNode(state, "Failed to write to coin database");
        nLastFlush = nNow;
    }
    if (fDoFullFlush || ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) && nNow > nLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000)) {
        // Update best block in wallet (so we can detect restored wallets)
        GetMainSignals().SetBestChain(chainActive.GetLocator());
        nLastSetChain = nNow;
    }
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error while flushing: ") + e.what());
    }

    LogPrint( "bench", "%s finished in %.6f s\n", __func__, 0.000001 * ( GetTimeMicros() - beginMicros ) ) ;
    return true ;
}

void FlushStateToDisk() {
    CValidationState state;
    FlushStateToDisk( state, FLUSH_STATE_ALWAYS ) ;
}

void PruneAndFlush() {
    CValidationState state;
    fCheckForPruning = true;
    FlushStateToDisk( state, FLUSH_STATE_NONE ) ;
}

/* CAmount static CountBlockNewCoins( const CBlock & block, unsigned int blockHeight, const CChainParams & chainparams )
{
    CAmount blockFees = 0 ;
    for ( const CTransactionRef & tx : block.vtx )
    {
        if ( ! tx->IsCoinBase() )
        {
            CCoinsViewCache view( pcoinsTip ) ;
            CAmount txValueIn = 0 ;
            for ( const CTxIn & txin : tx->vin )
            {
                const CCoins * coins = view.AccessCoins( txin.prevout.hash ) ;
                if ( coins != nullptr && txin.prevout.n < coins->vout.size() ) {
                    const CTxOut & vout = coins->vout[ txin.prevout.n ] ;
                    txValueIn += vout.nValue ;
                } else {
                    CTransactionRef prevoutTx ;
                    uint256 blockSha256 ;
                    if ( GetTransaction( txin.prevout.hash, prevoutTx, chainparams.GetConsensus( blockHeight ), blockSha256, true ) ) {
                        const CTxOut & vout = prevoutTx->vout[ txin.prevout.n ] ;
                        txValueIn += vout.nValue ;
                    } // else {
                      //     LogPrintf( "%s: can't get prevout for input %s of tx %s from block height=%u sha256_hash=%s\n", __func__,
                      //         txin.ToString(), tx->GetTxHash().GetHex(), blockHeight, block.GetSha256Hash().GetHex(),
                      //         fTxIndex ? "" : ", most probably due to txindex=0" ) ;
                      // }
                }
            }

            CAmount txValueOut = 0 ;
            for ( const CTxOut & txout : tx->vout )
                txValueOut += txout.nValue ;

            if ( txValueIn > txValueOut )
                blockFees += txValueIn - txValueOut ;
        }
    }

    const CAmount blockReward = block.vtx[ 0 ]->vout[ 0 ].nValue ;

    CAmount newCoins = blockReward - blockFees ;
    // LogPrintf( "%s: block %u generated %ld new coins, with fees %ld full reward is %ld\n", __func__,
    //            blockHeight, newCoins, blockFees, blockReward ) ;
    return newCoins ;
} */

/* void static UpdateTipBlockNewCoins( const CChainParams & chainparams )
{
    CBlockIndex * chainTip = chainActive.Tip() ;
    CBlock tipBlock ;
    if ( ReadBlockFromDisk( tipBlock, chainTip, chainparams.GetConsensus( chainTip->nHeight ) ) )
    {
        chainTip->nBlockNewCoins = CountBlockNewCoins( tipBlock, chainTip->nHeight, chainparams ) ;
    }
} */

/** Update chainActive and related internal data structures */
void static UpdateTip( CBlockIndex * pindexNew, const CChainParams & chainParams )
{
    chainActive.SetTip( pindexNew ) ;

    // New best block
    mempool.AddTransactionsUpdated( 1 ) ;

    cvBlockChange.notify_all();

    static bool fWarned = false;
    std::vector<std::string> warningMessages;
    if (!IsInitialBlockDownload())
    {
        int nUpgraded = 0;
        const CBlockIndex* pindex = chainActive.Tip();
        for (int bit = 0; bit < VERSIONBITS_NUM_BITS; bit++) {
            WarningBitsConditionChecker checker(bit);
            ThresholdState state = checker.GetStateFor(pindex, chainParams.GetConsensus(pindex->nHeight), warningcache[bit]);
            if (state == THRESHOLD_ACTIVE || state == THRESHOLD_LOCKED_IN) {
                if (state == THRESHOLD_ACTIVE) {
                    std::string strWarning = strprintf(_("Warning: unknown new rules activated (versionbit %i)"), bit);
                    SetMiscWarning(strWarning);
                    if (!fWarned) {
                        AlertNotify(strWarning);
                        fWarned = true;
                    }
                } else {
                    warningMessages.push_back(strprintf("unknown new rules are about to activate (versionbit %i)", bit));
                }
            }
        }
        // Check the version of the last 100 blocks to see if we need to upgrade:
        for (int i = 0; i < 100 && pindex != NULL; i++)
        {
            int32_t nExpectedVersion = ComputeBlockVersion(pindex->pprev, chainParams.GetConsensus(pindex->nHeight));
            if (pindex->GetBaseVersion() > VERSIONBITS_LAST_OLD_BLOCK_VERSION && (pindex->GetBaseVersion() & ~nExpectedVersion) != 0)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            warningMessages.push_back(strprintf("%d of last 100 blocks have unexpected version", nUpgraded));
        if (nUpgraded > 100/2)
        {
            std::string strWarning = _( "Warning: Unknown block versions being mined! It's possible unknown rules are in effect" ) ;
            // notify GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            SetMiscWarning(strWarning);
            if (!fWarned) {
                AlertNotify(strWarning);
                fWarned = true;
            }
        }
    }

    ////UpdateTipBlockNewCoins( chainParams ) ;

    CBlockHeader newBlock = chainActive.Tip()->GetBlockHeader( chainParams.GetConsensus( chainActive.Height() ) ) ;
    double progress = GuessVerificationProgress( chainParams.TxData(), chainActive.Tip() ) ;
    LogPrintf( "%s: tip block height=%d sha256_hash=%s scrypt_hash=%s version=0x%x%s newcoins=%lu txs=+%u(%lu) date='%s',%s cache=%.1fMiB(%u txs)\n", __func__,
        chainActive.Height(),
        /* chainActive.Tip()->GetBlockSha256Hash().ToString() */ newBlock.GetSha256Hash().ToString(),
        newBlock.GetScryptHash().ToString(),
        newBlock.nVersion, newBlock.IsAuxpowInVersion() ? strprintf( " auxpow=%s", newBlock.auxpow->ToString() ) : "",
        chainActive.Tip()->nBlockNewCoins,
        chainActive.Tip()->nBlockTx, static_cast< unsigned long >( chainActive.Tip()->nChainTx ),
        DateTimeStrFormat( "%Y-%m-%d %H:%M:%S", /* chainActive.Tip()->GetBlockTime() */ newBlock.nTime ),
        ( progress > 0.99999 ) ? "" : strprintf( " progress=%.3f", progress * 100 ) + std::string( "%" ),
        pcoinsTip->DynamicMemoryUsage() * ( 1.0 / ( 1 << 20 ) ), pcoinsTip->GetCacheSize() ) ;

    if ( ! warningMessages.empty() )
        LogPrintf( "%s: warning='%s'\n", __func__, boost::algorithm::join( warningMessages, ", " ) ) ;
}

/** Disconnect chainActive's tip. You probably want to call mempool.removeForReorg and manually re-limit mempool size after this, with cs_main held */
bool static DisconnectTip( CValidationState & state, const CChainParams & chainparams, bool fBare = false )
{
    CBlockIndex * pindexDelete = chainActive.Tip() ;
    assert( pindexDelete != nullptr ) ;

    // Read block from disk
    CBlock block ;
    if ( ! ReadBlockFromDisk( block, pindexDelete, chainparams.GetConsensus( chainActive.Height() ) ) )
        return AbortNode( state, "Failed to read block" ) ;

    LogPrintf( "%s: disconnect block height=%d sha256_hash=%s scrypt_hash=%s version=0x%x%s date='%s'\n", __func__,
        pindexDelete->nHeight,
        block.GetSha256Hash().ToString(), block.GetScryptHash().ToString(),
        block.nVersion, block.IsAuxpowInVersion() ? "(auxpow)" : "",
        DateTimeStrFormat( "%Y-%m-%d %H:%M:%S", pindexDelete->GetBlockTime() ) ) ;

    // Apply the block atomically to the chain state
    int64_t benchTime = GetTimeMicros();
    {
        CCoinsViewCache view( pcoinsTip ) ;
        if ( ! DisconnectBlock( block, state, pindexDelete, view ) )
            return error( "%s: DisconnectBlock %s failed", __func__, pindexDelete->GetBlockSha256Hash().ToString() ) ;
        bool flushed = view.Flush() ;
        assert( flushed ) ;
    }
    LogPrint( "bench", "- Disconnect block: %.2fms\n", ( GetTimeMicros() - benchTime ) * 0.001 ) ;

    // Write the chain state to disk
    if ( ! FlushStateToDisk( state, FLUSH_STATE_IF_NEEDED ) )
        return false ;

    if ( ! fBare ) {
        // Resurrect mempool transactions from the disconnected block
        std::vector< uint256 > vHashUpdate ;
        for ( const auto & it : block.vtx ) {
            const CTransaction & tx = *it ;
            // ignore validation errors in resurrected transactions
            CValidationState stateDummy ;
            if ( tx.IsCoinBase() || ! AcceptToMemoryPool( mempool, stateDummy, it, false, NULL, NULL ) ) {
                mempool.removeRecursive( tx, MemPoolRemovalReason::REORG ) ;
            } else if ( mempool.exists( tx.GetTxHash() ) ) {
                vHashUpdate.push_back( tx.GetTxHash() ) ;
            }
        }
        // AcceptToMemoryPool/addUnchecked all assume that new mempool entries have
        // no in-mempool children, which is generally not true when adding
        // previously-confirmed transactions back to the mempool.
        // UpdateTransactionsFromBlock finds descendants of any transactions in this
        // block that were added back and cleans up the mempool state
        mempool.UpdateTransactionsFromBlock( vHashUpdate ) ;
    }

    // Update chainActive and related variables
    UpdateTip( pindexDelete->pprev, chainparams ) ;

    // Let wallets know transactions went from 1-confirmed to 0-confirmed or conflicted
    for ( const auto & tx : block.vtx ) {
        GetMainSignals().SyncTransaction( *tx, pindexDelete->pprev, CMainSignals::SYNC_TRANSACTION_NOT_IN_BLOCK ) ;
    }
    return true ;
}

static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

/**
 * Used to track blocks whose transactions were applied to the UTXO state as a
 * part of a single ActivateBestChainStep call
 */
struct ConnectTrace {
    std::vector<std::pair<CBlockIndex*, std::shared_ptr<const CBlock> > > blocksConnected;
};

/**
 * Connect a new block to chainActive. pblock is either NULL or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk
 *
 * The block is always added to connectTrace (either after loading from disk or by copying
 * pblock) - if that is not intended, care must be taken to remove the last entry in
 * blocksConnected in case of failure
 */
bool static ConnectTip( CValidationState & state, const CChainParams & chainparams, CBlockIndex * pindexNew, const std::shared_ptr< const CBlock > & pblock, ConnectTrace & connectTrace )
{
    assert( pindexNew->pprev == chainActive.Tip() ) ;

    // Read block from disk
    int64_t nTime1 = GetTimeMicros();
    if (!pblock) {
        std::shared_ptr<CBlock> pblockNew = std::make_shared<CBlock>();
        connectTrace.blocksConnected.emplace_back(pindexNew, pblockNew);
        if (!ReadBlockFromDisk(*pblockNew, pindexNew, chainparams.GetConsensus(pindexNew->nHeight)))
            return AbortNode(state, "Failed to read block");
    } else {
        connectTrace.blocksConnected.emplace_back(pindexNew, pblock);
    }
    const CBlock& blockConnecting = *connectTrace.blocksConnected.back().second;
    // Apply the block atomically to the chain state
    int64_t nTime2 = GetTimeMicros(); nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogPrint("bench", "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view( pcoinsTip ) ;
        bool rv = ConnectBlock(blockConnecting, state, pindexNew, view, chainparams);
        GetMainSignals().BlockChecked(blockConnecting, state);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(pindexNew, state);
            return error("ConnectTip(): ConnectBlock %s failed", pindexNew->GetBlockSha256Hash().ToString());
        }
        nTime3 = GetTimeMicros(); nTimeConnectTotal += nTime3 - nTime2;
        LogPrint("bench", "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        bool flushed = view.Flush();
        assert(flushed);
    }
    int64_t nTime4 = GetTimeMicros(); nTimeFlush += nTime4 - nTime3;
    LogPrint("bench", "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary
    if ( ! FlushStateToDisk( state, FLUSH_STATE_IF_NEEDED ) )
        return false ;
    int64_t nTime5 = GetTimeMicros(); nTimeChainState += nTime5 - nTime4;
    LogPrint("bench", "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);

    // Remove conflicting transactions from the mempool
    mempool.removeForBlock( blockConnecting.vtx, pindexNew->nHeight ) ;

    // Update chainActive & related variables
    UpdateTip( pindexNew, chainparams ) ;

    int64_t nTime6 = GetTimeMicros(); nTimePostConnect += nTime6 - nTime5; nTimeTotal += nTime6 - nTime1;
    LogPrint("bench", "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint("bench", "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    return true;
}

/**
 * Return the tip of the highest chain that isn't known to be invalid
 * (it's however far from certain to be valid)
 */
static CBlockIndex* FindHighestChain()
{
    do {
        CBlockIndex * pindexNew = nullptr ;

        // Find the best candidate header
        {
            std::set< CBlockIndex*, CBlockIndexComparator >::reverse_iterator it = setOfBlockIndexCandidates.rbegin() ;
            if ( it == setOfBlockIndexCandidates.rend() )
                return nullptr ;
            pindexNew = *it ;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already
        CBlockIndex *pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setOfBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK ;
            bool fMissingData = ! ( pindexTest->nStatus & BLOCK_DATA_EXISTS ) ;
            if ( fFailedChain || fMissingData ) {
                // Candidate chain is not usable (either invalid or missing data)
                if ( fFailedChain && ( pindexBestInvalid == nullptr || pindexNew->nHeight > pindexBestInvalid->nHeight ) )
                    pindexBestInvalid = pindexNew ;
                CBlockIndex * pindexFailed = pindexNew ;
                // Remove the entire chain from the set
                while (pindexTest != pindexFailed) {
                    if (fFailedChain) {
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (fMissingData) {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding
                        // to setOfBlockIndexCandidates again
                        mapBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setOfBlockIndexCandidates.erase( pindexFailed ) ;
                    pindexFailed = pindexFailed->pprev ;
                }
                setOfBlockIndexCandidates.erase( pindexTest ) ;
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while(true);
}

/** Delete all entries in setOfBlockIndexCandidates that are worse than the current tip */
static void PruneBlockIndexCandidates()
{
    // Note that we can't delete the current block itself, as we may need to return to it later
    // when reorganization to a better block fails
    std::set< CBlockIndex*, CBlockIndexComparator >::iterator it = setOfBlockIndexCandidates.begin() ;
    while ( it != setOfBlockIndexCandidates.end() && setOfBlockIndexCandidates.value_comp()( *it, chainActive.Tip() ) ) {
        setOfBlockIndexCandidates.erase( it ++ ) ;
    }
    // Either the current tip or a successor of it we're working towards is left in setOfBlockIndexCandidates
    assert( ! setOfBlockIndexCandidates.empty() ) ;
}

/**
 * Try to make some progress towards making pindexHighest the active block,
 * pblock is either NULL or a pointer to a highest block
 */
static bool ActivateBestChainStep( CValidationState & state, const CChainParams & chainparams, CBlockIndex * pindexHighest, const std::shared_ptr< const CBlock > & pblock, bool & fInvalidFound, ConnectTrace & connectTrace )
{
    AssertLockHeld( cs_main ) ;

    const CBlockIndex * pindexOldTip = chainActive.Tip() ;
    const CBlockIndex * pindexFork = chainActive.FindFork( pindexHighest ) ;

    // Disconnect active blocks which are no longer in the best chain
    bool fBlocksDisconnected = false ;
    while ( chainActive.Tip() && chainActive.Tip() != pindexFork ) {
        if ( ! DisconnectTip( state, chainparams ) )
            return false ;
        fBlocksDisconnected = true ;
    }

    // Build list of new blocks to connect
    std::vector< CBlockIndex * > vpindexToConnect ;
    bool fContinue = true ;
    int nHeight = pindexFork ? pindexFork->nHeight : -1 ;
    while ( fContinue && nHeight != pindexHighest->nHeight ) {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way
        int nTargetHeight = std::min( nHeight + 32, pindexHighest->nHeight ) ;
        vpindexToConnect.clear() ;
        vpindexToConnect.reserve( nTargetHeight - nHeight ) ;
        CBlockIndex * pindexIter = pindexHighest->GetAncestor( nTargetHeight ) ;
        while ( pindexIter && pindexIter->nHeight != nHeight ) {
            vpindexToConnect.push_back( pindexIter ) ;
            pindexIter = pindexIter->pprev ;
        }
        nHeight = nTargetHeight ;

        // Connect new blocks
        BOOST_REVERSE_FOREACH(CBlockIndex *pindexConnect, vpindexToConnect) {
            if ( ! ConnectTip( state, chainparams, pindexConnect, pindexConnect == pindexHighest ? pblock : std::shared_ptr< const CBlock >(), connectTrace ) ) {
                if ( state.IsInvalid() ) {
                    // The block violates a consensus rule
                    if ( ! state.CorruptionPossible() )
                        SayAboutRejectedChain( vpindexToConnect.back() ) ;
                    state = CValidationState() ;
                    fInvalidFound = true ;
                    fContinue = false ;
                    // If we didn't actually connect the block, don't notify listeners about it
                    connectTrace.blocksConnected.pop_back() ;
                    break ;
                } else {
                    // A system error occurred (disk space, database error, ...)
                    return false ;
                }
            } else {
                PruneBlockIndexCandidates() ;
                if ( pindexOldTip == nullptr || chainActive.Tip()->nHeight > pindexOldTip->nHeight ) {
                    // We're in a better position than we were. Return temporarily to release the lock
                    fContinue = false ;
                    break ;
                }
            }
        }
    }

    if (fBlocksDisconnected) {
        mempool.removeForReorg(pcoinsTip, chainActive.Height() + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
        LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
    }
    mempool.check(pcoinsTip);

    // Callbacks/notifications for a new best chain
    if (fInvalidFound)
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back());
    else
        CheckForkWarningConditions();

    return true;
}

static void NotifyHeaderTip() {
    bool fNotify = false;
    bool fInitialBlockDownload = false;
    static CBlockIndex* pindexHeaderOld = NULL;
    CBlockIndex* pindexHeader = NULL;
    {
        LOCK(cs_main);
        pindexHeader = pindexBestHeader;

        if (pindexHeader != pindexHeaderOld) {
            fNotify = true;
            fInitialBlockDownload = IsInitialBlockDownload();
            pindexHeaderOld = pindexHeader;
        }
    }
    // Send block tip changed notifications without cs_main
    if (fNotify) {
        uiInterface.NotifyHeaderTip(fInitialBlockDownload, pindexHeader);
    }
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either NULL or a pointer to a block
 * that is already loaded to avoid loading it again from disk
 */
bool ActivateBestChain( CValidationState & state, const CChainParams & chainparams, std::shared_ptr< const CBlock > pblock )
{
    // Note that while we're often called here from ProcessNewBlock, this is
    // far from a guarantee. Things in the P2P/RPC will often end up calling
    // us in the middle of ProcessNewBlock - do not assume pblock is set sanely

    CBlockIndex * pindexHighest = nullptr ;
    CBlockIndex * pindexNewTip = nullptr ;
    do {
        if ( ShutdownRequested() ) break ;

        const CBlockIndex * pindexFork ;
        ConnectTrace connectTrace;
        bool fInitialDownload;
        {
          LOCK( cs_main ) ;
          {   // TODO: Temporarily ensure that mempool removals are notified before
              // connected transactions.  This shouldn't matter, but the abandoned
              // state of transactions in our wallet is currently cleared when we
              // receive another notification and there is a race condition where
              // notification of a connected conflict might cause an outside process
              // to abandon a transaction and then have it inadvertantly cleared by
              // the notification that the conflicted transaction was evicted
            MemPoolConflictRemovalTracker mrt( mempool ) ;
            CBlockIndex * pindexOldTip = chainActive.Tip() ;
            if ( pindexHighest == nullptr ) {
                pindexHighest = FindHighestChain() ;
            }

            // Whether we have anything to do at all
            if ( pindexHighest == nullptr || pindexHighest == chainActive.Tip() )
                return true ;

            bool fInvalidFound = false ;
            const std::shared_ptr< const CBlock > nullBlockPtr ;
            const std::shared_ptr< const CBlock > & theBlock =
                    ( pblock != nullptr && pblock->GetSha256Hash() == pindexHighest->GetBlockSha256Hash() ? pblock : nullBlockPtr ) ;

            if ( ! ActivateBestChainStep( state, chainparams, pindexHighest, theBlock, fInvalidFound, connectTrace ) )
                return false ;

            if ( fInvalidFound ) {
                // reset it, may need another branch now
                pindexHighest = nullptr ;
            }

            pindexNewTip = chainActive.Tip() ;
            pindexFork = chainActive.FindFork( pindexOldTip ) ;
            fInitialDownload = IsInitialBlockDownload() ;

            // throw all transactions though the signal-interface

          } // MemPoolConflictRemovalTracker destroyed and conflict evictions are notified

            // Transactions in the connnected block are notified
            for (const auto& pair : connectTrace.blocksConnected) {
                assert(pair.second);
                const CBlock& block = *(pair.second);
                for (unsigned int i = 0; i < block.vtx.size(); i++)
                    GetMainSignals().SyncTransaction(*block.vtx[i], pair.first, i);
            }
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip)

        // Notifications/callbacks that can run without cs_main

        // Notify external listeners about the new tip
        GetMainSignals().UpdatedBlockTip(pindexNewTip, pindexFork, fInitialDownload);

        // Always notify the UI if a new block tip was connected
        if (pindexFork != pindexNewTip) {
            uiInterface.NotifyBlockTip(fInitialDownload, pindexNewTip);
        }
    } while ( pindexNewTip != pindexHighest ) ;

    CheckBlockIndex( chainparams.GetConsensus( pindexNewTip->nHeight ) ) ;

    // Write changes periodically to disk, after relay
    if ( ! FlushStateToDisk( state, FLUSH_STATE_PERIODIC ) ) {
        return false ;
    }

    return true ;
}


bool PreciousBlock( CValidationState & state, const CChainParams & params, CBlockIndex * pindex )
{
    {
        LOCK( cs_main ) ;
        if ( pindex->nHeight < chainActive.Tip()->nHeight ) {
            // Nothing to do, this block is not at the tip
            return true ;
        }
        if ( chainActive.Tip()->nHeight > nLastPreciousHeight ) {
            // The chain has been extended since the last call, reset the counter
            nBlockReverseSequenceId = -1 ;
        }
        nLastPreciousHeight = chainActive.Tip()->nHeight ;
        setOfBlockIndexCandidates.erase( pindex ) ;
        pindex->nSequenceId = nBlockReverseSequenceId ;
        if ( nBlockReverseSequenceId > std::numeric_limits< int32_t >::min() ) {
            // We can't keep reducing the counter if somebody really wants to
            // call preciousblock 2**31-1 times on the same set of tips...
            nBlockReverseSequenceId -- ;
        }
        if ( pindex->IsValid( BLOCK_VALID_TRANSACTIONS ) && pindex->nChainTx ) {
            setOfBlockIndexCandidates.insert( pindex ) ;
            PruneBlockIndexCandidates() ;
        }
    }

    return ActivateBestChain( state, params ) ;
}

bool InvalidateBlock( CValidationState & state, const CChainParams & chainparams, CBlockIndex * pindex )
{
    AssertLockHeld( cs_main ) ;

    // Mark the block itself as rejected
    pindex->nStatus |= BLOCK_FAILED_VALID ;
    setOfDirtyBlockIndices.insert( pindex ) ;
    setOfBlockIndexCandidates.erase( pindex ) ;

    while ( chainActive.Contains( pindex ) ) {
        CBlockIndex * pindexWalk = chainActive.Tip() ;
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD ;
        setOfDirtyBlockIndices.insert( pindexWalk ) ;
        setOfBlockIndexCandidates.erase( pindexWalk ) ;
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it
        if ( ! DisconnectTip( state, chainparams ) ) {
            mempool.removeForReorg( pcoinsTip, chainActive.Height() + 1, STANDARD_LOCKTIME_VERIFY_FLAGS ) ;
            return false ;
        }
    }

    LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

    // The resulting new best tip may not be in setOfBlockIndexCandidates anymore, so
    // add it again
    for ( BlockMap::iterator it = mapBlockIndex.begin() ; it != mapBlockIndex.end() ; ++ it ) {
        if ( it->second->IsValid( BLOCK_VALID_TRANSACTIONS ) && it->second->nChainTx > 0
                && ! setOfBlockIndexCandidates.value_comp()( it->second, chainActive.Tip() ) )
        {
            setOfBlockIndexCandidates.insert( it->second ) ;
        }
    }

    SayAboutRejectedChain( pindex ) ;
    mempool.removeForReorg(pcoinsTip, chainActive.Height() + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindex->pprev);
    return true ;
}

bool ResetBlockFailureFlags( CBlockIndex * pindex )
{
    if ( pindex == nullptr ) return false ;

    LogPrintf( "%s: reconsidering block sha256_hash=%s height=%d date=%s\n", __func__,
                pindex->GetBlockSha256Hash().ToString(), pindex->nHeight,
                    DateTimeStrFormat( "%Y-%m-%d %H:%M:%S", pindex->GetBlockTime() ) ) ;

    AssertLockHeld( cs_main ) ;

    int nHeight = pindex->nHeight ;

    // Remove the invalidity flag from this block and all its descendants
    for ( BlockMap::iterator it = mapBlockIndex.begin() ; it != mapBlockIndex.end() ; ++ it )
    {
        if ( ! it->second->IsValid() && it->second->GetAncestor( nHeight ) == pindex ) {
            it->second->nStatus &= ~BLOCK_FAILED_MASK ;
            setOfDirtyBlockIndices.insert( it->second ) ;
            if ( it->second->IsValid( BLOCK_VALID_TRANSACTIONS ) && it->second->nChainTx > 0
                    && setOfBlockIndexCandidates.value_comp()( chainActive.Tip(), it->second ) )
            {
                setOfBlockIndexCandidates.insert( it->second ) ;
            }
            if ( it->second == pindexBestInvalid ) {
                // Reset invalid block marker if it was pointing to one of those
                pindexBestInvalid = nullptr ;
            }
        }
    }

    // Remove the invalidity flag from all ancestors too
    while ( pindex != nullptr ) {
        if ( pindex->nStatus & BLOCK_FAILED_MASK ) {
            pindex->nStatus &= ~BLOCK_FAILED_MASK ;
            setOfDirtyBlockIndices.insert( pindex ) ;
        }
        pindex = pindex->pprev ;
    }

    return true ;
}

CBlockIndex* AddToBlockIndex( const CBlockHeader & block )
{
    // Check for duplicate
    uint256 hash = block.GetSha256Hash() ;
    BlockMap::iterator it = mapBlockIndex.find( hash ) ;
    if ( it != mapBlockIndex.end() )
        return it->second ;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex( block ) ;
    assert( pindexNew ) ;
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers to get
    // a competitive advantage
    pindexNew->nSequenceId = 0 ;
    BlockMap::iterator mi = mapBlockIndex.insert( std::make_pair( hash, pindexNew ) ).first ;
    pindexNew->SetBlockSha256Hash( mi->first ) ;
    BlockMap::iterator miPrev = mapBlockIndex.find( block.hashPrevBlock ) ;
    if ( miPrev != mapBlockIndex.end() )
    {
        pindexNew->pprev = ( *miPrev ).second ;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1 ;
        pindexNew->BuildSkip() ;
    }
    pindexNew->nTimeMax = ( pindexNew->pprev ? std::max( pindexNew->pprev->nTimeMax, pindexNew->nTime ) : pindexNew->nTime ) ;
    pindexNew->RaiseValidity( BLOCK_VALID_TREE ) ;
    if ( pindexBestHeader == nullptr || pindexBestHeader->nHeight < pindexNew->nHeight )
        pindexBestHeader = pindexNew ;

    setOfDirtyBlockIndices.insert( pindexNew ) ;

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS) */
bool ReceivedBlockTransactions( const CBlock & block, CValidationState & state, CBlockIndex * pindexNew, const CDiskBlockPos & pos )
{
    pindexNew->nBlockTx = block.vtx.size() ;
    pindexNew->nChainTx = 0 ;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_DATA_EXISTS ;
    if (IsWitnessEnabled(pindexNew->pprev, Params().GetConsensus(pindexNew->nHeight))) {
        pindexNew->nStatus |= BLOCK_OPT_WITNESS;
    }
    pindexNew->RaiseValidity( BLOCK_VALID_TRANSACTIONS ) ;
    setOfDirtyBlockIndices.insert( pindexNew ) ;

    if (pindexNew->pprev == NULL || pindexNew->pprev->nChainTx) {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS
        std::deque<CBlockIndex*> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected
        while (!queue.empty()) {
            CBlockIndex * pindex = queue.front() ;
            queue.pop_front() ;
            pindex->nChainTx = ( pindex->pprev ? pindex->pprev->nChainTx : 0 ) + pindex->nBlockTx ;
            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if ( chainActive.Tip() == nullptr || ! setOfBlockIndexCandidates.value_comp()( pindex, chainActive.Tip() ) ) {
                setOfBlockIndexCandidates.insert( pindex ) ;
            }
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                std::multimap<CBlockIndex*, CBlockIndex*>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE)) {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}

bool FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false)
{
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown) {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE) {
            nFile++;
            if (vinfoBlockFile.size() <= nFile) {
                vinfoBlockFile.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    if ((int)nFile != nLastBlockFile) {
        if (!fKnown) {
            LogPrintf("Leaving block file %i: %s\n", nLastBlockFile, vinfoBlockFile[nLastBlockFile].ToString());
        }
        FlushBlockFile(!fKnown);
        nLastBlockFile = nFile;
    }

    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile[nFile].nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    else
        vinfoBlockFile[nFile].nSize += nAddSize;

    if (!fKnown) {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (fPruneMode)
                fCheckForPruning = true;
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
                FILE *file = OpenBlockFile(pos);
                if (file) {
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            }
            else
                return state.Error("out of disk space");
        }
    }

    setOfDirtyBlockFiles.insert( nFile ) ;
    return true ;
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize)
{
    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    pos.nPos = vinfoBlockFile[ nFile ].nUndoSize ;
    unsigned int nNewSize = vinfoBlockFile[ nFile ].nUndoSize += nAddSize ;
    setOfDirtyBlockFiles.insert( nFile ) ;

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (fPruneMode)
            fCheckForPruning = true;
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE *file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
            return state.Error("out of disk space");
    }

    return true;
}

bool CheckBlockHeader( const CBlockHeader & block, CValidationState & state, bool fCheckPOW )
{
    // Check proof of work matches claimed amount
    // We don't have block height as this is called without knowing the previous block, but that's okay
    if ( fCheckPOW && ! CheckDogecoinProofOfWork( block, Params().GetConsensus(0) ) )
        return state.DoS( 10, false, REJECT_INVALID, "high-hash", false, "proof of work failed" ) ;

    return true ;
}

bool CheckBlock(const CBlock& block, CValidationState& state, bool fCheckPOW, bool fCheckMerkleRoot)
{
    // These are checks that are independent of context.

    if (block.fChecked)
        return true;

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, fCheckPOW))
        return false;

    // Check the merkle root.
    if (fCheckMerkleRoot) {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS( 20, false, REJECT_INVALID, "bad-txnmrklroot", true, "hashMerkleRoot mismatch" ) ;

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS( 50, false, REJECT_INVALID, "bad-txns-duplicate", true, "duplicate transaction" ) ;
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.
    // Note that witness malleability is checked in ContextualCheckBlock, so no
    // checks that use witness data may be performed here.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() > MAX_BLOCK_BASE_SIZE || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) > MAX_BLOCK_BASE_SIZE)
        return state.DoS( 10, false, REJECT_INVALID, "bad-blk-length", false, "size limits failed" ) ;

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase())
        return state.DoS( 20, false, REJECT_INVALID, "bad-cb-missing", false, "first tx is not coinbase" ) ;
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i]->IsCoinBase())
            return state.DoS( 20, false, REJECT_INVALID, "bad-cb-multiple", false, "more than one coinbase" ) ;

    // Check transactions
    for ( const auto & tx : block.vtx )
        if ( ! CheckTransaction( *tx, state, true ) )
            return state.Invalid(
                false, state.GetRejectCode(), state.GetRejectReason(),
                strprintf( "Transaction check failed (tx hash %s) %s", tx->GetTxHash().ToString(), state.GetDebugMessage() )
            ) ;

    unsigned int nSigOps = 0;
    for (const auto& tx : block.vtx)
    {
        nSigOps += GetLegacySigOpCount(*tx);
    }
    if (nSigOps * WITNESS_SCALE_FACTOR > MAX_BLOCK_SIGOPS_COST)
        return state.DoS( 10, false, REJECT_INVALID, "bad-blk-sigops", false, "out-of-bounds SigOpCount" ) ;

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    return true;
}

static bool CheckIndexAgainstCheckpoint( const CBlockIndex* pindexPrev, CValidationState& state, const CChainParams& chainparams, const uint256& hash )
{
    if ( pindexPrev->GetBlockSha256Hash() == chainparams.GetConsensus(0).hashGenesisBlock )
        return true ;

    int nHeight = pindexPrev->nHeight + 1 ;
    // Don't accept any forks from the main chain prior to last checkpoint
    CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint( chainparams.getCheckpoints() ) ;
    if ( pcheckpoint && nHeight < pcheckpoint->nHeight )
        return state.DoS( 20, error("%s: forked chain older than last checkpoint (height %d)", __func__, nHeight) ) ;

    return true ;
}

bool IsWitnessEnabled(const CBlockIndex* pindexPrev, const Consensus::Params& params)
{
    // Dogecoin: Disable SegWit
    return false;
    // LOCK(cs_main);
    // return (VersionBitsState(pindexPrev, params, Consensus::DEPLOYMENT_SEGWIT, versionbitscache) == THRESHOLD_ACTIVE);
}

// Compute at which vout of the block's coinbase transaction the witness
// commitment occurs, or -1 if not found.
static int GetWitnessCommitmentIndex(const CBlock& block)
{
    int commitpos = -1;
    if (!block.vtx.empty()) {
        for (size_t o = 0; o < block.vtx[0]->vout.size(); o++) {
            if (block.vtx[0]->vout[o].scriptPubKey.size() >= 38 && block.vtx[0]->vout[o].scriptPubKey[0] == OP_RETURN && block.vtx[0]->vout[o].scriptPubKey[1] == 0x24 && block.vtx[0]->vout[o].scriptPubKey[2] == 0xaa && block.vtx[0]->vout[o].scriptPubKey[3] == 0x21 && block.vtx[0]->vout[o].scriptPubKey[4] == 0xa9 && block.vtx[0]->vout[o].scriptPubKey[5] == 0xed) {
                commitpos = o;
            }
        }
    }
    return commitpos;
}

void UpdateUncommittedBlockStructures(CBlock& block, const CBlockIndex* pindexPrev, const Consensus::Params& consensusParams)
{
    int commitpos = GetWitnessCommitmentIndex(block);
    static const std::vector<unsigned char> nonce(32, 0x00);
    if (commitpos != -1 && IsWitnessEnabled(pindexPrev, consensusParams) && !block.vtx[0]->HasWitness()) {
        CMutableTransaction tx(*block.vtx[0]);
        tx.vin[0].scriptWitness.stack.resize(1);
        tx.vin[0].scriptWitness.stack[0] = nonce;
        block.vtx[0] = MakeTransactionRef(std::move(tx));
    }
}

std::vector<unsigned char> GenerateCoinbaseCommitment(CBlock& block, const CBlockIndex* pindexPrev, const Consensus::Params& consensusParams)
{
    std::vector<unsigned char> commitment;
    int commitpos = GetWitnessCommitmentIndex(block);
    std::vector<unsigned char> ret(32, 0x00);
    if (consensusParams.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout != 0) {
        if (commitpos == -1) {
            uint256 witnessroot = BlockWitnessMerkleRoot(block, NULL);
            CHash256().Write(witnessroot.begin(), 32).Write(&ret[0], 32).Finalize(witnessroot.begin());
            CTxOut out;
            out.nValue = 0;
            out.scriptPubKey.resize(38);
            out.scriptPubKey[0] = OP_RETURN;
            out.scriptPubKey[1] = 0x24;
            out.scriptPubKey[2] = 0xaa;
            out.scriptPubKey[3] = 0x21;
            out.scriptPubKey[4] = 0xa9;
            out.scriptPubKey[5] = 0xed;
            memcpy(&out.scriptPubKey[6], witnessroot.begin(), 32);
            commitment = std::vector<unsigned char>(out.scriptPubKey.begin(), out.scriptPubKey.end());
            CMutableTransaction tx(*block.vtx[0]);
            tx.vout.push_back(out);
            block.vtx[0] = MakeTransactionRef(std::move(tx));
        }
    }
    UpdateUncommittedBlockStructures(block, pindexPrev, consensusParams);
    return commitment;
}

bool ContextualCheckBlockHeader( const CBlockHeader & block, CValidationState & state, const CBlockIndex * pindexPrev, int64_t nAdjustedTime )
{
    const int nHeight = pindexPrev == NULL ? 0 : pindexPrev->nHeight + 1;
    const Consensus::Params& consensusParams = Params().GetConsensus( nHeight ) ;

    if ( block.IsLegacy() && ! consensusParams.fAllowLegacyBlocks )
        return state.DoS( 20, error( "%s : legacy block when it is too late", __func__ ),
                          REJECT_INVALID, "late-legacy-block" ) ;

    // Dogecoin: Disallow AuxPow blocks before it is activated
    // TODO: Remove this test, as checkpoints will enforce this for us now
    // NOTE: Previously this had its own fAllowAuxPoW flag, but that's always the opposite of fAllowLegacyBlocks
    if ( consensusParams.fAllowLegacyBlocks && block.IsAuxpowInVersion() )
        return state.DoS( 20, error("%s : auxpow blocks are not allowed at height %d, parameters effective from %d",
                                    __func__, pindexPrev->nHeight + 1, consensusParams.nHeightEffective),
                          REJECT_INVALID, "early-auxpow-block" ) ;

    // Check proof of work
    // Smaller values of bits ("higher difficulty") aren't accepted as well as bigger ones
    uint32_t bitsRequired = GetNextWorkRequired( pindexPrev, &block, consensusParams, /* talkative */ fDebug ) ;
    if ( block.nBits != bitsRequired ) {
        LogPrintf( "%s: inexact proof-of-work bits: 0x%08x != 0x%08x for block sha256_hash=%s scrypt_hash=%s\n", __func__,
                   block.nBits, bitsRequired, block.GetSha256Hash().ToString(), block.GetScryptHash().ToString() ) ;
        if ( block.nBits >> 4 != bitsRequired >> 4 )
            return state.DoS( 12, false, REJECT_INVALID, "bad-diffbits", false,
                        strprintf( "proof-of-work bits are too inexact: 0x%07x0 != 0x%07x0", block.nBits >> 4, bitsRequired >> 4 ) ) ;
    }

    // Check timestamp
    uint64_t timeLimitInPast = ( ! Params().UseMedianTimePast() ) ? pindexPrev->nTime : pindexPrev->GetMedianTimePast() ;
    uint64_t timeLimitInFuture = nAdjustedTime + ( NameOfChain() == "inu" ? 0 : 2 * 60 * 60 ) ;

    if ( block.GetBlockTime() <= timeLimitInPast )
        return state.Invalid( false, REJECT_INVALID, "time-too-old", "block's time is too early in the past" ) ;

    if ( block.GetBlockTime() > timeLimitInFuture )
        return state.Invalid( false, REJECT_INVALID, "time-too-new", "block's time is too far in the future" ) ;

    // Reject outdated version blocks when 95% (75% on testnet) of the network has upgraded:
    // check for version 2, 3 and 4 upgrades
    // Dogecoin: Version 2 enforcement was never used
    if ( ( block.GetBaseVersion() < 3 && nHeight >= consensusParams.BIP66Height ) )
        return state.Invalid(
            false, REJECT_OBSOLETE, strprintf( "obsolete-version(0x%x)", block.nVersion ),
            strprintf( "rejected version=0x%x block as obsolete", block.nVersion )
        ) ;

    // Dogecoin: Introduce supermajority rules for v4 blocks
    if ( block.GetBaseVersion() < 4 && IsSuperMajority( 4, pindexPrev, consensusParams.nMajorityRejectBlockOutdated, consensusParams ) )
        return state.Invalid(
            false, REJECT_OBSOLETE, strprintf( "obsolete-version(0x%x)", block.nVersion ),
            strprintf( "rejected v3 block (version=0x%x) due to supermajority of v4 blocks", block.nVersion )
        ) ;

    return true ;
}

bool ContextualCheckBlock( const CBlock & block, CValidationState & state, const CBlockIndex * pindexPrev )
{
    const int nHeight = ( pindexPrev == nullptr ) ? 0 : pindexPrev->nHeight + 1 ;
    const CChainParams& chainParams = Params();
    const Consensus::Params& consensusParams = chainParams.GetConsensus(nHeight);

    // Start enforcing BIP113 (Median Time Past) using versionbits logic
    // Dogecoin: We probably want to disable this
    int nLockTimeFlags = 0 ;
    if ( Params().UseMedianTimePast() ) {
        if ( VersionBitsState( pindexPrev, consensusParams, Consensus::DEPLOYMENT_CSV, versionbitscache ) == THRESHOLD_ACTIVE )
            nLockTimeFlags |= LOCKTIME_MEDIAN_TIME_PAST ;
    }

    int64_t nLockTimeCutoff = ( nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST )
                              ? pindexPrev->GetMedianTimePast()
                              : block.GetBlockTime() ;

    // Check that all transactions are finalized
    for ( const auto & tx : block.vtx ) {
        if ( ! IsFinalTx( *tx, nHeight, nLockTimeCutoff ) ) {
            return state.Invalid( false, REJECT_INVALID, "bad-txns-nonfinal", "non-final transaction" ) ;
        }
    }

    // Enforce rule that the coinbase starts with serialized block height
    if ( nHeight >= consensusParams.BIP34Height )
    {
        CScript expect = CScript() << nHeight ;
        if ( block.vtx[0]->vin[0].scriptSig.size() < expect.size() ||
            ! std::equal( expect.begin(), expect.end(), block.vtx[0]->vin[0].scriptSig.begin() ) ) {
            return state.DoS( 50, false, REJECT_INVALID, "bad-cb-height", false, "block height mismatch in coinbase" ) ;
        }
    }

    // Inu chain: minimum time delay between blocks
    // and the big minimum delay if the block has only single coinbase transaction
    if ( NameOfChain() == "inu" )
    {
        uint32_t prevtime = ( pindexPrev != nullptr ) ? pindexPrev->nTime : Params().GenesisBlock().nTime ;
        if ( block.vtx.size() == 1 ) {
            if ( block.nTime - prevtime < /* one hour */ 60 * 60 )
                return state.Invalid( false, REJECT_INVALID, "coinbase-only-block-delay", "too early for the coinbase-only block" ) ;
        } else {
            if ( block.nTime - prevtime < 20 )
                return state.Invalid( false, REJECT_INVALID, "block-delay", "too early for the next block" ) ;
        }
    }

    // Validation for witness commitments
    // * We compute the witness hash (which is the hash including witnesses) of all the block's transactions, except the
    //   coinbase (where 0x0000....0000 is used instead)
    // * The coinbase scriptWitness is a stack of a single 32-byte vector, containing a witness nonce (unconstrained)
    // * We build a merkle tree with all those witness hashes as leaves (similar to the hashMerkleRoot in the block header)
    // * There must be at least one output whose scriptPubKey is a single 36-byte push, the first 4 bytes of which are
    //   from chain parameters, and the following 32 bytes are SHA256^2(witness root, witness nonce). In case there are
    //   multiple, the last one is used
    bool fHaveWitness = false;
    if (VersionBitsState(pindexPrev, consensusParams, Consensus::DEPLOYMENT_SEGWIT, versionbitscache) == THRESHOLD_ACTIVE) {
        int commitpos = GetWitnessCommitmentIndex(block);
        if (commitpos != -1) {
            bool malleated = false;
            uint256 hashWitness = BlockWitnessMerkleRoot(block, &malleated);
            // The malleation check is ignored; as the transaction tree itself
            // already does not permit it, it is impossible to trigger in the
            // witness tree
            if (block.vtx[0]->vin[0].scriptWitness.stack.size() != 1 || block.vtx[0]->vin[0].scriptWitness.stack[0].size() != 32) {
                return state.DoS( 10, false, REJECT_INVALID, "bad-witness-nonce-size", true, strprintf("%s : invalid witness nonce size", __func__) ) ;
            }
            CHash256().Write(hashWitness.begin(), 32).Write(&block.vtx[0]->vin[0].scriptWitness.stack[0][0], 32).Finalize(hashWitness.begin());
            if (memcmp(hashWitness.begin(), &block.vtx[0]->vout[commitpos].scriptPubKey[6], 32)) {
                return state.DoS( 10, false, REJECT_INVALID, "bad-witness-merkle-match", true, strprintf("%s : witness merkle commitment mismatch", __func__) ) ;
            }
            fHaveWitness = true;
        }
    }

    // No witness data is allowed in blocks that don't commit to witness data, as this would otherwise leave room for spam
    if (!fHaveWitness) {
        for (size_t i = 0; i < block.vtx.size(); i++) {
            if (block.vtx[i]->HasWitness()) {
                return state.DoS( 10, false, REJECT_INVALID, "unexpected-witness", true, strprintf("%s : unexpected witness data found", __func__) ) ;
            }
        }
    }

    // After the coinbase witness nonce and commitment are verified,
    // we can check if the block weight passes (before we've checked the
    // coinbase witness, it would be possible for the weight to be too
    // large by filling up the coinbase witness, which doesn't change
    // the block hash, so we couldn't mark the block as permanently
    // failed).
    if (GetBlockWeight(block) > MAX_BLOCK_WEIGHT) {
        return state.DoS( 20, false, REJECT_INVALID, "bad-blk-weight", false, strprintf("%s : weight limit failed", __func__) ) ;
    }

    return true;
}

static bool AcceptBlockHeader( const CBlockHeader& block, CValidationState& state, const CChainParams& chainparams, CBlockIndex** ppindex )
{
    AssertLockHeld(cs_main);
    // Check for duplicate
    uint256 hash = block.GetSha256Hash() ;
    BlockMap::iterator miSelf = mapBlockIndex.find( hash ) ;
    CBlockIndex *pindex = NULL ;
    if ( hash != chainparams.GetConsensus( 0 ).hashGenesisBlock )
    {
        if (miSelf != mapBlockIndex.end()) {
            // Block header is already known
            pindex = miSelf->second;
            if (ppindex)
                *ppindex = pindex;
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return state.Invalid(error("%s: block %s is marked invalid", __func__, hash.ToString()), 0, "duplicate");
            return true;
        }

        if (!CheckBlockHeader(block, state))
            return error("%s: Consensus::CheckBlockHeader: %s, %s", __func__, hash.ToString(), FormatStateMessage(state));

        // Get previous block index
        CBlockIndex* pindexPrev = nullptr ;
        BlockMap::iterator mi = mapBlockIndex.find( block.hashPrevBlock ) ;
        if ( mi == mapBlockIndex.end() )
            return state.DoS( 2, error("%s: previous block not found", __func__), 0, "bad-prevblk" ) ;
        pindexPrev = ( *mi ).second ;
        if ( pindexPrev->nStatus & BLOCK_FAILED_MASK )
            return state.DoS( 10, error("%s: previous block marked as rejected", __func__), REJECT_INVALID, "bad-prevblk" ) ;

        assert( pindexPrev != nullptr ) ;
        if ( ! CheckIndexAgainstCheckpoint( pindexPrev, state, chainparams, hash ) )
            return error( "%s: CheckIndexAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str() ) ;

        if ( ! ContextualCheckBlockHeader( block, state, pindexPrev, GetAdjustedTime() ) )
            return error( "%s: Consensus::ContextualCheckBlockHeader: %s, %s", __func__, hash.ToString(), FormatStateMessage( state ) ) ;
    }
    if (pindex == NULL)
        pindex = AddToBlockIndex(block);

    if (ppindex)
        *ppindex = pindex;

    CheckBlockIndex(chainparams.GetConsensus(pindex->nHeight));

    return true;
}

// Exposed wrapper for AcceptBlockHeader
bool ProcessNewBlockHeaders(const std::vector<CBlockHeader>& headers, CValidationState& state, const CChainParams& chainparams, const CBlockIndex** ppindex)
{
    {
        LOCK(cs_main);
        for (const CBlockHeader& header : headers) {
            CBlockIndex *pindex = NULL; // Use a temp pindex instead of ppindex to avoid a const_cast
            if (!AcceptBlockHeader(header, state, chainparams, &pindex)) {
                return false;
            }
            if (ppindex) {
                *ppindex = pindex;
            }
        }
    }
    NotifyHeaderTip();
    return true;
}

/** Store block on disk. If dbp is non-NULL, the file is known to already reside on disk */
static bool AcceptBlock( const std::shared_ptr< const CBlock > & pblock, CValidationState & state, const CChainParams & chainparams, CBlockIndex** ppindex, bool fRequested, const CDiskBlockPos* dbp, bool* fNewBlock )
{
    const CBlock& block = *pblock;

    if (fNewBlock) *fNewBlock = false;
    AssertLockHeld(cs_main);

    CBlockIndex *pindexDummy = NULL;
    CBlockIndex *&pindex = ppindex ? *ppindex : pindexDummy;

    if (!AcceptBlockHeader(block, state, chainparams, &pindex))
        return false;

    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new, is higher than the tip
    // and isn't too many blocks ahead
    bool fAlreadyHave = pindex->nStatus & BLOCK_DATA_EXISTS ;
    bool isHigher = ( chainActive.Tip() ? pindex->nHeight > chainActive.Tip()->nHeight : true ) ;

    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks
    bool fTooFarAhead = ( pindex->nHeight > int(chainActive.Height() + MIN_BLOCKS_TO_KEEP) ) ;

    // TODO: Decouple this function from the block download logic by removing fRequested
    // This requires some new chain datastructure to efficiently look up if a
    // block is in a chain leading to a candidate for best tip, despite not
    // being such a candidate itself

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks
    if ( fAlreadyHave ) return true ;
    if ( ! fRequested ) {  // If we didn't ask for it:
        if ( pindex->nBlockTx != 0 ) return true ;  // this is a previously-processed block that was pruned
        if ( ! isHigher ) return true ;  // don't process shorter chains
        if ( fTooFarAhead ) return true ;  // block is too high
    }

    if ( fNewBlock ) *fNewBlock = true ;

    if ( ! CheckBlock( block, state ) ||
            ! ContextualCheckBlock( block, state, pindex->pprev ) ) {
        if ( state.IsInvalid() && ! state.CorruptionPossible() ) {
            pindex->nStatus |= BLOCK_FAILED_VALID ;
            setOfDirtyBlockIndices.insert( pindex ) ;
        }
        return error("%s: %s", __func__, FormatStateMessage(state));
    }

    // Header is valid/has work, merkle tree and segwit merkle tree are good...RELAY NOW
    // (but if it does not build on our best tip, let the SendMessages loop relay it)
    if (!IsInitialBlockDownload() && chainActive.Tip() == pindex->pprev)
        GetMainSignals().NewPoWValidBlock(pindex, pblock);

    int nHeight = pindex->nHeight;

    // Write block to history file
    try {
        unsigned int nBlockSize = ::GetSerializeSize( block, SER_DISK, PEER_VERSION ) ;
        CDiskBlockPos blockPos;
        if (dbp != NULL)
            blockPos = *dbp;
        if (!FindBlockPos(state, blockPos, nBlockSize+8, nHeight, block.GetBlockTime(), dbp != NULL))
            return error("AcceptBlock(): FindBlockPos failed");
        if (dbp == NULL)
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                AbortNode(state, "Failed to write block");
        if ( ! ReceivedBlockTransactions( block, state, pindex, blockPos ) )
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error: ") + e.what());
    }

    if (fCheckForPruning)
        FlushStateToDisk( state, FLUSH_STATE_NONE ) ; // just allocate more disk space for block files

    return true;
}

bool ProcessNewBlock( const CChainParams & chainparams, const std::shared_ptr< const CBlock > pblock, bool fForceProcessing, bool * fNewBlock )
{
    assert( pblock != nullptr ) ;
    LogPrintf( "%s: block sha256_hash=%s scrypt_hash=%s version=0x%x%s date='%s'\n", __func__,
                pblock->GetSha256Hash().GetHex(), pblock->GetScryptHash().GetHex(),
                pblock->nVersion, pblock->IsAuxpowInVersion() ? "(auxpow)" : "",
                DateTimeStrFormat( "%Y-%m-%d %H:%M:%S", pblock->nTime ) ) ;
    {
        if ( fNewBlock != nullptr ) *fNewBlock = false ;
        CValidationState state ;
        // Ensure that CheckBlock() passes before calling AcceptBlock, as belt-and-suspenders
        bool ret = CheckBlock( *pblock, state ) ;

        LOCK( cs_main ) ;

        if ( ret ) {
            CBlockIndex * pindex = nullptr ;

            // Store to disk
            ret = AcceptBlock( pblock, state, chainparams, &pindex, fForceProcessing, nullptr, fNewBlock ) ;
        }
        CheckBlockIndex( chainparams.GetConsensus( chainActive.Height() ) ) ;
        if ( ! ret ) {
            GetMainSignals().BlockChecked( *pblock, state ) ;
            return error( "%s: AcceptBlock FAILED", __func__ ) ;
        }
    }

    NotifyHeaderTip() ;

    CValidationState state ; // only used to report errors, not invalidity, ignore it
    if ( ! ActivateBestChain( state, chainparams, pblock ) )
        return error( "%s: ActivateBestChain failed", __func__ ) ;

    return true ;
}

bool TestBlockValidity( CValidationState & state, const CChainParams & chainparams, const CBlock & block, CBlockIndex * pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot )
{
    AssertLockHeld(cs_main);
    assert( pindexPrev && pindexPrev == chainActive.Tip() ) ;
    if ( ! CheckIndexAgainstCheckpoint( pindexPrev, state, chainparams, block.GetSha256Hash() ) )
        return error( "%s: CheckIndexAgainstCheckpoint(): %s", __func__, state.GetRejectReason().c_str() ) ;

    CCoinsViewCache viewNew( pcoinsTip ) ;
    CBlockIndex indexDummy( block ) ;
    indexDummy.pprev = pindexPrev ;
    indexDummy.nHeight = pindexPrev->nHeight + 1 ;

    if ( ! ContextualCheckBlockHeader( block, state, pindexPrev, GetAdjustedTime() ) ) {
        LogPrintf( "%s: Consensus::ContextualCheckBlockHeader: %s\n", __func__, FormatStateMessage( state ) ) ;
        return false ;
    }
    // CheckBlockHeader is invoked by CheckBlock
    if ( ! CheckBlock( block, state, fCheckPOW, fCheckMerkleRoot ) ) {
        LogPrintf( "%s: Consensus::CheckBlock: %s", __func__, FormatStateMessage( state ) ) ;
        return false ;
    }
    if ( ! ContextualCheckBlock( block, state, pindexPrev ) ) {
        if ( state.GetRejectReason() != "coinbase-only-block-delay" && state.GetRejectReason() != "block-delay" )
            LogPrintf( "%s: Consensus::ContextualCheckBlock: %s\n", __func__, FormatStateMessage( state ) ) ;
        return false ;
    }
    if ( ! ConnectBlock( block, state, &indexDummy, viewNew, chainparams, true ) )
        return false ;

    assert( state.IsValid() ) ;
    return true ;
}

/**
 * BLOCK PRUNING CODE
 */

/* Calculate the amount of disk space the block & undo files currently use */
uint64_t CalculateCurrentUsage()
{
    uint64_t retval = 0;
    for ( const CBlockFileInfo & file : vinfoBlockFile ) {
        retval += file.nSize + file.nUndoSize ;
    }
    return retval ;
}

/* Prune a block file (modify associated database entries)*/
void PruneOneBlockFile(const int fileNumber)
{
    for ( BlockMap::iterator it = mapBlockIndex.begin() ; it != mapBlockIndex.end() ; ++ it ) {
        CBlockIndex* pindex = it->second;
        if ( pindex->nFile == fileNumber ) {
            pindex->nStatus &= ~BLOCK_DATA_EXISTS ;
            pindex->nStatus &= ~BLOCK_UNDO_EXISTS ;
            pindex->nFile = 0 ;
            pindex->nDataPos = 0 ;
            pindex->nUndoPos = 0 ;
            setOfDirtyBlockIndices.insert( pindex ) ;

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setOfBlockIndexCandidates
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex->pprev);
            while (range.first != range.second) {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator _it = range.first;
                range.first++;
                if (_it->second == pindex) {
                    mapBlocksUnlinked.erase(_it);
                }
            }
        }
    }

    vinfoBlockFile[ fileNumber ].SetNull() ;
    setOfDirtyBlockFiles.insert( fileNumber ) ;
}


void UnlinkPrunedFiles(const std::set<int>& setFilesToPrune)
{
    for (std::set<int>::iterator it = setFilesToPrune.begin(); it != setFilesToPrune.end(); ++it) {
        CDiskBlockPos pos(*it, 0);
        boost::filesystem::remove(GetBlockPosFilename(pos, "blk"));
        boost::filesystem::remove(GetBlockPosFilename(pos, "rev"));
        LogPrintf("Prune: %s deleted blk/rev (%05u)\n", __func__, *it);
    }
}

/* Calculate the block/rev files to delete based on height specified by user with RPC command pruneblockchain */
void FindFilesToPruneManual(std::set<int>& setFilesToPrune, int nManualPruneHeight)
{
    assert(fPruneMode && nManualPruneHeight > 0);

    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == NULL)
        return;

    // last block to prune is the lesser of (user-specified height, MIN_BLOCKS_TO_KEEP from the tip)
    unsigned int nLastBlockWeCanPrune = std::min((unsigned)nManualPruneHeight, chainActive.Height() - MIN_BLOCKS_TO_KEEP);
    int count=0;
    for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
        if (vinfoBlockFile[fileNumber].nSize == 0 || vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
            continue;
        PruneOneBlockFile(fileNumber);
        setFilesToPrune.insert(fileNumber);
        count++;
    }
    LogPrintf("Prune (Manual): prune_height=%d removed %d blk/rev pairs\n", nLastBlockWeCanPrune, count);
}

/* This function is called from the RPC code for pruneblockchain */
void PruneBlockFilesManual(int nManualPruneHeight)
{
    CValidationState state;
    FlushStateToDisk( state, FLUSH_STATE_NONE, nManualPruneHeight ) ;
}

/* Calculate the block/rev files that should be deleted to remain under target*/
void FindFilesToPrune(std::set<int>& setFilesToPrune, uint64_t nPruneAfterHeight)
{
    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == NULL || nPruneTarget == 0) {
        return;
    }
    if ((uint64_t)chainActive.Height() <= nPruneAfterHeight) {
        return;
    }

    unsigned int nLastBlockWeCanPrune = chainActive.Height() - MIN_BLOCKS_TO_KEEP;
    uint64_t nCurrentUsage = CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files
    // So we should leave a buffer under our target to account for another allocation
    // before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count=0;

    if (nCurrentUsage + nBuffer >= nPruneTarget) {
        for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
            nBytesToPrune = vinfoBlockFile[fileNumber].nSize + vinfoBlockFile[fileNumber].nUndoSize;

            if (vinfoBlockFile[fileNumber].nSize == 0)
                continue;

            if (nCurrentUsage + nBuffer < nPruneTarget)  // are we below our target?
                break;

            // don't prune files that could have a block within MIN_BLOCKS_TO_KEEP of the main chain's tip but keep scanning
            if (vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
                continue;

            PruneOneBlockFile(fileNumber);
            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LogPrint("prune", "Prune: target=%dMiB actual=%dMiB diff=%dMiB max_prune_height=%d removed %d blk/rev pairs\n",
           nPruneTarget/1024/1024, nCurrentUsage/1024/1024,
           ((int64_t)nPruneTarget - (int64_t)nCurrentUsage)/1024/1024,
           nLastBlockWeCanPrune, count);
}

bool CheckDiskSpace( uint64_t nAdditionalBytes )
{
    uint64_t nFreeBytesAvailable = boost::filesystem::space( GetDirForData() ).available ;

    if ( nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes )
        return AbortNode( "Disk space is low!", _("Error: Disk space is low!") ) ;

    return true;
}

FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
{
    if (pos.IsNull())
        return NULL;
    boost::filesystem::path path = GetBlockPosFilename(pos, prefix);
    boost::filesystem::create_directories(path.parent_path());
    FILE* file = fopen(path.string().c_str(), "rb+");
    if (!file && !fReadOnly)
        file = fopen(path.string().c_str(), "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return NULL;
    }
    if (pos.nPos) {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return NULL;
        }
    }
    return file;
}

FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "rev", fReadOnly);
}

boost::filesystem::path GetBlockPosFilename( const CDiskBlockPos & pos, const char * prefix )
{
    return GetDirForData() / "blocks" / strprintf( "%s%05u.dat", prefix, pos.nFile ) ;
}

CBlockIndex * InsertBlockIndex( uint256 hash )
{
    if ( hash.IsNull() ) return nullptr ;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find( hash ) ;
    if ( mi != mapBlockIndex.end() )
        return mi->second ;

    // Create new
    CBlockIndex * pindexNew = new CBlockIndex() ;
    if ( pindexNew == nullptr )
        throw std::runtime_error( std::string(__func__) + ": new CBlockIndex failed" ) ;
    mi = mapBlockIndex.insert( std::make_pair( hash, pindexNew ) ).first ;
    pindexNew->SetBlockSha256Hash( mi->first ) ;

    return pindexNew ;
}

static std::atomic< bool > loadingBlockIndexDB( false ) ;

bool static LoadBlockIndexDB( const CChainParams & chainparams )
{
    loadingBlockIndexDB = true ;
    if ( ! pblocktree->LoadBlockIndexGuts( InsertBlockIndex, loadingBlockIndexDB ) )
        return false ;

    if ( ! loadingBlockIndexDB || ShutdownRequested() ) {
        LogPrintf( "%s: stopping\n", __func__ ) ;
        throw std::string( "stopthread" ) ;
    }

    // sort blocks in chain by height
    std::vector< std::pair< int, CBlockIndex* > > vSortedByHeight ;
    vSortedByHeight.reserve( mapBlockIndex.size() ) ;
    for ( const std::pair< uint256, CBlockIndex* > & item : mapBlockIndex )
    {
        CBlockIndex * pindex = item.second ;
        vSortedByHeight.push_back( std::make_pair( pindex->nHeight, pindex ) ) ;
    }
    std::sort( vSortedByHeight.begin(), vSortedByHeight.end() ) ;
    for ( const std::pair< int, CBlockIndex* > & item : vSortedByHeight )
    {
        CBlockIndex * pindex = item.second ;

        // calculate nChainCoins, the summary number of coins generated in the chain up to and including this block
        /* CBlock block ;
        if ( ReadBlockFromDisk( block, pindex, chainparams.GetConsensus( pindex->nHeight ) ) ) {
            pindex->nBlockNewCoins = CountBlockNewCoins( block, pindex->nHeight, chainparams ) ;
            pindex->nChainCoins = ( pindex->pprev != nullptr ? pindex->pprev->nChainCoins : 0 ) + pindex->nBlockNewCoins ;
        } */

        pindex->nTimeMax = ( pindex->pprev ? std::max( pindex->pprev->nTimeMax, pindex->nTime ) : pindex->nTime ) ;
        // We can link the chain of blocks for which we've received transactions at some point
        // Pruned nodes may have deleted the block
        if ( pindex->nBlockTx > 0 ) {
            if ( pindex->pprev != nullptr ) {
                if ( pindex->pprev->nChainTx > 0 ) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nBlockTx ;
                } else {
                    pindex->nChainTx = 0 ;
                    mapBlocksUnlinked.insert( std::make_pair( pindex->pprev, pindex ) ) ;
                }
            } else {
                pindex->nChainTx = pindex->nBlockTx ;
            }
        }
        if ( pindex->IsValid( BLOCK_VALID_TRANSACTIONS ) && ( pindex->nChainTx > 0 || pindex->pprev == nullptr ) )
            setOfBlockIndexCandidates.insert( pindex ) ;
        if ( pindex->nStatus & BLOCK_FAILED_MASK &&
                ( ! pindexBestInvalid || pindex->nHeight > pindexBestInvalid->nHeight ) )
            pindexBestInvalid = pindex ;
        if ( pindex->pprev != nullptr )
            pindex->BuildSkip() ;
        if ( pindex->IsValid( BLOCK_VALID_TREE )
                && ( pindexBestHeader == nullptr || CBlockIndexComparator()( pindexBestHeader, pindex ) ) )
            pindexBestHeader = pindex ;
    }

    // Load block file info
    pblocktree->ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf( "%s: last block file = %i\n", __func__, nLastBlockFile ) ;
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
        pblocktree->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    }
    LogPrintf( "%s: last block file info %s\n", __func__, vinfoBlockFile[ nLastBlockFile ].ToString() ) ;
    for (int nFile = nLastBlockFile + 1; true; nFile++) {
        CBlockFileInfo info;
        if (pblocktree->ReadBlockFileInfo(nFile, info)) {
            vinfoBlockFile.push_back(info);
        } else {
            break;
        }
    }

    // Check presence of blk files
    LogPrintf( "Checking all blk files are present...\n" ) ;
    std::set< int > setBlkDataFiles ;
    for ( const std::pair< uint256, CBlockIndex* > & item : mapBlockIndex )
    {
        CBlockIndex* pindex = item.second ;
        if ( pindex->nStatus & BLOCK_DATA_EXISTS ) {
            setBlkDataFiles.insert( pindex->nFile ) ;
        }
    }
    for ( std::set< int >::iterator it = setBlkDataFiles.begin() ; it != setBlkDataFiles.end() ; ++ it )
    {
        CDiskBlockPos pos( *it, 0 ) ;
        if ( CAutoFile( OpenBlockFile( pos, true ), SER_DISK, PEER_VERSION ).isNull() )
            return false ;
    }

    // Check whether we have ever pruned block & undo files
    pblocktree->ReadFlag("prunedblockfiles", fHavePruned);
    if ( fHavePruned )
        LogPrintf( "%s: block files have previously been pruned\n", __func__ );

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    fReindex |= fReindexing;

    // Check whether we have a transaction index
    pblocktree->ReadFlag( "txindex", fTxIndex ) ;
    LogPrintf( "%s: transaction index is %s\n", __func__, fTxIndex ? "on" : "off" ) ;

    // Load pointer to end of best chain
    BlockMap::iterator it = mapBlockIndex.find( pcoinsTip->GetSha256OfBestBlock() ) ;
    if ( it == mapBlockIndex.end() )
        return true ;

    chainActive.SetTip( it->second ) ;

    ////UpdateTipBlockNewCoins( chainparams ) ;

    PruneBlockIndexCandidates() ;

    CBlockHeader tipBlock = chainActive.Tip()->GetBlockHeader( chainparams.GetConsensus( chainActive.Height() ) ) ;
    double progress = GuessVerificationProgress( chainparams.TxData(), chainActive.Tip() ) ;
    LogPrintf( "%s: chain's tip height=%d sha256_hash=%s scrypt_hash=%s version=0x%x%s date='%s', progress=%s\n", __func__,
        chainActive.Height(),
        /* chainActive.Tip()->GetBlockSha256Hash().ToString() */ tipBlock.GetSha256Hash().ToString(),
        tipBlock.GetScryptHash().ToString(),
        tipBlock.nVersion, tipBlock.IsAuxpowInVersion() ? "(auxpow)" : "",
        DateTimeStrFormat( "%Y-%m-%d %H:%M:%S", /* chainActive.Tip()->GetBlockTime() */ tipBlock.nTime ),
        strprintf( "%.3f", progress * 100 ) + std::string( "%" ) ) ;

    return true ;
}

WVerifyDB::WVerifyDB()
{
    uiInterface.ShowProgress( _("Verifying blocks..."), 0 ) ;
}

WVerifyDB::~WVerifyDB()
{
    uiInterface.ShowProgress( "", 100 ) ;
}

bool WVerifyDB::VerifyDB( const CChainParams & chainparams, AbstractCoinsView * coinsview, int nCheckLevel, int nCheckDepth )
{
    verifying = true ;

    LOCK( cs_main ) ;
    if ( chainActive.Tip() == nullptr || chainActive.Tip()->pprev == nullptr )
        return true ;

    // Verify blocks in the best chain
    if ( nCheckDepth <= 0 ) nCheckDepth = 1000000000 ;
    if ( nCheckDepth > chainActive.Height() )
        nCheckDepth = chainActive.Height() ;
    nCheckLevel = std::max( 0, std::min( 4, nCheckLevel ) ) ;
    LogPrintf( "Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel ) ;

    CCoinsViewCache coins( coinsview ) ;
    CBlockIndex* pindexState = chainActive.Tip() ;
    CBlockIndex* pindexFailure = nullptr ;
    int nGoodTransactions = 0 ;
    CValidationState state ;

    int reportDone = 0 ;
    LogPrintf( "[0%%]..." ) ;

    for ( CBlockIndex* pindex = chainActive.Tip() ; pindex && pindex->pprev ; pindex = pindex->pprev )
    {
        if ( ! verifying || ShutdownRequested() ) {
            LogPrintf( "%s: stopping\n", __func__ ) ;
            throw std::string( "stopthread" ) ;
        }

        int percentageDone = std::max(1, std::min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100))));
        if ( reportDone < percentageDone / 10 ) {
            // report every 10% step
            LogPrintf( "[%d%%]...", percentageDone ) ;
            reportDone = percentageDone / 10 ;
        }
        uiInterface.ShowProgress( _("Verifying blocks..."), percentageDone ) ;
        if ( pindex->nHeight < chainActive.Height() - nCheckDepth )
            break ;
        if ( fPruneMode && ! ( pindex->nStatus & BLOCK_DATA_EXISTS ) ) {
            // If pruning, only go back as far as we have data
            LogPrintf( "%s: block verification stopping at height %d (pruning, no data)\n", __func__, pindex->nHeight ) ;
            break ;
        }

        CBlock block ;
        // check level 0: read from disk
        if ( ! ReadBlockFromDisk( block, pindex, chainparams.GetConsensus( pindex->nHeight ) ) )
            return error( "%s: *** ReadBlockFromDisk failed at height %d, sha256_hash=%s", __func__,
                            pindex->nHeight, pindex->GetBlockSha256Hash().ToString() ) ;

        // check level 1: verify block validity
        if ( nCheckLevel >= 1 && ! CheckBlock( block, state ) )
            return error( "%s: *** found bad block at height %d, sha256_hash=%s (%s)\n", __func__,
                          pindex->nHeight, pindex->GetBlockSha256Hash().ToString(), FormatStateMessage( state ) ) ;

        // check level 2: verify undo validity
        if ( nCheckLevel >= 2 && pindex != nullptr ) {
            CBlockUndo undo ;
            CDiskBlockPos pos = pindex->GetUndoPos() ;
            if ( ! pos.IsNull() ) {
                if ( ! UndoReadFromDisk( undo, pos, pindex->pprev->GetBlockSha256Hash() ) )
                    return error( "%s: *** found bad undo data at height %d, sha256_hash=%s\n", __func__,
                                    pindex->nHeight, pindex->GetBlockSha256Hash().ToString() ) ;
            }
        }

        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if ( nCheckLevel >= 3 && pindex == pindexState &&
                ( coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage() ) <= nCoinCacheUsage ) {
            bool fClean = true ;
            if ( ! DisconnectBlock( block, state, pindex, coins, &fClean ) )
                return error( "%s: *** irrecoverable inconsistency in block data at height %d, sha256_hash=%s", __func__,
                                pindex->nHeight, pindex->GetBlockSha256Hash().ToString() ) ;
            pindexState = pindex->pprev ;
            if ( ! fClean ) {
                nGoodTransactions = 0 ;
                pindexFailure = pindex ;
            } else
                nGoodTransactions += block.vtx.size() ;
        }

        if ( ShutdownRequested() ) return true ;
    }
    if ( pindexFailure != nullptr )
        return error( "%s: *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n", __func__,
                        chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions ) ;

    // check level 4: try reconnecting blocks
    if ( nCheckLevel >= 4 ) {
        CBlockIndex * pindex = pindexState ;
        while ( pindex != chainActive.Tip() )
        {
            if ( ! verifying || ShutdownRequested() ) {
                LogPrintf( "%s: stopping\n", __func__ ) ;
                throw std::string( "stopthread" ) ;
            }

            uiInterface.ShowProgress( _("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))) ) ;
            pindex = chainActive.Next( pindex ) ;
            CBlock block ;
            if ( ! ReadBlockFromDisk( block, pindex, chainparams.GetConsensus( pindex->nHeight ) ) )
                return error( "%s: *** ReadBlockFromDisk failed at height %d, sha256_hash=%s", __func__,
                                pindex->nHeight, pindex->GetBlockSha256Hash().ToString() ) ;
            if ( ! ConnectBlock( block, state, pindex, coins, chainparams ) )
                return error( "%s: *** found unconnectable block at height %d, sha256_hash=%s", __func__,
                                pindex->nHeight, pindex->GetBlockSha256Hash().ToString() ) ;
        }
    }

    LogPrintf( "[DONE]\n" ) ;
    LogPrintf( "%s: no coin database inconsistencies in last %i blocks (%i transactions)\n", __func__,
                chainActive.Height() - pindexState->nHeight, nGoodTransactions ) ;

    verifying = false ;
    return true ;
}

bool RewindBlockIndex(const CChainParams& params)
{
    LOCK(cs_main);

    int nHeight = 1;
    while (nHeight <= chainActive.Height()) {
        if (IsWitnessEnabled(chainActive[nHeight - 1], params.GetConsensus(nHeight - 1)) && !(chainActive[nHeight]->nStatus & BLOCK_OPT_WITNESS)) {
            break;
        }
        nHeight++;
    }

    // nHeight is now the height of the first insufficiently-validated block, or tipheight + 1
    CValidationState state ;
    CBlockIndex* pindex = chainActive.Tip() ;
    while ( chainActive.Height() >= nHeight ) {
        if ( fPruneMode && ! ( chainActive.Tip()->nStatus & BLOCK_DATA_EXISTS ) ) {
            // If pruning, don't try to rewind past "has data" point;
            // since older blocks can't be served anyway, there's
            // no need to walk further, and trying to DisconnectTip()
            // will fail (and require a needless reindex/redownload
            // of the blockchain)
            break ;
        }
        if ( ! DisconnectTip( state, params, true ) ) {
            return error( "%s: unable to disconnect block at height %i", __func__, pindex->nHeight ) ;
        }
        // Occasionally flush state to disk
        if ( ! FlushStateToDisk( state, FLUSH_STATE_PERIODIC ) )
            return false ;
    }

    // Reduce validity flag and have-data flags.
    // We do this after actual disconnecting, otherwise we'll end up writing the lack of data
    // to disk before writing the chainstate, resulting in a failure to continue if interrupted
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
        CBlockIndex* pindexIter = it->second;

        // Note: If we encounter an insufficiently validated block that
        // is on chainActive, it must be because we are a pruning node, and
        // this block or some successor doesn't HAVE_DATA, so we were unable to
        // rewind all the way.  Blocks remaining on chainActive at this point
        // must not have their validity reduced
        if (IsWitnessEnabled(pindexIter->pprev, params.GetConsensus(pindexIter->nHeight)) && !(pindexIter->nStatus & BLOCK_OPT_WITNESS) && !chainActive.Contains(pindexIter)) {
            // Reduce validity
            pindexIter->nStatus = std::min<unsigned int>(pindexIter->nStatus & BLOCK_VALID_MASK, BLOCK_VALID_TREE) | (pindexIter->nStatus & ~BLOCK_VALID_MASK);
            // Remove have-data flags
            pindexIter->nStatus &= ~ ( BLOCK_DATA_EXISTS | BLOCK_UNDO_EXISTS ) ;
            // Remove storage location
            pindexIter->nFile = 0;
            pindexIter->nDataPos = 0;
            pindexIter->nUndoPos = 0;
            // Remove various other things
            pindexIter->nBlockTx = 0 ;
            pindexIter->nChainTx = 0 ;
            pindexIter->nSequenceId = 0 ;

            // Make sure it gets written
            setOfDirtyBlockIndices.insert( pindexIter ) ;
            // Update indexes
            setOfBlockIndexCandidates.erase( pindexIter ) ;

            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> ret = mapBlocksUnlinked.equal_range(pindexIter->pprev);
            while (ret.first != ret.second) {
                if (ret.first->second == pindexIter) {
                    mapBlocksUnlinked.erase(ret.first++);
                } else {
                    ++ret.first;
                }
            }
        } else if ( pindexIter->IsValid( BLOCK_VALID_TRANSACTIONS ) && pindexIter->nChainTx > 0 ) {
            setOfBlockIndexCandidates.insert( pindexIter ) ;
        }
    }

    PruneBlockIndexCandidates();

    CheckBlockIndex( params.GetConsensus( chainActive.Height() ) ) ;

    if ( ! FlushStateToDisk( state, FLUSH_STATE_ALWAYS ) )
        return false ;

    return true ;
}

// May NOT be used after any connections are up as much
// of the peer-processing logic assumes a consistent
// block index state
void UnloadBlockIndex()
{
    LOCK( cs_main ) ;
    setOfBlockIndexCandidates.clear() ;
    chainActive.SetTip( nullptr ) ;
    pindexBestInvalid = NULL;
    pindexBestHeader = NULL;
    mempool.clear();
    mapBlocksUnlinked.clear();
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    nBlockSequenceId = 1;
    setOfDirtyBlockIndices.clear() ;
    setOfDirtyBlockFiles.clear() ;
    versionbitscache.Clear() ;
    for ( int b = 0; b < VERSIONBITS_NUM_BITS; b ++ ) {
        warningcache[ b ].clear() ;
    }

    for ( BlockMap::value_type & entry : mapBlockIndex ) {
        delete entry.second ;
    }
    mapBlockIndex.clear() ;
    fHavePruned = false ;
}

bool LoadBlockIndex( const CChainParams & chainparams )
{
    // Load block index from databases
    if ( ! fReindex && ! LoadBlockIndexDB( chainparams ) )
        return false ;

    return true ;
}

bool InitBlockIndex(const CChainParams& chainparams)
{
    LOCK(cs_main);

    // Check whether we're already initialized
    if (chainActive.Genesis() != NULL)
        return true;

    // Use the provided setting for -txindex in the new database
    fTxIndex = GetBoolArg("-txindex", DEFAULT_TXINDEX);
    pblocktree->WriteFlag("txindex", fTxIndex);
    LogPrintf("Initializing databases...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!fReindex) {
        try {
            CBlock &block = const_cast<CBlock&>(chainparams.GenesisBlock());
            // Start new block file
            unsigned int nBlockSize = ::GetSerializeSize( block, SER_DISK, PEER_VERSION ) ;
            CDiskBlockPos blockPos;
            CValidationState state;
            if (!FindBlockPos(state, blockPos, nBlockSize+8, 0, block.GetBlockTime()))
                return error("LoadBlockIndex(): FindBlockPos failed");
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                return error("LoadBlockIndex(): writing genesis block to disk failed");
            CBlockIndex *pindex = AddToBlockIndex(block);
            if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
                return error("LoadBlockIndex(): genesis block not accepted");
            // Force a chainstate write so that when we VerifyDB in a moment, it doesn't check stale data
            return FlushStateToDisk( state, FLUSH_STATE_ALWAYS ) ;
        } catch (const std::runtime_error& e) {
            return error("LoadBlockIndex(): failed to initialize block database: %s", e.what());
        }
    }

    return true;
}

bool LoadExternalBlockFile(const CChainParams& chainparams, FILE* fileIn, CDiskBlockPos *dbp)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat( fileIn, 2 * MAX_BLOCK_SERIALIZED_SIZE, MAX_BLOCK_SERIALIZED_SIZE + 8, SER_DISK, PEER_VERSION ) ;
        uint64_t nRewind = blkdat.GetPos();
        while ( ! blkdat.eof() )
        {
            if ( ShutdownRequested() ) break ;

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[CMessageHeader::MESSAGE_START_SIZE];
                blkdat.FindByte(chainparams.MessageStart()[0]);
                nRewind = blkdat.GetPos()+1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainparams.MessageStart(), CMessageHeader::MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SERIALIZED_SIZE)
                    continue;
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
                CBlock& block = *pblock;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetSha256Hash() ;
                if ( hash != chainparams.GetConsensus( 0 ).hashGenesisBlock
                            && mapBlockIndex.find( block.hashPrevBlock ) == mapBlockIndex.end() )
                {
                    LogPrint("reindex", "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(),
                            block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                if ( mapBlockIndex.count( hash ) == 0 ||
                        ( mapBlockIndex[ hash ]->nStatus & BLOCK_DATA_EXISTS ) == 0 ) {
                    LOCK(cs_main);
                    CValidationState state;
                    if (AcceptBlock(pblock, state, chainparams, NULL, true, dbp, NULL))
                        nLoaded++;
                    if (state.IsError())
                        break;
                } else if (hash != chainparams.GetConsensus(0).hashGenesisBlock && mapBlockIndex[hash]->nHeight % 1000 == 0) {
                    LogPrint("reindex", "Block Import: already had block %s at height %d\n", hash.ToString(), mapBlockIndex[hash]->nHeight);
                }

                // Activate the genesis block so normal node progress can continue
                if ( hash == chainparams.GetConsensus( 0 ).hashGenesisBlock ) {
                    CValidationState state ;
                    if ( ! ActivateBestChain( state, chainparams ) ) break ;
                }

                NotifyHeaderTip();

                // Recursively process earlier encountered successors of this block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        std::shared_ptr<CBlock> pblockrecursive = std::make_shared<CBlock>();
                        // TODO: Need a valid consensus height
                        if ( ReadBlockFromDisk( *pblockrecursive, it->second, chainparams.GetConsensus( 0 ) ) )
                        {
                            LogPrint( "reindex", "%s: Processing out of order child %s of %s\n", __func__,
                                      pblockrecursive->GetSha256Hash().ToString(), head.ToString() ) ;
                            LOCK( cs_main ) ;
                            CValidationState dummy ;
                            if ( AcceptBlock( pblockrecursive, dummy, chainparams, NULL, true, &it->second, NULL ) )
                            {
                                nLoaded ++ ;
                                queue.push_back( pblockrecursive->GetSha256Hash() ) ;
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                        NotifyHeaderTip();
                    }
                }
            } catch (const std::exception& e) {
                LogPrintf("%s: Deserialize or I/O error - %s\n", __func__, e.what());
            }
        }
    } catch (const std::runtime_error& e) {
        AbortNode(std::string("System error: ") + e.what());
    }
    if ( nLoaded > 0 )
        LogPrintf( "Loaded %i blocks from external file in %.3f s\n", nLoaded, 0.001 * ( GetTimeMillis() - nStart ) ) ;
    return nLoaded > 0;
}

void static CheckBlockIndex(const Consensus::Params& consensusParams)
{
    if (!fCheckBlockIndex) {
        return;
    }

    LOCK(cs_main);

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain
    // (a few of the tests when iterating the block tree require that chainActive has been initialized)
    if (chainActive.Height() < 0) {
        assert(mapBlockIndex.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree
    std::multimap < CBlockIndex*, CBlockIndex* > forward ;
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
        forward.insert(std::make_pair(it->second->pprev, it->second));
    }

    assert(forward.size() == mapBlockIndex.size());

    std::pair< std::multimap< CBlockIndex*, CBlockIndex* >::iterator, std::multimap< CBlockIndex*, CBlockIndex* >::iterator > rangeGenesis = forward.equal_range( NULL ) ;
    CBlockIndex * pindex = rangeGenesis.first->second ;
    rangeGenesis.first ++ ;
    assert( rangeGenesis.first == rangeGenesis.second ) ; // there is only one index entry with parent NULL

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = nullptr ; // Oldest ancestor of pindex which is invalid
    CBlockIndex* pindexFirstMissing = nullptr ; // Oldest ancestor of pindex which does not have BLOCK_DATA_EXISTS
    CBlockIndex* pindexFirstNeverProcessed = nullptr ; // Oldest ancestor of pindex for which nTx == 0
    CBlockIndex* pindexFirstNotTreeValid = nullptr ; // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not)
    CBlockIndex* pindexFirstNotTransactionsValid = nullptr ; // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not)
    CBlockIndex* pindexFirstNotChainValid = nullptr ; // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not)
    CBlockIndex* pindexFirstNotScriptsValid = nullptr ; // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not)
    while ( pindex != nullptr )
    {
        nNodes ++ ;

        if ( pindexFirstInvalid == nullptr && pindex->nStatus & BLOCK_FAILED_VALID )
            pindexFirstInvalid = pindex ;
        if ( pindexFirstMissing == nullptr && ! ( pindex->nStatus & BLOCK_DATA_EXISTS ) )
            pindexFirstMissing = pindex ;
        if ( pindexFirstNeverProcessed == nullptr && pindex->nBlockTx == 0 )
            pindexFirstNeverProcessed = pindex ;
        if ( pindex->pprev != nullptr && pindexFirstNotTreeValid == nullptr &&
                ( pindex->nStatus & BLOCK_VALID_MASK ) < BLOCK_VALID_TREE )
            pindexFirstNotTreeValid = pindex ;
        if ( pindex->pprev != nullptr && pindexFirstNotTransactionsValid == nullptr &&
                ( pindex->nStatus & BLOCK_VALID_MASK ) < BLOCK_VALID_TRANSACTIONS )
            pindexFirstNotTransactionsValid = pindex ;
        if ( pindex->pprev != nullptr && pindexFirstNotChainValid == nullptr &&
                ( pindex->nStatus & BLOCK_VALID_MASK ) < BLOCK_VALID_CHAIN )
            pindexFirstNotChainValid = pindex ;
        if ( pindex->pprev != nullptr && pindexFirstNotScriptsValid == nullptr &&
                ( pindex->nStatus & BLOCK_VALID_MASK ) < BLOCK_VALID_SCRIPTS )
            pindexFirstNotScriptsValid = pindex ;

        // Begin actual consistency checks
        if ( pindex->pprev == nullptr ) {
            // Genesis block checks
            assert( pindex->GetBlockSha256Hash() == consensusParams.hashGenesisBlock ) ; // Genesis block's hash must match
            assert( pindex == chainActive.Genesis() ) ; // The current active chain's genesis block must be this block
        }
        if (pindex->nChainTx == 0) assert(pindex->nSequenceId <= 0);  // nSequenceId can't be set positive for blocks that aren't linked (negative is used for preciousblock)
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or not pruning has occurred)
        // HAVE_DATA is only equivalent to nTx > 0 (or VALID_TRANSACTIONS) if no pruning has occurred
        if ( ! fHavePruned ) {
            // If we've never pruned, then HAVE_DATA should be equivalent to nBlockTx > 0
            assert( ! ( pindex->nStatus & BLOCK_DATA_EXISTS ) == ( pindex->nBlockTx == 0 ) ) ;
            assert( pindexFirstMissing == pindexFirstNeverProcessed ) ;
        } else {
            // If we have pruned, then we can only say that HAVE_DATA implies nBlockTx > 0
            if ( pindex->nStatus & BLOCK_DATA_EXISTS ) assert( pindex->nBlockTx > 0 ) ;
        }
        if ( pindex->nStatus & BLOCK_UNDO_EXISTS ) assert( pindex->nStatus & BLOCK_DATA_EXISTS ) ;

        assert( ( ( pindex->nStatus & BLOCK_VALID_MASK ) >= BLOCK_VALID_TRANSACTIONS ) == ( pindex->nBlockTx > 0 ) ) ; // This is pruning-independent
        // All parents having had data (at some point) is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set
        assert((pindexFirstNeverProcessed != NULL) == (pindex->nChainTx == 0)); // nChainTx != 0 is used to signal that all parent blocks have been processed (but may have been pruned)
        assert((pindexFirstNotTransactionsValid != NULL) == (pindex->nChainTx == 0));
        assert( pindex->nHeight == nHeight ) ; // nHeight must be consistent
        assert( pindex->pprev == nullptr || pindex->nHeight > pindex->pprev->nHeight ) ; // For every block except the genesis block, the height must be larger than the parent's
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight))); // The pskip pointer must point back for all but the first 2 blocks
        assert(pindexFirstNotTreeValid == NULL); // All mapBlockIndex entries must at least be TREE valid

        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == NULL); // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == NULL); // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == NULL); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == NULL) {
            // Checks for not-invalid blocks
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0); // The failed mask cannot be set for blocks without invalid parents
        }
        if ( ! CBlockIndexComparator()( pindex, chainActive.Tip() ) && pindexFirstNeverProcessed == nullptr ) {
            if ( pindexFirstInvalid == nullptr ) {
                // If this block sorts at least as good as the current tip and
                // is valid and we have all data for its parents, it must be in
                // setOfBlockIndexCandidates.  chainActive.Tip() must also be there
                // even if some data has been pruned
                if ( pindexFirstMissing == nullptr || pindex == chainActive.Tip() ) {
                    assert( setOfBlockIndexCandidates.count( pindex ) ) ;
                }
                // If some parent is missing, then it could be that this block was in
                // setOfBlockIndexCandidates but had to be removed because of the missing data.
                // In this case it must be in mapBlocksUnlinked -- see test below
            }
        } else { // If this block sorts worse than the current tip or some ancestor's block has never been seen,
                 // it cannot be in setOfBlockIndexCandidates
            assert( setOfBlockIndexCandidates.count( pindex ) == 0 ) ;
        }
        // Check whether this block is in mapBlocksUnlinked
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if ( pindex->pprev && ( pindex->nStatus & BLOCK_DATA_EXISTS ) &&
                pindexFirstNeverProcessed != nullptr && pindexFirstInvalid == nullptr ) {
            // If this block has block data available, some parent was never received, and has no invalid parents, it must be in mapBlocksUnlinked
            assert(foundInUnlinked);
        }
        if ( ! ( pindex->nStatus & BLOCK_DATA_EXISTS ) ) assert( ! foundInUnlinked ) ; // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
        if ( pindexFirstMissing == nullptr ) assert( ! foundInUnlinked ) ; // We aren't missing data for any parent -- cannot be in mapBlocksUnlinked
        if ( pindex->pprev && ( pindex->nStatus & BLOCK_DATA_EXISTS ) &&
                pindexFirstNeverProcessed == nullptr && pindexFirstMissing != nullptr ) {
            // We have data for this block, have received data for all parents at some point,
            // but we're currently missing data for some parent
            assert( fHavePruned ) ; // We must have pruned
            // This block may have entered mapBlocksUnlinked if:
            //  - it has a descendant that at some point was higher than the tip, and
            //  - we tried switching to that descendant but were missing data
            //    for some intermediate block between chainActive and the tip
            // So if this block is itself better than chainActive.Tip() and it wasn't
            // in setOfBlockIndexCandidates, then it must be in mapBlocksUnlinked
            if ( ! CBlockIndexComparator()( pindex, chainActive.Tip() ) &&
                    setOfBlockIndexCandidates.count( pindex ) == 0 ) {
                if ( pindexFirstInvalid == nullptr ) {
                    assert( foundInUnlinked ) ;
                }
            }
        }
        // assert(pindex->GetBlockSha256Hash() == pindex->GetBlockHeader().GetSha256Hash()); // Perhaps too slow
        // End: actual consistency checks

        // Try descending into the first subnode
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> range = forward.equal_range(pindex);
        if (range.first != range.second) {
            // A subnode was found
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node
        // Move upwards until we reach a node of which we have not yet visited the last child
        while ( pindex != nullptr ) {
            // We are going to either move to a parent or a sibling of pindex
            // If pindex was the first with a certain property, unset the corresponding variable
            if ( pindex == pindexFirstInvalid ) pindexFirstInvalid = nullptr ;
            if ( pindex == pindexFirstMissing ) pindexFirstMissing = nullptr ;
            if ( pindex == pindexFirstNeverProcessed ) pindexFirstNeverProcessed = nullptr ;
            if ( pindex == pindexFirstNotTreeValid ) pindexFirstNotTreeValid = nullptr ;
            if ( pindex == pindexFirstNotTransactionsValid ) pindexFirstNotTransactionsValid = nullptr ;
            if ( pindex == pindexFirstNotChainValid ) pindexFirstNotChainValid = nullptr ;
            if ( pindex == pindexFirstNotScriptsValid ) pindexFirstNotScriptsValid = nullptr ;
            // Find our parent
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited
            std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child
                rangePar.first++;
            }
            // Proceed to the next one
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to the sibling
                pindex = rangePar.first->second;
                break;
            } else {
                // Move up further
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}

CBlockFileInfo* GetBlockFileInfo(size_t n)
{
    return &vinfoBlockFile.at(n);
}

ThresholdState VersionBitsTipState(const Consensus::Params& params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsState(chainActive.Tip(), params, pos, versionbitscache);
}

int VersionBitsTipStateSinceHeight(const Consensus::Params& params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsStateSinceHeight(chainActive.Tip(), params, pos, versionbitscache);
}

// Guess how far we are in the verification process at the given block index
double GuessVerificationProgress( const ChainTxData & data, const CBlockIndex * pindex )
{
    if ( pindex == nullptr ) return 0 ;

    std::time_t secondsNow = std::time( nullptr ) ;

    double fTxTotal =
        ( pindex->nChainTx <= data.nTxCount )
            ? data.nTxCount + ( secondsNow - data.nTime ) * data.dTxRate
            : pindex->nChainTx + ( secondsNow - pindex->GetBlockTime() ) * data.dTxRate ;

    return pindex->nChainTx / fTxTotal ;
}

class CMainCleanup
{
public:
    CMainCleanup() {}
    ~CMainCleanup() {
        // block headers
        BlockMap::iterator it1 = mapBlockIndex.begin();
        for (; it1 != mapBlockIndex.end(); it1++)
            delete (*it1).second;
        mapBlockIndex.clear();
    }
} instance_of_cmaincleanup;
