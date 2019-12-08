// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef DOGECOIN_QT_CHAINSYNCOVERLAY_H
#define DOGECOIN_QT_CHAINSYNCOVERLAY_H

#include <QDateTime>
#include <QWidget>

//! The required delta of headers to the estimated number of available headers until we show the IBD progress
static constexpr int HEADER_HEIGHT_DELTA_SYNC = 24 ;

namespace Ui {
    class ChainSyncOverlay ;
}

/** Modal overlay to display information about the chain-sync state */
class ChainSyncOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit ChainSyncOverlay( QWidget * parent ) ;
    ~ChainSyncOverlay() ;

public Q_SLOTS:
    void tipUpdate(int count, const QDateTime& blockDate, double nVerificationProgress);
    void setKnownBestHeight(int count, const QDateTime& blockDate);

    void toggleVisibility();
    // will show or hide the modal layer
    void showHide(bool hide = false, bool userRequested = false);
    void closeClicked();
    bool isLayerVisible() { return layerIsVisible; }

protected:
    bool eventFilter(QObject * obj, QEvent * ev);
    bool event(QEvent* ev);

private:
    Ui::ChainSyncOverlay * ui ;
    int bestHeaderHeight ; // best known height (based on the headers)
    QDateTime bestHeaderDate;
    QVector<QPair<qint64, double> > blockProcessTime;
    bool layerIsVisible;
    bool userClosed;
};

#endif
