// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "miner.h"

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "dogecoin.h"
#include "hash.h"
#include "validation.h"
#include "net.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilstr.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#include "wallet/wallet.h"

#include <algorithm>
#include <queue>
#include <utility>

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;
uint64_t nLastBlockWeight = 0;

class ScoreCompare
{
public:
    ScoreCompare() {}

    bool operator()(const CTxMemPool::txiter a, const CTxMemPool::txiter b)
    {
        return CompareTxMemPoolEntryByScore()(*b,*a); // Convert to less than
    }
};

int64_t UpdateTime( CBlockHeader * pblock, const Consensus::Params & consensusParams, const CBlockIndex * pindexPrev )
{
    int64_t nOldTime = pblock->nTime ;
    int64_t nNewTime = ( NameOfChain() == "inu" ) ?
                            GetAdjustedTime() :
                            std::max( pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime() ) ;

    if ( nNewTime > nOldTime )
        pblock->nTime = nNewTime ;

    // Updating time can change bits of work
    pblock->nBits = GetNextWorkRequired( pindexPrev, pblock, consensusParams ) ;

    return nNewTime - nOldTime ;
}

BlockAssembler::BlockAssembler( const CChainParams & _chainparams )
    : chainparams( _chainparams )
{
    // Block resource limits
    // If neither -blockmaxsize or -blockmaxweight is given, limit to DEFAULT_BLOCK_MAX_*
    // If only one is given, only restrict the specified resource.
    // If both are given, restrict both.
    nBlockMaxWeight = DEFAULT_BLOCK_MAX_WEIGHT;
    nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
    bool fWeightSet = false;
    if (IsArgSet("-blockmaxweight")) {
        nBlockMaxWeight = GetArg("-blockmaxweight", DEFAULT_BLOCK_MAX_WEIGHT);
        nBlockMaxSize = MAX_BLOCK_SERIALIZED_SIZE;
        fWeightSet = true;
    }
    if (IsArgSet("-blockmaxsize")) {
        nBlockMaxSize = GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
        if (!fWeightSet) {
            nBlockMaxWeight = nBlockMaxSize * WITNESS_SCALE_FACTOR;
        }
    }

    if ( IsArgSet( "-blockmintxfee" ) ) {
        CAmount n = 0 ;
        ParseMoney( GetArg( "-blockmintxfee", "" ), n ) ;
        blockMinFeeRate = CFeeRate( n ) ;
    } else {
        blockMinFeeRate = CFeeRate( 0 ) ;
    }

    // Limit weight to between 4K and MAX_BLOCK_WEIGHT-4K for sanity:
    nBlockMaxWeight = std::max((unsigned int)4000, std::min((unsigned int)(MAX_BLOCK_WEIGHT-4000), nBlockMaxWeight));
    // Limit size to between 1K and MAX_BLOCK_SERIALIZED_SIZE-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MAX_BLOCK_SERIALIZED_SIZE-1000), nBlockMaxSize));
    // Whether we need to account for byte usage (in addition to weight usage)
    fNeedSizeAccounting = (nBlockMaxSize < MAX_BLOCK_SERIALIZED_SIZE-1000);
}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockSize = 1000;
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    fIncludeWitness = false;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;

    lastFewTxs = 0;
    blockFinished = false;
}

