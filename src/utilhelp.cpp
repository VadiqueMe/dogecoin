// Copyright (c) 2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "utilstrencodings.h" // FormatParagraph

static const int screenWidth = 79 ;
static const int optIndent = 2 ;
static const int msgIndent = 7 ;

std::string HelpMessageOpt( const std::string & option, const std::string & message )
{
    return std::string( optIndent, ' ' ) + std::string( option ) + std::string( "\n" )
                + std::string( msgIndent, ' ' )
                    + FormatParagraph( message, screenWidth - msgIndent, msgIndent )
                        + std::string( "\n\n" ) ;
}
