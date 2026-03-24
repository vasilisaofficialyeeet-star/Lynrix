// RLTrainingView.swift — Glassmorphism 2026 RL Brain Panel (Lynrix v2.5)

import SwiftUI

struct RLTrainingView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    private var brainColor: Color { engine.rlv2State.trainingActive ? LxColor.neonLime : LxColor.coolSteel }
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                rlHeader
                
                // Key metrics
                HStack(spacing: 12) {
                    NeonMetricCard(loc.t("rl.entropyAlpha"), value: String(format: "%.4f", engine.rlv2State.entropyAlpha),
                                   color: LxColor.electricCyan)
                    NeonMetricCard(loc.t("rl.klDiv"), value: String(format: "%.6f", engine.rlv2State.klDivergence),
                                   color: engine.rlv2State.klDivergence > 0.02 ? LxColor.bloodRed : LxColor.neonLime)
                    NeonMetricCard(loc.t("rl.clipFrac"), value: String(format: "%.3f", engine.rlv2State.clipFraction),
                                   color: engine.rlv2State.clipFraction > 0.2 ? LxColor.amber : LxColor.neonLime)
                    NeonMetricCard(loc.t("rl.explVar"), value: String(format: "%.3f", engine.rlv2State.explainedVariance),
                                   color: engine.rlv2State.explainedVariance > 0.8 ? LxColor.neonLime : LxColor.amber)
                }
                
                HStack(spacing: 12) {
                    NeonMetricCard(loc.t("rl.epochs"), value: "\(engine.rlv2State.epochsCompleted)", color: LxColor.electricCyan)
                    NeonMetricCard(loc.t("rl.rollbacks"), value: "\(engine.rlv2State.rollbackCount)",
                                   color: engine.rlv2State.rollbackCount > 5 ? LxColor.bloodRed : LxColor.neonLime)
                    NeonMetricCard(loc.t("rl.lr"), value: String(format: "%.1e", engine.rlv2State.learningRate), color: LxColor.magentaPink)
                    NeonMetricCard(loc.t("rl.buffer"), value: "\(engine.rlv2State.bufferSize)/\(engine.rlv2State.bufferCapacity)",
                                   color: LxColor.coolSteel)
                }
                
                stateVectorCard
                actionVectorCard
                rewardHistoryCard
                
                HStack(spacing: 12) {
                    ppoDiagnosticsCard
                    rlActionsCard
                }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - Header
    
    private var rlHeader: some View {
        GlassPanel(neon: brainColor) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 8) {
                        Image(systemName: "brain")
                            .font(.system(size: 18, weight: .bold))
                            .foregroundColor(brainColor)
                            .shadow(color: brainColor.opacity(0.5), radius: 6)
                        Text(loc.t("rl.title"))
                            .font(LxFont.mono(16, weight: .bold))
                            .foregroundColor(theme.textPrimary)
                    }
                    Text(loc.t("rl.subtitle"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                }
                Spacer()
                StatusBadge(
                    engine.rlv2State.trainingActive ? loc.t("rl.training") : loc.t("rl.idle"),
                    color: brainColor,
                    pulse: engine.rlv2State.trainingActive
                )
            }
        }
    }
    
    // MARK: - 32-dim State Heatmap
    
    private var stateVectorCard: some View {
        let stateNames = [
            "mid_price", "spread", "imbalance", "vwap_delta",
            "volatility", "trend_ema", "momentum", "rsi",
            "ob_depth_bid", "ob_depth_ask", "trade_flow", "funding",
            "regime_low", "regime_high", "regime_trend", "regime_mr",
            "pos_size", "pos_pnl", "pos_duration", "drawdown",
            "fill_prob", "cancel_rate", "slippage", "impact_bps",
            "pred_up", "pred_down", "confidence", "threshold",
            "rl_reward_ema", "rl_value", "rl_entropy", "rl_step_frac"
        ]
        
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("rl.stateVector"), icon: "square.grid.4x3.fill", color: LxColor.electricCyan)
                
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 2), count: 8), spacing: 2) {
                    ForEach(0..<32, id: \.self) { i in
                        let val = i < engine.rlv2State.stateVector.count ? engine.rlv2State.stateVector[i] : 0
                        VStack(spacing: 1) {
                            Rectangle()
                                .fill(neonHeatColor(val))
                                .frame(height: 24)
                                .clipShape(RoundedRectangle(cornerRadius: 2))
                                .shadow(color: neonHeatColor(val).opacity(0.3), radius: 2)
                            Text(i < stateNames.count ? stateNames[i] : "s\(i)")
                                .font(LxFont.mono(7))
                                .foregroundColor(theme.textTertiary)
                                .lineLimit(1)
                            Text(String(format: "%.2f", val))
                                .font(LxFont.mono(8, weight: .medium))
                                .foregroundColor(theme.textSecondary)
                        }
                    }
                }
            }
        }
    }
    
    // MARK: - 4-dim Action Vector
    
    private var actionVectorCard: some View {
        let actionNames = [loc.t("rl.signalDelta"), loc.t("rl.posScale"), loc.t("rl.offsetBps"), loc.t("rl.requoteScale")]
        let actionColors: [Color] = [LxColor.electricCyan, LxColor.neonLime, LxColor.amber, LxColor.magentaPink]
        
        return GlassPanel(neon: LxColor.neonLime) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("rl.actionVector"), icon: "slider.horizontal.3", color: LxColor.neonLime)
                
                HStack(spacing: 12) {
                    ForEach(0..<4, id: \.self) { i in
                        let val = i < engine.rlv2State.actionVector.count ? engine.rlv2State.actionVector[i] : 0
                        VStack(spacing: 6) {
                            Text(actionNames[i])
                                .font(LxFont.mono(8))
                                .foregroundColor(theme.textTertiary)
                                .multilineTextAlignment(.center)
                                .lineLimit(2)
                            
                            // Neon vertical bar
                            ZStack(alignment: .center) {
                                RoundedRectangle(cornerRadius: 3)
                                    .fill(actionColors[i].opacity(0.05))
                                    .frame(width: 30, height: 60)
                                
                                let normalized = max(min(val, 1.0), -1.0)
                                if normalized >= 0 {
                                    VStack {
                                        Spacer()
                                        RoundedRectangle(cornerRadius: 2)
                                            .fill(actionColors[i].opacity(0.5))
                                            .frame(width: 28, height: CGFloat(normalized) * 30)
                                            .shadow(color: actionColors[i].opacity(0.3), radius: 3)
                                    }
                                    .frame(height: 30)
                                    .offset(y: -15)
                                } else {
                                    VStack {
                                        RoundedRectangle(cornerRadius: 2)
                                            .fill(actionColors[i].opacity(0.5))
                                            .frame(width: 28, height: CGFloat(-normalized) * 30)
                                            .shadow(color: actionColors[i].opacity(0.3), radius: 3)
                                        Spacer()
                                    }
                                    .frame(height: 30)
                                    .offset(y: 15)
                                }
                                
                                Rectangle()
                                    .fill(theme.textTertiary.opacity(0.3))
                                    .frame(width: 30, height: 0.5)
                            }
                            .frame(width: 30, height: 60)
                            
                            GlowText(String(format: "%+.3f", val),
                                     font: LxFont.mono(10, weight: .bold), color: actionColors[i], glow: 3)
                        }
                        .frame(maxWidth: .infinity)
                    }
                }
                
                HStack(spacing: 16) {
                    GlassMetric(loc.t("rl.signalDelta"), value: String(format: "%+.3f", engine.rlState.signalThresholdDelta),
                                color: LxColor.electricCyan)
                    GlassMetric(loc.t("rl.posScale"), value: String(format: "%+.3f", engine.rlState.positionSizeScale),
                                color: LxColor.neonLime)
                    GlassMetric(loc.t("rl.offsetBps"), value: String(format: "%+.3f", engine.rlState.orderOffsetBps),
                                color: LxColor.amber)
                    GlassMetric(loc.t("rl.requoteScale"), value: String(format: "%+.3f", engine.rlState.requoteFreqScale),
                                color: LxColor.magentaPink)
                }
            }
        }
    }
    
    // MARK: - Reward History
    
    private var rewardHistoryCard: some View {
        let rewardColor = engine.rlState.avgReward >= 0 ? LxColor.neonLime : LxColor.magentaPink
        return GlassPanel(neon: rewardColor) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("rl.rewardHistory"), icon: "chart.xyaxis.line", color: rewardColor)
                    Spacer()
                    GlowText(String(format: "%+.4f", engine.rlState.avgReward),
                             font: LxFont.metric, color: rewardColor, glow: 4)
                }
                
                if engine.rlv2State.rewardHistory.count > 1 {
                    PnLChartShape(data: engine.rlv2State.rewardHistory)
                        .stroke(rewardColor, lineWidth: 1.5)
                        .frame(height: 80)
                        .background(
                            PnLChartShape(data: engine.rlv2State.rewardHistory)
                                .fill(
                                    LinearGradient(
                                        colors: [rewardColor.opacity(0.15), Color.clear],
                                        startPoint: .top, endPoint: .bottom
                                    )
                                )
                        )
                        .shadow(color: rewardColor.opacity(0.1), radius: 4)
                } else {
                    RoundedRectangle(cornerRadius: 6)
                        .fill(theme.glassHighlight)
                        .frame(height: 80)
                        .overlay(
                            Text(loc.t("rl.waitingForData"))
                                .font(LxFont.label)
                                .foregroundColor(theme.textTertiary)
                        )
                }
            }
        }
    }
    
    // MARK: - PPO Diagnostics
    
    private var ppoDiagnosticsCard: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("rl.ppoDiagnostics"), icon: "gearshape.2", color: LxColor.electricCyan)
                GlassMetric(loc.t("rl.policyLoss"), value: String(format: "%.6f", engine.rlState.policyLoss), color: theme.textPrimary)
                GlassMetric(loc.t("rl.valueLoss"), value: String(format: "%.6f", engine.rlState.valueLoss), color: theme.textPrimary)
                GlassMetric(loc.t("rl.approxKL"), value: String(format: "%.6f", engine.rlv2State.approxKl),
                            color: engine.rlv2State.approxKl > 0.015 ? LxColor.amber : theme.textPrimary)
                GlassMetric(loc.t("rl.valueEstimate"), value: String(format: "%+.4f", engine.rlState.valueEstimate), color: LxColor.electricCyan)
                GlassMetric(loc.t("rl.totalStepsLabel"), value: "\(engine.rlState.totalSteps)", color: theme.textPrimary)
                GlassMetric(loc.t("rl.totalUpdates"), value: "\(engine.rlState.totalUpdates)", color: theme.textPrimary)
            }
        }
    }
    
    // MARK: - RL Actions
    
    private var rlActionsCard: some View {
        GlassPanel(neon: LxColor.neonLime) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("rl.agentStatus"), icon: "brain.head.profile", color: LxColor.neonLime)
                GlassMetric(loc.t("rl.exploring"), value: engine.rlState.exploring ? loc.t("common.yes") : loc.t("common.no"),
                            color: engine.rlState.exploring ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("rl.rollbacks"), value: "\(engine.rlv2State.rollbackCount)",
                            color: engine.rlv2State.rollbackCount > 3 ? LxColor.bloodRed : theme.textPrimary)
                GlassMetric(loc.t("rl.epochs"), value: "\(engine.rlv2State.epochsCompleted)", color: LxColor.electricCyan)
                GlassMetric(loc.t("rl.signalDelta"), value: String(format: "%+.4f", engine.rlState.signalThresholdDelta), color: LxColor.electricCyan)
                GlassMetric(loc.t("rl.posScale"), value: String(format: "%.3f", engine.rlState.positionSizeScale), color: LxColor.neonLime)
                GlassMetric(loc.t("rl.offsetBps"), value: String(format: "%+.2f", engine.rlState.orderOffsetBps), color: LxColor.amber)
            }
        }
    }
    
    // MARK: - Helpers
    
    private func neonHeatColor(_ value: Double) -> Color {
        let clamped = max(min(value, 1.0), -1.0)
        if clamped > 0 {
            return LxColor.neonLime.opacity(0.15 + 0.85 * clamped)
        } else if clamped < 0 {
            return LxColor.magentaPink.opacity(0.15 + 0.85 * (-clamped))
        }
        return LxColor.coolSteel.opacity(0.15)
    }
}

struct PnLChartShape: Shape {
    let data: [Double]
    
    func path(in rect: CGRect) -> Path {
        guard data.count > 1 else { return Path() }
        let minVal = data.min() ?? 0
        let maxVal = data.max() ?? 1
        let range = maxVal - minVal
        let yScale = range > 0 ? rect.height / CGFloat(range) : 1
        let xStep = rect.width / CGFloat(data.count - 1)
        
        var path = Path()
        for (i, val) in data.enumerated() {
            let x = CGFloat(i) * xStep
            let y = rect.height - CGFloat(val - minVal) * yScale
            if i == 0 {
                path.move(to: CGPoint(x: x, y: y))
            } else {
                path.addLine(to: CGPoint(x: x, y: y))
            }
        }
        return path
    }
}
