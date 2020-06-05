// Copyright (c) 2020 vadique
// Distributed under the WTFPLv2 software license http://www.wtfpl.net

#include "utilthread.h"

#if defined(HAVE_CONFIG_H)
#include "config/dogecoin-config.h"
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h> // for prctl, PR_SET_NAME, PR_GET_NAME
#endif

#if (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
#include <pthread.h>
#include <pthread_np.h>
#endif

void RenameThread( const std::string & name )
{
#if defined(PR_SET_NAME)
    // only the first 15 characters are used (plus 16th as nul terminator)
    ::prctl( PR_SET_NAME, name.c_str(), 0, 0, 0 ) ;
#elif (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
    pthread_set_name_np( pthread_self(), name.c_str() ) ;
#elif defined(MAC_OSX)
    pthread_setname_np( name.c_str() ) ;
#else
    ( void ) name ; // not used
#endif
}

void JoinAll( std::vector< std::thread > & threads )
{
    for ( std::thread & thread : threads )
        if ( thread.joinable() ) thread.join() ;
}

// change it to true for a clean exit
std::atomic < bool > fRequestedShutdown { false } ;

bool ShutdownRequested()
{
    return fRequestedShutdown ;
}

void RequestShutdown()
{
    fRequestedShutdown = true ;
}

void RejectShutdown()
{
    if ( fRequestedShutdown )
        fRequestedShutdown = false ;
}
