// VarDashboardView.swift — Glassmorphism 2026 VaR Stress Matrix (Lynrix v2.5)

import SwiftUI

struct VarDashboardView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                varHeader
                
                // VaR metric cards
                HStack(spacing: 12) {
                    NeonMetricCard(loc.t("var.var95"), value: String(format: "$%.2f", abs(engine.varState.var95)),
                                   color: LxColor.amber, subtitle: pctOfPortfolio(engine.varState.var95))
                    NeonMetricCard(loc.t("var.var99"), value: String(format: "$%.2f", abs(engine.varState.var99)),
                                   color: LxColor.magentaPink, subtitle: pctOfPortfolio(engine.varState.var99))
                    NeonMetricCard(loc.t("var.cvar95"), value: String(format: "$%.2f", abs(engine.varState.cvar95)),
                                   color: LxColor.amber, subtitle: pctOfPortfolio(engine.varState.cvar95))
                    NeonMetricCard(loc.t("var.cvar99"), value: String(format: "$%.2f", abs(engine.varState.cvar99)),
                                   color: LxColor.bloodRed, subtitle: pctOfPortfolio(engine.varState.cvar99))
                }
                
                // Methods comparison
                methodsPanel
                
                // Stress scenarios
                stressScenariosPanel
                
                // Risk summary
                riskSummaryPanel
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - Header
    
    private var varHeader: some View {
        GlassPanel(neon: LxColor.magentaPink) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 8) {
                        Image(systemName: "shield.lefthalf.filled")
                            .font(.system(size: 18, weight: .bold))
                            .foregroundColor(LxColor.magentaPink)
                            .shadow(color: LxColor.magentaPink.opacity(0.5), radius: 6)
                        Text(loc.t("var.title"))
                            .font(LxFont.mono(16, weight: .bold))
                            .foregroundColor(theme.textPrimary)
                    }
                    Text(loc.t("var.subtitle"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 2) {
                    Text(loc.t("var.portfolio"))
                        .font(LxFont.mono(8, weight: .bold))
                        .foregroundColor(theme.textTertiary)
                    GlowText(String(format: "$%.2f", engine.varState.portfolioValue),
                             font: LxFont.metric, color: LxColor.electricCyan, glow: 4)
                }
            }
        }
    }
    
    // MARK: - Methods
    
    private var methodsPanel: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 10) {
                GlassSectionHeader(loc.t("var.methodsComparison"), icon: "chart.bar.xaxis", color: LxColor.electricCyan)
                
                VarMethodRow(loc.t("var.parametricGaussian"), value: engine.varState.parametricVar, color: LxColor.electricCyan)
                VarMethodRow(loc.t("var.historicalSim"), value: engine.varState.historicalVar, color: LxColor.neonLime)
                VarMethodRow("Monte Carlo (\(engine.varState.monteCarloSamples) sims)", value: engine.varState.monteCarloVar, color: LxColor.magentaPink)
                
                // Neon comparison bars
                GeometryReader { geo in
                    let maxVal = max(abs(engine.varState.parametricVar), max(abs(engine.varState.historicalVar), abs(engine.varState.monteCarloVar)))
                    let scale = maxVal > 0 ? geo.size.width * 0.7 / maxVal : 1.0
                    
                    VStack(spacing: 5) {
                        neonBar("Param", width: abs(engine.varState.parametricVar) * scale, color: LxColor.electricCyan)
                        neonBar("Hist", width: abs(engine.varState.historicalVar) * scale, color: LxColor.neonLime)
                        neonBar("MC", width: abs(engine.varState.monteCarloVar) * scale, color: LxColor.magentaPink)
                    }
                }
                .frame(height: 55)
            }
        }
    }
    
    // MARK: - Stress Scenarios
    
    private var stressScenariosPanel: some View {
        GlassPanel(neon: LxColor.bloodRed) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("var.stressScenarios"), icon: "bolt.trianglebadge.exclamationmark", color: LxColor.bloodRed)
                
                ForEach(0..<8, id: \.self) { i in
                    let loss = i < engine.varState.stressScenarioLosses.count ? engine.varState.stressScenarioLosses[i] : 0
                    let name = i < engine.varState.stressScenarioNames.count ? engine.varState.stressScenarioNames[i] : "Scenario \(i+1)"
                    glassStressRow(name, loss: loss, index: i)
                }
            }
        }
    }
    
    // MARK: - Risk Summary
    
    private var riskSummaryPanel: some View {
        GlassPanel(neon: LxColor.amber) {
            VStack(alignment: .leading, spacing: 10) {
                GlassSectionHeader(loc.t("var.riskSummary"), icon: "chart.bar.doc.horizontal", color: LxColor.amber)
                
                HStack(spacing: 16) {
                    VStack(alignment: .leading, spacing: 6) {
                        GlassMetric(loc.t("var.maxDrawdown"), value: String(format: "%.2f%%", engine.strategyMetrics.maxDrawdownPct),
                                    color: LxColor.magentaPink)
                        GlassMetric(loc.t("var.currentDD"), value: String(format: "%.2f%%", engine.strategyMetrics.currentDrawdown),
                                    color: engine.strategyMetrics.currentDrawdown > 3 ? LxColor.bloodRed : theme.textPrimary)
                        GlassMetric(loc.t("var.cbDrawdown"), value: String(format: "%.2f%%", engine.circuitBreaker.drawdownPct * 100),
                                    color: theme.textPrimary)
                    }
                    VStack(alignment: .leading, spacing: 6) {
                        GlassMetric(loc.t("var.sharpe"), value: String(format: "%.2f", engine.strategyMetrics.sharpeRatio),
                                    color: engine.strategyMetrics.sharpeRatio > 1 ? LxColor.neonLime : LxColor.amber)
                        GlassMetric(loc.t("var.sortino"), value: String(format: "%.2f", engine.strategyMetrics.sortinoRatio),
                                    color: theme.textPrimary)
                        GlassMetric(loc.t("var.calmar"), value: String(format: "%.2f", engine.strategyMetrics.calmarRatio),
                                    color: theme.textPrimary)
                    }
                    VStack(alignment: .leading, spacing: 6) {
                        GlassMetric(loc.t("var.cb"), value: engine.circuitBreaker.tripped ? loc.t("dashboard.tripped") : loc.t("common.ok"),
                                    color: engine.circuitBreaker.tripped ? LxColor.bloodRed : LxColor.neonLime)
                        GlassMetric(loc.t("var.recovery"), value: String(format: "%.2f", engine.strategyMetrics.recoveryFactor),
                                    color: theme.textPrimary)
                        GlassMetric(loc.t("var.winRate"), value: String(format: "%.1f%%", engine.strategyMetrics.winRate * 100),
                                    color: engine.strategyMetrics.winRate > 0.5 ? LxColor.neonLime : LxColor.amber)
                    }
                }
            }
        }
    }
    
    // MARK: - Helpers
    
    private func pctOfPortfolio(_ value: Double) -> String {
        String(format: "%.2f%%", abs(value) / max(engine.varState.portfolioValue, 1) * 100)
    }
    
    private func neonBar(_ label: String, width: Double, color: Color) -> some View {
        HStack(spacing: 4) {
            Text(label)
                .font(LxFont.mono(8, weight: .medium))
                .foregroundColor(theme.textTertiary)
                .frame(width: 35, alignment: .trailing)
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 3)
                    .fill(color.opacity(0.08))
                    .frame(height: 12)
                RoundedRectangle(cornerRadius: 3)
                    .fill(
                        LinearGradient(colors: [color.opacity(0.4), color.opacity(0.15)],
                                       startPoint: .leading, endPoint: .trailing)
                    )
                    .frame(width: max(CGFloat(width), 3), height: 12)
                    .shadow(color: color.opacity(0.3), radius: 3)
            }
        }
    }
    
    private func glassStressRow(_ name: String, loss: Double, index: Int) -> some View {
        let icons = ["bolt.slash", "waveform.path.ecg.rectangle", "drop.triangle",
                     "arrow.down.right.circle", "percent", "dollarsign.arrow.circlepath",
                     "link.badge.plus", "exclamationmark.triangle"]
        let severity = abs(loss) / max(engine.varState.portfolioValue, 1) * 100
        let color: Color = severity > 20 ? LxColor.bloodRed : (severity > 10 ? LxColor.amber : (severity > 5 ? LxColor.gold : LxColor.neonLime))
        
        return HStack(spacing: 8) {
            Image(systemName: index < icons.count ? icons[index] : "questionmark.circle")
                .font(.system(size: 11))
                .foregroundColor(color)
                .shadow(color: color.opacity(0.4), radius: 2)
                .frame(width: 18)
            Text(name)
                .font(LxFont.mono(10))
                .foregroundColor(theme.textSecondary)
                .lineLimit(1)
            Spacer()
            Text(String(format: "-$%.2f", abs(loss)))
                .font(LxFont.mono(10, weight: .bold))
                .foregroundColor(color)
                .shadow(color: color.opacity(0.3), radius: 2)
            Text(String(format: "(%.1f%%)", severity))
                .font(LxFont.micro)
                .foregroundColor(theme.textTertiary)
        }
    }
}

// MARK: - VaR Method Row

struct VarMethodRow: View {
    @Environment(\.theme) var theme
    let label: String
    let value: Double
    let color: Color
    
    init(_ label: String, value: Double, color: Color) {
        self.label = label
        self.value = value
        self.color = color
    }
    
    var body: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(color)
                .frame(width: 7, height: 7)
                .shadow(color: color.opacity(0.5), radius: 3)
            Text(label)
                .font(LxFont.mono(10))
                .foregroundColor(theme.textSecondary)
            Spacer()
            Text(String(format: "$%.2f", abs(value)))
                .font(LxFont.mono(11, weight: .bold))
                .foregroundColor(color)
                .shadow(color: color.opacity(0.3), radius: 2)
        }
    }
}
