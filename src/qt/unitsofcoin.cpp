// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "unitsofcoin.h"

#include "chainparamsbase.h"

#include <QStringList>

UnitsOfCoin::UnitsOfCoin( QObject * parent )
    : QAbstractListModel( parent )
    , unitlist( availableUnits() )
{
}

QList< unitofcoin > UnitsOfCoin::availableUnits()
{
    QList< unitofcoin > unitlist ;
    unitlist.append( unitofcoin::Cointoshi ) ;
    unitlist.append( unitofcoin::uCoin ) ;
    unitlist.append( unitofcoin::mCoin ) ;
    unitlist.append( unitofcoin::oneCoin ) ;
    unitlist.append( unitofcoin::kCoin ) ;
    unitlist.append( unitofcoin::theCoin ) ;
    unitlist.append( unitofcoin::MCoin ) ;
    return unitlist ;
}

bool UnitsOfCoin::isUnitOfCoin( int unitInt )
{
    switch ( unitofcoin( unitInt ) )
    {
    case unitofcoin::MCoin :
    case unitofcoin::theCoin :
    case unitofcoin::kCoin :
    case unitofcoin::oneCoin :
    case unitofcoin::mCoin :
    case unitofcoin::uCoin :
    case unitofcoin::Cointoshi :
        return true ;
    default :
        return false ;
    }
}

QString UnitsOfCoin::name( const unitofcoin & unit )
{
    QString nameOfE8Currency = QString::fromStdString( NameOfE8Currency() ) ;
    QString nameOfChain = QString::fromStdString( NameOfChain() ) ;

    switch ( unit )
    {
        case unitofcoin::MCoin : return QString( nameOfChain != "inu" ? "M" : "Mega-" ) + nameOfE8Currency ;
        case unitofcoin::theCoin : return QString::fromStdString( NameOfE12Currency() ) ;
        case unitofcoin::kCoin : return QString( nameOfChain != "inu" ? "k" : "kilo-" ) + nameOfE8Currency ;
        case unitofcoin::oneCoin : return nameOfE8Currency ;
        case unitofcoin::mCoin : return QString( nameOfChain != "inu" ? "m" : "milli-" ) + nameOfE8Currency ;
        case unitofcoin::uCoin : return QString::fromUtf8( nameOfChain != "inu" ? "μ" : "micro-" ) + nameOfE8Currency ;
        case unitofcoin::Cointoshi : return QString( "dogetoshi" ) + ( nameOfChain == "main" ? "" : "::" + nameOfChain ) ;
        default: return QString( "some " ) + nameOfE8Currency ;
    }
}

QString UnitsOfCoin::description( const unitofcoin & unit )
{
    QString nameOfE8Currency = QString::fromStdString( NameOfE8Currency() ) ;
    switch ( unit )
    {
        case unitofcoin::MCoin : return QString( "Mega-Dogecoins (1" THIN_SP_UTF8 "000" THIN_SP_UTF8 "000 " + nameOfE8Currency + ")" ) ;
        case unitofcoin::theCoin : return QString( "Þe Ðogecoins (1" THIN_SP_UTF8 "0000 " + nameOfE8Currency + ")" ) ;
        case unitofcoin::kCoin : return QString( "Kilo-Dogecoins (1" THIN_SP_UTF8 "000 " + nameOfE8Currency + ")" ) ;
        case unitofcoin::oneCoin : return QString( "Dogecoins (1 / 1" THIN_SP_UTF8 "0000 " + UnitsOfCoin::name( unitofcoin::theCoin ) + ")" ) ;
        case unitofcoin::mCoin : return QString( "Milli-Dogecoins (1 / 1" THIN_SP_UTF8 "000 " + nameOfE8Currency + ")" ) ;
        case unitofcoin::uCoin : return QString( "Micro-Dogecoins (1 / 1" THIN_SP_UTF8 "000" THIN_SP_UTF8 "000 " + nameOfE8Currency + ")" ) ;
        case unitofcoin::Cointoshi : return QString( "Dogetoshis (1 / 1" THIN_SP_UTF8 "0000" THIN_SP_UTF8 "0000 " + nameOfE8Currency + ")" ) ;
        default: return QString( "Wow-Some-Dogecoins" ) ;
    }
}

qint64 UnitsOfCoin::factor( const unitofcoin & unit )
{
    switch ( unit )
    {
    case unitofcoin::MCoin :   return 100000000000000 ;
    case unitofcoin::theCoin : return 1000000000000 ;
    case unitofcoin::kCoin :   return 100000000000 ;
    case unitofcoin::oneCoin : return 100000000 ;
    case unitofcoin::mCoin :   return 100000 ;
    case unitofcoin::uCoin :   return 100 ;
    case unitofcoin::Cointoshi : default : return 1 ;
    }
}

int UnitsOfCoin::decimals( const unitofcoin & unit )
{
    switch ( unit )
    {
    case unitofcoin::MCoin : return 14 ;
    case unitofcoin::theCoin : return 12 ;
    case unitofcoin::kCoin : return 11 ;
    case unitofcoin::oneCoin : return 8 ;
    case unitofcoin::mCoin : return 5 ;
    case unitofcoin::uCoin : return 2 ;
    case unitofcoin::Cointoshi : default : return 0 ;
    }
}

