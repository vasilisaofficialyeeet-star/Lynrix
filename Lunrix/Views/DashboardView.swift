// DashboardView.swift — Glassmorphism 2026 main dashboard (Lynrix v2.5)

import SwiftUI

struct DashboardView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    @ObservedObject private var layout = WidgetLayoutManager.shared
    
    private func w(_ key: String) -> Bool {
        layout.isVisible(WidgetID(screen: .dashboard, key: key))
    }
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                HStack {
                    Spacer()
                    WidgetVisibilityMenu(screen: .dashboard)
                }
                .padding(.bottom, -8)
                
                if w("hero") { heroRow }
                if w("pnl") { pnlRow }
                if w("quickBadges") { v24BadgesRow }
                if w("equityCurve") { equityCurvePanel }
                
                HStack(spacing: 12) {
                    if w("throughput") { throughputPanel }
                    if w("latency") { latencyPanel }
                }
                
                HStack(spacing: 12) {
                    if w("aiPrediction") { aiPredictionPanel }
                    if w("circuitBreaker") { circuitBreakerPanel }
                }
                
                if w("recentLogs") { logsPanel }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - Hero Row
    
    private var heroRow: some View {
        HStack(spacing: 12) {
            // Mid Price hero
            GlassPanel(neon: LxColor.electricCyan) {
                VStack(spacing: 4) {
                    Text(loc.t("dashboard.midPrice"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    HeroNumber(
                        String(format: "%.2f", engine.orderBook.midPrice),
                        color: LxColor.electricCyan
                    )
                }
                .frame(maxWidth: .infinity)
            }
            
            // Spread
            GlassPanel(neon: LxColor.amber) {
                VStack(spacing: 4) {
                    Text(loc.t("dashboard.spread"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    GlowText(
                        String(format: "%.4f", engine.orderBook.spread),
                        font: LxFont.bigMetric,
                        color: LxColor.amber
                    )
                    Text(String(format: "%.2f bps", engine.orderBook.spread / max(engine.orderBook.midPrice, 1) * 10000))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
                .frame(maxWidth: .infinity)
            }
            
            // Regime
            GlassPanel(neon: LxColor.regime(engine.regime.current)) {
                VStack(spacing: 6) {
                    Text(loc.t("dashboard.regime"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    RegimeBadge(regime: engine.regime.current, confidence: engine.regime.confidence)
                    GlassMetric(loc.t("dashboard.volatility"), value: String(format: "%.4f", engine.regime.volatility),
                                color: theme.textPrimary)
                }
                .frame(maxWidth: .infinity)
            }
            
            // Engine Status
            GlassPanel(neon: engineStatusColor) {
                VStack(spacing: 6) {
                    Text(loc.t("dashboard.engine"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                    EngineStatusBadge(status: engine.status)
                    GlassMetric(loc.t("common.mode"), value: engine.paperMode ? loc.t("common.paper") : loc.t("common.live"),
                                color: engine.paperMode ? LxColor.electricCyan : LxColor.bloodRed)
                }
                .frame(maxWidth: .infinity)
            }
        }
    }
    
    // MARK: - PnL Row
    
    private var pnlRow: some View {
        HStack(spacing: 12) {
            NeonMetricCard(loc.t("dashboard.position"),
                           value: String(format: "%.4f", engine.position.size),
                           color: engine.position.isLong ? LxColor.neonLime : LxColor.magentaPink,
                           subtitle: engine.position.isLong ? loc.t("common.long") : (engine.position.size == 0 ? loc.t("common.flat") : loc.t("common.short")))
            
            NeonMetricCard(loc.t("dashboard.unrealized"),
                           value: String(format: "%+.2f", engine.position.unrealizedPnl),
                           color: engine.position.unrealizedPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink,
                           subtitle: "$")
            
            NeonMetricCard(loc.t("dashboard.realized"),
                           value: String(format: "%+.2f", engine.position.realizedPnl),
                           color: engine.position.realizedPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink,
                           subtitle: "$")
            
            NeonMetricCard(loc.t("dashboard.netPnl"),
                           value: String(format: "%+.2f", engine.position.netPnl),
                           color: engine.position.netPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink,
                           subtitle: "$")
        }
    }
    
    // MARK: - Quick Badges
    
    private var v24BadgesRow: some View {
        HStack(spacing: 12) {
            NeonMetricCard(loc.t("dashboard.var99"),
                           value: String(format: "$%.2f", abs(engine.varState.var99)),
                           color: LxColor.magentaPink,
                           subtitle: loc.t("dashboard.risk"))
            
            NeonMetricCard(loc.t("dashboard.rlReward"),
                           value: String(format: "%+.4f", engine.rlState.avgReward),
                           color: engine.rlState.avgReward >= 0 ? LxColor.neonLime : LxColor.magentaPink,
                           subtitle: loc.t("dashboard.avg"))
            
            NeonMetricCard(loc.t("dashboard.osmActive"),
                           value: "\(engine.osmSummary.activeOrders)",
                           color: LxColor.electricCyan,
                           subtitle: loc.t("dashboard.orders"))
            
            NeonMetricCard(loc.t("dashboard.chaos"),
                           value: engine.chaosState.enabled ? loc.t("common.on") : loc.t("common.off"),
                           color: engine.chaosState.enabled ? LxColor.bloodRed : theme.textTertiary,
                           subtitle: engine.chaosState.enabled ? "\(engine.chaosState.totalInjections) \(loc.t("dashboard.inj"))" : loc.t("common.disabled"))
        }
    }
    
    // MARK: - Equity Curve
    
    private var equityCurvePanel: some View {
        GlassPanel(neon: engine.position.netPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("dashboard.equityCurve"), icon: "chart.xyaxis.line",
                                       color: engine.position.netPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink)
                    Spacer()
                    SignedNumber(engine.position.netPnl, format: "%+.2f $", font: LxFont.metric)
                }
                AnimatedEquityCurve(
                    data: engine.pnlHistory,
                    positive: LxColor.neonLime,
                    negative: LxColor.magentaPink,
                    height: 90
                )
            }
        }
    }
    
    // MARK: - Throughput
    
    private var throughputPanel: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("dashboard.throughput"), icon: "speedometer", color: LxColor.electricCyan)
                GlassMetric(loc.t("dashboard.obUpdates"), value: "\(engine.metrics.obUpdates)", color: theme.textPrimary)
                GlassMetric(loc.t("dashboard.trades"), value: "\(engine.metrics.tradesTotal)", color: theme.textPrimary)
                GlassMetric(loc.t("dashboard.signals"), value: "\(engine.metrics.signalsTotal)", color: LxColor.electricCyan)
                GlassMetric(loc.t("dashboard.ordersSent"), value: "\(engine.metrics.ordersSent)", color: LxColor.electricCyan)
                GlassMetric(loc.t("dashboard.ordersFilled"), value: "\(engine.metrics.ordersFilled)", color: LxColor.neonLime)
                GlassMetric(loc.t("dashboard.wsReconnects"), value: "\(engine.metrics.wsReconnects)",
                            color: engine.metrics.wsReconnects > 0 ? LxColor.amber : theme.textTertiary)
            }
        }
    }
    
    // MARK: - Latency
    
    private var latencyPanel: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("dashboard.latency"), icon: "timer", color: LxColor.electricCyan)
                GlassMetric(loc.t("dashboard.e2eP50"), value: String(format: "%.0f us", engine.metrics.e2eLatencyP50Us),
                            color: LxColor.electricCyan)
                GlassMetric(loc.t("dashboard.e2eP99"), value: String(format: "%.0f us", engine.metrics.e2eLatencyP99Us),
                            color: engine.metrics.e2eLatencyP99Us > 100 ? LxColor.amber : LxColor.electricCyan)
                GlassMetric(loc.t("dashboard.featureP50"), value: String(format: "%.0f us", engine.metrics.featLatencyP50Us),
                            color: theme.textPrimary)
                GlassMetric(loc.t("dashboard.featureP99"), value: String(format: "%.0f us", engine.metrics.featLatencyP99Us),
                            color: theme.textPrimary)
                GlassMetric(loc.t("dashboard.modelP50"), value: String(format: "%.0f us", engine.metrics.modelLatencyP50Us),
                            color: theme.textPrimary)
                GlassMetric(loc.t("dashboard.modelP99"), value: String(format: "%.0f us", engine.metrics.modelLatencyP99Us),
                            color: engine.metrics.modelLatencyP99Us > 500 ? LxColor.amber : theme.textPrimary)
            }
        }
    }
    
    // MARK: - AI Prediction
    
    private var aiPredictionPanel: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 10) {
                GlassSectionHeader(loc.t("dashboard.aiPrediction"), icon: "brain", color: LxColor.electricCyan)
                HStack(spacing: 20) {
                    VStack(spacing: 2) {
                        Text(loc.t("dashboard.confidence"))
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                        GlowText(
                            String(format: "%.1f%%", engine.prediction.modelConfidence * 100),
                            font: LxFont.bigMetric,
                            color: engine.prediction.modelConfidence > 0.7 ? LxColor.neonLime : LxColor.amber
                        )
                    }
                    Spacer()
                    VStack(spacing: 2) {
                        Text(loc.t("dashboard.pUp"))
                            .font(LxFont.micro)
                            .foregroundColor(LxColor.neonLime.opacity(0.6))
                        GlowText(
                            String(format: "%.1f%%", engine.prediction.probUp * 100),
                            font: LxFont.metric,
                            color: LxColor.neonLime,
                            glow: 4
                        )
                        NeonProgressBar(value: engine.prediction.probUp, color: LxColor.neonLime)
                            .frame(width: 60)
                    }
                    VStack(spacing: 2) {
                        Text(loc.t("dashboard.pDown"))
                            .font(LxFont.micro)
                            .foregroundColor(LxColor.magentaPink.opacity(0.6))
                        GlowText(
                            String(format: "%.1f%%", engine.prediction.probDown * 100),
                            font: LxFont.metric,
                            color: LxColor.magentaPink,
                            glow: 4
                        )
                        NeonProgressBar(value: engine.prediction.probDown, color: LxColor.magentaPink)
                            .frame(width: 60)
                    }
                }
            }
        }
    }
    
    // MARK: - Circuit Breaker
    
    private var circuitBreakerPanel: some View {
        let cbColor = engine.circuitBreaker.tripped ? LxColor.bloodRed :
                       (engine.circuitBreaker.inCooldown ? LxColor.amber : LxColor.neonLime)
        return GlassPanel(neon: cbColor) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("dashboard.circuitBreaker"), icon: "exclamationmark.triangle", color: cbColor)
                    Spacer()
                    StatusBadge(
                        engine.circuitBreaker.tripped ? loc.t("dashboard.tripped") : (engine.circuitBreaker.inCooldown ? loc.t("dashboard.cooldown") : loc.t("common.ok")),
                        color: cbColor,
                        pulse: engine.circuitBreaker.tripped
                    )
                }
                GlassMetric(loc.t("dashboard.drawdown"), value: String(format: "%.2f%%", engine.circuitBreaker.drawdownPct * 100),
                            color: engine.circuitBreaker.drawdownPct > 0.03 ? LxColor.bloodRed : theme.textPrimary)
                GlassMetric(loc.t("dashboard.consecLosses"), value: "\(engine.circuitBreaker.consecutiveLosses)",
                            color: engine.circuitBreaker.consecutiveLosses > 3 ? LxColor.amber : theme.textPrimary)
                GlassMetric(loc.t("dashboard.peakPnl"), value: String(format: "%.2f $", engine.circuitBreaker.peakPnl),
                            color: theme.textPrimary)
            }
        }
    }
    
    // MARK: - Logs
    
    private var logsPanel: some View {
        GlassPanel(neon: LxColor.coolSteel, padding: 12) {
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    GlassSectionHeader(loc.t("dashboard.recentLogs"), icon: "doc.text", color: LxColor.coolSteel)
                    Spacer()
                    Text("\(engine.logs.count)")
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
                ForEach(engine.logs.suffix(8).reversed()) { log in
                    HStack(spacing: 8) {
                        RoundedRectangle(cornerRadius: 1)
                            .fill(logNeonColor(log.level))
                            .frame(width: 2, height: 12)
                            .shadow(color: logNeonColor(log.level).opacity(0.5), radius: 2)
                        Text(log.level.rawValue.uppercased())
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(logNeonColor(log.level))
                            .frame(width: 36, alignment: .leading)
                        Text(log.message)
                            .font(LxFont.mono(10))
                            .foregroundColor(theme.textSecondary)
                            .lineLimit(1)
                        Spacer()
                    }
                }
            }
        }
    }
    
    // MARK: - Helpers
    
    private var engineStatusColor: Color {
        switch engine.status {
        case .idle:       return LxColor.coolSteel
        case .connecting: return LxColor.amber
        case .connected:  return LxColor.electricCyan
        case .trading:    return LxColor.neonLime
        case .error:      return LxColor.bloodRed
        case .stopping:   return LxColor.amber
        }
    }
    
    private func logNeonColor(_ level: LogEntry.LogLevel) -> Color {
        switch level {
        case .debug: return theme.textTertiary
        case .info:  return LxColor.electricCyan
        case .warn:  return LxColor.amber
        case .error: return LxColor.bloodRed
        }
    }
}

