// Copyright (c) 2019-2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "utilstr.h"

std::string trimSpaces( const std::string & s )
{
    auto start = s.begin() ;
    while ( start != s.end() && std::isspace( *start ) )
        ++ start ;

    auto end = s.end() ;
    do {
        -- end ;
    } while ( std::distance( start, end ) > 0 && std::isspace( *end ) ) ;

    return std::string( start, end + 1 ) ;
}

double stringToDouble( const std::string & s )
{
    // save current locale
    const auto currentLocale = std::setlocale( LC_NUMERIC, nullptr ) ;
    // use "C" locale for '.' as the radix point
    std::setlocale( LC_NUMERIC, "C" ) ;

    double result = 0 ;

    try {
        result = std::stod( s ) ;
    } catch ( ... ) {
        // revert to current locale and rethrow
        std::setlocale( LC_NUMERIC, currentLocale ) ;
        throw ;
    }

    // revert to current locale
    std::setlocale( LC_NUMERIC, currentLocale ) ;

    return result ;
}

// empty "" as begin delimiter means "from the very first character"
// empty "" as end delimiter means "till the very last character"
// returns empty string if input has no both of specified delimiters, or if input has begin delimiter after end delimiter
std::string substringBetween( const std::string & in, const std::string & begin, const std::string & end )
{
    std::string out( "" ) ;

    size_t first = ( begin.size() > 0 ) ? in.find( begin ) : 0 ;
    size_t last = ( first != std::string::npos )
                        ? ( end.size() > 0 ? in.find( end, first + 1 ) : in.size() ) : std::string::npos ;

    if ( first != std::string::npos && last != std::string::npos ) {
        first += begin.size() ;
        if ( first < last )
            out = in.substr( first, last - first ) ;
    }

    return out ;
}

#include <sstream>

std::string toStringWithOrdinalSuffix( unsigned int number )
{
    unsigned int mod10 = number % 10 ;
    unsigned int mod100 = number % 100 ;

    std::ostringstream result ;
    result << number ;

    if ( mod10 == 1 && mod100 != 11 )
        result << "st";
    else if ( mod10 == 2 && mod100 != 12 )
        result << "nd";
    else if ( mod10 == 3 && mod100 != 13 )
        result << "rd" ;
    else
        result << "th" ;

    return result.str() ;
}
