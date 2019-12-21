// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2019 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#ifndef QT_TRAFFICGRAPHWIDGET_H
#define QT_TRAFFICGRAPHWIDGET_H

#include <QWidget>
#include <QQueue>

class NetworkModel ;

QT_BEGIN_NAMESPACE
class QPaintEvent;
class QTimer;
QT_END_NAMESPACE

class TrafficGraphWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrafficGraphWidget( QWidget * parent = nullptr ) ;

    void setNetworkModel( NetworkModel * model ) ;
    int getGraphRangeMinutes() const ;

    void setSentColor( const QColor & color ) {  colorForSent = color ;  }
    void setReceivedColor( const QColor & color ) {  colorForReceived = color ;  }

protected:
    void paintEvent( QPaintEvent * ) ;

public Q_SLOTS:
    void updateRates() ;
    void setGraphRangeMinutes( int minutes ) ;
    void clearTrafficGraph() ;

private:
    void paintPath(QPainterPath &path, QQueue<float> &samples);

    QTimer *timer;
    float fMax;
    int nMinutes ;
    QQueue<float> vSamplesIn;
    QQueue<float> vSamplesOut;
    quint64 nLastBytesIn;
    quint64 nLastBytesOut;
    NetworkModel * networkModel ;

    QColor colorForSent ;
    QColor colorForReceived ;

} ;

#endif
