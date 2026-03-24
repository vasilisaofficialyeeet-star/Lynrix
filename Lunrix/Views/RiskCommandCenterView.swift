// RiskCommandCenterView.swift — Portfolio & Risk Command Center (Lynrix v2.5)

import SwiftUI

struct RiskCommandCenterView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    @ObservedObject private var layout = WidgetLayoutManager.shared
    
    private func w(_ key: String) -> Bool {
        layout.isVisible(WidgetID(screen: .riskCenter, key: key))
    }
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                HStack {
                    Spacer()
                    WidgetVisibilityMenu(screen: .riskCenter)
                }
                .padding(.bottom, -8)
                
                // Mode & Kill Switch Status
                modeAndHaltPanel
                
                // Position & PnL
                if w("position") { positionPanel }
                
                // Risk Utilization
                if w("riskUtil") { riskUtilizationPanel }
                
                // VaR Summary
                if w("varSummary") { varSummaryPanel }
                
                // Circuit Breaker
                if w("circuitBreaker") { circuitBreakerPanel }
                
                // Kill Switch Controls
                if w("killSwitch") { killSwitchPanel }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - Mode & Halt Banner
    
    private var modeAndHaltPanel: some View {
        GlassPanel(neon: engine.killSwitch.isAnyHaltActive ? LxColor.bloodRed : engine.tradingMode.color) {
            VStack(spacing: 10) {
                HStack {
                    GlassSectionHeader(loc.t("risk.commandCenter"), icon: "shield.checkerboard", color: engine.tradingMode.color)
                    Spacer()
                    TradingModeBadge(mode: engine.tradingMode)
                }
                
                if engine.killSwitch.isAnyHaltActive {
                    HStack(spacing: 8) {
                        Image(systemName: "exclamationmark.octagon.fill")
                            .font(.system(size: 14, weight: .bold))
                            .foregroundColor(LxColor.bloodRed)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(loc.t("risk.haltActive"))
                                .font(LxFont.mono(12, weight: .bold))
                                .foregroundColor(LxColor.bloodRed)
                            Text(engine.killSwitch.primaryReason)
                                .font(LxFont.mono(10))
                                .foregroundColor(theme.textSecondary)
                                .lineLimit(2)
                        }
                        Spacer()
                    }
                    .padding(8)
                    .background(
                        RoundedRectangle(cornerRadius: 8)
                            .fill(LxColor.bloodRed.opacity(0.08))
                            .overlay(
                                RoundedRectangle(cornerRadius: 8)
                                    .stroke(LxColor.bloodRed.opacity(0.2), lineWidth: 0.5)
                            )
                    )
                }
                
                HStack(spacing: 16) {
                    RiskQuickStat(
                        label: loc.t("risk.engineStatus"),
                        value: loc.t(engine.status.locKey),
                        color: engine.status == .trading ? LxColor.neonLime : (engine.status == .error ? LxColor.bloodRed : LxColor.coolSteel)
                    )
                    RiskQuickStat(
                        label: loc.t("risk.uptime"),
                        value: String(format: "%.1fh", engine.systemMonitor.uptimeHours),
                        color: LxColor.electricCyan
                    )
                    RiskQuickStat(
                        label: loc.t("risk.wsReconnects"),
                        value: "\(engine.metrics.wsReconnects)",
                        color: engine.metrics.wsReconnects > 5 ? LxColor.amber : LxColor.neonLime
                    )
                }
            }
        }
    }
    
    // MARK: - Position Panel
    
    private var positionPanel: some View {
        let posColor: Color = engine.position.hasPosition
            ? (engine.position.isLong ? LxColor.neonLime : LxColor.magentaPink)
            : LxColor.coolSteel
        
        return GlassPanel(neon: posColor) {
            VStack(spacing: 10) {
                HStack {
                    GlassSectionHeader(loc.t("risk.positionPnl"), icon: "chart.line.uptrend.xyaxis", color: posColor)
                    Spacer()
                    if engine.position.hasPosition {
                        StatusBadge(engine.position.isLong ? loc.t("common.long") : loc.t("common.short"), color: posColor)
                    } else {
                        Text(loc.t("common.flat"))
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                    }
                }
                
                HStack(spacing: 12) {
                    RiskMetricCard(
                        label: loc.t("portfolio.size"),
                        value: String(format: "%.4f", engine.position.size),
                        color: posColor
                    )
                    RiskMetricCard(
                        label: loc.t("portfolio.unrealized"),
                        value: String(format: "$%.2f", engine.position.unrealizedPnl),
                        color: engine.position.unrealizedPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink
                    )
                    RiskMetricCard(
                        label: loc.t("portfolio.realized"),
                        value: String(format: "$%.2f", engine.position.realizedPnl),
                        color: engine.position.realizedPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink
                    )
                    RiskMetricCard(
                        label: loc.t("portfolio.netPnl"),
                        value: String(format: "$%.2f", engine.position.netPnl),
                        color: engine.position.netPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink
                    )
                }
            }
        }
    }
    
    // MARK: - Risk Utilization
    
    private var riskUtilizationPanel: some View {
        GlassPanel(neon: LxColor.amber) {
            VStack(spacing: 10) {
                GlassSectionHeader(loc.t("risk.utilization"), icon: "gauge.with.dots.needle.33percent", color: LxColor.amber)
                
                VStack(spacing: 8) {
                    RiskBar(
                        label: loc.t("risk.positionUsed"),
                        current: abs(engine.position.size),
                        limit: engine.config.maxPositionSize,
                        color: LxColor.electricCyan
                    )
                    RiskBar(
                        label: loc.t("risk.drawdownUsed"),
                        current: engine.circuitBreaker.drawdownPct,
                        limit: engine.config.maxDrawdown,
                        color: LxColor.amber
                    )
                    RiskBar(
                        label: loc.t("risk.dailyLossUsed"),
                        current: max(-engine.strategyMetrics.dailyPnl, 0),
                        limit: engine.config.maxDailyLoss,
                        color: LxColor.magentaPink
                    )
                    RiskBar(
                        label: loc.t("risk.leverageUsed"),
                        current: abs(engine.position.size) * engine.orderBook.midPrice / max(engine.varState.portfolioValue, 1),
                        limit: engine.config.maxLeverage,
                        color: LxColor.gold
                    )
                }
            }
        }
    }
    
    // MARK: - VaR Summary
    
    private var varSummaryPanel: some View {
        GlassPanel(neon: LxColor.magentaPink) {
            VStack(spacing: 10) {
                GlassSectionHeader(loc.t("risk.varSummary"), icon: "shield.lefthalf.filled", color: LxColor.magentaPink)
                
                HStack(spacing: 12) {
                    RiskMetricCard(label: loc.t("var.var95"), value: String(format: "$%.2f", abs(engine.varState.var95)), color: LxColor.amber)
                    RiskMetricCard(label: loc.t("var.var99"), value: String(format: "$%.2f", abs(engine.varState.var99)), color: LxColor.magentaPink)
                    RiskMetricCard(label: loc.t("var.cvar95"), value: String(format: "$%.2f", abs(engine.varState.cvar95)), color: LxColor.amber)
                    RiskMetricCard(label: loc.t("var.cvar99"), value: String(format: "$%.2f", abs(engine.varState.cvar99)), color: LxColor.bloodRed)
                }
            }
        }
    }
    
    // MARK: - Circuit Breaker
    
    private var circuitBreakerPanel: some View {
        let cbColor: Color = engine.circuitBreaker.tripped ? LxColor.bloodRed : (engine.circuitBreaker.inCooldown ? LxColor.amber : LxColor.neonLime)
        
        return GlassPanel(neon: cbColor) {
            VStack(spacing: 10) {
                HStack {
                    GlassSectionHeader(loc.t("risk.circuitBreaker"), icon: "bolt.trianglebadge.exclamationmark", color: cbColor)
                    Spacer()
                    StatusBadge(
                        engine.circuitBreaker.tripped ? loc.t("dashboard.tripped") :
                            (engine.circuitBreaker.inCooldown ? loc.t("dashboard.cooldown") : loc.t("common.ok")),
                        color: cbColor,
                        pulse: engine.circuitBreaker.tripped
                    )
                }
                
                HStack(spacing: 12) {
                    RiskMetricCard(
                        label: loc.t("dashboard.drawdown"),
                        value: String(format: "%.2f%%", engine.circuitBreaker.drawdownPct * 100),
                        color: engine.circuitBreaker.drawdownPct > engine.config.maxDrawdown * 0.8 ? LxColor.bloodRed : theme.textPrimary
                    )
                    RiskMetricCard(
                        label: loc.t("dashboard.consecLosses"),
                        value: "\(engine.circuitBreaker.consecutiveLosses)",
                        color: engine.circuitBreaker.consecutiveLosses > 5 ? LxColor.amber : theme.textPrimary
                    )
                    RiskMetricCard(
                        label: loc.t("dashboard.peakPnl"),
                        value: String(format: "$%.2f", engine.circuitBreaker.peakPnl),
                        color: LxColor.electricCyan
                    )
                }
            }
        }
    }
    
    // MARK: - Kill Switch Panel
    
    private var killSwitchPanel: some View {
        GlassPanel(neon: engine.killSwitch.isAnyHaltActive ? LxColor.bloodRed : LxColor.coolSteel) {
            VStack(spacing: 10) {
                GlassSectionHeader(loc.t("risk.killSwitches"), icon: "power", color: engine.killSwitch.isAnyHaltActive ? LxColor.bloodRed : LxColor.coolSteel)
                
                HStack(spacing: 12) {
                    KillSwitchIndicator(
                        label: loc.t("risk.globalKill"),
                        active: engine.killSwitch.globalHalt,
                        reason: engine.killSwitch.globalHaltReason
                    )
                    KillSwitchIndicator(
                        label: loc.t("risk.strategyKill"),
                        active: engine.killSwitch.strategyHalt,
                        reason: engine.killSwitch.strategyHaltReason
                    )
                    KillSwitchIndicator(
                        label: loc.t("risk.cbKill"),
                        active: engine.killSwitch.circuitBreakerHalt,
                        reason: engine.circuitBreaker.tripped ? loc.t("dashboard.tripped") : ""
                    )
                }
                
                HStack(spacing: 8) {
                    NeonButton(loc.t("risk.activateGlobalKill"), icon: "power", color: LxColor.bloodRed) {
                        engine.activateGlobalKill(reason: loc.t("risk.manualGlobalKill"))
                    }
                    .disabled(engine.killSwitch.globalHalt)
                    
                    if engine.killSwitch.globalHalt {
                        NeonButton(loc.t("risk.resetGlobalKill"), icon: "arrow.counterclockwise", color: LxColor.amber) {
                            engine.resetGlobalKill()
                        }
                    }
                    
                    Spacer()
                }
            }
        }
    }
}