// MARK: - Neon Metric Card

struct NeonMetricCard: View {
    let title: String
    let value: String
    let color: Color
    let subtitle: String
    
    @State private var isHovered = false
    @Environment(\.theme) var theme
    
    init(_ title: String, value: String, color: Color, subtitle: String = "") {
        self.title = title
        self.value = value
        self.color = color
        self.subtitle = subtitle
    }
    
    var body: some View {
        VStack(spacing: 4) {
            Text(title)
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(LxColor.coolSteel)
                .tracking(0.5)
            Text(value)
                .font(LxFont.metric)
                .foregroundColor(color)
                .shadow(color: color.opacity(isHovered ? 0.5 : 0.25), radius: isHovered ? 6 : 3)
            if !subtitle.isEmpty {
                Text(subtitle)
                    .font(LxFont.micro)
                    .foregroundColor(LxColor.coolSteel)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 10)
        .padding(.horizontal, 8)
        .background(
            ZStack {
                if theme.isDark {
                    RoundedRectangle(cornerRadius: 10)
                        .fill(Color(white: 0.11, opacity: 0.92))
                } else {
                    RoundedRectangle(cornerRadius: 10)
                        .fill(theme.panelBackground)
                }
                RoundedRectangle(cornerRadius: 10)
                    .fill(color.opacity(isHovered ? 0.06 : 0.02))
            }
        )
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(color.opacity(isHovered ? 0.25 : 0.08), lineWidth: 0.5)
        )
        .shadow(color: color.opacity(isHovered ? 0.15 : 0.05), radius: isHovered ? 8 : 3)
        .scaleEffect(isHovered ? 1.02 : 1.0)
        .animation(LxAnimation.micro, value: isHovered)
        .onHover { hovering in isHovered = hovering }
    }
}
