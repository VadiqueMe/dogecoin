// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "coinamountfield.h"

#include "unitsofcoin.h"
#include "guiconstants.h"
#include "qvaluecombobox.h"

#include <QApplication>
#include <QAbstractSpinBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>

/* QSpinBox that uses fixed-point numbers internally with specific formatting/parsing stuff
 */
class AmountSpinBox: public QAbstractSpinBox
{
    Q_OBJECT

public:
    explicit AmountSpinBox( QWidget * parent ) : QAbstractSpinBox( parent )
        , currentUnit( UnitsOfCoin::oneCoin )
        , singleStep( UnitsOfCoin::factor( currentUnit ) )
        , maximumValue( UnitsOfCoin::maxMoney() )
    {
        setAlignment( Qt::AlignRight ) ;

        connect( lineEdit(), SIGNAL( textEdited(QString) ), this, SIGNAL( textOfValueEdited() ) ) ;
    }

    QValidator::State validate( QString & text, int & pos ) const
    {
        if ( text.isEmpty() )
            return QValidator::Intermediate ;

        bool ok = false;
        parse( text, &ok ) ;

        /* return Intermediate so that fixup() is called on defocus */
        return ok ? QValidator::Intermediate : QValidator::Invalid ;
    }

    void fixup( QString & input ) const
    {
        bool ok = false ;
        CAmount val = parse( input, &ok ) ;
        if ( ok )
        {
            input = UnitsOfCoin::format( currentUnit, val, false, UnitsOfCoin::separatorAlways ) ;
            lineEdit()->setText( input ) ;
        }
    }

    CAmount value( bool * valueOk = nullptr ) const
    {
        return parse( text(), valueOk ) ;
    }

    void setValue( const CAmount& value )
    {
        lineEdit()->setText( UnitsOfCoin::format( currentUnit, value, false, UnitsOfCoin::separatorAlways ) ) ;
        Q_EMIT valueSetByProgram() ;
    }

    CAmount getMaximumValue() const {  return maximumValue ;  }

    void setMaximumValue( const CAmount & max ) {  maximumValue = max ;  }

    void stepBy( int steps )
    {
        bool ok = false ;
        CAmount val = value( &ok ) ;
        val = val + steps * singleStep ;
        val = qMin( qMax( val, CAmount(0) ), maximumValue ) ;
        setValue( val ) ;

        Q_EMIT valueStepped() ;
    }

    void setDisplayUnit(int unit)
    {
        bool ok = false ;
        CAmount val = value( &ok ) ;

        currentUnit = unit ;

        if ( ok ) setValue( val ) ;
        else clear() ;
    }

    void setSingleStep( const CAmount & step ) {  singleStep = step ;  }

    QSize minimumSizeHint() const
    {
        if(cachedMinimumSizeHint.isEmpty())
        {
            ensurePolished();

            const QFontMetrics fm(fontMetrics());
            int h = lineEdit()->minimumSizeHint().height();
            int w = fm.width( UnitsOfCoin::format( UnitsOfCoin::oneCoin, UnitsOfCoin::maxMoney(), false, UnitsOfCoin::separatorAlways ) ) ;
            w += 2; // cursor blinking space

            QStyleOptionSpinBox opt;
            initStyleOption(&opt);
            QSize hint(w, h);
            QSize extra(35, 6);
            opt.rect.setSize(hint + extra);
            extra += hint - style()->subControlRect(QStyle::CC_SpinBox, &opt,
                                                    QStyle::SC_SpinBoxEditField, this).size();
            // get closer to final result by repeating the calculation
            opt.rect.setSize(hint + extra);
            extra += hint - style()->subControlRect(QStyle::CC_SpinBox, &opt,
                                                    QStyle::SC_SpinBoxEditField, this).size();
            hint += extra;
            hint.setHeight(h);

            opt.rect = rect();

            cachedMinimumSizeHint = style()->sizeFromContents(QStyle::CT_SpinBox, &opt, hint, this)
                                    .expandedTo(QApplication::globalStrut());
        }
        return cachedMinimumSizeHint;
    }

private:
    int currentUnit ;
    CAmount singleStep ;
    CAmount maximumValue ;

    mutable QSize cachedMinimumSizeHint ;

