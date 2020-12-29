// Copyright (c) 2019-2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "amount.h"

#include "chainparamsbase.h"

std::string NameOfE8Currency()
{
    std::string nameOfCurrency( "DOGE" ) ;

    if ( NameOfChain() == "inu" ) nameOfCurrency = "i" + nameOfCurrency ;
    else if ( NameOfChain() != "main" ) nameOfCurrency += NameOfChain() ;

    return nameOfCurrency ;
}

std::string NameOfE12Currency()
{
    std::string nameOfCurrency = NameOfE8Currency() ;

    size_t at = nameOfCurrency.find( "D" ) ;
    if ( at != std::string::npos )
        nameOfCurrency.replace( at, 1, "Ð" ) ; // Unicode Character “Ð” (U+00D0)

    return nameOfCurrency ;
}
