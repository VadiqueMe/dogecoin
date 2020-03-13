// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOINCONSENSUS_H
#define DOGECOINCONSENSUS_H

#include "amount.h"

#if defined(BUILD_DOGECOIN_INTERNAL) && defined(HAVE_CONFIG_H)
#include "config/dogecoin-config.h"
  #if defined(_WIN32)
    #if defined(DLL_EXPORT)
      #if defined(HAVE_FUNC_ATTRIBUTE_DLLEXPORT)
        #define EXPORT_SYMBOL __declspec(dllexport)
      #else
        #define EXPORT_SYMBOL
      #endif
    #endif
  #elif defined(HAVE_FUNC_ATTRIBUTE_VISIBILITY)
    #define EXPORT_SYMBOL __attribute__ ((visibility ("default")))
  #endif
#elif defined(MSC_VER) && !defined(STATIC_LIBDOGECOINCONSENSUS)
  #define EXPORT_SYMBOL __declspec(dllimport)
#endif

#ifndef EXPORT_SYMBOL
  #define EXPORT_SYMBOL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DOGECOINCONSENSUS_API_VER 1

typedef enum dogecoinconsensus_error_t
{
    dogecoinconsensus_ERR_OK = 0,
    dogecoinconsensus_ERR_TX_INDEX,
    dogecoinconsensus_ERR_TX_SIZE_MISMATCH,
    dogecoinconsensus_ERR_TX_DESERIALIZE,
    dogecoinconsensus_ERR_AMOUNT_REQUIRED,
    dogecoinconsensus_ERR_INVALID_FLAGS,
} dogecoinconsensus_error ;

/** Script verification flags */
enum
{
    dogecoinconsensus_SCRIPT_FLAGS_VERIFY_NONE                = 0,
    dogecoinconsensus_SCRIPT_FLAGS_VERIFY_P2SH                = (1U << 0), // evaluate P2SH (BIP16) subscripts
    dogecoinconsensus_SCRIPT_FLAGS_VERIFY_DERSIG              = (1U << 2), // enforce strict DER (BIP66) compliance
    dogecoinconsensus_SCRIPT_FLAGS_VERIFY_NULLDUMMY           = (1U << 4), // enforce NULLDUMMY (BIP147)
    dogecoinconsensus_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY = (1U << 9), // enable CHECKLOCKTIMEVERIFY (BIP65)
    dogecoinconsensus_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY = (1U << 10), // enable CHECKSEQUENCEVERIFY (BIP112)
    dogecoinconsensus_SCRIPT_FLAGS_VERIFY_WITNESS             = (1U << 11), // enable WITNESS (BIP141)
    dogecoinconsensus_SCRIPT_FLAGS_VERIFY_ALL                 = dogecoinconsensus_SCRIPT_FLAGS_VERIFY_P2SH |
                                                                dogecoinconsensus_SCRIPT_FLAGS_VERIFY_DERSIG |
                                                                dogecoinconsensus_SCRIPT_FLAGS_VERIFY_NULLDUMMY |
                                                                dogecoinconsensus_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY |
                                                                dogecoinconsensus_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY |
                                                                dogecoinconsensus_SCRIPT_FLAGS_VERIFY_WITNESS
} ;

/// Returns 1 if the input nIn of the serialized transaction pointed to by
/// txTo correctly spends the scriptPubKey pointed to by scriptPubKey under
/// the additional constraints specified by flags.
/// If not NULL, err will contain an error/success code for the operation
EXPORT_SYMBOL int dogecoinconsensus_verify_script(
                        const unsigned char * scriptPubKey, unsigned int scriptPubKeyLen,
                        const unsigned char * txTo, unsigned int txToLen,
                        unsigned int nIn, unsigned int flags, dogecoinconsensus_error * err ) ;

EXPORT_SYMBOL int dogecoinconsensus_verify_script_with_amount(
                        const unsigned char * scriptPubKey, unsigned int scriptPubKeyLen, CAmount amount,
                        const unsigned char * txTo, unsigned int txToLen,
                        unsigned int nIn, unsigned int flags, dogecoinconsensus_error * err ) ;

EXPORT_SYMBOL unsigned int dogecoinconsensus_version() ;

#ifdef __cplusplus
} // extern "C"
#endif

#undef EXPORT_SYMBOL

#endif
