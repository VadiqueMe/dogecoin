// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "transactiondesc.h"

#include "unitsofcoin.h"
#include "guiutil.h"
#include "paymentserver.h"
#include "transactionrecord.h"
#include "core_io.h"
#include "base58.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "validation.h"
#include "script/script.h"
#include "timedata.h"
#include "util.h"
#include "wallet/db.h"
#include "wallet/wallet.h"

#include <stdint.h>
#include <string>

QString TransactionDesc::FormatTxStatus(const CWalletTx& wtx)
{
    AssertLockHeld(cs_main);
    if (!CheckFinalTx(wtx))
    {
        if (wtx.tx->nLockTime < LOCKTIME_THRESHOLD)
            return tr("Open for %n more block(s)", "", wtx.tx->nLockTime - chainActive.Height());
        else
            return tr("Open until %1").arg(GUIUtil::dateTimeStr(wtx.tx->nLockTime));
    }
    else
    {
        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < 0)
            return tr("conflicted with a transaction with %1 confirmations").arg(-nDepth);
        else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
            return tr("%1/offline").arg(nDepth);
        else if (nDepth == 0)
            return tr("0/unconfirmed, %1").arg((wtx.InMempool() ? tr("in memory pool") : tr("not in memory pool"))) + (wtx.isAbandoned() ? ", "+tr("abandoned") : "");
        else if ( nDepth < GetArg( "-txconfirmtarget", DEFAULT_TX_CONFIRMATIONS ) )
            return tr("%1/unconfirmed").arg(nDepth);
        else
            return tr("%1 confirmations").arg(nDepth);
    }
}

