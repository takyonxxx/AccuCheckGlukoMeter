#include "healthstatusdialog.h"
#include "healthadvisor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
#include <QPushButton>
#include <QWidget>
#include <QPainter>
#include <QToolTip>
#include <QMouseEvent>
#include <QtMath>

namespace {

QString colorForValue(double v) {
    if (qIsNaN(v)) return "#2A3142";
    if (v < HealthAdvisor::kLow)  return "#D64545";
    if (v > HealthAdvisor::kVeryHigh) return "#C0392B";
    if (v > HealthAdvisor::kHigh) return "#E08A2B";
    return "#3DA35D";
}

struct Sev {
    QString accent;   // left border / dot colour
    QString chip;     // small label text
};
Sev sevStyle(HealthSeverity s) {
    switch (s) {
    case HealthSeverity::Critical: return {"#E5484D", "ONEMLI"};
    case HealthSeverity::Warning:  return {"#E08A2B", "DIKKAT"};
    case HealthSeverity::Tip:      return {"#3DA35D", "ONERI"};
    case HealthSeverity::Info:
    default:                       return {"#3475F0", "BILGI"};
    }
}

// A compact 24-cell strip coloured by the average glucose of each hour.
class HourStrip : public QWidget {
public:
    explicit HourStrip(const HealthReport &r, QWidget *parent = nullptr)
        : QWidget(parent), m_r(r) {
        setMinimumHeight(54);
        setMouseTracking(true);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        const double w = width() / 24.0;
        const int barTop = 4, barH = height() - 22;
        QFont f = p.font(); f.setPointSizeF(7.5); p.setFont(f);
        for (int h = 0; h < 24; ++h) {
            QRectF cell(h * w + 1, barTop, w - 2, barH);
            p.fillRect(cell, QColor(colorForValue(m_r.hourAvg[h])));
            if (h % 3 == 0) {
                p.setPen(QColor("#7E8794"));
                p.drawText(QRectF(h * w, height() - 16, w * 3, 14),
                           Qt::AlignLeft, QString("%1h").arg(h));
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        const int h = qBound(0, int(e->position().x() / (width() / 24.0)), 23);
        if (m_r.hourCount[h] > 0 && !qIsNaN(m_r.hourAvg[h]))
            QToolTip::showText(e->globalPosition().toPoint(),
                QString("%1 - ort %2 mg/dL (%3 olcum)")
                    .arg(HealthAdvisor::hourLabel(h))
                    .arg(qRound(m_r.hourAvg[h]))
                    .arg(m_r.hourCount[h]), this);
        else
            QToolTip::hideText();
    }
private:
    HealthReport m_r;
};

} // namespace

HealthStatusDialog::HealthStatusDialog(const QList<GlucoseReading> &readings, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Health Status");
    setStyleSheet("background:#12161F;");
    resize(560, 760);

    const HealthReport rep = HealthAdvisor::analyze(readings);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea{background:#12161F;border:none;}");
    outer->addWidget(scroll, 1);

    auto *content = new QWidget();
    content->setStyleSheet("background:#12161F;");
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(14);
    scroll->setWidget(content);

    // ---- Header ----
    auto *title = new QLabel("Health Status");
    title->setStyleSheet("color:#EAEAEA; font-size:20px; font-weight:700;");
    root->addWidget(title);

    if (!rep.valid) {
        auto *empty = new QLabel("Henuz analiz edilecek kayit yok.\n"
                                 "Once cihaza baglanip verileri yukleyin.");
        empty->setStyleSheet("color:#9AA3AE; font-size:13px;");
        empty->setWordWrap(true);
        root->addWidget(empty);
        root->addStretch();
        auto *closeEmpty = new QPushButton("Kapat");
        closeEmpty->setMinimumHeight(44);
        closeEmpty->setStyleSheet("QPushButton{background:#3475F0;color:white;font-size:15px;"
                                  "font-weight:600;border-radius:10px;}");
        connect(closeEmpty, &QPushButton::clicked, this, &QDialog::accept);
        outer->addWidget(closeEmpty);
        return;
    }

    auto *sub = new QLabel(QString("%1 olcum  -  %2 - %3")
        .arg(rep.count)
        .arg(rep.firstDate.toString("dd.MM.yyyy"))
        .arg(rep.lastDate.toString("dd.MM.yyyy")));
    sub->setStyleSheet("color:#9AA3AE; font-size:12px;");
    root->addWidget(sub);

    // ---- Metric tiles ----
    auto makeTile = [&](const QString &val, const QString &cap, const QString &valColor) {
        auto *box = new QFrame();
        box->setStyleSheet("QFrame{background:#1B2331;border-radius:10px;}");
        auto *bl = new QVBoxLayout(box);
        bl->setContentsMargins(12, 10, 12, 10);
        bl->setSpacing(2);
        auto *v = new QLabel(val);
        v->setStyleSheet(QString("color:%1; font-size:18px; font-weight:700;").arg(valColor));
        auto *c = new QLabel(cap);
        c->setStyleSheet("color:#9AA3AE; font-size:11px;");
        bl->addWidget(v); bl->addWidget(c);
        return box;
    };
    auto *tiles = new QGridLayout();
    tiles->setHorizontalSpacing(10);
    tiles->setVerticalSpacing(10);
    tiles->addWidget(makeTile(QString::number(qRound(rep.avg)) + " mg/dL", "Ortalama", "#EAEAEA"), 0, 0);
    tiles->addWidget(makeTile(QString::number(rep.a1c, 'f', 1) + "%", "Tahmini HbA1c", "#EAEAEA"), 0, 1);
    tiles->addWidget(makeTile(QString::number(qRound(rep.pctInRange)) + "%", "Hedef Aralik (70-180)",
                              rep.pctInRange >= 70 ? "#3DA35D" : "#E08A2B"), 1, 0);
    tiles->addWidget(makeTile(QString::number(qRound(rep.cv)) + "%", "Degiskenlik (CV)",
                              rep.cv < 36 ? "#3DA35D" : "#E08A2B"), 1, 1);
    root->addLayout(tiles);

    // ---- Time-in-range stacked bar ----
    auto *tirLabel = new QLabel("Zaman dagilimi");
    tirLabel->setStyleSheet("color:#C7CDD6; font-size:13px; font-weight:600;");
    root->addWidget(tirLabel);

    auto *barRow = new QHBoxLayout();
    barRow->setSpacing(0);
    struct Seg { double pct; QString color; QString name; };
    const QList<Seg> segs = {
        { rep.pctLow - rep.pctVeryLow, "#D64545", "Dusuk" },
        { rep.pctVeryLow,              "#C0392B", "Cok dusuk" },
        { rep.pctInRange,              "#3DA35D", "Hedefte" },
        { rep.pctHigh - rep.pctVeryHigh, "#E08A2B", "Yuksek" },
        { rep.pctVeryHigh,             "#C0392B", "Cok yuksek" },
    };
    for (const auto &s : segs) {
        if (s.pct <= 0.0) continue;
        auto *seg = new QLabel();
        seg->setFixedHeight(22);
        seg->setStyleSheet(QString("background:%1;").arg(s.color));
        seg->setToolTip(QString("%1: %2%").arg(s.name).arg(s.pct, 0, 'f', 1));
        barRow->addWidget(seg, qMax(1, qRound(s.pct * 10)));
    }
    auto *barWrap = new QFrame();
    barWrap->setStyleSheet("QFrame{border-radius:6px;}");
    barWrap->setLayout(barRow);
    barRow->setContentsMargins(0, 0, 0, 0);
    root->addWidget(barWrap);

    auto *tirLegend = new QLabel(QString("Hedefte %1%   -   Yuksek %2%   -   Dusuk %3%")
        .arg(rep.pctInRange, 0, 'f', 0).arg(rep.pctHigh, 0, 'f', 0).arg(rep.pctLow, 0, 'f', 0));
    tirLegend->setStyleSheet("color:#9AA3AE; font-size:11px;");
    root->addWidget(tirLegend);

    // ---- Hourly heat strip ----
    auto *hourLabel = new QLabel("Saatlik seyir (gun ici ortalama)");
    hourLabel->setStyleSheet("color:#C7CDD6; font-size:13px; font-weight:600;");
    root->addWidget(hourLabel);
    root->addWidget(new HourStrip(rep, content));

    // ---- Insights ----
    auto *adviceLabel = new QLabel("Oneriler");
    adviceLabel->setStyleSheet("color:#C7CDD6; font-size:13px; font-weight:600; margin-top:4px;");
    root->addWidget(adviceLabel);

    for (const auto &ins : rep.insights) {
        const Sev sv = sevStyle(ins.severity);
        auto *card = new QFrame();
        card->setStyleSheet(QString(
            "QFrame{background:#1B2331;border-radius:10px;"
            "border-left:4px solid %1;}").arg(sv.accent));
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(14, 12, 14, 12);
        cl->setSpacing(6);

        auto *head = new QHBoxLayout();
        auto *chip = new QLabel(sv.chip);
        chip->setStyleSheet(QString("color:%1; font-size:10px; font-weight:700;"
                                    "letter-spacing:1px;").arg(sv.accent));
        auto *t = new QLabel(ins.title);
        t->setStyleSheet("color:#EAEAEA; font-size:14px; font-weight:700;");
        t->setWordWrap(true);
        head->addWidget(chip);
        head->addWidget(t, 1);
        cl->addLayout(head);

        auto *b = new QLabel(ins.body);
        b->setStyleSheet("color:#C7CDD6; font-size:13px;");
        b->setWordWrap(true);
        cl->addWidget(b);

        root->addWidget(card);
    }

    root->addStretch();

    // ---- Close button (fixed footer) ----
    auto *closeBtn = new QPushButton("Kapat");
    closeBtn->setMinimumHeight(46);
    closeBtn->setStyleSheet("QPushButton{background:#3475F0;color:white;font-size:15px;"
                            "font-weight:600;border-radius:10px;margin:14px 20px;}");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    outer->addWidget(closeBtn);
}
