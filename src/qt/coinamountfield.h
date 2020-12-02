// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_COINAMOUNTFIELD_H
#define DOGECOIN_QT_COINAMOUNTFIELD_H

#include "amount.h"
#include "unitsofcoin.h"

#include <QWidget>

class AmountSpinBox ;

QT_BEGIN_NAMESPACE
class QValueComboBox ;
QT_END_NAMESPACE

/** Widget for entering coin amounts
  */
class CoinAmountField: public QWidget
{
    Q_OBJECT

    // ugly hack: for some unknown reason CAmount (instead of qint64) does not work here as expected
    // discussion: https://github.com/bitcoin/bitcoin/pull/5117
    Q_PROPERTY(qint64 value READ value WRITE setValue NOTIFY valueChanged USER true)

public:
    explicit CoinAmountField( QWidget * parent = nullptr ) ;

    CAmount value( bool * valueOk = nullptr ) const ;

    void setValue( const CAmount & value ) ;

    /** Sets a maximum value **/
    void setMaximumValue( const CAmount & max ) ;

    CAmount getMaximumValue() const ;

    /** Set single step in atomary coin units or by chosen unit **/
    void setSingleStep( const CAmount & step ) ;
    void setSingleStep( unitofcoin unit ) ;

    /** Make read-only **/
    void setReadOnly( bool fReadOnly ) ;

    /** Mark current value as invalid in UI */
    void setValid( bool valid ) ;
    /** Perform input validation, mark field as invalid if entered value is not valid */
    bool validate() ;

    /** Change unit used for amount */
    void setUnitOfCoin( unitofcoin unit ) ;

    /** Make field empty and ready for new input */
    void clear() ;

    /** Enable/Disable */
    void setEnabled( bool fEnabled ) ;

    /** Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907),
        in these cases we have to set it up manually */
    QWidget * setupTabChain( QWidget * prev ) ;

Q_SIGNALS:
    void valueEdited( qint64 val ) ; // only when edited by the user
    void valueChanged( qint64 val ) ; // edited by the user or set programmatically
    void unitChanged( unitofcoin unit ) ; // new unit selected

protected:
    /** Intercept focus-in event and ',' key presses */
    bool eventFilter( QObject * object, QEvent * event ) ;

private:
    AmountSpinBox * amount ;
    QValueComboBox * unitComboBox ;

private Q_SLOTS:
    void amountEdited() ;
    void amountChanged() ;
    void unitIndexChanged( int idx ) ;

};

#endif