QString TransactionDesc::toHTML( CWallet * wallet, CWalletTx & wtx, TransactionRecord * rec, int unit )
{
    QString strHTML ;

    LOCK2(cs_main, wallet->cs_wallet);
    strHTML.reserve(4000);
    strHTML += "<html><font face='verdana, arial, helvetica, sans-serif'>";

    int64_t nTime = wtx.GetTxTime() ;
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    CAmount nNet = nCredit - nDebit;

    strHTML += "<b>" + tr("Status") + ":</b> " + FormatTxStatus(wtx);
    int nRequests = wtx.GetRequestCount() ;
    if ( nRequests != -1 )
    {
        if ( nRequests == 0 )
            strHTML += tr(", has not been successfully broadcast yet");
        else if ( nRequests > 0 )
            strHTML += tr(", broadcast through %n node(s)", "", nRequests);
    }
    strHTML += "<br>";

    strHTML += "<b>" + tr("Date") + ":</b> " + ( nTime != 0 ? GUIUtil::dateTimeStr( nTime ) : "0" ) + "<br>" ;

    //
    // From
    //
    if (wtx.IsCoinBase())
    {
        strHTML += "<b>" + tr("Source") + ":</b> " + tr("Generated") + "<br>";
    }
    else if (wtx.mapValue.count("from") && !wtx.mapValue["from"].empty())
    {
        // Online transaction
        strHTML += "<b>" + tr("From") + ":</b> " + GUIUtil::HtmlEscape(wtx.mapValue["from"]) + "<br>";
    }
    else
    {
        // Offline transaction
        if (nNet > 0)
        {
            // Credit
            if ( CDogecoinAddress( rec->address ).IsValid() )
            {
                CTxDestination address = CDogecoinAddress( rec->address ).Get() ;
                if (wallet->mapAddressBook.count(address))
                {
                    strHTML += "<b>" + tr("From") + ":</b> " + tr("unknown") + "<br>";
                    strHTML += "<b>" + tr("To") + ":</b> ";
                    strHTML += GUIUtil::HtmlEscape(rec->address);
                    QString addressOwned = (::IsMine(*wallet, address) == ISMINE_SPENDABLE) ? tr("own address") : tr("watch-only");
                    if (!wallet->mapAddressBook[address].name.empty())
                        strHTML += " (" + addressOwned + ", " + tr("label") + ": " + GUIUtil::HtmlEscape(wallet->mapAddressBook[address].name) + ")";
                    else
                        strHTML += " (" + addressOwned + ")";
                    strHTML += "<br>";
                }
            }
        }
    }

    //
    // To
    //
    if (wtx.mapValue.count("to") && !wtx.mapValue["to"].empty())
    {
        // Online transaction
        std::string strAddress = wtx.mapValue["to"];
        strHTML += "<b>" + tr("To") + ":</b> ";
        CTxDestination dest = CDogecoinAddress( strAddress ).Get() ;
        if (wallet->mapAddressBook.count(dest) && !wallet->mapAddressBook[dest].name.empty())
            strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[dest].name) + " ";
        strHTML += GUIUtil::HtmlEscape(strAddress) + "<br>";
    }

    //
    // Amount
    //
    if ( wtx.IsCoinBase() && nCredit == 0 )
    {
        //
        // Coinbase
        //
        CAmount nUnmatured = 0 ;
        for ( const CTxOut & txout : wtx.tx->vout )
            nUnmatured += wallet->GetCredit( txout, ISMINE_ALL ) ;
        strHTML += "<b>" + tr("Credit") + ":</b> ";
        if ( wtx.IsInMainChain() )
            strHTML += UnitsOfCoin::formatHtmlWithUnit( unit, nUnmatured )+ " (" + tr("matures in %n more block(s)", "", wtx.GetBlocksToMaturity()) + ")" ;
        else
            strHTML += "(" + tr("not accepted") + ")" ;
        strHTML += "<br>" ;
    }
    else if ( nNet > 0 )
    {
        //
        // Credit
        //
        strHTML += "<b>" + tr("Credit") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, nNet ) + "<br>" ;
    }
    else
    {
        isminetype fAllFromMe = ISMINE_SPENDABLE ;
        for ( const CTxIn & txin : wtx.tx->vin )
        {
            isminetype mine = wallet->IsMine( txin ) ;
            if ( fAllFromMe > mine ) fAllFromMe = mine ;
        }

        isminetype fAllToMe = ISMINE_SPENDABLE ;
        for ( const CTxOut & txout : wtx.tx->vout )
        {
            isminetype mine = wallet->IsMine( txout ) ;
            if ( fAllToMe > mine ) fAllToMe = mine ;
        }

        if ( fAllFromMe )
        {
            if ( fAllFromMe & ISMINE_WATCH_ONLY )
                strHTML += "<b>" + tr("From") + ":</b> " + tr("watch-only") + "<br>";

            //
            // Debit
            //
            for ( const CTxOut & txout : wtx.tx->vout )
            {
                // Ignore change
                isminetype toSelf = wallet->IsMine(txout);
                if ((toSelf == ISMINE_SPENDABLE) && (fAllFromMe == ISMINE_SPENDABLE))
                    continue;

                if (!wtx.mapValue.count("to") || wtx.mapValue["to"].empty())
                {
                    // Offline transaction
                    CTxDestination address;
                    if (ExtractDestination(txout.scriptPubKey, address))
                    {
                        strHTML += "<b>" + tr("To") + ":</b> ";
                        if (wallet->mapAddressBook.count(address) && !wallet->mapAddressBook[address].name.empty())
                            strHTML += GUIUtil::HtmlEscape(wallet->mapAddressBook[address].name) + " ";
                        strHTML += GUIUtil::HtmlEscape( CDogecoinAddress( address ).ToString() ) ;
                        if(toSelf == ISMINE_SPENDABLE)
                            strHTML += " (own address)";
                        else if(toSelf & ISMINE_WATCH_ONLY)
                            strHTML += " (watch-only)";
                        strHTML += "<br>";
                    }
                }

                strHTML += "<b>" + tr("Debit") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, -txout.nValue ) + "<br>" ;
                if(toSelf)
                    strHTML += "<b>" + tr("Credit") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, txout.nValue ) + "<br>" ;
            }

            if ( fAllToMe )
            {
                // Payment to self
                CAmount nChange = wtx.GetChange();
                CAmount nValue = nCredit - nChange;
                strHTML += "<b>" + tr("Total debit") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, -nValue ) + "<br>" ;
                strHTML += "<b>" + tr("Total credit") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, nValue ) + "<br>" ;
            }

            CAmount nTxFee = nDebit - wtx.tx->GetValueOut();
            if (nTxFee > 0)
                strHTML += "<b>" + tr("Transaction fee") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, -nTxFee ) + "<br>" ;
        }
        else
        {
            //
            // Mixed debit transaction
            //
            for ( const CTxIn & txin : wtx.tx->vin )
                if ( wallet->IsMine( txin ) )
                    strHTML += "<b>" + tr("Debit") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, -wallet->GetDebit( txin, ISMINE_ALL ) ) + "<br>" ;
            for ( const CTxOut & txout : wtx.tx->vout )
                if ( wallet->IsMine( txout ) )
                    strHTML += "<b>" + tr("Credit") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, wallet->GetCredit( txout, ISMINE_ALL ) ) + "<br>" ;
        }
    }

    strHTML += "<b>" + tr("Net amount") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, nNet, true ) + "<br>" ;

    //
    // Message
    //
    if (wtx.mapValue.count("message") && !wtx.mapValue["message"].empty())
        strHTML += "<br><b>" + tr("Message") + ":</b><br>" + GUIUtil::HtmlEscape(wtx.mapValue["message"], true) + "<br>";
    if (wtx.mapValue.count("comment") && !wtx.mapValue["comment"].empty())
        strHTML += "<br><b>" + tr("Comment") + ":</b><br>" + GUIUtil::HtmlEscape(wtx.mapValue["comment"], true) + "<br>";

    strHTML += "<b>" + tr("Hash of transaction") + ":</b> " + rec->getTxHash() + "<br>";
    strHTML += "<b>" + tr("Full size of transaction") + ":</b> " + QString::number( wtx.tx->GetFullSize() ) + " bytes<br>" ;
    strHTML += "<b>" + tr("Output index of subtransaction") + ":</b> " + QString::number( rec->getSubtransactionIndex() ) + "<br>" ;

    // Message from dogecoin:URI like dogecoin:D123...?message=example
    Q_FOREACH (const PAIRTYPE(std::string, std::string)& r, wtx.vOrderForm)
        if (r.first == "Message")
            strHTML += "<br><b>" + tr("Message") + ":</b><br>" + GUIUtil::HtmlEscape(r.second, true) + "<br>";

    //
    // PaymentRequest info:
    //
    Q_FOREACH (const PAIRTYPE(std::string, std::string)& r, wtx.vOrderForm)
    {
        if (r.first == "PaymentRequest")
        {
            PaymentRequestPlus req;
            req.parse(QByteArray::fromRawData(r.second.data(), r.second.size()));
            QString merchant;
            if (req.getMerchant(PaymentServer::getCertStore(), merchant))
                strHTML += "<b>" + tr("Merchant") + ":</b> " + GUIUtil::HtmlEscape(merchant) + "<br>";
        }
    }

    if ( wtx.IsCoinBase() )
    {
        quint32 nCoinbaseMaturity = Params().GetConsensus( chainActive.Height() ).nCoinbaseMaturity + 1 ;
        strHTML += "<br>" + tr( "Generated coins must mature %1 blocks before they can be spent. When you generated this block, it was broadcast to the network to be added to the block chain. If it fails to get into the chain, its state will change to \"not accepted\" and it won't be spendable. This may occasionally happen if another node generates a block within a few seconds of yours" ).arg(QString::number( nCoinbaseMaturity )) + "<br>";
    }

    //
    // More details
    //
    {
        strHTML += "<hr><br>" + QString( "More details" ) + "<br><br>" ;

        for ( const CTxIn & txin : wtx.tx->vin )
            if ( wallet->IsMine( txin ) )
                strHTML += "<b>" + tr("Debit") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, -wallet->GetDebit( txin, ISMINE_ALL ) ) + "<br>" ;
        for ( const CTxOut & txout : wtx.tx->vout )
            if ( wallet->IsMine( txout ) )
                strHTML += "<b>" + tr("Credit") + ":</b> " + UnitsOfCoin::formatHtmlWithUnit( unit, wallet->GetCredit( txout, ISMINE_ALL ) ) + "<br>" ;

        strHTML += "<br><b>" + tr("Transaction") + ":</b><br>" ;
        strHTML += GUIUtil::HtmlEscape( wtx.tx->ToString(), true ) ;

        strHTML += "<br><b>" + tr("Inputs") + ":</b>" ;
        strHTML += "<ul>" ;

        for ( const CTxIn & txin : wtx.tx->vin )
        {
            COutPoint prevout = txin.prevout ;

            CCoins prev ;
            if ( pcoinsTip->GetCoins( prevout.hash, prev ) )
            {
                if ( prevout.n < prev.vout.size() )
                {
                    strHTML += "<li>" ;
                    const CTxOut & vout = prev.vout[ prevout.n ] ;
                    CTxDestination address ;
                    if ( ExtractDestination( vout.scriptPubKey, address ) )
                    {
                        if ( wallet->mapAddressBook.count( address ) && ! wallet->mapAddressBook[ address ].name.empty() )
                            strHTML += GUIUtil::HtmlEscape( wallet->mapAddressBook[ address ].name ) + " " ;
                        strHTML += QString::fromStdString( CDogecoinAddress( address ).ToString() ) ;
                    }
                    strHTML = strHTML + " " + tr("Amount") + "=" + UnitsOfCoin::formatHtmlWithUnit( unit, vout.nValue ) ;
                    strHTML = strHTML + " IsMine=" + ( wallet->IsMine( vout ) & ISMINE_SPENDABLE ? tr("true") : tr("false") ) ;
                    strHTML = strHTML + " IsWatchOnly=" + ( wallet->IsMine( vout ) & ISMINE_WATCH_ONLY ? tr("true") : tr("false") ) ;
                    strHTML += "</li>" ;
                }
            }
        }

        strHTML += "</ul>" ;
    }
    strHTML += "<br>" ;

    //
    // Raw
    //
    strHTML += "<hr><br>" + QString( "Raw transaction" ) + "<br><br>" ;
    strHTML += TransactionDesc::getTxHex( rec, wallet ) ;

    strHTML += "</font></html>" ;
    return strHTML ;
}

QString TransactionDesc::getTxHex( TransactionRecord * rec, CWallet * wallet )
{
    LOCK2( cs_main, wallet->cs_wallet ) ;
    std::map< uint256, CWalletTx >::iterator mi = wallet->mapWallet.find( rec->hashOfTransaction ) ;
    if ( mi != wallet->mapWallet.end() )
    {
        std::string strHex = EncodeHexTx( static_cast< CTransaction >( mi->second ) ) ;
        return QString::fromStdString( strHex ) ;
    }
    return QString() ;
}
