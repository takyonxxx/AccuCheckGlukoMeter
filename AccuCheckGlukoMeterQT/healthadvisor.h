#ifndef HEALTHADVISOR_H
#define HEALTHADVISOR_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QPair>
#include <QDateTime>
#include <QtMath>
#include <algorithm>
#include <limits>
#include "glucosereading.h"

// Severity controls ordering and the colour used for an insight card.
enum class HealthSeverity { Critical, Warning, Info, Tip };

struct HealthInsight {
    HealthSeverity severity = HealthSeverity::Info;
    QString        title;
    QString        body;
};

// Result of analysing the full reading history. All percentages are 0..100.
struct HealthReport {
    bool      valid = false;
    int       count = 0;
    QDateTime firstDate;
    QDateTime lastDate;

    double avg = 0, sd = 0, cv = 0, a1c = 0, minV = 0, maxV = 0;

    double pctVeryLow = 0, pctLow = 0, pctInRange = 0, pctHigh = 0, pctVeryHigh = 0;
    int    nVeryLow = 0, nLow = 0, nInRange = 0, nHigh = 0, nVeryHigh = 0;

    // Per hour-of-day profile (index 0..23). NaN average = not enough samples.
    double hourAvg[24];
    int    hourCount[24];
    double hourHighPct[24];   // % of that hour's readings above 180
    double hourLowPct[24];    // % of that hour's readings below 70
    double hourRise[24];      // mean reading-to-reading change attributed to that hour

    QList<HealthInsight> insights;
};

class HealthAdvisor {
public:
    // Standard CGM thresholds (mg/dL).
    static constexpr double kVeryLow  = 54;
    static constexpr double kLow      = 70;
    static constexpr double kHigh     = 180;
    static constexpr double kVeryHigh = 250;

    static HealthReport analyze(const QList<GlucoseReading> &input) {
        HealthReport r;
        for (int h = 0; h < 24; ++h) {
            r.hourAvg[h] = std::numeric_limits<double>::quiet_NaN();
            r.hourCount[h] = 0;
            r.hourHighPct[h] = 0;
            r.hourLowPct[h] = 0;
            r.hourRise[h] = 0;
        }
        if (input.isEmpty())
            return r;

        // Work on a time-sorted copy so rise-rate maths are correct.
        QList<GlucoseReading> rs = input;
        std::sort(rs.begin(), rs.end(),
                  [](const GlucoseReading &a, const GlucoseReading &b){ return a.date < b.date; });

        r.valid     = true;
        r.count     = rs.size();
        r.firstDate = rs.first().date;
        r.lastDate  = rs.last().date;

        double sum = 0;
        r.minV = rs.first().mgdl;
        r.maxV = rs.first().mgdl;

        // Per-hour accumulators.
        double hourSum[24]  = {0};
        int    hourHigh[24] = {0};
        int    hourLow[24]  = {0};
        double riseSum[24]  = {0};
        int    riseCnt[24]  = {0};

        double prevVal = 0;
        QDateTime prevDate;
        bool havePrev = false;

        for (const auto &g : rs) {
            const double v = g.mgdl;
            sum += v;
            r.minV = qMin(r.minV, v);
            r.maxV = qMax(r.maxV, v);

            if (v < kVeryLow)       { r.nVeryLow++; }
            else if (v < kLow)      { r.nLow++; }
            else if (v <= kHigh)    { r.nInRange++; }
            else if (v <= kVeryHigh){ r.nHigh++; }
            else                    { r.nVeryHigh++; }

            const int h = g.date.time().hour();
            if (h >= 0 && h < 24) {
                hourSum[h] += v;
                r.hourCount[h]++;
                if (v > kHigh) hourHigh[h]++;
                if (v < kLow)  hourLow[h]++;

                if (havePrev) {
                    const qint64 gapMin = prevDate.secsTo(g.date) / 60;
                    // Only count change between closely-spaced samples (real CGM cadence).
                    if (gapMin > 0 && gapMin <= 20) {
                        riseSum[h] += (v - prevVal);
                        riseCnt[h]++;
                    }
                }
            }
            prevVal = v; prevDate = g.date; havePrev = true;
        }

        r.avg = sum / r.count;

        double varSum = 0;
        for (const auto &g : rs) varSum += (g.mgdl - r.avg) * (g.mgdl - r.avg);
        r.sd  = std::sqrt(varSum / r.count);
        r.cv  = r.avg > 0 ? (r.sd / r.avg) * 100.0 : 0;
        r.a1c = (r.avg + 46.7) / 28.7;   // ADAG estimate

        r.pctVeryLow  = 100.0 * r.nVeryLow  / r.count;
        r.pctLow      = 100.0 * (r.nVeryLow + r.nLow) / r.count;  // total below 70
        r.pctInRange  = 100.0 * r.nInRange  / r.count;
        r.pctHigh     = 100.0 * (r.nHigh + r.nVeryHigh) / r.count; // total above 180
        r.pctVeryHigh = 100.0 * r.nVeryHigh / r.count;

        for (int h = 0; h < 24; ++h) {
            if (r.hourCount[h] > 0) {
                r.hourAvg[h]     = hourSum[h] / r.hourCount[h];
                r.hourHighPct[h] = 100.0 * hourHigh[h] / r.hourCount[h];
                r.hourLowPct[h]  = 100.0 * hourLow[h]  / r.hourCount[h];
            }
            if (riseCnt[h] > 0)
                r.hourRise[h] = riseSum[h] / riseCnt[h];
        }

        buildInsights(r);
        return r;
    }

