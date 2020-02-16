// Copyright (c) 2019-2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_UTILSTR_H
#define DOGECOIN_UTILSTR_H

#include <string>

double stringToDouble( const std::string & s ) ;

std::string substringBetween( const std::string & in, const std::string & begin, const std::string & end ) ;

std::string toStringWithOrdinalSuffix( unsigned int number ) ;

#endif
