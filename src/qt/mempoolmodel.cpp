// Copyright (c) 2020-2021 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "mempoolmodel.h"

#include "primitives/transaction.h"
#include "txmempool.h"
#include "validation.h"

#include "unitsofcoin.h"
#include "tinyformat.h" // strprintf
#include "utiltime.h" // GetTime, DateTimeStrFormat

#include <QTableView>

bool MempoolRowLessThan::operator()( const MempoolTableRow & left, const MempoolTableRow & right ) const
{
    const MempoolTableRow * pLeft = &left ;
    const MempoolTableRow * pRight = &right ;

    if ( order == Qt::DescendingOrder )
        std::swap( pLeft, pRight ) ;

    switch ( column )
    {
        case MempoolModel::ColumnIndex::Priority:
            return pLeft->priority < pRight->priority ;
        case MempoolModel::ColumnIndex::Time:
            return pLeft->time < pRight->time ;
        case MempoolModel::ColumnIndex::Hash:
            return pLeft->hash < pRight->hash ;
        case MempoolModel::ColumnIndex::Credit:
            return pLeft->credit < pRight->credit ;
        case MempoolModel::ColumnIndex::Fee:
            return pLeft->fee < pRight->fee ;
    }

    return false ;
}

MempoolModel::MempoolModel( QTableView * parent )
    : QAbstractTableModel( parent )
    , columns()
    , sortColumn( 0 )
    , sortOrder( Qt::AscendingOrder )
{
    columns << "Priority" << "Time" << "Hash" << "Credit" << "Fee" ;
    refresh() ;
}

MempoolModel::~MempoolModel()
{
}

int MempoolModel::rowCount( const QModelIndex & parent ) const
{
    Q_UNUSED( parent ) ;
    return tableRows.size() ;
}

int MempoolModel::columnCount( const QModelIndex & parent ) const
{
    Q_UNUSED( parent ) ;
    return columns.length() ;
}

QVariant MempoolModel::data( const QModelIndex & index, int role ) const
{
    if ( ! index.isValid() ) return QVariant() ;

    MempoolTableRow * rowData = static_cast< MempoolTableRow * >( index.internalPointer() ) ;
    if ( rowData == nullptr ) return QVariant() ;

    if ( role == Qt::DisplayRole )
    {
        switch ( index.column() )
        {
        case Priority:
            return QString::fromStdString( strprintf( "%.1f", rowData->priority ) ) ;
        case Time:
        {
            std::string strToday = DateTimeStrFormat( "%Y-%m-%d", GetTime() ) ;
            std::string strTxDate = DateTimeStrFormat( "%Y-%m-%d", rowData->time ) ;
            if ( strToday.compare( strTxDate ) == 0 )
                return QString::fromStdString( DateTimeStrFormat( "%H:%M:%S", rowData->time ) ) ;
            else
                return QString::fromStdString( DateTimeStrFormat( "%Y-%m-%d %H:%M:%S", rowData->time ) ) ;
        }
        case Hash:
            return QString::fromStdString( rowData->hash.ToString() ) ;
        case Credit:
            return UnitsOfCoin::format( unitofcoin::oneCoin, rowData->credit ) ;
        case Fee:
            return UnitsOfCoin::format( unitofcoin::oneCoin, rowData->fee ) ;
        }
    }

    return QVariant() ;
}

QVariant MempoolModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( orientation == Qt::Horizontal ) {
        if ( role == Qt::DisplayRole && section < columns.size() ) {
            QString header = columns[ section ] ;
            if ( section == Credit || section == Fee )
               header += QString( " (" ) + UnitsOfCoin::name( unitofcoin::oneCoin ) + QString( ")" ) ;

            return header ;
        }
    }
    return QVariant() ;
}

Qt::ItemFlags MempoolModel::flags( const QModelIndex & index ) const
{
    if ( ! index.isValid() ) return 0 ;

    Qt::ItemFlags ret = Qt::ItemIsSelectable | Qt::ItemIsEnabled ;
    return ret ;
}

QModelIndex MempoolModel::index( int row, int column, const QModelIndex & parent ) const
{
    Q_UNUSED( parent ) ;

    if ( row < tableRows.size() )
    {
        MempoolTableRow * ptr = const_cast< MempoolTableRow * >( &tableRows[ row ] ) ;
        if ( ptr != nullptr )
            return createIndex( row, column, ptr ) ;
    }

    return QModelIndex() ;
}

bool MempoolModel::isEmpty() const
{
    return tableRows.size() == 0 ;
}

void MempoolModel::refresh()
{
    Q_EMIT layoutAboutToBeChanged() ;

    tableRows.clear() ;

    // collect info about mempool entries
    CTxMemPool::indexed_transaction_set::const_iterator mi = mempool.mapTx.begin() ;
    for ( ; mi != mempool.mapTx.end() ; ++ mi ) {
        const CTxMemPoolEntry * entry = &( *mi ) ;
        tableRows.push_back( MempoolTableRow(
                calculatePriority( entry ),
                entry->GetTime(),
                UintToArith256( entry->GetTx().GetTxHash() ),
                entry->GetTx().GetValueOut(),
                entry->GetFee()
        ) ) ;
    }

    if ( sortColumn >= 0 )
        std::stable_sort( tableRows.begin(), tableRows.end(), MempoolRowLessThan( sortColumn, sortOrder ) ) ;

    Q_EMIT layoutChanged() ;
}

void MempoolModel::sort( int column, Qt::SortOrder order )
{
    sortColumn = column ;
    sortOrder = order ;
    refresh() ;
}

/* static */
double MempoolModel::calculatePriority( const CTxMemPoolEntry * entry )
{
    if ( entry == nullptr ) return 0 ;

    double dPriority = entry->GetPriority( chainActive.Height() ) ;
    CAmount feeDelta ;
    mempool.ApplyDeltas( entry->GetTx().GetTxHash(), dPriority, feeDelta ) ;
    return dPriority ;
}