    /**
     * Parse a string into a number of base monetary units
     * @note returns 0 if can't parse
     */
    CAmount parse( const QString & text, bool * parseOk = nullptr ) const
    {
        CAmount val = 0 ;
        bool ok = UnitsOfCoin::parse( currentUnit, text, &val ) ;
        if ( ok ) {
            if ( val < 0 || val > UnitsOfCoin::maxMoney() )
                ok = false ;
            else if ( val > maximumValue )
                val = maximumValue ;
        }

        if ( parseOk != nullptr ) *parseOk = ok ;
        return ok ? val : 0 ;
    }

protected:
    bool event(QEvent *event)
    {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Comma)
            {
                // Translate a comma into a period
                QKeyEvent periodKeyEvent(event->type(), Qt::Key_Period, keyEvent->modifiers(), ".", keyEvent->isAutoRepeat(), keyEvent->count());
                return QAbstractSpinBox::event(&periodKeyEvent);
            }
        }
        return QAbstractSpinBox::event(event);
    }

    StepEnabled stepEnabled() const
    {
        if ( isReadOnly() )
            return StepNone ;
        if ( text().isEmpty() ) // enable step-up with empty field
            return StepUpEnabled ;

        StepEnabled steps = 0 ;
        bool ok = false ;
        CAmount val = value( &ok ) ;
        if ( ok )
        {
            if ( val > 0 )
                steps |= StepDownEnabled ;
            if ( val < maximumValue )
                steps |= StepUpEnabled ;
        }
        return steps ;
    }

Q_SIGNALS:
    void textOfValueEdited() ;
    void valueStepped() ;
    void valueSetByProgram() ;
};

#include "coinamountfield.moc"

CoinAmountField::CoinAmountField( QWidget * parent ) : QWidget( parent )
{
    amount = new AmountSpinBox( this ) ;
    amount->setLocale( QLocale::c() ) ;
    amount->installEventFilter( this ) ;
    amount->setMaximumWidth( 170 ) ;

    QHBoxLayout *layout = new QHBoxLayout( this ) ;
    layout->addWidget( amount ) ;
    unit = new QValueComboBox( this ) ;
    unit->setModel( new UnitsOfCoin( this ) ) ;
    layout->addWidget( unit ) ;
    layout->addStretch( 1 ) ;
    layout->setContentsMargins( 0, 0, 0, 0 ) ;

    setLayout( layout ) ;

    setFocusPolicy(Qt::TabFocus);
    setFocusProxy(amount);

    // If one if the widgets changes, the combined content changes as well
    connect( amount, SIGNAL( textOfValueEdited() ), this, SLOT( amountEdited() ) ) ;
    connect( amount, SIGNAL( valueStepped() ), this, SLOT( amountEdited() ) ) ;
    connect( amount, SIGNAL( valueSetByProgram() ), this, SLOT( amountChanged() ) ) ;
    connect( unit, SIGNAL( currentIndexChanged(int) ), this, SLOT( unitChanged(int) ) ) ;

    // Set default based on configuration
    unitChanged( unit->currentIndex() ) ;
}

void CoinAmountField::clear()
{
    amount->clear();
    unit->setCurrentIndex(0);
}

void CoinAmountField::setEnabled(bool fEnabled)
{
    amount->setEnabled(fEnabled);
    unit->setEnabled(fEnabled);
}

bool CoinAmountField::validate()
{
    bool valid = false;
    value(&valid);
    setValid(valid);
    return valid;
}

void CoinAmountField::setValid(bool valid)
{
    if (valid)
        amount->setStyleSheet("");
    else
        amount->setStyleSheet(STYLE_INVALID);
}

bool CoinAmountField::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
    {
        // Clear invalid flag on focus
        setValid(true);
    }
    return QWidget::eventFilter(object, event);
}

QWidget *CoinAmountField::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, amount);
    QWidget::setTabOrder(amount, unit);
    return unit;
}

CAmount CoinAmountField::value( bool * valueOk ) const
{
    return amount->value( valueOk ) ;
}

void CoinAmountField::setValue( const CAmount & value )
{
    amount->setValue( ( value <= getMaximumValue() ) ? value : getMaximumValue() ) ;
}

void CoinAmountField::amountEdited()
{
    bool valueOk = false ;
    CAmount val = value( &valueOk ) ;
    if ( valueOk && val <= getMaximumValue() ) {
        Q_EMIT valueEdited( val ) ;
        Q_EMIT valueChanged( val ) ;
    }
}

void CoinAmountField::amountChanged()
{
    bool valueOk = false ;
    CAmount val = value( &valueOk ) ;
    if ( valueOk && val <= getMaximumValue() ) {
        Q_EMIT valueChanged( val ) ;
    }
}

CAmount CoinAmountField::getMaximumValue() const
{
    return amount->getMaximumValue() ;
}

void CoinAmountField::setMaximumValue( const CAmount & max )
{
    amount->setMaximumValue( max ) ;
}

void CoinAmountField::setReadOnly(bool fReadOnly)
{
    amount->setReadOnly(fReadOnly);
}

void CoinAmountField::unitChanged(int idx)
{
    // Use description tooltip for current unit for the combobox
    unit->setToolTip(unit->itemData(idx, Qt::ToolTipRole).toString());

    // get new unit id
    int newUnit = unit->itemData( idx, UnitsOfCoin::UnitRole ).toInt() ;

    amount->setDisplayUnit(newUnit);
}

void CoinAmountField::setDisplayUnit(int newUnit)
{
    unit->setValue(newUnit);
}

void CoinAmountField::setSingleStep(const CAmount& step)
{
    amount->setSingleStep(step);
}
