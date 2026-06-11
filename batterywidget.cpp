#include "batterywidget.h"

#include <QPainter>
#include <QFont>

BatteryWidget::BatteryWidget(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(118, 30);
}

void BatteryWidget::setLevel(int pct)
{
    if (pct < -1) pct = -1;
    if (pct > 100) pct = 100;
    m_level = pct;
    update();
}

static QColor colorForLevel(int lvl)
{
    if (lvl < 0)   return QColor(QStringLiteral("#5a6675")); // unknown
    if (lvl <= 15) return QColor(QStringLiteral("#e53935")); // very low
    if (lvl <= 30) return QColor(QStringLiteral("#fb8c00")); // low
    return QColor(QStringLiteral("#43a047"));                // ok
}

void BatteryWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QColor col = colorForLevel(m_level);

    // Battery body geometry (left part of the widget).
    const qreal bodyW = 46.0;
    const qreal bodyH = 20.0;
    const qreal x = 1.0;
    const qreal y = (height() - bodyH) / 2.0;
    const QRectF body(x, y, bodyW, bodyH);
    const QRectF nub(x + bodyW, y + bodyH * 0.30, 3.5, bodyH * 0.40);

    // Outline + terminal nub.
    QPen pen(col);
    pen.setWidthF(1.8);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, 4, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawRoundedRect(nub, 1.5, 1.5);

    // Track (empty area) + proportional fill.
    const QRectF inner(x + 3, y + 3, bodyW - 6, bodyH - 6);
    p.setBrush(QColor(QStringLiteral("#202b3d")));
    p.drawRoundedRect(inner, 2, 2);

    if (m_level > 0) {
        QRectF fill = inner;
        fill.setWidth(inner.width() * (m_level / 100.0));
        p.setBrush(col);
        p.drawRoundedRect(fill, 2, 2);
    }

    // Percentage text to the right.
    QFont f = font();
    f.setPointSize(12);
    f.setBold(true);
    p.setFont(f);
    p.setPen(col);
    const QString txt = (m_level < 0) ? QStringLiteral("N/A")
                                      : QStringLiteral("%1%").arg(m_level);
    const QRectF textRect(x + bodyW + 12, 0,
                          width() - (x + bodyW + 12), height());
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, txt);
}