// MARK: - Supporting Components

struct TradingModeBadge: View {
    let mode: TradingMode
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: mode.icon)
                .font(.system(size: 9, weight: .bold))
            Text(loc.t(mode.locKey))
                .font(LxFont.mono(9, weight: .bold))
        }
        .foregroundColor(mode.color)
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
        .background(
            Capsule().fill(mode.color.opacity(0.1))
        )
        .overlay(
            Capsule().stroke(mode.color.opacity(0.25), lineWidth: 0.5)
        )
        .shadow(color: mode.color.opacity(0.2 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
    }
}

struct RiskQuickStat: View {
    let label: String
    let value: String
    let color: Color
    @Environment(\.theme) var theme
    
    var body: some View {
        VStack(spacing: 2) {
            Text(label)
                .font(LxFont.micro)
                .foregroundColor(theme.textTertiary)
            Text(value)
                .font(LxFont.mono(12, weight: .bold))
                .foregroundColor(color)
        }
        .frame(maxWidth: .infinity)
    }
}

struct RiskMetricCard: View {
    let label: String
    let value: String
    let color: Color
    @Environment(\.theme) var theme
    
    var body: some View {
        VStack(spacing: 4) {
            Text(label)
                .font(LxFont.micro)
                .foregroundColor(theme.textTertiary)
            Text(value)
                .font(LxFont.mono(12, weight: .bold))
                .foregroundColor(color)
                .shadow(color: color.opacity(0.3), radius: 2)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 6)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(color.opacity(0.04))
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(color.opacity(0.1), lineWidth: 0.5)
                )
        )
    }
}

