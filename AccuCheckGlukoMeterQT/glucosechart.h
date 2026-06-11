#ifndef GLUCOSECHART_H
#define GLUCOSECHART_H

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QList>
#include "glucosereading.h"

class GlucoseChart : public QChartView {
    Q_OBJECT
public:
    explicit GlucoseChart(QWidget *parent = nullptr);

    // hours <= 0  ==>  show all
    void setRangeHours(double hours);
    void setReadings(const QList<GlucoseReading> &readings);

private:
    void rebuild();

    QChart        *m_chart = nullptr;
    QLineSeries   *m_line = nullptr;
    QScatterSeries *m_last = nullptr;
    QAreaSeries   *m_band = nullptr;
    QLineSeries   *m_bandHi = nullptr;
    QLineSeries   *m_bandLo = nullptr;
    QLineSeries   *m_l70 = nullptr;
    QLineSeries   *m_l180 = nullptr;
    QLineSeries   *m_l250 = nullptr;
    QDateTimeAxis *m_axX = nullptr;
    QValueAxis    *m_axY = nullptr;

    QList<GlucoseReading> m_readings;
    double m_rangeHours = -1;   // -1 = all
};

#endif // GLUCOSECHART_H
