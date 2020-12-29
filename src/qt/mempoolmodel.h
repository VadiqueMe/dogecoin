// Copyright (c) 2020-2021 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_QT_MEMPOOLMODEL_H
#define DOGECOIN_QT_MEMPOOLMODEL_H

#include <QAbstractTableModel>
#include <QString>
#include <QStringList>

#include "amount.h"
#include "arith_uint256.h"

class CTxMemPoolEntry ;

class QTableView ;

class MempoolTableRow
{
public:
    double priority ;
    int64_t time ;
    arith_uint256 hash ;
    CAmount credit ;
    CAmount fee ;

    MempoolTableRow( double priorityIn, int64_t timeIn, const arith_uint256 & hashIn, CAmount creditIn, CAmount feeIn )
        : priority( priorityIn )
        , time( timeIn )
        , hash( hashIn )
        , credit( creditIn )
        , fee( feeIn )
    {}
} ;

class MempoolRowLessThan
{
public:
    MempoolRowLessThan( int sortColumn, Qt::SortOrder sortOrder ) : column( sortColumn ), order( sortOrder ) {}
    bool operator()( const MempoolTableRow & left, const MempoolTableRow & right ) const ;

private:
    int column ;
    Qt::SortOrder order ;
} ;

/**
   Qt model providing information about tx memory pool
 */
class MempoolModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit MempoolModel( QTableView * parent = nullptr ) ;
    ~MempoolModel() ;

    enum ColumnIndex {
        Priority = 0,
        Time = 1,
        Hash = 2,
        Credit = 3,
        Fee = 4
    } ;

    /** overridden from QAbstractTableModel */
    int rowCount( const QModelIndex & parent ) const ;
    int columnCount( const QModelIndex & parent ) const ;
    QVariant data( const QModelIndex & index, int role ) const ;
    QVariant headerData( int section, Qt::Orientation orientation, int role ) const ;
    QModelIndex index( int row, int column, const QModelIndex & parent ) const ;
    Qt::ItemFlags flags( const QModelIndex & index ) const ;
    void sort( int column, Qt::SortOrder order ) ;

    bool isEmpty() const ;

    static double calculatePriority( const CTxMemPoolEntry * entry ) ;

public Q_SLOTS:
    void refresh() ;

private:
    QStringList columns ;

    int sortColumn ;
    Qt::SortOrder sortOrder ;

    std::vector< MempoolTableRow > tableRows ;

} ;

#endif