std::unique_ptr< CBlockTemplate > BlockAssembler::CreateNewBlock( const CScript & scriptPubKeyIn, bool fMineWitnessTx )
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if(!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back( -1 ) ; // will be changed at the end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);
    CBlockIndex* pindexPrev = chainActive.Tip();
    nHeight = pindexPrev->nHeight + 1;

    const Consensus::Params & consensus = chainparams.GetConsensus( nHeight ) ;
    const int32_t nChainId = consensus.nAuxpowChainId ;
    const int32_t nVersion = VERSIONBITS_LAST_OLD_BLOCK_VERSION ; // ComputeBlockVersion( pindexPrev, consensus )
    pblock->SetBaseVersion(nVersion, nChainId);
    // regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->SetBaseVersion(GetArg("-blockversion", pblock->GetBaseVersion()), nChainId);

    pblock->nTime = GetAdjustedTime() ;

    //if ( false && NameOfChain() == "inu" )
        //pblock->nTime = pindexPrev->nTime + /* 1 hour */ 60 * 60 ; // expected to fail TestBlockValidity

    nLockTimeCutoff = ( NameOfChain() != "inu" && ( STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST ) )
                        ? pindexPrev->GetMedianTimePast()
                        : pblock->GetBlockTime() ;

    // Decide whether to include witness transactions
    // This is only needed in case the witness softfork activation is reverted
    // (which would require a very deep reorganization) or when
    // -promiscuousmempoolflags is used.
    // TODO: replace this with a call to main to assess validity of a mempool
    // transaction (which in most cases can be a no-op)
    fIncludeWitness = IsWitnessEnabled(pindexPrev, consensus) && fMineWitnessTx;

    addPriorityTxs();
    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    int64_t nTime1 = GetTimeMicros();

    nLastBlockTx = nBlockTx;
    nLastBlockSize = nBlockSize;
    nLastBlockWeight = nBlockWeight;

    // Create coinbase transaction
    CMutableTransaction coinbaseTx ;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    coinbaseTx.vout.resize(1);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
    CAmount subsidy = GetDogecoinBlockSubsidy( nHeight, consensus, pindexPrev->GetBlockSha256Hash() ) ;
    coinbaseTx.vout[0].nValue = nFees + subsidy ;
    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0 ;
    pblock->vtx[0] = MakeTransactionRef( std::move( coinbaseTx ) ) ;
    pblocktemplate->vchCoinbaseCommitment = GenerateCoinbaseCommitment( *pblock, pindexPrev, consensus ) ;
    pblocktemplate->vTxFees[ 0 ] = - nFees ;

    /* LogPrintf(
        "CreateNewBlock: size %u, block weight %u, txs %u, fees %.8f, sigops %d\n",
            GetSerializeSize( *pblock, SER_NETWORK, PROTOCOL_VERSION ),
            GetBlockWeight( *pblock ), nBlockTx, nFees / 100000000.0, nBlockSigOpsCost
    ) ; */

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockSha256Hash() ;
    UpdateTime( pblock, consensus, pindexPrev ) ;
    pblock->nBits          = GetNextWorkRequired( pindexPrev, pblock, consensus ) ;
    pblock->nNonce         = 0;
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    CValidationState state ;
    if ( ! TestBlockValidity( state, chainparams, *pblock, pindexPrev, false, false ) ) {
        if ( state.GetRejectReason() != "coinbase-only-block-delay" && state.GetRejectReason() != "block-delay" )
            throw std::runtime_error( strprintf( "%s: TestBlockValidity failed: %s", __func__, FormatStateMessage( state ) ) ) ;
        else return nullptr ;
    }

    int64_t nTime2 = GetTimeMicros() ;
    LogPrint( "bench", "CreateNewBlock packages: %.3f ms (%d packages, %d updated descendants), validity: %.3f ms (total %.3f ms)\n",
              0.001 * ( nTime1 - nTimeStart ), nPackagesSelected, nDescendantsUpdated,
              0.001 * ( nTime2 - nTime1 ), 0.001 * ( nTime2 - nTimeStart ) ) ;

    return std::move( pblocktemplate ) ;
}

bool BlockAssembler::isStillDependent( CTxMemPool::txiter iter )
{
    for ( CTxMemPool::txiter parent : mempool.GetMemPoolParents( iter ) )
    {
        if ( inBlock.count( parent ) == 0 )
            return true ;
    }
    return false ;
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
            iit++;
        }
    }
}


bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost)
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= nBlockMaxWeight)
        return false;
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST)
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - premature witness (in case segwit transactions are added to mempool before
//   segwit activation)
// - serialized size (in case -blockmaxsize is in use)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package)
{
    uint64_t nPotentialBlockSize = nBlockSize; // only used with fNeedSizeAccounting
    for ( const CTxMemPool::txiter it : package )
    {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
        if (!fIncludeWitness && it->GetTx().HasWitness())
            return false;
        if (fNeedSizeAccounting) {
            uint64_t nTxSize = ::GetSerializeSize(it->GetTx(), SER_NETWORK, PROTOCOL_VERSION);
            if (nPotentialBlockSize + nTxSize >= nBlockMaxSize) {
                return false;
            }
            nPotentialBlockSize += nTxSize;
        }
    }
    return true;
}

bool BlockAssembler::TestForBlock(CTxMemPool::txiter iter)
{
    if (nBlockWeight + iter->GetTxWeight() >= nBlockMaxWeight) {
        // If the block is so close to full that no more txs will fit
        // or if we've tried more than 50 times to fill remaining space
        // then flag that the block is finished
        if (nBlockWeight >  nBlockMaxWeight - 400 || lastFewTxs > 50) {
             blockFinished = true;
             return false;
        }
        // Once we're within 4000 weight of a full block, only look at 50 more txs
        // to try to fill the remaining space.
        if (nBlockWeight > nBlockMaxWeight - 4000) {
            lastFewTxs++;
        }
        return false;
    }

    if (fNeedSizeAccounting) {
        if (nBlockSize + ::GetSerializeSize(iter->GetTx(), SER_NETWORK, PROTOCOL_VERSION) >= nBlockMaxSize) {
            if (nBlockSize >  nBlockMaxSize - 100 || lastFewTxs > 50) {
                 blockFinished = true;
                 return false;
            }
            if (nBlockSize > nBlockMaxSize - 1000) {
                lastFewTxs++;
            }
            return false;
        }
    }

    if (nBlockSigOpsCost + iter->GetSigOpCost() >= MAX_BLOCK_SIGOPS_COST) {
        // If the block has room for no more sig ops then
        // flag that the block is finished
        if (nBlockSigOpsCost > MAX_BLOCK_SIGOPS_COST - 8) {
            blockFinished = true;
            return false;
        }
        // Otherwise attempt to find another tx with fewer sigops
        // to put in the block.
        return false;
    }

    // Must check that lock times are still valid
    // This can be removed once MTP is always enforced
    // as long as reorgs keep the mempool consistent.
    if (!IsFinalTx(iter->GetTx(), nHeight, nLockTimeCutoff))
        return false;

    return true;
}

void BlockAssembler::AddToBlock( CTxMemPool::txiter iter )
{
    pblock->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back( iter->GetFee() ) ;
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    if (fNeedSizeAccounting) {
        nBlockSize += ::GetSerializeSize(iter->GetTx(), SER_NETWORK, PROTOCOL_VERSION);
    }
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = GetBoolArg( "-printpriority", DEFAULT_PRINTPRIORITY ) ;
    if ( fPrintPriority ) {
        double dPriority = iter->GetPriority( nHeight ) ;
        CAmount dummy ;
        mempool.ApplyDeltas( iter->GetTx().GetTxHash(), dPriority, dummy ) ;
        LogPrintf("priority %.1f fee %s tx %s\n",
                  dPriority,
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetTxHash().ToString());
    }
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set &mapModifiedTx)
{
    int nDescendantsUpdated = 0;
    for ( const CTxMemPool::txiter it : alreadyAdded ) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for ( CTxMemPool::txiter desc : descendants ) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCostWithAncestors -= it->GetSigOpCost();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    assert (it != mempool.mapTx.end());
    if (mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it))
        return true;
    return false;
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, CTxMemPool::txiter entry, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next
void BlockAssembler::addPackageTxs( int & nPackagesSelected, int & nDescendantsUpdated )
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty())
    {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
                SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareModifiedEntry()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if ( packageFees < blockMinFeeRate.GetFeePerBytes( packageSize ) ) {
            // Everything else we might consider has a lower fee rate
            return ;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                    nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, iter, sortedEntries);

        for (size_t i=0; i<sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void BlockAssembler::addPriorityTxs()
{
    // How much of the block should be dedicated to high-priority/low-fee transactions
    unsigned int nBlockPrioritySize = GetArg( "-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE ) ;
    nBlockPrioritySize = std::min( nBlockMaxSize, nBlockPrioritySize ) ;

    if ( nBlockPrioritySize == 0 ) {
        return ;
    }

    bool fSizeAccounting = fNeedSizeAccounting;
    fNeedSizeAccounting = true;

    // This vector will be sorted into a priority queue:
    std::vector<TxCoinAgePriority> vecPriority;
    TxCoinAgePriorityCompare pricomparer;
    std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash> waitPriMap;
    typedef std::map<CTxMemPool::txiter, double, CTxMemPool::CompareIteratorByHash>::iterator waitPriIter;
    double actualPriority = -1;

    vecPriority.reserve(mempool.mapTx.size());
    for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin();
         mi != mempool.mapTx.end(); ++mi)
    {
        double dPriority = mi->GetPriority( nHeight ) ;
        CAmount dummy ;
        mempool.ApplyDeltas( mi->GetTx().GetTxHash(), dPriority, dummy ) ;
        vecPriority.push_back( TxCoinAgePriority( dPriority, mi ) ) ;
    }
    std::make_heap(vecPriority.begin(), vecPriority.end(), pricomparer);

    CTxMemPool::txiter iter;
    while (!vecPriority.empty() && !blockFinished) { // add a tx from priority queue to fill the blockprioritysize
        iter = vecPriority.front().second;
        actualPriority = vecPriority.front().first;
        std::pop_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
        vecPriority.pop_back();

        // If tx already in block, skip
        if (inBlock.count(iter)) {
            assert(false); // shouldn't happen for priority txs
            continue;
        }

        // cannot accept witness transactions into a non-witness block
        if (!fIncludeWitness && iter->GetTx().HasWitness())
            continue;

        // If tx is dependent on other mempool txs which haven't yet been included
        // then put it in the waitSet
        if (isStillDependent(iter)) {
            waitPriMap.insert(std::make_pair(iter, actualPriority));
            continue;
        }

        // If this tx fits in the block add it, otherwise keep looping
        if ( TestForBlock( iter ) ) {
            AddToBlock( iter ) ;

            // If now that this txs is added we've surpassed our desired priority size,
            // then we're done adding priority txs
            if ( nBlockSize >= nBlockPrioritySize ) {
                break;
            }

            // This tx was successfully added, so
            // add transactions that depend on this one to the priority queue to try again
            for ( CTxMemPool::txiter child : mempool.GetMemPoolChildren( iter ) )
            {
                waitPriIter wpiter = waitPriMap.find(child);
                if (wpiter != waitPriMap.end()) {
                    vecPriority.push_back(TxCoinAgePriority(wpiter->second,child));
                    std::push_heap(vecPriority.begin(), vecPriority.end(), pricomparer);
                    waitPriMap.erase(wpiter);
                }
            }
        }
    }
    fNeedSizeAccounting = fSizeAccounting;
}

void IncrementExtraNonce( CBlock * pblock, const CBlockIndex * pindexPrev, uint32_t & nExtraNonce )
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

//
// Internal miner
//

static bool ProcessBlockFound( const CBlock * const block, const CChainParams & chainparams )
{
    assert( block != nullptr ) ;
    CBlockIndex* lastTip = chainActive.Tip() ; /* mapBlockIndex[ chainActive.Tip()->GetBlockSha256Hash() ] */

    {
        LOCK( cs_main );

        if ( lastTip != nullptr &&
                block->hashPrevBlock != lastTip->GetBlockSha256Hash() )
        {   // the generated block isn't above the chain's current tip block
            bool isLost = true ;

            // is it above the previous block?
            if ( lastTip->pprev != nullptr &&
                    lastTip->pprev->GetBlockSha256Hash() == block->hashPrevBlock )
            {
                // and not too far in time?
                uint32_t deltaTime = std::abs( block->nTime - lastTip->GetBlockTime() ) ;
                if ( deltaTime <= ( chainparams.GetConsensus( lastTip->nHeight ).nPowTargetSpacing >> 2 ) )
                {
                    // then rewind the chain one block back to connect the generated block, replacing the tip
                    CValidationState state ;
                    InvalidateBlock( state, chainparams, lastTip ) ;
                    if ( state.IsValid() ) // no && ActivateBestChain here, or previously replaced ones will reconnect
                        isLost = false ;
                }
            }

            if ( isLost ) {
                if ( lastTip->nStatus & BLOCK_FAILED_MASK ) ResetBlockFailureFlags( lastTip ) ;
                CValidationState state ;
                ActivateBestChain( state, chainparams ) ;
                return error( "%s: generated block with sha256_hash=%s is lost", __func__, block->GetSha256Hash().GetHex() ) ;
            }
        }
    }

    // Found a solution
    LogPrintf( block->ToString() ) ;
    LogPrintf( "reward %s\n", FormatMoney( block->vtx[0]->vout[0].nValue ) ) ;

    // Say about the new block
    GetMainSignals().BlockFound( block->GetSha256Hash() ) ;

    // Process this block the same as if it were received from another node
    bool newBlockOk = ProcessNewBlock( chainparams, std::make_shared< const CBlock >( *block ), true, nullptr ) ;

    if ( lastTip->nStatus & BLOCK_FAILED_MASK ) {
        LOCK( cs_main );
        ResetBlockFailureFlags( lastTip ) ; // it was just to rewind, not to mark as rejected forever
    }

    if ( ! newBlockOk ) {
        CValidationState state ;
        ActivateBestChain( state, chainparams ) ;
        return error( "%s: ProcessNewBlock, block not accepted", __func__ ) ;
    }

    return true ;
}

static CAmount newCoinsByKind( CAmount maximumCoins, const std::string & kind, std::mt19937 & random )
{
    CAmount newCoins = maximumCoins ;
    if ( kind == "zero" ) {
        newCoins = 0 ;
    }
    else if ( kind.find( "custom" ) /* begins with */ == 0 ) {
        // (amount)

        std::string amountString = substringBetween( kind, "(", ")" ) ;
        CAmount customAmount = -1 ;
        if ( amountString != "" ) {
            try {
                customAmount = std::stoul( amountString ) ;
            } catch ( const std::invalid_argument & ia ) {
                customAmount = -1 ;
            }
        }

        if ( customAmount < newCoins && customAmount > 0 )
            newCoins = customAmount ;
    }
    else if ( kind == "random" ) {
        std::uniform_int_distribution< long > distribution( 0, newCoins - 1 ) ;
        newCoins = distribution( random ) + 1 ;
    }
    else if ( kind.find( "piece" ) /* begins with */ == 0 ) {
        // (numerator)[denominator]
        // piece is full 1 if numerator is absent, denominator is absent or is larger than numerator

        std::string numeratorString = substringBetween( kind, "(", ")" ) ;
        std::string denominatorString( "" ) ;
        if ( numeratorString != "" )
            denominatorString = substringBetween( kind, "[", "]" ) ;

        unsigned long numerator = 1 ;
        unsigned long denominator = 1 ;

        if ( numeratorString != "" && denominatorString != "" ) {
            try {
                numerator = std::stoul( numeratorString ) ;
                denominator = std::stoul( denominatorString ) ;
            } catch ( const std::invalid_argument & ia ) {
                numerator = 1 ;
                denominator = 1 ;
            }
        }

        if ( numerator < denominator )
            newCoins = ( numerator * newCoins ) / denominator ;
    }
    else if ( kind.find( "multiplier" ) /* begins with */ == 0 ) {
        // (multiplier), when larger than 1 it has the effect of 1

        std::string multiplierString = substringBetween( kind, "(", ")" ) ;
        double multiplier = 1 ;
        if ( multiplierString != "" ) {
            try {
                multiplier = stringToDouble( multiplierString ) ;
            } catch ( ... ) {
                multiplier = 1 ;
            }
        }

        if ( multiplier < 1 && multiplier > 0 )
            newCoins = multiplier * newCoins ;
    }

    return newCoins ;
}

void MiningThread::MineBlocks()
{
    if ( finished ) return ;

    LogPrintf( "MiningThread (%d) started\n", numberOfThread ) ;
    RenameThread( strprintf( "digger-%d", numberOfThread ) ) ;

    GetMainSignals().ScriptForMining( coinbaseScript ) ;

    try {
        // Throw an error if no script was provided. This can happen
        // due to some internal error but also if the keypool is empty.
        // In the latter case, already the pointer is nil
        if ( coinbaseScript == nullptr || coinbaseScript->reserveScript.empty() )
            throw std::runtime_error( "No coinbase script available (mining needs a wallet)" ) ;

        uint32_t nExtraNonce = 0 ;

        while ( ! finished )
        {
            currentCandidate.reset() ;

            if ( chainparams.MiningRequiresPeers() ) {
                // wait for the network to come online hence don't waste time mining
                // on an obsolete chain
                do {
                    if ( g_connman->hasConnectedNodes() && ! IsInitialBlockDownload() )
                        break ;

                    if ( mempool.size() > 0 ) break ;

                    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) ) ;
                }
                while ( ! finished ) ;
            }

            if ( finished ) break ;

            //
            // Create new block
            //

            unsigned int transactionsInMempool = mempool.GetTransactionsUpdated() ;
            CBlockIndex * pindexPrev = chainActive.Tip() ;

            bool candidateOk = false ;
            do {
                candidateOk = assembleNewBlockCandidate() ;
                if ( candidateOk && currentCandidate != nullptr ) break ;
                std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) ) ;
            } while ( ! finished ) ;
            if ( finished ) break ;

            if ( recreateBlock ) recreateBlock = false ;

            CAmount newCoins = newCoinsByKind( getAmountOfCoinsBeingGenerated(), kindOfHowManyCoinsToGenerate, randomNumber ) ;

            if ( newCoins != getAmountOfCoinsBeingGenerated() ) {
                CMutableTransaction coinbase( * currentCandidate->block.vtx[ 0 ] ) ;
                coinbase.vout[ 0 ].nValue = newCoins - currentCandidate->vTxFees[ 0 ] ;
                currentCandidate->block.vtx[ 0 ] = MakeTransactionRef( std::move( coinbase ) ) ;
            }

            const Consensus::Params & consensus = chainparams.GetConsensus( chainActive.Tip()->nHeight + 1 ) ;

            //if ( ! consensus.fStrictChainId ) {
                //currentCandidate->block.nVersion &= 0xff ;
                //currentCandidate->block.nVersion |= randomNumber() & 0xff0000 ;
            //}

            CBlock * currentBlock = &currentCandidate->block ;
            if ( currentBlock->IsAuxpowInVersion() ) currentBlock->SetAuxpow( nullptr ) ;
            IncrementExtraNonce( currentBlock, pindexPrev, nExtraNonce ) ;

            //
            // Search
            //

            scanBeginsMillis = GetTimeMillis() ;
            noncesScanned = 0 ;
            smallestScryptHashBlock = ~ arith_uint256() ;

            uint32_t solutionBits = currentBlock->nBits ;
            arith_uint256 solutionHash = arith_uint256().SetCompact( solutionBits ) ;
            currentBlock->nNonce = randomNumber() ;

            LogPrintf(
                "Running MiningThread (%d) with %u transactions in block (%u bytes)%s%s\n",
                numberOfThread,
                currentBlock->vtx.size(),
                ::GetSerializeSize( *currentBlock, SER_NETWORK, PROTOCOL_VERSION ),
                ( verbose ? strprintf( ", looking for scrypt hash <= %s", solutionHash.GetHex() ) : "" ),
                ( verbose ? strprintf( ", random initial nonce 0x%x", currentBlock->nNonce ) : "" )
            ) ;

            while ( true )
            {
                bool found = false ;
                while ( ! found ) // scan nonces
                {
                    currentBlock->nNonce ++ ;
                    noncesScanned ++ ;

                    arith_uint256 arithPowHash = UintToArith256( currentBlock->GetScryptHash() ) ;
                    if ( arithPowHash < smallestScryptHashBlock ) smallestScryptHashBlock = arithPowHash ;

                    if ( CheckProofOfWork( *currentBlock, solutionBits, consensus ) )
                    {   // found a solution
                        found = true ; break ;
                    }

                    // not found after trying for a while
                    if ( ( currentBlock->nNonce & 0xfff ) == 0 )
                        break ;

                    if ( finished || recreateBlock ) break ;
                }

                if ( smallestScryptHashBlock < smallestScryptHashAll )
                        smallestScryptHashAll = smallestScryptHashBlock ;

                if ( found ) // found a solution
                {
                    std::string proofOfWorkFound = strprintf( "MiningThread (%d):\n", numberOfThread ) ;
                    proofOfWorkFound += strprintf( "proof-of-work found with nonce 0x%x\n", currentBlock->nNonce ) ;
                    proofOfWorkFound += strprintf( "   scrypt hash %s\n   <= solution %s\n",
                                                    currentBlock->GetScryptHash().GetHex(), solutionHash.GetHex() ) ;
                    if ( NameOfChain() == "inu" ) {
                        proofOfWorkFound += strprintf( "   sha256 hash %s\n   <= solution %s\n",
                                                        currentBlock->GetSha256Hash().GetHex(), ( solutionHash << 1 ).GetHex() ) ;
                        proofOfWorkFound += strprintf( "   lyra2re2 hash %s\n    <=  solution %s\n",
                                                        currentBlock->GetLyra2Re2Hash().GetHex(), solutionHash.GetHex() ) ;
                    }

                    LogPrintf( proofOfWorkFound ) ;

                    if ( ProcessBlockFound( currentBlock, chainparams ) ) {
                        howManyBlocksWereGeneratedByThisThread ++ ;
                    }

                    coinbaseScript->KeepScript() ;

                    // for regression testing, stop mining after a block is found
                    if ( chainparams.MineBlocksOnDemand() )
                        throw std::string( "stop" ) ;

                    break ;
                }

                if ( finished ) break ;

                if ( recreateBlock )
                    break ;

                // check if block candidate needs to be rebuilt
                if ( pindexPrev != chainActive.Tip() )
                    break ; // new chain's tip
                if ( mempool.GetTransactionsUpdated() != transactionsInMempool
                           && GetTimeMillis() - scanBeginsMillis > 20999 )
                    break ; // new transactions
                if ( GetTimeMillis() - scanBeginsMillis > 20 * 60000 )
                    break ; // too long
                if ( ! g_connman->hasConnectedNodes() && chainparams.MiningRequiresPeers() )
                    break ; // no peers connected

                // update block's time
                int64_t deltaTime = UpdateTime( currentBlock, consensus, pindexPrev ) ;

                // recreate the block if the clock has run backwards, to get the actual time
                if ( deltaTime < 0 ) break ;

                // changing block's time can change proof-of-work bits
                if ( solutionBits != currentBlock->nBits ) {
                    solutionBits = currentBlock->nBits ;
                    solutionHash.SetCompact( solutionBits ) ;
                }

                // next nonce is random
                currentBlock->nNonce = randomNumber() ;
            }

            if ( verbose )
                LogPrintf( "MiningThread (%d) scanned %s\n", numberOfThread, threadMiningInfoString() ) ;

            allNoncesByThread += noncesScanned ;
            noncesScanned = 0 ;
        }
    } catch ( const std::string & s ) {
        if ( s == "stop" ) {
            endOfThread() ;
            return ;
        } else throw ;
    } catch ( const std::runtime_error & e ) {
        LogPrintf( "MiningThread (%d) runtime error: %s\n", numberOfThread, e.what() ) ;
        endOfThread() ;
        return ;
    }
}

