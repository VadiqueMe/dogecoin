// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_CONSENSUSLIB_H
#define DOGECOIN_CONSENSUSLIB_H

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

typedef enum consensus_script_error_t
{
    consensus_SCRIPT_ERR_OK = 0,
    consensus_SCRIPT_ERR_TX_INDEX,
    consensus_SCRIPT_ERR_TX_SIZE_MISMATCH,
    consensus_SCRIPT_ERR_TX_DESERIALIZE,
    consensus_segwit_SCRIPT_ERR_AMOUNT_REQUIRED,
    consensus_SCRIPT_ERR_INVALID_FLAGS,
} consensus_script_error ;

/** Script verification flags */
enum
{
    consensus_SCRIPT_FLAGS_VERIFY_NONE                = 0,
    consensus_SCRIPT_FLAGS_VERIFY_P2SH                = (1U << 0), // evaluate P2SH (BIP16) subscripts
    consensus_SCRIPT_FLAGS_VERIFY_DERSIG              = (1U << 2), // enforce strict DER (BIP66) compliance
    consensus_SCRIPT_FLAGS_VERIFY_NULLDUMMY           = (1U << 4), // enforce NULLDUMMY (BIP147)
    consensus_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY = (1U << 9), // enable CHECKLOCKTIMEVERIFY (BIP65)
    consensus_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY = (1U << 10), // enable CHECKSEQUENCEVERIFY (BIP112)
    consensus_segwit_SCRIPT_FLAGS_VERIFY_WITNESS      = (1U << 11), // enable WITNESS (BIP141)

    consensus_SCRIPT_FLAGS_VERIFY_ALL                 = consensus_SCRIPT_FLAGS_VERIFY_P2SH |
                                                        consensus_SCRIPT_FLAGS_VERIFY_DERSIG |
                                                        consensus_SCRIPT_FLAGS_VERIFY_NULLDUMMY |
                                                        consensus_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY |
                                                        consensus_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY |
                                                        consensus_segwit_SCRIPT_FLAGS_VERIFY_WITNESS
} ;

/// Returns 1 if the input nIn of the serialized transaction pointed to by
/// txTo correctly spends the scriptPubKey pointed to by scriptPubKey under
/// the additional constraints specified by flags.
/// If not NULL, err will contain an error/success code for the operation
EXPORT_SYMBOL int dogecoinconsensus_verify_script(
                        const unsigned char * scriptPubKey, unsigned int scriptPubKeyLen,
                        const unsigned char * txTo, unsigned int txToLen,
                        unsigned int nIn, unsigned int flags, consensus_script_error * err ) ;

EXPORT_SYMBOL int dogecoinconsensus_verify_script_with_amount(
                        const unsigned char * scriptPubKey, unsigned int scriptPubKeyLen, CAmount amount,
                        const unsigned char * txTo, unsigned int txToLen,
                        unsigned int nIn, unsigned int flags, consensus_script_error * err ) ;

EXPORT_SYMBOL unsigned int consensuslib_version() ;

#ifdef __cplusplus
} // extern "C"
#endif

#undef EXPORT_SYMBOL

#endif