struct RiskBar: View {
    let label: String
    let current: Double
    let limit: Double
    let color: Color
    @Environment(\.theme) var theme
    
    private var ratio: Double {
        guard limit > 0 else { return 0 }
        return min(current / limit, 1.0)
    }
    
    private var barColor: Color {
        if ratio > 0.9 { return LxColor.bloodRed }
        if ratio > 0.7 { return LxColor.amber }
        return color
    }
    
    var body: some View {
        HStack(spacing: 8) {
            Text(label)
                .font(LxFont.mono(9))
                .foregroundColor(theme.textSecondary)
                .frame(width: 100, alignment: .trailing)
            
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 3)
                        .fill(theme.backgroundSecondary)
                        .frame(height: 10)
                    RoundedRectangle(cornerRadius: 3)
                        .fill(
                            LinearGradient(colors: [barColor.opacity(0.6), barColor.opacity(0.3)],
                                           startPoint: .leading, endPoint: .trailing)
                        )
                        .frame(width: max(CGFloat(ratio) * geo.size.width, 2), height: 10)
                        .shadow(color: barColor.opacity(0.3), radius: 2)
                }
            }
            .frame(height: 10)
            
            Text(String(format: "%.0f%%", ratio * 100))
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(barColor)
                .frame(width: 36, alignment: .trailing)
        }
    }
}

struct KillSwitchIndicator: View {
    let label: String
    let active: Bool
    var reason: String = ""
    @Environment(\.theme) var theme
    
    var body: some View {
        VStack(spacing: 4) {
            Circle()
                .fill(active ? LxColor.bloodRed : LxColor.neonLime)
                .frame(width: 10, height: 10)
                .shadow(color: (active ? LxColor.bloodRed : LxColor.neonLime).opacity(0.5), radius: 4)
            Text(label)
                .font(LxFont.micro)
                .foregroundColor(theme.textSecondary)
            Text(active ? "HALT" : "OK")
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(active ? LxColor.bloodRed : LxColor.neonLime)
            if active && !reason.isEmpty {
                Text(reason)
                    .font(LxFont.mono(7))
                    .foregroundColor(theme.textTertiary)
                    .lineLimit(1)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 6)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(active ? LxColor.bloodRed.opacity(0.05) : Color.clear)
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(active ? LxColor.bloodRed.opacity(0.15) : theme.borderSubtle, lineWidth: 0.5)
                )
        )
    }
}
