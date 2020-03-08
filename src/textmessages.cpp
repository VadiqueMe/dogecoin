// Copyright (c) 2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "textmessages.h"

namespace TextMessages {

static std::queue< Message > messages ;

bool hasMoreMessages()
{
    return ! messages.empty() ;
}

Message getNextMessage()
{
    if ( ! hasMoreMessages() ) return Message() ;

    Message next = messages.front() ;
    messages.pop() ;
    return next ;
}

void addMessage( const Message & message )
{
    if ( message.isNull() ) return ;

    messages.push( message ) ;
    LogPrintf( "%s: new text message %s\n", __func__, messages.back().toString() ) ;
}

}
