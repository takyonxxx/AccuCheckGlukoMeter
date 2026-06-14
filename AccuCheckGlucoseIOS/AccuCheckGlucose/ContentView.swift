import SwiftUI
import Charts

struct ContentView: View {
    @EnvironmentObject var ble: BLEManager
    @Environment(\.verticalSizeClass) private var vSize

    enum Range: String, CaseIterable, Identifiable {
        case h8 = "8 Saat", h24 = "24 Saat", all = "Tümü"
        var id: String { rawValue }
        var hours: Double? { self == .h8 ? 8 : (self == .h24 ? 24 : nil) }
    }
    @State private var range: Range = .all
    @State private var showHealth = false

    private var isLandscape: Bool { vSize == .compact }

    private func color(for value: Double) -> Color {
        if value < 70 { return .red }
        if value > 180 { return .orange }
        return .green
    }
    private func rangeLabel(for value: Double) -> String {
        if value < 70 { return "Low" }
        if value > 180 { return "High" }
        return "In range"
    }

    // Average glucose + estimated HbA1c over ALL cached records (ADAG).
    private var averages: (avg: Double, a1c: Double)? {
        let vals = ble.readings.map { $0.mgdl }
        guard !vals.isEmpty else { return nil }
        let avg = vals.reduce(0, +) / Double(vals.count)
        return (avg, (avg + 46.7) / 28.7)
    }

    // MARK: - Chart window
    private struct ChartWindow {
        let pts: [GlucoseReading]
        let start: Date; let end: Date
        let strideHours: Int
        let yMin: Double; let yMax: Double
        let scrollable: Bool
        let visibleSeconds: Double
        let initialX: Date
    }

    private func window() -> ChartWindow? {
        let all = ble.readings
        guard let firstDate = all.first?.date, let lastDate = all.last?.date else { return nil }

        let scrollable = (range == .all)
        var start: Date
        var end = lastDate
        let visibleSeconds: Double = 24 * 3600
        var initialX = lastDate

        if let h = range.hours {                 // 8 / 24 : fixed window
            start = lastDate.addingTimeInterval(-h * 3600)
        } else {                                 // Tümü : full span, scroll a 24h window
            start = firstDate
            initialX = lastDate.addingTimeInterval(-24 * 3600)
        }
        if end.timeIntervalSince(start) < 3600 {
            start = end.addingTimeInterval(-1800)
            end = end.addingTimeInterval(1800)
        }

        let pts = all.filter { $0.date >= start && $0.date <= end }
        let values = pts.map { $0.mgdl }
        let yMax = max(300, (values.max() ?? 300) + 20)
        let yMin = min(50, (values.min() ?? 50) - 10)
        let strideHours = (range == .h8) ? 1 : 4

        return ChartWindow(pts: pts, start: start, end: end, strideHours: strideHours,
                           yMin: yMin, yMax: yMax, scrollable: scrollable,
                           visibleSeconds: visibleSeconds, initialX: initialX)
    }

    // MARK: - Body (responsive)
    var body: some View {
        ZStack {
            Color(red: 0.07, green: 0.09, blue: 0.13).ignoresSafeArea()

            if isLandscape {
                HStack(spacing: 16) {
                    VStack(spacing: 10) {
                        titleView
                        valueView(big: false)
                        averagesView
                        statusView
                        Spacer(minLength: 0)
                        buttonsView
                    }
                    .frame(maxWidth: 300)

                    VStack(spacing: 8) {
                        chartCard.frame(maxHeight: .infinity)
                        pickerView
                    }
                    .frame(maxWidth: .infinity)
                }
                .padding(.horizontal, 16).padding(.vertical, 10)
            } else {
                VStack(spacing: 14) {
                    titleView
                    statusView
                    valueView(big: true)
                    averagesView
                    chartCard.frame(minHeight: 260)
                    pickerView
                    Spacer(minLength: 0)
                    buttonsView
                }
                .padding(20)
            }
        }
        .sheet(isPresented: $showHealth) {
            HealthStatusView(readings: ble.readings)
        }
    }

