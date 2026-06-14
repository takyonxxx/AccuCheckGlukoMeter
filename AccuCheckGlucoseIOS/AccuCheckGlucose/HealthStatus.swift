import SwiftUI

// MARK: - Model

enum HealthSeverity: Int {
    case critical = 0, warning, info, tip

    var accent: Color {
        switch self {
        case .critical: return Color(red: 0.90, green: 0.28, blue: 0.30)
        case .warning:  return Color(red: 0.88, green: 0.54, blue: 0.17)
        case .tip:      return Color(red: 0.24, green: 0.64, blue: 0.36)
        case .info:     return Color(red: 0.20, green: 0.46, blue: 0.95)
        }
    }
    var chip: String {
        switch self {
        case .critical: return "ÖNEMLİ"
        case .warning:  return "DİKKAT"
        case .tip:      return "ÖNERİ"
        case .info:     return "BİLGİ"
        }
    }
}

struct HealthInsight: Identifiable {
    let id = UUID()
    let severity: HealthSeverity
    let title: String
    let body: String
}

struct HourStat {
    var count = 0
    var avg: Double? = nil
    var highPct = 0.0    // % of this hour's readings above 180
    var lowPct = 0.0     // % below 70
    var rise = 0.0       // mean reading-to-reading change attributed to this hour
}

struct HealthReport {
    var valid = false
    var count = 0
    var firstDate = Date()
    var lastDate = Date()

    var avg = 0.0, sd = 0.0, cv = 0.0, a1c = 0.0, minV = 0.0, maxV = 0.0

    var pctVeryLow = 0.0, pctLow = 0.0, pctInRange = 0.0, pctHigh = 0.0, pctVeryHigh = 0.0
    var nVeryLow = 0, nLow = 0, nInRange = 0, nHigh = 0, nVeryHigh = 0

    var hours: [HourStat] = Array(repeating: HourStat(), count: 24)
    var insights: [HealthInsight] = []
}

// MARK: - Analysis engine

enum HealthAdvisor {
    static let veryLow = 54.0, low = 70.0, high = 180.0, veryHigh = 250.0
    static let minHourSamples = 3

    static func hourLabel(_ h: Int) -> String { String(format: "%02d:00", h % 24) }

    static func analyze(_ input: [GlucoseReading]) -> HealthReport {
        var r = HealthReport()
        guard !input.isEmpty else { return r }

        let rs = input.sorted { $0.date < $1.date }
        let cal = Calendar.current

        r.valid = true
        r.count = rs.count
        r.firstDate = rs.first!.date
        r.lastDate = rs.last!.date

        var sum = 0.0
        r.minV = rs.first!.mgdl
        r.maxV = rs.first!.mgdl

        var hourSum  = [Double](repeating: 0, count: 24)
        var hourCnt  = [Int](repeating: 0, count: 24)
        var hourHigh = [Int](repeating: 0, count: 24)
        var hourLow  = [Int](repeating: 0, count: 24)
        var riseSum  = [Double](repeating: 0, count: 24)
        var riseCnt  = [Int](repeating: 0, count: 24)

        var prev: GlucoseReading? = nil

        for g in rs {
            let v = g.mgdl
            sum += v
            r.minV = min(r.minV, v)
            r.maxV = max(r.maxV, v)

            if v < veryLow       { r.nVeryLow += 1 }
            else if v < low      { r.nLow += 1 }
            else if v <= high    { r.nInRange += 1 }
            else if v <= veryHigh{ r.nHigh += 1 }
            else                 { r.nVeryHigh += 1 }

            let h = cal.component(.hour, from: g.date)
            if h >= 0 && h < 24 {
                hourSum[h] += v
                hourCnt[h] += 1
                if v > high { hourHigh[h] += 1 }
                if v < low  { hourLow[h] += 1 }
                if let p = prev {
                    let gapMin = g.date.timeIntervalSince(p.date) / 60.0
                    if gapMin > 0, gapMin <= 20 {
                        riseSum[h] += (v - p.mgdl)
                        riseCnt[h] += 1
                    }
                }
            }
            prev = g
        }

        r.avg = sum / Double(r.count)
        let varSum = rs.reduce(0.0) { $0 + ($1.mgdl - r.avg) * ($1.mgdl - r.avg) }
        r.sd  = (varSum / Double(r.count)).squareRoot()
        r.cv  = r.avg > 0 ? r.sd / r.avg * 100.0 : 0
        r.a1c = (r.avg + 46.7) / 28.7

        let n = Double(r.count)
        r.pctVeryLow  = 100.0 * Double(r.nVeryLow) / n
        r.pctLow      = 100.0 * Double(r.nVeryLow + r.nLow) / n
        r.pctInRange  = 100.0 * Double(r.nInRange) / n
        r.pctHigh     = 100.0 * Double(r.nHigh + r.nVeryHigh) / n
        r.pctVeryHigh = 100.0 * Double(r.nVeryHigh) / n

        for h in 0..<24 {
            var hs = HourStat()
            hs.count = hourCnt[h]
            if hourCnt[h] > 0 {
                hs.avg = hourSum[h] / Double(hourCnt[h])
                hs.highPct = 100.0 * Double(hourHigh[h]) / Double(hourCnt[h])
                hs.lowPct  = 100.0 * Double(hourLow[h]) / Double(hourCnt[h])
            }
            if riseCnt[h] > 0 { hs.rise = riseSum[h] / Double(riseCnt[h]) }
            r.hours[h] = hs
        }

        r.insights = buildInsights(r)
        return r
    }

