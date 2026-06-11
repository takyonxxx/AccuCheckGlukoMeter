#ifndef GLUCOSECHART_H
#define GLUCOSECHART_H

#include <QWidget>
#include <QVector>
#include <QPointF>

// Self-painted glucose line chart. Each point is (x = minutes since session
// start, y = mg/dL). Draws the 70-180 target band, the trend line, and
// highlights the most recent value.
class GlucoseChart : public QWidget
{
public:
    explicit GlucoseChart(QWidget *parent = nullptr);

    void setData(const QVector<QPointF> &points);
    void clearData();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<QPointF> m_pts;
};

#endif // GLUCOSECHART_H
