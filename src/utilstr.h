// Copyright (c) 2019-2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_UTILSTR_H
#define DOGECOIN_UTILSTR_H

#include <string>

#include <boost/signals2/signal.hpp>

/** Signals for translation */
class CTranslationInterface
{
public:
    /** Translate a message to the native language of the user */
    boost::signals2::signal< std::string ( const char * text ) > Translate ;
} ;

extern CTranslationInterface translationInterface ;

/**
 * Translation function: Call Translate signal on UI interface, which returns a boost::optional result.
 * If no translation slot is registered, nothing is returned, and simply return the input
 */
inline std::string _( const char * text )
{
    boost::optional< std::string > translated = translationInterface.Translate( text ) ;
    return translated ? ( *translated ) : text ;
}

std::string trimSpaces( const std::string & s ) ;

double stringToDouble( const std::string & s ) ;

std::string substringBetween( const std::string & in, const std::string & begin, const std::string & end ) ;

std::string toStringWithOrdinalSuffix( unsigned int number ) ;

#endif
