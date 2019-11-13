// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "unitsofcoin.h"

#include "primitives/transaction.h"

#include <QStringList>

UnitsOfCoin::UnitsOfCoin( QObject * parent )
    : QAbstractListModel( parent )
    , unitlist( availableUnits() )
{
}

QList< UnitsOfCoin::Unit > UnitsOfCoin::availableUnits()
{
    QList< UnitsOfCoin::Unit > unitlist ;
    unitlist.append( Cointoshi ) ;
    unitlist.append( uCoin ) ;
    unitlist.append( mCoin ) ;
    unitlist.append( oneCoin ) ;
    unitlist.append( kCoin ) ;
    unitlist.append( MCoin ) ;
    return unitlist ;
}

bool UnitsOfCoin::isOk( int unit )
{
    switch ( unit )
    {
    case MCoin :
    case kCoin :
    case oneCoin :
    case mCoin :
    case uCoin :
    case Cointoshi :
        return true ;
    default :
        return false ;
    }
}

QString UnitsOfCoin::name( int unit )
{
    switch ( unit )
    {
    case MCoin : return QString( "MDOGE" ) ;
    case kCoin : return QString( "kDOGE" ) ;
    case oneCoin : return QString( "DOGE" ) ;
    case mCoin : return QString( "mDOGE" ) ;
    case uCoin : return QString::fromUtf8( "μDOGE" ) ;
    case Cointoshi : return QString( "dogetoshi" ) ;
    default: return QString( "some DOGE" ) ;
    }
}

QString UnitsOfCoin::description( int unit )
{
    switch ( unit )
    {
    case MCoin : return QString( "Mega-Dogecoins (1" THIN_SP_UTF8 "000" THIN_SP_UTF8 "000)" ) ;
    case kCoin : return QString( "Kilo-Dogecoins (1" THIN_SP_UTF8 "000)" ) ;
    case oneCoin : return QString( "Dogecoins" ) ;
    case mCoin : return QString( "Milli-Dogecoins (1 / 1" THIN_SP_UTF8 "000)" ) ;
    case uCoin : return QString( "Micro-Dogecoins (1 / 1" THIN_SP_UTF8 "000" THIN_SP_UTF8 "000)" ) ;
    case Cointoshi : return QString( "Dogetoshis (1 / 1" THIN_SP_UTF8 "0000" THIN_SP_UTF8 "0000)" ) ;
    default: return QString( "Wow-Dogecoins (???)" ) ;
    }
}

qint64 UnitsOfCoin::factor( int unit )
{
    switch ( unit )
    {
    case MCoin :  return 100000000000000 ;
    case kCoin :  return 100000000000 ;
    case oneCoin :  return 100000000 ;
    case mCoin :  return 100000 ;
    case uCoin :  return 100 ;
    case Cointoshi : default :  return 1 ;
    }
}

int UnitsOfCoin::decimals( int unit )
{
    switch ( unit )
    {
    case MCoin : return 14 ;
    case kCoin : return 11 ;
    case oneCoin : return 8 ;
    case mCoin : return 5 ;
    case uCoin : return 2 ;
    case Cointoshi : default : return 0 ;
    }
}

QString UnitsOfCoin::format( int unit, const CAmount& nIn, bool fPlus, SeparatorStyle separators )
{   // not using straight sprintf here to NOT to get localized formatting

    if ( ! isOk( unit ) )
        return QString( "unknown unit (" ) + QString::number( unit ) + QString( ")" ) ;

    qint64 n = (qint64)nIn;
    qint64 coin = factor(unit);
    int num_decimals = decimals(unit);
    qint64 n_abs = (n > 0 ? n : -n);
    qint64 quotient = n_abs / coin;
    qint64 remainder = n_abs % coin;
    QString quotient_str = QString::number(quotient);
    QString remainder_str = QString::number(remainder).rightJustified(num_decimals, '0');

    // Use SI-style thin space separators as these are locale independent and can't be
    // confused with the decimal marker
    QChar thin_sp(THIN_SP_CP);
    int q_size = quotient_str.size();
    int digitsInGroup = ( unit == Cointoshi ) ? 4 : 3 ;
    if ( separators == separatorAlways || ( separators == separatorStandard && q_size > ( digitsInGroup + 1 ) ) )
        for ( int i = digitsInGroup ; i < q_size ; i += digitsInGroup )
            quotient_str.insert( q_size - i, thin_sp ) ;

    if ( n < 0 )
        quotient_str.insert( 0, '-' ) ;
    else if ( fPlus && n > 0 )
        quotient_str.insert( 0, '+' ) ;

    QString result = quotient_str ;
    if ( unit != Cointoshi )
        result += QString( "." ) + remainder_str ;

    return result ;
}


// NOTE: Using formatWithUnit in an HTML context risks wrapping
// quantities at the thousands separator. More subtly, it also results
// in a standard space rather than a thin space, due to a bug in Qt's
// XML whitespace canonicalisation
//
// Please take care to use formatHtmlWithUnit instead, when
// appropriate

QString UnitsOfCoin::formatWithUnit( int unit, const CAmount& amount, bool plussign, SeparatorStyle separators )
{
    return format( unit, amount, plussign, separators ) + QString(" ") + name( unit ) ;
}

QString UnitsOfCoin::formatHtmlWithUnit( int unit, const CAmount& amount, bool plussign, SeparatorStyle separators )
{
    QString str(formatWithUnit(unit, amount, plussign, separators));
    str.replace(QChar(THIN_SP_CP), QString(THIN_SP_HTML));
    return QString("<span style='white-space: nowrap;'>%1</span>").arg(str);
}


bool UnitsOfCoin::parse( int unit, const QString &value, CAmount *val_out )
{
    if ( ! isOk( unit ) || value.isEmpty() )
        return false ; // don't parse unknown unit or empty string

    int num_decimals = decimals(unit);

    // Ignore spaces and thin spaces when parsing
    QStringList parts = removeSpaces(value).split(".");

    if ( parts.size() > 2 )
        return false ; // more than one dot

    QString whole = parts[0];
    QString decimals;

    if(parts.size() > 1)
    {
        decimals = parts[1];
    }
    if(decimals.size() > num_decimals)
    {
        return false; // Exceeds max precision
    }
    bool ok = false;
    QString str = whole + decimals.leftJustified(num_decimals, '0');

    if(str.size() > 18)
    {
        return false; // Longer numbers will exceed 63 bits
    }
    CAmount retvalue(str.toLongLong(&ok));
    if(val_out)
    {
        *val_out = retvalue;
    }
    return ok;
}

int UnitsOfCoin::rowCount( const QModelIndex & parent ) const
{
    Q_UNUSED(parent);
    return unitlist.size();
}

QVariant UnitsOfCoin::data( const QModelIndex & index, int role ) const
{
    int row = index.row();
    if(row >= 0 && row < unitlist.size())
    {
        Unit unit = unitlist.at(row);
        switch(role)
        {
        case Qt::EditRole:
        case Qt::DisplayRole:
            return QVariant(name(unit));
        case Qt::ToolTipRole:
            return QVariant(description(unit));
        case UnitRole:
            return QVariant(static_cast<int>(unit));
        }
    }
    return QVariant();
}

CAmount UnitsOfCoin::maxMoney()
{
    return MAX_MONEY ;
}
