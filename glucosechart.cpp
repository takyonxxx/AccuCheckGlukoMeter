#include "glucosechart.h"

#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <algorithm>

GlucoseChart::GlucoseChart(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GlucoseChart::setData(const QVector<QPointF> &points)
{
    m_pts = points;
    std::sort(m_pts.begin(), m_pts.end(),
              [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    update();
}

void GlucoseChart::clearData()
{
    m_pts.clear();
    update();
}

void GlucoseChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Panel background.
    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QColor(QStringLiteral("#243347")));
    p.setBrush(QColor(QStringLiteral("#0f1420")));
    p.drawRoundedRect(panel, 10, 10);

    const qreal left = 40, right = 14, top = 14, bottom = 24;
    const QRectF plot(left, top, width() - left - right, height() - top - bottom);

    if (m_pts.isEmpty()) {
        p.setPen(QColor(QStringLiteral("#5a6675")));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("No history yet"));
        return;
    }

    qreal dmin = 1e9, dmax = -1e9;
    const qreal xmin = m_pts.first().x();
    qreal xmax = m_pts.last().x();
    for (const QPointF &pt : m_pts) {
        dmin = qMin(dmin, pt.y());
        dmax = qMax(dmax, pt.y());
    }
    qreal yMin = qMin(70.0, dmin) - 10.0;
    qreal yMax = qMax(180.0, dmax) + 10.0;
    if (yMax - yMin < 40.0) yMax = yMin + 40.0;
    if (xmax <= xmin) xmax = xmin + 1.0;

    auto X = [&](qreal v) { return plot.left() + (v - xmin) / (xmax - xmin) * plot.width(); };
    auto Y = [&](qreal v) { return plot.bottom() - (v - yMin) / (yMax - yMin) * plot.height(); };

    // Target range band 70-180.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(67, 160, 71, 38));
    p.drawRect(QRectF(plot.left(), Y(180), plot.width(), Y(70) - Y(180)));

    // 70 / 180 guide lines + labels.
    p.setPen(QPen(QColor(QStringLiteral("#3a5169")), 1, Qt::DashLine));
    p.drawLine(QPointF(plot.left(), Y(70)),  QPointF(plot.right(), Y(70)));
    p.drawLine(QPointF(plot.left(), Y(180)), QPointF(plot.right(), Y(180)));

    QFont sf = font(); sf.setPointSize(8); p.setFont(sf);
    p.setPen(QColor(QStringLiteral("#6b7787")));
    p.drawText(QRectF(0, Y(180) - 8, left - 6, 16),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("180"));
    p.drawText(QRectF(0, Y(70) - 8, left - 6, 16),
               Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("70"));

    // Trend line.
    QPainterPath path;
    for (int i = 0; i < m_pts.size(); ++i) {
        const QPointF q(X(m_pts[i].x()), Y(m_pts[i].y()));
        if (i == 0) path.moveTo(q);
        else        path.lineTo(q);
    }
    p.setPen(QPen(QColor(QStringLiteral("#2f9fed")), 2));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Highlight the most recent point + value.
    const QPointF last = m_pts.last();
    const QPointF lp(X(last.x()), Y(last.y()));
    const QColor lc = (last.y() < 70.0)  ? QColor(QStringLiteral("#e53935"))
                    : (last.y() > 180.0) ? QColor(QStringLiteral("#fb8c00"))
                                         : QColor(QStringLiteral("#43a047"));
    p.setPen(Qt::NoPen);
    p.setBrush(lc);
    p.drawEllipse(lp, 4, 4);
    QFont bf = font(); bf.setPointSize(10); bf.setBold(true); p.setFont(bf);
    p.setPen(lc);
    p.drawText(QRectF(lp.x() - 40, lp.y() - 22, 80, 16),
               Qt::AlignCenter, QString::number(last.y(), 'f', 0));

    // X axis span.
    QFont xf = font(); xf.setPointSize(8); p.setFont(xf);
    p.setPen(QColor(QStringLiteral("#6b7787")));
    const qreal spanMin = xmax - xmin;
    const QString leftLbl = spanMin >= 60.0
        ? QStringLiteral("-%1h").arg(spanMin / 60.0, 0, 'f', 1)
        : QStringLiteral("-%1m").arg(spanMin, 0, 'f', 0);
    p.drawText(QRectF(plot.left(), plot.bottom() + 4, 90, 16),
               Qt::AlignLeft, leftLbl);
    p.drawText(QRectF(plot.right() - 60, plot.bottom() + 4, 60, 16),
               Qt::AlignRight, QStringLiteral("now"));
}
