// PortfolioView.swift — Glassmorphism 2026 Portfolio (Lynrix v2.5)

import SwiftUI

struct PortfolioView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    private var posColor: Color {
        engine.position.hasPosition
            ? (engine.position.isLong ? LxColor.neonLime : LxColor.magentaPink)
            : LxColor.coolSteel
    }
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                // Position header
                GlassPanel(neon: posColor) {
                    VStack(spacing: 12) {
                        HStack {
                            GlassSectionHeader(loc.t("portfolio.currentPosition"), icon: "arrow.up.arrow.down", color: posColor)
                            Spacer()
                            if engine.position.hasPosition {
                                StatusBadge(engine.position.isLong ? loc.t("common.long") : loc.t("common.short"), color: posColor)
                            } else {
                                Text(loc.t("common.flat"))
                                    .font(LxFont.micro)
                                    .foregroundColor(theme.textTertiary)
                            }
                        }
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 3), spacing: 12) {
                            PortfolioMetric(label: loc.t("portfolio.size"), value: String(format: "%.4f", engine.position.size),
                                          unit: engine.config.symbol)
                            PortfolioMetric(label: loc.t("portfolio.entry"), value: String(format: "%.1f", engine.position.entryPrice))
                            PortfolioMetric(label: loc.t("dashboard.midPrice"), value: String(format: "%.1f", engine.orderBook.midPrice))
                        }
                    }
                }
                
                // PnL
                GlassPanel(neon: engine.position.netPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink) {
                    VStack(spacing: 12) {
                        GlassSectionHeader(loc.t("portfolio.pnlMetrics"), icon: "chart.line.uptrend.xyaxis",
                                           color: engine.position.netPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink)
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 4), spacing: 12) {
                            PnlMetric(label: loc.t("portfolio.unrealized"), value: engine.position.unrealizedPnl)
                            PnlMetric(label: loc.t("portfolio.realized"), value: engine.position.realizedPnl)
                            PnlMetric(label: "Funding", value: engine.position.fundingImpact)
                            PnlMetric(label: loc.t("portfolio.netPnl"), value: engine.position.netPnl, isBold: true)
                        }
                    }
                }
                
                // Execution stats
                GlassPanel(neon: LxColor.electricCyan) {
                    VStack(spacing: 12) {
                        GlassSectionHeader(loc.t("portfolio.executionStats"), icon: "arrow.left.arrow.right", color: LxColor.electricCyan)
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 4), spacing: 12) {
                            StatMetric(label: loc.t("dashboard.ordersSent"), value: "\(engine.metrics.ordersSent)")
                            StatMetric(label: loc.t("dashboard.ordersFilled"), value: "\(engine.metrics.ordersFilled)", color: LxColor.neonLime)
                            StatMetric(label: loc.t("sysmon.ordersCancelled"), value: "\(engine.metrics.ordersCancelled)", color: LxColor.amber)
                            StatMetric(label: loc.t("portfolio.fillRate"), value: fillRate, color: LxColor.electricCyan)
                        }
                    }
                }
                
                // Latency
                GlassPanel(neon: LxColor.electricCyan) {
                    VStack(spacing: 12) {
                        GlassSectionHeader(loc.t("portfolio.latency"), icon: "clock.fill", color: LxColor.electricCyan)
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 4), spacing: 12) {
                            StatMetric(label: loc.t("dashboard.e2eP50"), value: String(format: "%.1f us", engine.metrics.e2eLatencyP50Us), color: LxColor.electricCyan)
                            StatMetric(label: loc.t("dashboard.e2eP99"), value: String(format: "%.1f us", engine.metrics.e2eLatencyP99Us), color: LxColor.electricCyan)
                            StatMetric(label: loc.t("dashboard.featureP50"), value: String(format: "%.1f us", engine.metrics.featLatencyP50Us), color: LxColor.magentaPink)
                            StatMetric(label: loc.t("dashboard.featureP99"), value: String(format: "%.1f us", engine.metrics.featLatencyP99Us), color: LxColor.magentaPink)
                        }
                    }
                }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    private var fillRate: String {
        guard engine.metrics.ordersSent > 0 else { return "—" }
        let rate = Double(engine.metrics.ordersFilled) / Double(engine.metrics.ordersSent) * 100
        return String(format: "%.1f%%", rate)
    }
}

struct PortfolioMetric: View {
    let label: String
    let value: String
    var unit: String = ""
    @Environment(\.theme) var theme
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(LxFont.micro)
                .foregroundColor(theme.textTertiary)
            HStack(alignment: .firstTextBaseline, spacing: 4) {
                Text(value)
                    .font(LxFont.mono(13, weight: .medium))
                    .foregroundColor(theme.textPrimary)
                if !unit.isEmpty {
                    Text(unit)
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
            }
        }
    }
}

struct PnlMetric: View {
    let label: String
    let value: Double
    var isBold: Bool = false
    @Environment(\.theme) var theme
    
    private var pnlColor: Color {
        value > 0.0001 ? LxColor.neonLime : (value < -0.0001 ? LxColor.magentaPink : LxColor.coolSteel)
    }
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(LxFont.micro)
                .foregroundColor(theme.textTertiary)
            Text(String(format: "%+.4f", value))
                .font(isBold ? LxFont.metric : LxFont.mono(13, weight: .medium))
                .foregroundColor(pnlColor)
                .shadow(color: isBold ? pnlColor.opacity(0.3) : .clear, radius: isBold ? 3 : 0)
        }
    }
}

struct StatMetric: View {
    let label: String
    let value: String
    var color: Color? = nil
    @Environment(\.theme) var theme
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(LxFont.micro)
                .foregroundColor(theme.textTertiary)
            Text(value)
                .font(LxFont.mono(13, weight: .medium))
                .foregroundColor(color ?? theme.textPrimary)
        }
    }
}
