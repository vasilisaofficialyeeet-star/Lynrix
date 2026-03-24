// SystemMonitorView.swift — Glassmorphism 2026 System Monitor (Lynrix v2.5)

import SwiftUI
import Charts

struct SystemMonitorView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    @ObservedObject private var layout = WidgetLayoutManager.shared
    
    private func w(_ key: String) -> Bool {
        layout.isVisible(WidgetID(screen: .systemMonitor, key: key))
    }
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                HStack {
                    Spacer()
                    WidgetVisibilityMenu(screen: .systemMonitor)
                }
                .padding(.bottom, -8)
                
                HStack(spacing: 12) {
                    if w("resources") { resourcesCard }
                    if w("throughput") { throughputCard }
                }
                
                HStack(spacing: 12) {
                    if w("latency") { latencyCard }
                    if w("network") { networkCard }
                }
                
                if w("inference") { inferenceCard }
                if w("stageHistograms") { stageHistogramsCard }
                
                if w("jitterAlerts") && !jitterAlerts.isEmpty {
                    jitterAlertsCard
                }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - Resources
    
    private var resourcesCard: some View {
        let sys = engine.systemMonitor
        let cpuColor = sys.cpuUsagePct > 80 ? LxColor.bloodRed : (sys.cpuUsagePct > 50 ? LxColor.amber : LxColor.neonLime)
        return GlassPanel(neon: cpuColor) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("sysmon.resources"), icon: "cpu", color: cpuColor)
                GlassMetric(loc.t("sysmon.cpu"), value: String(format: "%.1f%%", sys.cpuUsagePct), color: cpuColor)
                GlassMetric(loc.t("sysmon.cpuCores"), value: "\(sys.cpuCores)", color: theme.textPrimary)
                GlassMetric(loc.t("sysmon.memory"), value: String(format: "%.1f MB", sys.memoryUsedMb),
                            color: sys.memoryUsedMb > 500 ? LxColor.amber : theme.textPrimary)
                GlassMetric(loc.t("sysmon.peakMemory"), value: String(format: "%.1f MB", Double(sys.memoryPeakBytes) / 1_048_576), color: theme.textPrimary)
                GlassMetric(loc.t("sysmon.uptime"), value: formatUptime(sys.uptimeHours), color: theme.textPrimary)
                
                if sys.gpuAvailable {
                    Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                    GlassMetric(loc.t("sysmon.gpu"), value: sys.gpuName, color: LxColor.electricCyan)
                    GlassMetric(loc.t("sysmon.gpuUsage"), value: String(format: "%.1f%%", sys.gpuUsagePct), color: LxColor.electricCyan)
                }
            }
        }
    }
    
    // MARK: - Throughput
    
    private var throughputCard: some View {
        let sys = engine.systemMonitor
        return GlassPanel(neon: LxColor.neonLime) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("sysmon.throughput"), icon: "speedometer", color: LxColor.neonLime)
                GlassMetric(loc.t("sysmon.ticksSec"), value: String(format: "%.0f", sys.ticksPerSec),
                            color: sys.ticksPerSec > 50 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("sysmon.signalsSec"), value: String(format: "%.1f", sys.signalsPerSec), color: theme.textPrimary)
                GlassMetric(loc.t("sysmon.ordersSec"), value: String(format: "%.1f", sys.ordersPerSec), color: theme.textPrimary)
                Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                GlassMetric(loc.t("sysmon.obUpdates"), value: formatCount(engine.metrics.obUpdates), color: theme.textPrimary)
                GlassMetric(loc.t("sysmon.trades"), value: formatCount(engine.metrics.tradesTotal), color: theme.textPrimary)
                GlassMetric(loc.t("sysmon.signals"), value: formatCount(engine.metrics.signalsTotal), color: theme.textPrimary)
                GlassMetric(loc.t("sysmon.ordersSent"), value: formatCount(engine.metrics.ordersSent), color: theme.textPrimary)
                GlassMetric(loc.t("sysmon.ordersFilled"), value: formatCount(engine.metrics.ordersFilled), color: LxColor.neonLime)
            }
        }
    }
    
    // MARK: - Latency
    
    private var latencyCard: some View {
        let sys = engine.systemMonitor
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("sysmon.latency"), icon: "clock.fill", color: LxColor.electricCyan)
                neonLatencyRow(loc.t("sysmon.websocket"), p50: sys.wsLatencyP50Us, p99: sys.wsLatencyP99Us)
                neonLatencyRow(loc.t("sysmon.features"), p50: sys.featLatencyP50Us, p99: sys.featLatencyP99Us)
                neonLatencyRow(loc.t("sysmon.model"), p50: sys.modelLatencyP50Us, p99: sys.modelLatencyP99Us)
                neonLatencyRow(loc.t("sysmon.endToEnd"), p50: sys.e2eLatencyP50Us, p99: sys.e2eLatencyP99Us)
                Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                GlassMetric(loc.t("sysmon.exchangeRtt"), value: String(format: "%.1f ms", sys.exchangeLatencyMs),
                            color: sys.exchangeLatencyMs > 100 ? LxColor.bloodRed : (sys.exchangeLatencyMs > 50 ? LxColor.amber : LxColor.neonLime))
            }
        }
    }
    
    // MARK: - Network
    
    private var networkCard: some View {
        let met = engine.metrics
        return GlassPanel(neon: LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("sysmon.network"), icon: "network", color: LxColor.magentaPink)
                GlassMetric(loc.t("sysmon.wsReconnects"), value: "\(met.wsReconnects)",
                            color: met.wsReconnects > 0 ? LxColor.amber : LxColor.neonLime)
                GlassMetric(loc.t("sysmon.ordersCancelled"), value: formatCount(met.ordersCancelled), color: theme.textPrimary)
                Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                if #available(macOS 14.0, *), !engine.drawdownHistory.isEmpty {
                    Text(loc.t("sysmon.drawdownHistory"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    Chart {
                        ForEach(Array(engine.drawdownHistory.enumerated()), id: \.offset) { idx, val in
                            AreaMark(x: .value("T", idx), y: .value("DD", val * 100))
                                .foregroundStyle(LxColor.magentaPink.opacity(0.3))
                        }
                    }
                    .frame(height: 60)
                    .chartYAxis { AxisMarks(position: .leading) }
                }
            }
        }
    }
    
    // MARK: - Inference
    
    private var inferenceCard: some View {
        let sys = engine.systemMonitor
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("sysmon.mlInference"), icon: "brain.head.profile", color: LxColor.electricCyan)
                    Spacer()
                    StatusBadge(sys.inferenceBackend, color: sys.inferenceBackend == "CPU" ? LxColor.electricCyan : LxColor.neonLime)
                    if engine.accuracy.usingOnnx {
                        StatusBadge(loc.t("ai.onnx"), color: LxColor.magentaPink)
                    }
                }
                HStack(spacing: 24) {
                    VStack(alignment: .leading, spacing: 4) {
                        GlassMetric(loc.t("sysmon.inferenceP50"), value: String(format: "%.0f µs", sys.modelLatencyP50Us), color: LxColor.electricCyan)
                        GlassMetric(loc.t("sysmon.inferenceP99"), value: String(format: "%.0f µs", sys.modelLatencyP99Us),
                                    color: sys.modelLatencyP99Us > 1000 ? LxColor.amber : theme.textPrimary)
                    }
                    VStack(alignment: .leading, spacing: 4) {
                        GlassMetric(loc.t("sysmon.gpuAvailable"), value: sys.gpuAvailable ? loc.t("common.yes") : loc.t("common.no"),
                                    color: sys.gpuAvailable ? LxColor.neonLime : LxColor.coolSteel)
                        GlassMetric(loc.t("sysmon.gpuName"), value: sys.gpuName, color: theme.textPrimary)
                    }
                }
            }
        }
    }
    
    // MARK: - Per-Stage HdrHistogram
    
    private var stageHistogramsCard: some View {
        GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("sysmon.stageHistograms"), icon: "chart.bar.xaxis", color: LxColor.gold)
                    Spacer()
                    Text("\(engine.stageHistograms.count) \(loc.t("sysmon.stages"))")
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
                
                if engine.stageHistograms.isEmpty {
                    Text(loc.t("sysmon.noHistograms"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                        .frame(maxWidth: .infinity, alignment: .center)
                        .padding()
                } else {
                    HStack {
                        Text(loc.t("sysmon.stage")).frame(width: 100, alignment: .leading)
                        Text(loc.t("sysmon.count")).frame(width: 60, alignment: .trailing)
                        Text(loc.t("sysmon.mean")).frame(width: 55, alignment: .trailing)
                        Text(loc.t("sysmon.p50")).frame(width: 55, alignment: .trailing)
                        Text(loc.t("sysmon.p90")).frame(width: 55, alignment: .trailing)
                        Text(loc.t("sysmon.p99")).frame(width: 55, alignment: .trailing)
                        Text(loc.t("sysmon.p999")).frame(width: 55, alignment: .trailing)
                        Text(loc.t("sysmon.max")).frame(width: 60, alignment: .trailing)
                        Text(loc.t("sysmon.stddev")).frame(width: 55, alignment: .trailing)
                    }
                    .font(LxFont.mono(9, weight: .bold))
                    .foregroundColor(theme.textTertiary)
                    
                    ForEach(engine.stageHistograms) { h in
                        HStack {
                            HStack(spacing: 4) {
                                if h.jitterAlert {
                                    Image(systemName: "exclamationmark.triangle.fill")
                                        .font(.system(size: 8))
                                        .foregroundColor(LxColor.amber)
                                        .shadow(color: LxColor.amber.opacity(0.4), radius: 2)
                                }
                                Text(h.stageName)
                            }
                            .frame(width: 100, alignment: .leading)
                            .foregroundColor(theme.textSecondary)
                            Text(formatHistCount(h.count)).frame(width: 60, alignment: .trailing)
                                .foregroundColor(theme.textPrimary)
                            Text(fmtUs(h.meanUs)).frame(width: 55, alignment: .trailing)
                                .foregroundColor(theme.textPrimary)
                            Text(fmtUs(h.p50Us)).frame(width: 55, alignment: .trailing)
                                .foregroundColor(theme.textPrimary)
                            Text(fmtUs(h.p90Us)).frame(width: 55, alignment: .trailing)
                                .foregroundColor(theme.textPrimary)
                            Text(fmtUs(h.p99Us)).frame(width: 55, alignment: .trailing)
                                .foregroundColor(h.p99Us > 100 ? LxColor.amber : theme.textPrimary)
                            Text(fmtUs(h.p999Us)).frame(width: 55, alignment: .trailing)
                                .foregroundColor(h.p999Us > 500 ? LxColor.bloodRed : theme.textPrimary)
                            Text(fmtUs(h.maxUs)).frame(width: 60, alignment: .trailing)
                                .foregroundColor(h.maxUs > 1000 ? LxColor.bloodRed : theme.textPrimary)
                            Text(fmtUs(h.stddevUs)).frame(width: 55, alignment: .trailing)
                                .foregroundColor(theme.textPrimary)
                        }
                        .font(LxFont.mono(10))
                    }
                }
            }
        }
    }
    
    // MARK: - Jitter Alerts
    
    private var jitterAlerts: [StageHistogramModel] {
        engine.stageHistograms.filter { $0.jitterAlert }
    }
    
    private var jitterAlertsCard: some View {
        GlassPanel(neon: LxColor.amber) {
            VStack(alignment: .leading, spacing: 8) {
                HStack(spacing: 6) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundColor(LxColor.amber)
                        .shadow(color: LxColor.amber.opacity(0.4), radius: 3)
                    Text(loc.t("sysmon.jitterAlerts"))
                        .font(LxFont.mono(10, weight: .bold))
                        .foregroundColor(LxColor.amber)
                }
                
                ForEach(jitterAlerts) { h in
                    HStack {
                        Text(h.stageName)
                            .font(LxFont.mono(10))
                            .foregroundColor(theme.textPrimary)
                        Spacer()
                        if h.p99Us > 100 {
                            Text(String(format: "p99=%.0f µs", h.p99Us))
                                .font(LxFont.mono(9))
                                .foregroundColor(LxColor.amber)
                        }
                        if h.maxUs > 1000 {
                            Text(String(format: "max=%.0f µs", h.maxUs))
                                .font(LxFont.mono(9))
                                .foregroundColor(LxColor.bloodRed)
                        }
                    }
                }
            }
        }
    }
    
    // MARK: - Helpers
    
    private func neonLatencyRow(_ label: String, p50: Double, p99: Double) -> some View {
        HStack {
            Text(label)
                .font(LxFont.mono(10))
                .foregroundColor(theme.textTertiary)
                .frame(width: 80, alignment: .leading)
            Spacer()
            Text("\(loc.t("sysmon.p50")): \(String(format: "%.0f", p50))")
                .font(LxFont.mono(10))
                .foregroundColor(LxColor.electricCyan)
            Text("\(loc.t("sysmon.p99")): \(String(format: "%.0f", p99))")
                .font(LxFont.mono(10))
                .foregroundColor(p99 > 1000 ? LxColor.amber : theme.textPrimary)
        }
    }
    
    private func formatUptime(_ hours: Double) -> String {
        if hours < 1 { return String(format: "%.0f min", hours * 60) }
        if hours < 24 { return String(format: "%.1f h", hours) }
        return String(format: "%.1f d", hours / 24)
    }
    
    private func formatCount(_ count: UInt64) -> String {
        if count >= 1_000_000 { return String(format: "%.1fM", Double(count) / 1_000_000) }
        if count >= 1_000 { return String(format: "%.1fK", Double(count) / 1_000) }
        return "\(count)"
    }
    
    private func formatHistCount(_ count: UInt64) -> String {
        if count >= 1_000_000 { return String(format: "%.1fM", Double(count) / 1_000_000) }
        if count >= 1_000 { return String(format: "%.1fK", Double(count) / 1_000) }
        return "\(count)"
    }
    
    private func fmtUs(_ us: Double) -> String {
        if us < 1 { return String(format: "%.2f", us) }
        if us < 100 { return String(format: "%.1f", us) }
        return String(format: "%.0f", us)
    }
}