std::string MiningThread::threadMiningInfoString( bool withSmallestHashes ) const
{
    std::string aboutSmallestHashBlock ;
    std::string aboutSmallestHashAll ;
    if ( withSmallestHashes ) {
        aboutSmallestHashBlock = " with smallest scrypt hash " + smallestScryptHashBlock.GetHex() ;
        aboutSmallestHashAll = " smallest scrypt hash ever " + smallestScryptHashAll.GetHex() ;
    }
    return strprintf (
        "%d nonces for current block candidate (%.3f nonces/s)%s, %ld nonces overall (%.3f nonces/s)%s",
        howManyNoncesAreTriedForCurrentBlock(), getBlockNoncesPerSecond(), aboutSmallestHashBlock,
        howManyNoncesAreEverTriedByThisThread(), getAllNoncesPerSecond(), aboutSmallestHashAll
    ) ;
}

bool MiningThread::assembleNewBlockCandidate()
{
    if ( currentCandidate != nullptr ) currentCandidate.reset() ;

    if ( coinbaseScript == nullptr || coinbaseScript->reserveScript.empty() )
        return false ;

    try {
        std::unique_ptr< BlockAssembler > assembler( new BlockAssembler( chainparams ) ) ;
        currentCandidate = assembler->CreateNewBlock( coinbaseScript->reserveScript ) ;
        if ( currentCandidate == nullptr )
            return false ;
    } catch ( const std::runtime_error & e ) {
        return false ;
    }

    return true ;
}

