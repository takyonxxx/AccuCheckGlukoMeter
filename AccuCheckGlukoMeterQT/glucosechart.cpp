#include "glucosechart.h"
#include <QtCharts/QChart>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <algorithm>

GlucoseChart::GlucoseChart(QWidget *parent) : QChartView(parent) {
    m_chart = new QChart();
    m_chart->legend()->hide();
    m_chart->setBackgroundBrush(QBrush(QColor("#F5F7FA")));
    m_chart->setPlotAreaBackgroundBrush(QBrush(Qt::white));
    m_chart->setPlotAreaBackgroundVisible(true);
    m_chart->setMargins(QMargins(6, 6, 6, 6));

    m_axX = new QDateTimeAxis();
    m_axX->setFormat("HH:mm");
    m_axX->setTickCount(7);
    m_axX->setLabelsColor(QColor("#33415C"));
    m_axX->setGridLineColor(QColor("#D5DAE2"));

    m_axY = new QValueAxis();
    m_axY->setRange(40, 320);
    m_axY->setLabelsColor(QColor("#33415C"));
    m_axY->setGridLineColor(QColor("#D5DAE2"));
    m_axY->setLabelFormat("%d");

    m_chart->addAxis(m_axX, Qt::AlignBottom);
    m_chart->addAxis(m_axY, Qt::AlignLeft);

    setChart(m_chart);
    setRenderHint(QPainter::Antialiasing);
}

void GlucoseChart::setRangeHours(double hours) {
    m_rangeHours = hours;
    rebuild();
}

void GlucoseChart::setReadings(const QList<GlucoseReading> &readings) {
    m_readings = readings;
    rebuild();
}

void GlucoseChart::rebuild() {
    // remove old series
    m_chart->removeAllSeries();
    m_line = nullptr; m_last = nullptr; m_band = nullptr;
    m_bandHi = nullptr; m_bandLo = nullptr;
    m_l70 = m_l180 = m_l250 = nullptr;

    if (m_readings.isEmpty()) return;

    const QDateTime lastDate = m_readings.last().date;
    const QDateTime firstDate = m_readings.first().date;
    QDateTime start = (m_rangeHours > 0) ? lastDate.addSecs(qint64(-m_rangeHours * 3600))
                                         : firstDate;
    QDateTime end = lastDate;
    if (start.secsTo(end) < 3600) {
        start = end.addSecs(-1800);
        end = end.addSecs(1800);
    }

    QList<GlucoseReading> pts;
    double maxV = 0, minV = 1000;
    for (const auto &r : m_readings) {
        if (r.date >= start && r.date <= end) {
            pts.append(r);
            maxV = qMax(maxV, r.mgdl);
            minV = qMin(minV, r.mgdl);
        }
    }
    if (pts.isEmpty()) return;

    const double yMax = qMax(300.0, maxV + 20);
    const double yMin = qMin(50.0, minV - 10);
    m_axY->setRange(yMin, yMax);
    m_axX->setRange(start, end);

    const double spanHours = start.secsTo(end) / 3600.0;
    m_axX->setFormat(spanHours > 36 ? "dd MMM" : "HH:mm");
    m_axX->setTickCount(7);

    const qreal x0 = start.toMSecsSinceEpoch();
    const qreal x1 = end.toMSecsSinceEpoch();

    // target band 70-180
    m_bandHi = new QLineSeries(); m_bandHi->append(x0, 180); m_bandHi->append(x1, 180);
    m_bandLo = new QLineSeries(); m_bandLo->append(x0, 70);  m_bandLo->append(x1, 70);
    m_band = new QAreaSeries(m_bandHi, m_bandLo);
    m_band->setColor(QColor(60, 180, 90, 36));
    m_band->setBorderColor(QColor(0, 0, 0, 0));
    m_chart->addSeries(m_band);
    m_band->attachAxis(m_axX); m_band->attachAxis(m_axY);

    // threshold lines
    auto addLine = [&](double y, QColor c, bool dash) -> QLineSeries* {
        auto *s = new QLineSeries();
        s->append(x0, y); s->append(x1, y);
        QPen p(c); p.setWidthF(1.0);
        if (dash) p.setStyle(Qt::DashLine);
        s->setPen(p);
        m_chart->addSeries(s);
        s->attachAxis(m_axX); s->attachAxis(m_axY);
        return s;
    };
    m_l70  = addLine(70,  QColor("#9AA3AE"), true);
    m_l180 = addLine(180, QColor("#9AA3AE"), true);
    m_l250 = addLine(250, QColor(230, 150, 40, 180), false);

    // glucose line
    m_line = new QLineSeries();
    for (const auto &r : pts)
        m_line->append(r.date.toMSecsSinceEpoch(), r.mgdl);
    QPen lp(QColor("#2974D1")); lp.setWidthF(2.2);
    m_line->setPen(lp);
    m_chart->addSeries(m_line);
    m_line->attachAxis(m_axX); m_line->attachAxis(m_axY);

    // last point
    const auto &lastR = pts.last();
    m_last = new QScatterSeries();
    m_last->setMarkerSize(11);
    QColor lc = lastR.mgdl < 70 ? QColor("#D64545")
              : (lastR.mgdl > 180 ? QColor("#E08A2B") : QColor("#3DA35D"));
    m_last->setColor(lc);
    m_last->setBorderColor(lc);
    m_last->append(lastR.date.toMSecsSinceEpoch(), lastR.mgdl);
    m_chart->addSeries(m_last);
    m_last->attachAxis(m_axX); m_last->attachAxis(m_axY);
}