    static QString hourLabel(int h) {
        return QString("%1:00").arg(h, 2, 10, QChar('0'));
    }

private:
    // A meaningful hour needs at least this many samples before we draw a conclusion.
    static constexpr int kMinHourSamples = 3;

    // Merge consecutive hours that satisfy pred() into [start,end] ranges.
    template <typename Pred>
    static QList<QPair<int,int>> windows(const HealthReport &r, Pred pred) {
        QList<QPair<int,int>> out;
        int start = -1;
        for (int h = 0; h < 24; ++h) {
            const bool ok = r.hourCount[h] >= kMinHourSamples && pred(h);
            if (ok && start < 0) start = h;
            if (!ok && start >= 0) { out.append({start, h - 1}); start = -1; }
        }
        if (start >= 0) out.append({start, 23});
        return out;
    }

    static QString rangeText(int a, int b) {
        // b is the last *hour bucket*, so the window ends at (b+1):00.
        return QString("%1 - %2").arg(hourLabel(a)).arg(hourLabel((b + 1) % 24));
    }

    static void buildInsights(HealthReport &r) {
        QList<HealthInsight> &out = r.insights;

        // ---- 1. Safety first: hypoglycaemia ----
        if (r.nVeryLow > 0 || r.pctLow >= 4.0) {
            QString body = QString(
                "Kayitlarda dusuk seker (70 mg/dL alti) okumalarinin orani %1. "
                "Bunlarin %2 kadari ciddi dusuk (54 mg/dL alti) seviyede. ")
                .arg(r.pctLow, 0, 'f', 1)
                .arg(r.pctVeryLow, 0, 'f', 1);

            auto lows = windows(r, [&](int h){ return r.hourLowPct[h] >= 15.0; });
            if (!lows.isEmpty()) {
                QStringList parts;
                for (const auto &w : lows) parts << rangeText(w.first, w.second);
                body += QString("Dusukler ozellikle %1 saatlerinde yogunlasiyor. "
                                "Bu saatlerde ogun atlamamaya, yaninizda hizli karbonhidrat "
                                "(meyve suyu, glikoz tableti) bulundurmaya dikkat edin.")
                        .arg(parts.join(", "));
            } else {
                body += "Dusuk sekerin tekrarladigi durumda ogun/atistirmalik zamanlamanizi "
                        "ve aksam ic-ecekleri (ozellikle alkol) gozden gecirin.";
            }
            out.append({HealthSeverity::Critical, "Dusuk seker (hipoglisemi) uyarisi", body});
        }

        // ---- 2. High-glucose time windows (eating/drinking focus) ----
        auto highs = windows(r, [&](int h){
            return !qIsNaN(r.hourAvg[h]) && (r.hourAvg[h] > kHigh || r.hourHighPct[h] >= 50.0);
        });
        for (const auto &w : highs) {
            double s = 0; int n = 0;
            for (int h = w.first; h <= w.second; ++h) { s += r.hourAvg[h]; n++; }
            const int wavg = n ? qRound(s / n) : 0;
            out.append({HealthSeverity::Warning,
                QString("Yuksek seyir: %1").arg(rangeText(w.first, w.second)),
                QString("Bu saat araliginda seker ortalamasi yaklasik %1 mg/dL ile hedefin "
                        "(70-180) ustunde. Bu pencerede ogunlerinizi daha hafif tutmayi, hizli "
                        "karbonhidrati (beyaz ekmek, seker, meyve suyu) azaltmayi; once sebze/protein "
                        "sonra karbonhidrat yemeyi ve ogun sonrasi 10-15 dakikalik kisa bir yuruyusu "
                        "deneyin. Bu saatlerde sekerli ve gazli icecekler yerine su tercih edin.")
                .arg(wavg)});
        }

        // ---- 3. When the sharpest rises happen (often post-meal) ----
        int riseHour = -1; double riseMax = 0;
        for (int h = 0; h < 24; ++h) {
            if (r.hourCount[h] >= kMinHourSamples && r.hourRise[h] > riseMax) {
                riseMax = r.hourRise[h]; riseHour = h;
            }
        }
        if (riseHour >= 0 && riseMax >= 6.0) {
            out.append({HealthSeverity::Info,
                QString("En hizli yukselis: %1 civari").arg(hourLabel(riseHour)),
                QString("Seker en hizli bu saat civarinda yukseliyor; bu genellikle bir ogune "
                        "denk gelir. Bu ogunde porsiyonu bolmek, daha dusuk glisemik indeksli "
                        "secimler yapmak ve yemegi yavas yemek yukselisi yumusatabilir.")});
        }

        // ---- 4. Overnight pattern ----
        int nightLow = 0, nightCnt = 0;
        for (int h = 0; h <= 5; ++h) { nightLow += qRound(r.hourLowPct[h] * r.hourCount[h] / 100.0); nightCnt += r.hourCount[h]; }
        if (nightCnt >= kMinHourSamples && nightLow > 0) {
            out.append({HealthSeverity::Warning, "Gece dusukleri (00:00 - 06:00)",
                "Gece saatlerinde dusuk seker okumalari var. Aksam ogununun zamanlamasini, "
                "gec saatte yogun egzersizi ve aksam alkol tuketimini gozden gecirmek faydali olabilir. "
                "Yatmadan once seker seyrini kontrol etmeyi aliskanlik haline getirin."});
        }

        // ---- 5. Variability ----
        if (r.cv >= 36.0) {
            out.append({HealthSeverity::Warning, "Yuksek degiskenlik",
                QString("Seker degiskenliginiz (CV %1) hedefin ustunde; saglikli aralikta CV "
                        "degerinin 36 puanin altinda olmasi onerilir, yani inis-cikislariniz belirgin. "
                        "Ogun saatlerini ve porsiyonlari gunden gune daha tutarli tutmak, ogunleri "
                        "atlamamak ve uyku duzenini korumak inis-cikislari azaltir.")
                .arg(QString::number(qRound(r.cv)) + "%")});
        }

        // ---- 6. Time in range feedback ----
        if (r.pctInRange >= 70.0) {
            out.append({HealthSeverity::Tip, "Hedef aralikta iyi gidiyorsunuz",
                QString("Okumalarin %1 kadari hedef aralikta (70-180). 70% uzeri iyi kabul edilir; "
                        "bu duzeni korumaya devam edin.").arg(QString::number(qRound(r.pctInRange)) + "%")});
        } else {
            out.append({HealthSeverity::Info, "Hedef aralikta kalma orani",
                QString("Okumalarin %1 kadari hedef aralikta (70-180). Genel hedef 70% ustudur. "
                        "Yukaridaki saat onerileri bu orani artirmaya yardimci olabilir.")
                .arg(QString::number(qRound(r.pctInRange)) + "%")});
        }

        // ---- 7. Always-on general tips ----
        out.append({HealthSeverity::Tip, "Genel oneriler",
            "- Gun boyu yeterli su icin; susuzluk sekeri yukseltebilir.\n"
            "- Ogunlerde once lif/sebze ve protein, sonra karbonhidrat tuketin.\n"
            "- Ogunlerden sonra 10-15 dakikalik yuruyus seker piklerini azaltir.\n"
            "- Ogun ve uyku saatlerini mumkun oldugunca sabit tutun.\n"
            "- Sekerli/gazli icecekler yerine su veya seker icermeyen secenekleri tercih edin."});

        // ---- 8. Disclaimer (kept last) ----
        out.append({HealthSeverity::Info, "Onemli not",
            "Bu analiz yalnizca kendi olcum gecmisinizden cikarilan genel bir degerlendirmedir; "
            "tibbi teshis ya da tedavi onerisi degildir. Ilac/insulin dozunuzu kendi basiniza "
            "degistirmeyin. Tekrarlayan dusuk/yuksek seker veya bu onerilerle ilgili kararlar "
            "icin hekiminize ve diyetisyeninize danisin."});
    }
};

#endif // HEALTHADVISOR_H