QString UnitsOfCoin::format( const unitofcoin & unit, const CAmount & nIn, bool fPlus, SeparatorStyle separators )
{   // not using straight sprintf here to NOT to get localized formatting
    qint64 n = (qint64)nIn ;
    qint64 coin = factor( unit ) ;
    int num_decimals = decimals( unit ) ;
    qint64 n_abs = ( n > 0 ? n : -n ) ;
    qint64 quotient = n_abs / coin ;
    qint64 remainder = n_abs % coin ;
    QString quotient_str = QString::number( quotient ) ;
    QString remainder_str = QString::number( remainder ).rightJustified( num_decimals, '0' ) ;

    // Use SI-style thin space separators as these are locale independent and can't be
    // confused with the decimal marker
    QChar thin_sp( THIN_SP_CP ) ;
    int q_size = quotient_str.size() ;
    const int digitsInGroup = ( unit == unitofcoin::Cointoshi || unit == unitofcoin::theCoin ) ? 4 : 3 ;

    if ( separators == SeparatorStyle::always || ( separators == SeparatorStyle::usual && q_size > ( digitsInGroup + 1 ) ) )
        for ( int i = digitsInGroup ; i < q_size ; i += digitsInGroup )
            quotient_str.insert( q_size - i, thin_sp ) ;

    if ( n < 0 )
        quotient_str.insert( 0, '-' ) ;
    else if ( fPlus && n > 0 )
        quotient_str.insert( 0, '+' ) ;

    if ( unit == unitofcoin::theCoin && ( separators == SeparatorStyle::always || separators == SeparatorStyle::usual ) ) {
        // int remainder_size = remainder_str.size() ; // is equal to num_decimals
        for ( int i = digitsInGroup ; i < num_decimals ; i += digitsInGroup )
            remainder_str.insert( num_decimals - i, QChar( /* ’ */ 0x2019 ) ) ;
    }

    QString result = quotient_str ;
    if ( unit != unitofcoin::Cointoshi )
        result += QString( "." ) + remainder_str ;

    return result ;
}

QString UnitsOfCoin::format( int unitInt, const CAmount & amount, bool plussign, SeparatorStyle separators )
{
    if ( isUnitOfCoin( unitInt ) )
        return UnitsOfCoin::format( unitofcoin( unitInt ), amount, plussign, separators ) ;
    else
        return QString( "unknown unit (" ) + QString::number( unitInt ) + QString( ")" ) ;
}

// NOTE: Using formatWithUnit in an HTML context risks wrapping
// quantities at the thousands separator. More subtly, it also results
// in a standard space rather than a thin space, due to a bug in Qt's
// XML whitespace canonicalisation
//
// Please take care to use formatHtmlWithUnit instead, when appropriate

QString UnitsOfCoin::formatWithUnit( const unitofcoin & unit, const CAmount & amount, bool plussign, SeparatorStyle separators )
{
    return format( unit, amount, plussign, separators ) + QString(" ") + name( unit ) ;
}

QString UnitsOfCoin::formatHtmlWithUnit( const unitofcoin & unit, const CAmount & amount, bool plussign, SeparatorStyle separators )
{
    QString str( formatWithUnit( unit, amount, plussign, separators ) ) ;
    str.replace( QChar( THIN_SP_CP ), QString( THIN_SP_HTML ) ) ;
    return QString( "<span style='white-space: nowrap;'>%1</span>" ).arg( str ) ;
}

bool UnitsOfCoin::parseString( const unitofcoin & unit, const QString & string, CAmount * out )
{
    if ( /* ! isUnitOfCoin( static_cast< int >( unit ) ) || */ string.isEmpty() )
        return false ;

    int num_decimals = decimals( unit ) ;

    const QString & nameOfUnit = name( unit ) ;
    QString value = string ;
    if ( value.contains( nameOfUnit ) )
        value = value.remove( nameOfUnit ) ; // value is the string without unit's name

    QStringList parts = removeSpaces( value ).remove( QChar( /* ’ */ 0x2019 ) ).split( "." ) ;

    if ( parts.size() > 2 )
        return false ; // more than one dot

    QString whole = parts[ 0 ] ;
    QString decimals ;
    if ( parts.size() > 1 ) decimals = parts[ 1 ] ;

    if ( decimals.size() > num_decimals )
        return false ; // exceeds max precision

    bool ok = false ;
    QString str = whole + decimals.leftJustified( num_decimals, '0' ) ;

    if ( str.size() > 18 )
        return false ; // longer numbers will exceed 63 bits

    CAmount retvalue( str.toLongLong( &ok ) ) ;

    if ( out != nullptr ) *out = retvalue ;

    return ok ;
}

int UnitsOfCoin::rowCount( const QModelIndex & parent ) const
{
    Q_UNUSED( parent ) ;
    return unitlist.size() ;
}

QVariant UnitsOfCoin::data( const QModelIndex & index, int role ) const
{
    int row = index.row() ;
    if ( row >= 0 && row < unitlist.size() )
    {
        unitofcoin unit = unitlist.at( row ) ;
        switch ( role )
        {
        case Qt::EditRole:
        case Qt::DisplayRole:
            return QVariant( name( unit ) ) ;
        case Qt::ToolTipRole:
            return QVariant( description( unit ) ) ;
        case UnitRole:
            return QVariant( static_cast< int >( unit ) ) ;
        }
    }
    return QVariant() ;
}

CAmount UnitsOfCoin::maxMoney()
{
    return MAX_MONEY ;
}
