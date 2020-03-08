// Copyright (c) 2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#ifndef DOGECOIN_TEXTMESSAGES_H
#define DOGECOIN_TEXTMESSAGES_H

#include <string>
#include <queue>
#include <sstream>

#include "net.h"

namespace TextMessages {

class Message
{

public:

    Message( const std::string & message, NodeId from, int64_t time )
        : text( message )
        , fromNode( from )
        , timeReceived( time )
    {}

    Message() : text( "" ), fromNode( -1 ), timeReceived( -1 ) {}

    bool isNull() const {  return text.empty() ;  }

    const std::string & getText() const {  return text ;  }
    NodeId getFromNodeId() const {  return fromNode ;  }
    int64_t getTimeReceived() const {  return timeReceived ;  }

    std::string toString() const
    {
        std::ostringstream ss ;
        ss << "\"" << getText() << "\"" ;
        ss << " " << "from peer=" << getFromNodeId() << " at " << getTimeReceived() ;
        return ss.str() ;
    }

private:

    std::string text ;
    NodeId fromNode ;
    int64_t timeReceived ;

} ;

bool hasMoreMessages() ;

Message getNextMessage() ;

void addMessage( const Message & message ) ;

}

#endif