static std::vector < std::unique_ptr< MiningThread > > miningThreads ;
static std::mutex miningThreads_mutex ;
static std::string currentWayForNewCoins = "maximum" ;

const MiningThread * const getMiningThreadByNumber( size_t number )
{
    for ( auto const & thread : miningThreads )
        if ( thread->getNumberOfThread() == number )
            return thread.get() ;

    return nullptr ;
}

size_t HowManyMiningThreads()
{
    std::lock_guard < std::mutex > lock( miningThreads_mutex ) ;
    return miningThreads.size() ;
}

void MiningThread::endOfThread( bool bin )
{
    finished = true ;
    currentCandidate.reset() ;
    theThread.join() ;
    LogPrintf( "MiningThread (%d) finished\n", numberOfThread ) ;

    if ( bin ) {
        std::lock_guard < std::mutex > lock( miningThreads_mutex ) ;
        for ( std::vector< std::unique_ptr< MiningThread > >::const_iterator it = miningThreads.begin() ;
                it != miningThreads.end() ; ++ it )
            if ( this == it->get() ) {  miningThreads.erase( it ) ; break ;  }
    }
}

void GenerateCoins( bool generate, int nThreads, const CChainParams & chainparams )
{
    {
        std::lock_guard < std::mutex > lock( miningThreads_mutex ) ;
        miningThreads.clear() ;
    }

    if ( nThreads < 0 )
        nThreads = GetNumCores() ;

    if ( nThreads == 0 || ! generate )
        return ;

    int64_t sizeOfKeypool = GetArg( "-keypool", DEFAULT_KEYPOOL_SIZE ) ;
    if ( nThreads > sizeOfKeypool )
        nThreads = sizeOfKeypool ;

    {
        std::lock_guard < std::mutex > lock( miningThreads_mutex ) ;
        for ( unsigned int i = 1 ; i <= nThreads ; i++ )
            miningThreads.push_back( std::unique_ptr< MiningThread >( new MiningThread( i, chainparams ) ) ) ;
    }

    ChangeKindOfHowManyCoinsToGenerate( currentWayForNewCoins ) ;
}

void ChangeKindOfHowManyCoinsToGenerate( const std::string & kind )
{
    {
        std::lock_guard < std::mutex > lock( miningThreads_mutex ) ;

        for ( std::unique_ptr< MiningThread > & th : miningThreads )
            th->setKindOfHowManyCoinsToGenerate( kind ) ;
    }

    currentWayForNewCoins = kind ;
}

CAmount GetCurrentNewBlockSubsidy()
{
    return GetDogecoinBlockSubsidy(
                chainActive.Tip()->nHeight + 1,
                Params().GetConsensus( chainActive.Tip()->nHeight + 1 ),
                chainActive.Tip()->GetBlockSha256Hash() ) ;
}
