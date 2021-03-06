// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2019-2020 vadique
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php

#include "trafficgraphwidget.h"
#include "networkmodel.h"

#include <QPainter>
#include <QColor>
#include <QTimer>

#include <cmath>

#define DESIRED_SAMPLES         800

#define XMARGIN                 10
#define YMARGIN                 10

TrafficGraphWidget::TrafficGraphWidget( QWidget * parent )
    : QWidget( parent )
    , timer( nullptr )
    , fMax( 0.0f )
    , nMinutes( 0 )
    , vSamplesIn()
    , vSamplesOut()
    , nLastBytesIn( 0 )
    , nLastBytesOut( 0 )
    , networkModel( nullptr )
    , colorForSent( Qt::magenta )
    , colorForReceived( Qt::green )
{
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), SLOT(updateRates()));
}

void TrafficGraphWidget::setNetworkModel( NetworkModel * model )
{
    networkModel = model ;
    if ( model != nullptr ) {
        nLastBytesIn = model->getTotalBytesRecv() ;
        nLastBytesOut = model->getTotalBytesSent() ;
    }
}

int TrafficGraphWidget::getGraphRangeMinutes() const
{
    return nMinutes ;
}

void TrafficGraphWidget::paintPath(QPainterPath &path, QQueue<float> &samples)
{
    int h = height() - YMARGIN * 2, w = width() - XMARGIN * 2;
    int sampleCount = samples.size(), x = XMARGIN + w, y;
    if(sampleCount > 0) {
        path.moveTo(x, YMARGIN + h);
        for(int i = 0; i < sampleCount; ++i) {
            x = XMARGIN + w - w * i / DESIRED_SAMPLES;
            y = YMARGIN + h - (int)(h * samples.at(i) / fMax);
            path.lineTo(x, y);
        }
        path.lineTo(x, YMARGIN + h);
    }
}

void TrafficGraphWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if(fMax <= 0.0f) return;

    QColor axisCol(Qt::gray);
    int h = height() - YMARGIN * 2;
    painter.setPen(axisCol);
    painter.drawLine(XMARGIN, YMARGIN + h, width() - XMARGIN, YMARGIN + h);

    // decide what order of magnitude we are
    int base = floor(log10(fMax));
    float val = pow(10.0f, base);

    const QString units     = tr("KB/s");
    const float yMarginText = 2.0;
    
    // draw lines
    painter.setPen(axisCol);
    painter.drawText(XMARGIN, YMARGIN + h - h * val / fMax-yMarginText, QString("%1 %2").arg(val).arg(units));
    for(float y = val; y < fMax; y += val) {
        int yy = YMARGIN + h - h * y / fMax;
        painter.drawLine(XMARGIN, yy, width() - XMARGIN, yy);
    }
    // if we drew 3 or fewer lines, break them up at the next lower order of magnitude
    if(fMax / val <= 3.0f) {
        axisCol = axisCol.darker();
        val = pow(10.0f, base - 1);
        painter.setPen(axisCol);
        painter.drawText(XMARGIN, YMARGIN + h - h * val / fMax-yMarginText, QString("%1 %2").arg(val).arg(units));
        int count = 1;
        for(float y = val; y < fMax; y += val, count++) {
            // don't overwrite lines drawn above
            if(count % 10 == 0)
                continue;
            int yy = YMARGIN + h - h * y / fMax;
            painter.drawLine(XMARGIN, yy, width() - XMARGIN, yy);
        }
    }

    if ( ! vSamplesIn.empty() ) {
        QPainterPath p ;
        paintPath( p, vSamplesIn ) ;

        QColor transparentColorForReceived(
            colorForReceived.red(), colorForReceived.green(), colorForReceived.blue(),
            colorForReceived.alpha() >> 1
        ) ;
        painter.fillPath( p, transparentColorForReceived ) ;
        painter.setPen( colorForReceived ) ;
        painter.drawPath( p ) ;
    }
    if ( ! vSamplesOut.empty() ) {
        QPainterPath p ;
        paintPath( p, vSamplesOut ) ;

        QColor transparentColorForSent = colorForSent ;
        transparentColorForSent.setAlpha( colorForSent.alpha() >> 1 ) ;
        painter.fillPath( p, transparentColorForSent ) ;
        painter.setPen( colorForSent ) ;
        painter.drawPath( p ) ;
    }
}

void TrafficGraphWidget::updateRates()
{
    if ( networkModel == nullptr ) return ;

    quint64 bytesIn = networkModel->getTotalBytesRecv(),
            bytesOut = networkModel->getTotalBytesSent() ;
    float inRate = (bytesIn - nLastBytesIn) / 1024.0f * 1000 / timer->interval();
    float outRate = (bytesOut - nLastBytesOut) / 1024.0f * 1000 / timer->interval();
    vSamplesIn.push_front(inRate);
    vSamplesOut.push_front(outRate);
    nLastBytesIn = bytesIn;
    nLastBytesOut = bytesOut;

    while(vSamplesIn.size() > DESIRED_SAMPLES) {
        vSamplesIn.pop_back();
    }
    while(vSamplesOut.size() > DESIRED_SAMPLES) {
        vSamplesOut.pop_back();
    }

    float tmax = 0.0f;
    Q_FOREACH(float f, vSamplesIn) {
        if(f > tmax) tmax = f;
    }
    Q_FOREACH(float f, vSamplesOut) {
        if(f > tmax) tmax = f;
    }
    fMax = tmax;
    update();
}

void TrafficGraphWidget::setGraphRangeMinutes( int minutes )
{
    nMinutes = minutes ;
    int msecsPerSample = nMinutes * 60 * 1000 / DESIRED_SAMPLES ;
    timer->stop() ;
    timer->setInterval( msecsPerSample ) ;

    clearTrafficGraph() ;
}

void TrafficGraphWidget::clearTrafficGraph()
{
    timer->stop() ;

    vSamplesOut.clear() ;
    vSamplesIn.clear() ;
    fMax = 0.0f ;

    if ( networkModel != nullptr ) {
        nLastBytesIn = networkModel->getTotalBytesRecv() ;
        nLastBytesOut = networkModel->getTotalBytesSent() ;
    }
    timer->start() ;
}
