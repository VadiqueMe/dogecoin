// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_UNITSOFCOIN_H
#define DOGECOIN_QT_UNITSOFCOIN_H

#include "amount.h"

#include <QAbstractListModel>
#include <QString>

// U+2009 THIN SPACE = UTF-8 E2 80 89
#define REAL_THIN_SP_CP 0x2009
#define REAL_THIN_SP_UTF8 "\xE2\x80\x89"
#define REAL_THIN_SP_HTML "&thinsp;"

// U+200A HAIR SPACE = UTF-8 E2 80 8A
#define HAIR_SP_CP 0x200A
#define HAIR_SP_UTF8 "\xE2\x80\x8A"
#define HAIR_SP_HTML "&#8202;"

// U+2006 SIX-PER-EM SPACE = UTF-8 E2 80 86
#define SIXPEREM_SP_CP 0x2006
#define SIXPEREM_SP_UTF8 "\xE2\x80\x86"
#define SIXPEREM_SP_HTML "&#8198;"

// U+2007 FIGURE SPACE = UTF-8 E2 80 87
#define FIGURE_SP_CP 0x2007
#define FIGURE_SP_UTF8 "\xE2\x80\x87"
#define FIGURE_SP_HTML "&#8199;"

// QMessageBox seems to have a bug whereby it doesn't display thin/hair spaces
// correctly.  Workaround is to display a space in a small font.  If you
// change this, please test that it doesn't cause the parent span to start
// wrapping.
#define HTML_HACK_SP "<span style='white-space: nowrap; font-size: 6pt'> </span>"

// Define THIN_SP_* variables to be our preferred type of thin space
#define THIN_SP_CP   REAL_THIN_SP_CP
#define THIN_SP_UTF8 REAL_THIN_SP_UTF8
#define THIN_SP_HTML HTML_HACK_SP

enum class unitofcoin
{
    MCoin = 3,
    theCoin = 10,
    kCoin = 2,
    oneCoin = 0,
    mCoin = 4,
    uCoin = 5,
    Cointoshi = 1
} ;

enum class SeparatorStyle
{
    never,
    usual,
    always
} ;

/* Units of coin definitions. Encapsulates parsing and formatting
   and serves as list model for drop-down selection boxes
*/
class UnitsOfCoin: public QAbstractListModel
{
    Q_OBJECT

public:
    explicit UnitsOfCoin( QObject * parent ) ;

    // get list of units, for drop-down box
    static QList< unitofcoin > availableUnits() ;

    // does this int represent a unit?
    static bool isUnitOfCoin( int unitInt ) ;

    // short name
    static QString name( const unitofcoin & unit ) ;
    // longer description
    static QString description( const unitofcoin & unit ) ;
    // number of atomary coin units per this unit
    static qint64 factor( const unitofcoin & unit ) ;
    // number of decimals left
    static int decimals( const unitofcoin & unit ) ;

    // format as string
    static QString format( const unitofcoin & unit, const CAmount & amount, bool plussign = false, SeparatorStyle separators = SeparatorStyle::usual ) ;
    static QString format( int unitInt, const CAmount & amount, bool plussign = false, SeparatorStyle separators = SeparatorStyle::usual ) ;
    // format as string with unit
    static QString formatWithUnit( const unitofcoin & unit, const CAmount & amount, bool plussign = false, SeparatorStyle separators = SeparatorStyle::usual ) ;
    // format as HTML string with unit
    static QString formatHtmlWithUnit( const unitofcoin & unit, const CAmount & amount, bool plussign = false, SeparatorStyle separators = SeparatorStyle::usual ) ;
    // parse string to coin amount
    static bool parseString( const unitofcoin & unit, const QString & string, CAmount * out ) ;

    // list model for unit drop-down selection box
    enum RoleIndex {
        /** Unit identifier */
        UnitRole = Qt::UserRole
    } ;

    int rowCount( const QModelIndex & parent ) const;
    QVariant data( const QModelIndex & index, int role ) const ;

    static QString removeSpaces( QString text )
    {
        text.remove( ' ' ) ;
        text.remove( QChar( THIN_SP_CP ) ) ;
#if (THIN_SP_CP != REAL_THIN_SP_CP)
        text.remove( QChar( REAL_THIN_SP_CP ) ) ;
#endif
        return text ;
    }

    // return maximum number of atomary coin units
    static CAmount maxMoney() ;

private:
    QList < unitofcoin > unitlist ;

} ;

#endif