    // Merge consecutive hours that satisfy `pred` into [start,end] index ranges.
    private static func windows(_ r: HealthReport, _ pred: (Int) -> Bool) -> [(Int, Int)] {
        var out: [(Int, Int)] = []
        var start = -1
        for h in 0..<24 {
            let ok = r.hours[h].count >= minHourSamples && pred(h)
            if ok && start < 0 { start = h }
            if !ok && start >= 0 { out.append((start, h - 1)); start = -1 }
        }
        if start >= 0 { out.append((start, 23)) }
        return out
    }

    private static func rangeText(_ a: Int, _ b: Int) -> String {
        "\(hourLabel(a)) - \(hourLabel((b + 1) % 24))"
    }

    private static func buildInsights(_ r: HealthReport) -> [HealthInsight] {
        var out: [HealthInsight] = []

        // 1. Safety first: hypoglycaemia
        if r.nVeryLow > 0 || r.pctLow >= 4.0 {
            var body = String(format:
                "Kayıtlarda düşük şeker (70 mg/dL altı) okumalarının oranı %%%.1f. Bunların %%%.1f kadarı ciddi düşük (54 mg/dL altı) seviyede. ",
                r.pctLow, r.pctVeryLow)
            let lows = windows(r) { r.hours[$0].lowPct >= 15.0 }
            if !lows.isEmpty {
                let parts = lows.map { rangeText($0.0, $0.1) }.joined(separator: ", ")
                body += "Düşükler özellikle \(parts) saatlerinde yoğunlaşıyor. Bu saatlerde öğün atlamamaya, yanınızda hızlı karbonhidrat (meyve suyu, glikoz tableti) bulundurmaya dikkat edin."
            } else {
                body += "Düşük şekerin tekrarladığı durumda öğün/atıştırmalık zamanlamanızı ve akşam içeceklerini (özellikle alkol) gözden geçirin."
            }
            out.append(HealthInsight(severity: .critical, title: "Düşük şeker (hipoglisemi) uyarısı", body: body))
        }

        // 2. High-glucose time windows (eating/drinking focus)
        let highs = windows(r) {
            if let a = r.hours[$0].avg { return a > high || r.hours[$0].highPct >= 50.0 }
            return false
        }
        for w in highs {
            var s = 0.0; var c = 0
            for h in w.0...w.1 { if let a = r.hours[h].avg { s += a; c += 1 } }
            let wavg = c > 0 ? Int((s / Double(c)).rounded()) : 0
            out.append(HealthInsight(
                severity: .warning,
                title: "Yüksek seyir: \(rangeText(w.0, w.1))",
                body: "Bu saat aralığında şeker ortalaması yaklaşık \(wavg) mg/dL ile hedefin (70-180) üstünde. Bu pencerede öğünlerinizi daha hafif tutmayı, hızlı karbonhidratı (beyaz ekmek, şeker, meyve suyu) azaltmayı; önce sebze/protein sonra karbonhidrat yemeyi ve öğün sonrası 10-15 dakikalık kısa bir yürüyüşü deneyin. Bu saatlerde şekerli ve gazlı içecekler yerine su tercih edin."))
        }

        // 3. When the sharpest rises happen (often post-meal)
        var riseHour = -1; var riseMax = 0.0
        for h in 0..<24 where r.hours[h].count >= minHourSamples {
            if r.hours[h].rise > riseMax { riseMax = r.hours[h].rise; riseHour = h }
        }
        if riseHour >= 0 && riseMax >= 6.0 {
            out.append(HealthInsight(
                severity: .info,
                title: "En hızlı yükseliş: \(hourLabel(riseHour)) civarı",
                body: "Şeker en hızlı bu saat civarında yükseliyor; bu genellikle bir öğüne denk gelir. Bu öğünde porsiyonu bölmek, daha düşük glisemik indeksli seçimler yapmak ve yemeği yavaş yemek yükselişi yumuşatabilir."))
        }

        // 4. Overnight lows (00:00 - 06:00)
        var nightLow = 0; var nightCnt = 0
        for h in 0...5 {
            nightLow += Int((r.hours[h].lowPct * Double(r.hours[h].count) / 100.0).rounded())
            nightCnt += r.hours[h].count
        }
        if nightCnt >= minHourSamples && nightLow > 0 {
            out.append(HealthInsight(
                severity: .warning,
                title: "Gece düşükleri (00:00 - 06:00)",
                body: "Gece saatlerinde düşük şeker okumaları var. Akşam öğününün zamanlamasını, geç saatte yoğun egzersizi ve akşam alkol tüketimini gözden geçirmek faydalı olabilir. Yatmadan önce şeker seyrini kontrol etmeyi alışkanlık haline getirin."))
        }

        // 5. Variability
        if r.cv >= 36.0 {
            out.append(HealthInsight(
                severity: .warning,
                title: "Yüksek değişkenlik",
                body: "Şeker değişkenliğiniz (CV \(Int(r.cv.rounded()))%) hedefin üstünde; sağlıklı aralıkta CV değerinin 36 puanın altında olması önerilir, yani iniş-çıkışlarınız belirgin. Öğün saatlerini ve porsiyonları günden güne daha tutarlı tutmak, öğünleri atlamamak ve uyku düzenini korumak iniş-çıkışları azaltır."))
        }

        // 6. Time in range feedback
        let tir = Int(r.pctInRange.rounded())
        if r.pctInRange >= 70.0 {
            out.append(HealthInsight(
                severity: .tip,
                title: "Hedef aralıkta iyi gidiyorsunuz",
                body: "Okumaların \(tir)% kadarı hedef aralıkta (70-180). 70% üzeri iyi kabul edilir; bu düzeni korumaya devam edin."))
        } else {
            out.append(HealthInsight(
                severity: .info,
                title: "Hedef aralıkta kalma oranı",
                body: "Okumaların \(tir)% kadarı hedef aralıkta (70-180). Genel hedef 70% üstüdür. Yukarıdaki saat önerileri bu oranı artırmaya yardımcı olabilir."))
        }

        // 7. Always-on general tips
        out.append(HealthInsight(
            severity: .tip,
            title: "Genel öneriler",
            body: """
            • Gün boyu yeterli su için; susuzluk şekeri yükseltebilir.
            • Öğünlerde önce lif/sebze ve protein, sonra karbonhidrat tüketin.
            • Öğünlerden sonra 10-15 dakikalık yürüyüş şeker piklerini azaltır.
            • Öğün ve uyku saatlerini mümkün olduğunca sabit tutun.
            • Şekerli/gazlı içecekler yerine su veya şeker içermeyen seçenekleri tercih edin.
            """))

        // 8. Disclaimer (kept last)
        out.append(HealthInsight(
            severity: .info,
            title: "Önemli not",
            body: "Bu analiz yalnızca kendi ölçüm geçmişinizden çıkarılan genel bir değerlendirmedir; tıbbi teşhis ya da tedavi önerisi değildir. İlaç/insülin dozunuzu kendi başınıza değiştirmeyin. Tekrarlayan düşük/yüksek şeker veya bu önerilerle ilgili kararlar için hekiminize ve diyetisyeninize danışın."))

        return out
    }
}