    // MARK: - Pieces
    private var titleView: some View {
        Text("Accu-Chek SmartGuide")
            .font(.headline)
            .foregroundStyle(Color(white: 0.80))
            .padding(.top, isLandscape ? 0 : 8)
    }

    @ViewBuilder private var statusView: some View {
        VStack(spacing: 4) {
            Text(ble.status)
                .font(.footnote)
                .multilineTextAlignment(.center)
                .foregroundStyle(Color(white: 0.92))
                .padding(.horizontal, 14).padding(.vertical, 6)
                .background(Color(red: 0.13, green: 0.17, blue: 0.24))
                .clipShape(Capsule())

            if ble.monitoring, let target = ble.nextUpdateAt {
                TimelineView(.periodic(from: .now, by: 1)) { context in
                    let remaining = max(0, Int(target.timeIntervalSince(context.date).rounded()))
                    Text(remaining > 0
                         ? String(format: "next update in %d:%02d", remaining / 60, remaining % 60)
                         : "updating…")
                        .font(.caption2.weight(.medium))
                        .foregroundStyle(Color(white: 0.62))
                }
            } else if ble.monitoring {
                Text("syncing…")
                    .font(.caption2.weight(.medium))
                    .foregroundStyle(Color(white: 0.62))
            }
        }
    }

