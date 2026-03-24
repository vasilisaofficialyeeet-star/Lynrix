// DiagnosticsView.swift — Glassmorphism 2026 Diagnostics (Lynrix v2.5)

import SwiftUI
import os.log

struct DiagnosticsView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    @ObservedObject private var layout = WidgetLayoutManager.shared
    
    private func w(_ key: String) -> Bool {
        layout.isVisible(WidgetID(screen: .diagnostics, key: key))
    }
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                HStack {
                    Spacer()
                    WidgetVisibilityMenu(screen: .diagnostics)
                }
                .padding(.bottom, -8)
                
                if w("system") { systemCard }
                if w("engine") { engineStateCard }
                if w("data") { dataStateCard }
                if w("performance") { performanceCard }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - System Info
    
    private var systemCard: some View {
        GlassPanel(neon: LxColor.electricCyan) {
                    VStack(alignment: .leading, spacing: 8) {
                        GlassSectionHeader(loc.t("diag.systemDiagnostics"), icon: "cpu", color: LxColor.electricCyan)
                        DiagRow(label: loc.t("diag.version"), value: appVersion, color: LxColor.electricCyan)
                        DiagRow(label: loc.t("diag.macos"), value: ProcessInfo.processInfo.operatingSystemVersionString)
                        DiagRow(label: loc.t("diag.cpuCores"), value: "\(ProcessInfo.processInfo.processorCount)")
                        DiagRow(label: loc.t("diag.ram"), value: "\(ProcessInfo.processInfo.physicalMemory / 1_073_741_824) GB")
                        DiagRow(label: loc.t("diag.processId"), value: "\(ProcessInfo.processInfo.processIdentifier)")
                        DiagRow(label: loc.t("diag.uptime"), value: formatUptime(ProcessInfo.processInfo.systemUptime))
                    }
                }
    }
    
    // MARK: - Engine State
    
    private var engineStateCard: some View {
        GlassPanel(neon: LxColor.neonLime) {
                    VStack(alignment: .leading, spacing: 8) {
                        GlassSectionHeader(loc.t("diag.engineState"), icon: "gearshape.2", color: LxColor.neonLime)
                        DiagRow(label: loc.t("diag.status"), value: loc.t(engine.status.locKey), color: statusColor)
                        DiagRow(label: loc.t("diag.mode"), value: engine.paperMode ? loc.t("common.paper") : loc.t("common.live"),
                                color: engine.paperMode ? LxColor.electricCyan : LxColor.bloodRed)
                        DiagRow(label: loc.t("diag.symbol"), value: engine.config.symbol)
                        DiagRow(label: loc.t("diag.orderQty"), value: String(format: "%.4f", engine.config.orderQty))
                        DiagRow(label: loc.t("diag.signalThreshold"), value: String(format: "%.2f", engine.config.signalThreshold))
                        DiagRow(label: loc.t("diag.reconnecting"), value: engine.isReconnecting ? loc.t("common.yes") : loc.t("common.no"),
                                color: engine.isReconnecting ? LxColor.amber : LxColor.neonLime)
                        DiagRow(label: loc.t("diag.chaosEnabled"), value: engine.chaosState.enabled ? loc.t("common.yes") : loc.t("common.no"),
                                color: engine.chaosState.enabled ? LxColor.amber : LxColor.neonLime)
                        DiagRow(label: loc.t("diag.replayLoaded"), value: engine.replayState.loaded ? loc.t("common.yes") : loc.t("common.no"))
                        DiagRow(label: loc.t("diag.rlTraining"), value: engine.rlv2State.trainingActive ? loc.t("common.active") : loc.t("common.idle"),
                                color: engine.rlv2State.trainingActive ? LxColor.neonLime : LxColor.coolSteel)
                    }
                }
    }
    
    // MARK: - Data State
    
    private var dataStateCard: some View {
        GlassPanel(neon: LxColor.magentaPink) {
                    VStack(alignment: .leading, spacing: 8) {
                        GlassSectionHeader(loc.t("diag.dataState"), icon: "chart.bar.doc.horizontal", color: LxColor.magentaPink)
                        DiagRow(label: loc.t("diag.obValid"), value: engine.orderBook.valid ? loc.t("common.yes") : loc.t("common.no"),
                                color: engine.orderBook.valid ? LxColor.neonLime : LxColor.bloodRed)
                        DiagRow(label: loc.t("diag.bidLevels"), value: "\(engine.orderBook.bids.count)")
                        DiagRow(label: loc.t("diag.askLevels"), value: "\(engine.orderBook.asks.count)")
                        DiagRow(label: loc.t("diag.bestBid"), value: String(format: "%.2f", engine.orderBook.bestBid))
                        DiagRow(label: loc.t("diag.bestAsk"), value: String(format: "%.2f", engine.orderBook.bestAsk))
                        DiagRow(label: loc.t("diag.logEntries"), value: "\(engine.logs.count)")
                        DiagRow(label: loc.t("diag.signals"), value: "\(engine.signals.count)")
                        DiagRow(label: loc.t("diag.trades"), value: "\(engine.trades.count)")
                        DiagRow(label: loc.t("diag.activeOrdersOSM"), value: "\(engine.osmSummary.activeOrders)")
                        DiagRow(label: loc.t("diag.stageHistograms"), value: "\(engine.stageHistograms.count)")
                    }
                }
    }
    
    // MARK: - Performance
    
    private var performanceCard: some View {
        GlassPanel(neon: LxColor.amber) {
                    VStack(alignment: .leading, spacing: 8) {
                        GlassSectionHeader(loc.t("diag.performance"), icon: "gauge.with.dots.needle.67percent", color: LxColor.amber)
                        DiagRow(label: loc.t("diag.obUpdates"), value: "\(engine.metrics.obUpdates)")
                        DiagRow(label: loc.t("diag.totalTrades"), value: "\(engine.metrics.tradesTotal)")
                        DiagRow(label: loc.t("diag.totalSignals"), value: "\(engine.metrics.signalsTotal)")
                        DiagRow(label: loc.t("diag.ordersSent"), value: "\(engine.metrics.ordersSent)")
                        DiagRow(label: loc.t("diag.wsReconnects"), value: "\(engine.metrics.wsReconnects)",
                                color: engine.metrics.wsReconnects > 0 ? LxColor.amber : LxColor.neonLime)
                        DiagRow(label: loc.t("diag.e2eP50"), value: String(format: "%.1f µs", engine.metrics.e2eLatencyP50Us), color: LxColor.electricCyan)
                        DiagRow(label: loc.t("diag.e2eP99"), value: String(format: "%.1f µs", engine.metrics.e2eLatencyP99Us), color: LxColor.electricCyan)
                        DiagRow(label: loc.t("diag.var99"), value: String(format: "$%.2f", abs(engine.varState.var99)), color: LxColor.magentaPink)
                        DiagRow(label: loc.t("diag.chaosInjections"), value: "\(engine.chaosState.totalInjections)",
                                color: engine.chaosState.totalInjections > 0 ? LxColor.bloodRed : theme.textPrimary)
                    }
                }
    }
    
    private var appVersion: String {
        let v = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "2.5.0"
        let b = Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "1"
        return "\(v) (\(b))"
    }
    
    private var statusColor: Color {
        switch engine.status {
        case .idle: return LxColor.coolSteel
        case .connecting: return LxColor.gold
        case .connected: return LxColor.electricCyan
        case .trading: return LxColor.neonLime
        case .error: return LxColor.bloodRed
        case .stopping: return LxColor.amber
        }
    }
    
    private func formatUptime(_ seconds: TimeInterval) -> String {
        let h = Int(seconds) / 3600
        let m = (Int(seconds) % 3600) / 60
        return "\(h)h \(m)m"
    }
}

struct DiagRow: View {
    @Environment(\.theme) var theme
    let label: String
    let value: String
    var color: Color? = nil
    
    var body: some View {
        HStack {
            Text(label)
                .font(LxFont.mono(10))
                .foregroundColor(theme.textTertiary)
                .frame(width: 150, alignment: .leading)
            Spacer()
            Text(value)
                .font(LxFont.mono(10, weight: .medium))
                .foregroundColor(color ?? theme.textPrimary)
        }
    }
}