// MARK: - View

struct HealthStatusView: View {
    let readings: [GlucoseReading]
    @Environment(\.dismiss) private var dismiss

    private var report: HealthReport { HealthAdvisor.analyze(readings) }

    private func color(for v: Double?) -> Color {
        guard let v = v else { return Color(white: 0.17) }
        if v < HealthAdvisor.low { return .red }
        if v > HealthAdvisor.veryHigh { return Color(red: 0.75, green: 0.22, blue: 0.17) }
        if v > HealthAdvisor.high { return .orange }
        return .green
    }

    var body: some View {
        let r = report
        NavigationStack {
            ZStack {
                Color(red: 0.07, green: 0.09, blue: 0.13).ignoresSafeArea()
                if !r.valid {
                    VStack(spacing: 8) {
                        Text("Henüz analiz edilecek kayıt yok.")
                            .foregroundStyle(Color(white: 0.75))
                        Text("Önce cihaza bağlanıp verileri yükleyin.")
                            .font(.footnote).foregroundStyle(Color(white: 0.5))
                    }
                } else {
                    ScrollView {
                        VStack(alignment: .leading, spacing: 16) {
                            header(r)
                            metricTiles(r)
                            tirSection(r)
                            hourSection(r)
                            adviceSection(r)
                        }
                        .padding(20)
                    }
                }
            }
            .navigationTitle("Health Status")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Kapat") { dismiss() }
                }
            }
        }
    }

    // MARK: Sections

    private func header(_ r: HealthReport) -> some View {
        let f = DateFormatter(); f.dateFormat = "dd.MM.yyyy"
        return Text("\(r.count) ölçüm  ·  \(f.string(from: r.firstDate)) - \(f.string(from: r.lastDate))")
            .font(.footnote)
            .foregroundStyle(Color(white: 0.55))
    }

    private func metricTiles(_ r: HealthReport) -> some View {
        let tirColor: Color = r.pctInRange >= 70 ? .green : .orange
        let cvColor: Color = r.cv < 36 ? .green : .orange
        return LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 10) {
            tile("\(Int(r.avg.rounded())) mg/dL", "Ortalama", Color(white: 0.92))
            tile(String(format: "%.1f%%", r.a1c), "Tahmini HbA1c", Color(white: 0.92))
            tile("\(Int(r.pctInRange.rounded()))%", "Hedef Aralık (70-180)", tirColor)
            tile("\(Int(r.cv.rounded()))%", "Değişkenlik (CV)", cvColor)
        }
    }

    private func tile(_ value: String, _ caption: String, _ valueColor: Color) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(value).font(.title3.weight(.bold)).foregroundStyle(valueColor)
            Text(caption).font(.caption2).foregroundStyle(Color(white: 0.6))
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 12).padding(.vertical, 10)
        .background(Color(red: 0.11, green: 0.14, blue: 0.19))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }

    private func tirSection(_ r: HealthReport) -> some View {
        let segs: [(Double, Color)] = [
            (r.pctLow - r.pctVeryLow, .red),
            (r.pctVeryLow, Color(red: 0.75, green: 0.22, blue: 0.17)),
            (r.pctInRange, .green),
            (r.pctHigh - r.pctVeryHigh, .orange),
            (r.pctVeryHigh, Color(red: 0.75, green: 0.22, blue: 0.17)),
        ]
        return VStack(alignment: .leading, spacing: 6) {
            Text("Zaman dağılımı").font(.subheadline.weight(.semibold)).foregroundStyle(Color(white: 0.8))
            GeometryReader { geo in
                HStack(spacing: 0) {
                    ForEach(Array(segs.enumerated()), id: \.offset) { _, s in
                        if s.0 > 0 {
                            Rectangle().fill(s.1)
                                .frame(width: max(0, geo.size.width * s.0 / 100.0))
                        }
                    }
                }
            }
            .frame(height: 22)
            .clipShape(RoundedRectangle(cornerRadius: 6))
            Text(String(format: "Hedefte %d%%   ·   Yüksek %d%%   ·   Düşük %d%%",
                        Int(r.pctInRange.rounded()), Int(r.pctHigh.rounded()), Int(r.pctLow.rounded())))
                .font(.caption2).foregroundStyle(Color(white: 0.55))
        }
    }

    private func hourSection(_ r: HealthReport) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Saatlik seyir (gün içi ortalama)")
                .font(.subheadline.weight(.semibold)).foregroundStyle(Color(white: 0.8))
            HStack(alignment: .bottom, spacing: 1) {
                ForEach(0..<24, id: \.self) { h in
                    VStack(spacing: 2) {
                        RoundedRectangle(cornerRadius: 1)
                            .fill(color(for: r.hours[h].avg))
                            .frame(height: 30)
                        if h % 3 == 0 {
                            Text("\(h)").font(.system(size: 7)).foregroundStyle(Color(white: 0.5))
                        } else {
                            Text(" ").font(.system(size: 7))
                        }
                    }
                }
            }
        }
    }

    private func adviceSection(_ r: HealthReport) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Öneriler").font(.subheadline.weight(.semibold)).foregroundStyle(Color(white: 0.8))
            ForEach(r.insights) { ins in
                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 8) {
                        Text(ins.severity.chip)
                            .font(.system(size: 10, weight: .bold))
                            .tracking(1)
                            .foregroundStyle(ins.severity.accent)
                        Text(ins.title)
                            .font(.subheadline.weight(.bold))
                            .foregroundStyle(Color(white: 0.92))
                        Spacer(minLength: 0)
                    }
                    Text(ins.body)
                        .font(.footnote)
                        .foregroundStyle(Color(white: 0.78))
                        .fixedSize(horizontal: false, vertical: true)
                }
                .padding(14)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Color(red: 0.11, green: 0.14, blue: 0.19))
                .overlay(
                    Rectangle().fill(ins.severity.accent).frame(width: 4),
                    alignment: .leading
                )
                .clipShape(RoundedRectangle(cornerRadius: 10))
            }
        }
    }
}

#Preview {
    let now = Date()
    let demo: [GlucoseReading] = (0..<400).map { i in
        let d = now.addingTimeInterval(Double(-i) * 300)
        let hour = Calendar.current.component(.hour, from: d)
        let bump = (hour >= 13 && hour <= 16) ? 60.0 : 0
        return GlucoseReading(offsetMin: Double(i) * 5, date: d, mgdl: 120 + bump + Double(i % 13) * 2)
    }
    return HealthStatusView(readings: demo)
}
