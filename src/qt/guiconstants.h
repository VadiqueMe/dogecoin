// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_GUICONSTANTS_H
#define DOGECOIN_QT_GUICONSTANTS_H

/* Milliseconds between model updates */
static const int MODEL_UPDATE_DELAY = 250 ;

/* AskPassphraseDialog -- Maximum passphrase length */
static const int MAX_PASSPHRASE_SIZE = 1024 ;

/* Size of icons in the bottom bar */
static const int BOTTOMBAR_ICONSIZE = 16 ;

static const bool DEFAULT_SPLASHSCREEN = true ;

/* Invalid field background style */
#define STYLE_INVALID "background:#FF8080"

/* Transaction list -- unconfirmed transaction */
#define COLOR_UNCONFIRMED QColor( 128, 128, 128 )
/* Transaction list -- negative amount */
#define COLOR_NEGATIVE QColor( 255, 0, 0 )
/* Transaction list -- bare address (without label) */
#define COLOR_BAREADDRESS QColor( 140, 140, 140 )
/* Transaction list -- TX status decoration - open until date */
#define COLOR_TX_STATUS_OPENUNTILDATE QColor( 64, 64, 255 )
/* Transaction list -- TX status decoration - abandoned */
#define COLOR_TX_STATUS_ABANDONED QColor( 200, 100, 100 )
/* Transaction list -- TX status decoration - default color */
#define COLOR_BLACK QColor( 0, 0, 0 )

/* Tooltips longer than this (in characters) are converted into rich text, so that they can be word-wrapped */
static const size_t TOOLTIP_WRAP_THRESHOLD = 80 ;

/* Maximum length of URI */
static const size_t MAX_URI_LENGTH = 255 ;

/* QRCodeDialog -- size of exported QR Code image */
static const unsigned int QR_IMAGE_SIZE = 300 ;

/* Number of frames in spinner animation */
#define SPINNER_FRAMES 36

#define QAPP_ORG_NAME "Dogecoin"
#define QAPP_ORG_DOMAIN "dogecoin.org"
#define QAPP_APP_NAME_DEFAULT "Dogecoin-Qt"
#define QAPP_APP_NAME_INU "Dogecoin-Qt-inu"
#define QAPP_APP_NAME_TESTNET "Dogecoin-Qt-testnet"

#endif