    @ViewBuilder private func valueView(big: Bool) -> some View {
        VStack(spacing: 2) {
            if let v = ble.latest {
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Text("\(Int(v.rounded()))")
                        .font(.system(size: big ? 76 : 52, weight: .bold))
                        .foregroundStyle(color(for: v))
                    Text(ble.trendArrow)
                        .font(.system(size: big ? 36 : 26, weight: .bold))
                        .foregroundStyle(color(for: v))
                }
                Text("mg/dL · \(rangeLabel(for: v))")
                    .font(.subheadline)
                    .foregroundStyle(color(for: v))
            } else {
                Text("--")
                    .font(.system(size: big ? 76 : 52, weight: .bold))
                    .foregroundStyle(Color(white: 0.4))
                Text("mg/dL").font(.subheadline).foregroundStyle(Color(white: 0.5))
            }
        }
    }

    @ViewBuilder private func metric(_ title: String, _ value: String) -> some View {
        VStack(spacing: 1) {
            Text(value).font(.subheadline.weight(.semibold)).foregroundStyle(Color(white: 0.92))
            Text(title).font(.caption2).foregroundStyle(Color(white: 0.55))
        }
        .padding(.horizontal, 12).padding(.vertical, 6)
        .background(Color(red: 0.12, green: 0.15, blue: 0.21))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }

    @ViewBuilder private var averagesView: some View {
        if let a = averages {
            HStack(spacing: 12) {
                metric("Avg", "\(Int(a.avg.rounded())) mg/dL")
                metric("Est. HbA1c", String(format: "%.1f%%", a.a1c))
            }
        }
    }

    private var pickerView: some View {
        Picker("", selection: $range) {
            ForEach(Range.allCases) { Text($0.rawValue).tag($0) }
        }
        .pickerStyle(.segmented)
    }

    @ViewBuilder private var buttonsView: some View {
        VStack(spacing: 10) {
            Button(action: { ble.monitoring ? ble.disconnect() : ble.connect() }) {
                Text(ble.monitoring ? "Stop"
                                    : (ble.isBusy ? "Working…" : "Connect & Load"))
                    .frame(maxWidth: .infinity).frame(height: 50)
                    .background(ble.monitoring ? Color(red: 0.80, green: 0.30, blue: 0.30)
                                               : Color(red: 0.20, green: 0.46, blue: 0.95))
                    .foregroundStyle(.white).font(.headline)
                    .clipShape(RoundedRectangle(cornerRadius: 10))
            }
            .disabled(ble.isBusy && !ble.monitoring)

            Button(action: { showHealth = true }) {
                Text("Health Status")
                    .frame(maxWidth: .infinity).frame(height: 42)
                    .foregroundStyle(Color(white: 0.9)).font(.headline)
                    .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color(white: 0.32)))
            }

            if !ble.deviceName.isEmpty {
                Text(ble.deviceName).font(.caption).foregroundStyle(Color(white: 0.5))
            }
        }
    }

    @ViewBuilder private var chartCard: some View {
        chart
            .frame(maxWidth: .infinity)
            .padding(.vertical, 14).padding(.horizontal, 10)
            .background(Color(red: 0.96, green: 0.97, blue: 0.98))
            .clipShape(RoundedRectangle(cornerRadius: 14))
    }

    @ViewBuilder private var chart: some View {
        if let w = window() {
            if w.scrollable, #available(iOS 17.0, *) {
                baseChart(w)
                    .chartXScale(domain: w.start...w.end)
                    .chartScrollableAxes(.horizontal)
                    .chartXVisibleDomain(length: w.visibleSeconds)
                    .chartScrollPosition(initialX: w.initialX)
            } else {
                baseChart(w)
                    .chartXScale(domain: w.start...w.end)
            }
        } else {
            VStack { Spacer(); Text("No history yet").foregroundStyle(Color(white: 0.45)); Spacer() }
        }
    }

    @ViewBuilder private func baseChart(_ w: ChartWindow) -> some View {
        Chart {
            RectangleMark(yStart: .value("low", 70), yEnd: .value("high", 180))
                .foregroundStyle(.green.opacity(0.16))
            RuleMark(y: .value("low", 70))
                .lineStyle(StrokeStyle(lineWidth: 1, dash: [5, 4]))
                .foregroundStyle(Color(white: 0.55))
            RuleMark(y: .value("high", 180))
                .lineStyle(StrokeStyle(lineWidth: 1, dash: [5, 4]))
                .foregroundStyle(Color(white: 0.55))
            RuleMark(y: .value("veryhigh", 250))
                .lineStyle(StrokeStyle(lineWidth: 1))
                .foregroundStyle(.orange.opacity(0.7))

            ForEach(w.pts) { r in
                LineMark(x: .value("Time", r.date), y: .value("mg/dL", r.mgdl))
                    .interpolationMethod(.monotone)
                    .lineStyle(StrokeStyle(lineWidth: 2.2))
                    .foregroundStyle(Color(red: 0.16, green: 0.45, blue: 0.82))
            }
            if let last = w.pts.last {
                PointMark(x: .value("Time", last.date), y: .value("mg/dL", last.mgdl))
                    .foregroundStyle(color(for: last.mgdl))
                    .symbolSize(120)
            }
        }
        .chartYScale(domain: w.yMin...w.yMax)
        .chartYAxis {
            AxisMarks(position: .trailing, values: [54, 70, 180, 250, 300]) { _ in
                AxisGridLine().foregroundStyle(Color(white: 0.80))
                AxisTick().foregroundStyle(Color(white: 0.80))
                AxisValueLabel().font(.caption.weight(.semibold)).foregroundStyle(Color(white: 0.28))
            }
        }
        .chartXAxis {
            AxisMarks(values: .stride(by: .hour, count: w.strideHours)) { value in
                AxisGridLine().foregroundStyle(Color(white: 0.80).opacity(0.7))
                AxisTick().foregroundStyle(Color(white: 0.80))
                AxisValueLabel(format: .dateTime.hour(.twoDigits(amPM: .omitted)).minute(.twoDigits))
                    .font(.caption2.weight(.semibold)).foregroundStyle(Color(white: 0.28))
            }
        }
    }
}

#Preview {
    ContentView().environmentObject(BLEManager())
}
