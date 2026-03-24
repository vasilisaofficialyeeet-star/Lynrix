// AIDashboardView.swift — Glassmorphism 2026 AI Decision Visualizer (Lynrix v2.5)

import SwiftUI
import Charts

struct AIDashboardView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                // Regime + Prediction
                HStack(spacing: 12) {
                    regimeCard
                    predictionCard
                }
                
                // Threshold + Circuit Breaker
                HStack(spacing: 12) {
                    thresholdCard
                    circuitBreakerCard
                }
                
                // Multi-horizon predictions
                horizonCard
                
                // Accuracy + Inference
                HStack(spacing: 12) {
                    accuracyCard
                    inferenceCard
                }
                
                // PnL chart
                pnlChartCard
                
                // Drawdown + Rolling Accuracy
                HStack(spacing: 12) {
                    drawdownChartCard
                    rollingAccuracyChartCard
                }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - Regime
    
    private var regimeCard: some View {
        let r = engine.regime
        let rColor = LxColor.regime(r.current)
        return GlassPanel(neon: rColor) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ai.marketRegime"), icon: "waveform.path.ecg", color: rColor)
                HStack {
                    RegimeBadge(regime: r.current, confidence: r.confidence)
                    Spacer()
                    GlowText(String(format: "%.0f%%", r.confidence * 100),
                             font: LxFont.bigMetric,
                             color: r.confidence > 0.7 ? LxColor.neonLime : LxColor.amber, glow: 5)
                }
                GlassMetric(loc.t("dashboard.volatility"), value: String(format: "%.4f", r.volatility), color: theme.textPrimary)
                GlassMetric(loc.t("ai.trendScore"), value: String(format: "%.3f", r.trendScore),
                            color: r.trendScore > 0.3 ? LxColor.neonLime : (r.trendScore < -0.3 ? LxColor.magentaPink : theme.textPrimary))
                GlassMetric(loc.t("ai.mrScore"), value: String(format: "%.3f", r.mrScore), color: theme.textPrimary)
                GlassMetric(loc.t("ai.liquidity"), value: String(format: "%.3f", r.liqScore), color: theme.textPrimary)
            }
        }
    }
    
    // MARK: - Prediction
    
    private var predictionCard: some View {
        let p = engine.prediction
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ai.modelPredictions"), icon: "brain.head.profile", color: LxColor.electricCyan)
                HStack(spacing: 16) {
                    VStack(spacing: 2) {
                        Text(p.direction)
                            .font(.system(size: 32))
                            .shadow(color: LxColor.electricCyan.opacity(0.4), radius: 6)
                        Text(loc.t("ai.direction"))
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                    }
                    VStack(spacing: 6) {
                        HStack {
                            Text(loc.t("ai.probUp"))
                                .font(LxFont.micro)
                                .foregroundColor(LxColor.neonLime.opacity(0.6))
                            Spacer()
                            GlowText(String(format: "%.1f%%", p.probUp * 100),
                                     font: LxFont.mono(11, weight: .bold), color: LxColor.neonLime, glow: 3)
                        }
                        NeonProgressBar(value: p.probUp, color: LxColor.neonLime)
                        HStack {
                            Text(loc.t("ai.probDown"))
                                .font(LxFont.micro)
                                .foregroundColor(LxColor.magentaPink.opacity(0.6))
                            Spacer()
                            GlowText(String(format: "%.1f%%", p.probDown * 100),
                                     font: LxFont.mono(11, weight: .bold), color: LxColor.magentaPink, glow: 3)
                        }
                        NeonProgressBar(value: p.probDown, color: LxColor.magentaPink)
                    }
                }
                GlassMetric(loc.t("ai.confidence"), value: String(format: "%.1f%%", p.modelConfidence * 100),
                            color: p.modelConfidence > 0.7 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("ai.inferenceLatency"), value: String(format: "%.0f us", p.inferenceLatencyUs),
                            color: LxColor.electricCyan)
            }
        }
    }
    
    // MARK: - Threshold
    
    private var thresholdCard: some View {
        let t = engine.threshold
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ai.adaptiveThresholds"), icon: "dial.medium", color: LxColor.electricCyan)
                HStack {
                    GlowText(String(format: "%.3f", t.currentThreshold),
                             font: LxFont.bigMetric, color: LxColor.electricCyan, glow: 5)
                    Spacer()
                    Text(String(format: "base: %.3f", t.baseThreshold))
                        .font(LxFont.mono(9))
                        .foregroundColor(theme.textTertiary)
                }
                GlassMetric(loc.t("ai.volatilityAdj"), value: String(format: "%+.4f", t.volatilityAdj), color: theme.textPrimary)
                GlassMetric(loc.t("ai.accuracyAdj"), value: String(format: "%+.4f", t.accuracyAdj), color: theme.textPrimary)
                GlassMetric(loc.t("ai.liquidityAdj"), value: String(format: "%+.4f", t.liquidityAdj), color: theme.textPrimary)
                GlassMetric(loc.t("ai.spreadAdj"), value: String(format: "%+.4f", t.spreadAdj), color: theme.textPrimary)
                GlassMetric(loc.t("ai.recentAccuracy"), value: String(format: "%.1f%%", t.recentAccuracy * 100),
                            color: t.recentAccuracy > 0.4 ? LxColor.neonLime : LxColor.magentaPink)
                GlassMetric(loc.t("ai.signals"), value: "\(t.correctSignals)/\(t.totalSignals)", color: LxColor.electricCyan)
            }
        }
    }
    
    // MARK: - Circuit Breaker
    
    private var circuitBreakerCard: some View {
        let cb = engine.circuitBreaker
        let cbColor = cb.tripped ? LxColor.bloodRed : (cb.inCooldown ? LxColor.amber : LxColor.neonLime)
        return GlassPanel(neon: cbColor) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("ai.circuitBreaker"), icon: "exclamationmark.triangle", color: cbColor)
                    Spacer()
                    StatusBadge(
                        cb.tripped ? loc.t("dashboard.tripped") : (cb.inCooldown ? loc.t("dashboard.cooldown") : loc.t("common.ok")),
                        color: cbColor,
                        pulse: cb.tripped
                    )
                }
                GlassMetric(loc.t("ai.tripped"), value: cb.tripped ? loc.t("common.yes") : loc.t("common.no"),
                            color: cb.tripped ? LxColor.bloodRed : LxColor.neonLime)
                GlassMetric(loc.t("ai.cooldownLabel"), value: cb.inCooldown ? loc.t("common.yes") : loc.t("common.no"),
                            color: cb.inCooldown ? LxColor.amber : LxColor.neonLime)
                GlassMetric(loc.t("dashboard.drawdown"), value: String(format: "%.2f%%", cb.drawdownPct * 100),
                            color: cb.drawdownPct > 0.03 ? LxColor.bloodRed : (cb.drawdownPct > 0.01 ? LxColor.amber : LxColor.neonLime))
                GlassMetric(loc.t("dashboard.consecLosses"), value: "\(cb.consecutiveLosses)",
                            color: cb.consecutiveLosses > 3 ? LxColor.amber : theme.textPrimary)
                
                NeonButton(loc.t("ai.resetCB"), icon: "arrow.counterclockwise", color: LxColor.amber) {
                    engine.addLog(.warn, "Circuit breaker manually reset")
                }
            }
        }
    }
    
    // MARK: - Horizon
    
    private var horizonCard: some View {
        let p = engine.prediction
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ai.multiHorizon"), icon: "clock.arrow.2.circlepath", color: LxColor.electricCyan)
                
                HStack(spacing: 0) {
                    Text(loc.t("ai.horizon")).frame(width: 60, alignment: .leading)
                    Text(loc.t("ai.probUp")).frame(width: 60, alignment: .trailing).foregroundColor(LxColor.neonLime.opacity(0.5))
                    Text(loc.t("ai.probDown")).frame(width: 60, alignment: .trailing).foregroundColor(LxColor.magentaPink.opacity(0.5))
                    Text(loc.t("ai.pFlat")).frame(width: 60, alignment: .trailing).foregroundColor(LxColor.electricCyan.opacity(0.5))
                    Text(loc.t("ai.move")).frame(width: 60, alignment: .trailing)
                    Text(loc.t("ai.dir")).frame(width: 40, alignment: .center)
                }
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(theme.textTertiary)
                
                glassHorizonRow("100ms", h: p.h100ms)
                glassHorizonRow("500ms", h: p.h500ms)
                glassHorizonRow("1s", h: p.h1s)
                glassHorizonRow("3s", h: p.h3s)
            }
        }
    }
    
    private func glassHorizonRow(_ label: String, h: (up: Double, down: Double, flat: Double)) -> some View {
        let dir = h.up > h.down + 0.05 ? "↑" : (h.down > h.up + 0.05 ? "↓" : "→")
        let dirColor = h.up > h.down + 0.05 ? LxColor.neonLime : (h.down > h.up + 0.05 ? LxColor.magentaPink : LxColor.electricCyan)
        let move = max(h.up, h.down)
        return HStack(spacing: 0) {
            Text(label)
                .frame(width: 60, alignment: .leading)
                .foregroundColor(theme.textSecondary)
            Text(String(format: "%.1f%%", h.up * 100))
                .foregroundColor(LxColor.neonLime)
                .frame(width: 60, alignment: .trailing)
            Text(String(format: "%.1f%%", h.down * 100))
                .foregroundColor(LxColor.magentaPink)
                .frame(width: 60, alignment: .trailing)
            Text(String(format: "%.1f%%", h.flat * 100))
                .foregroundColor(LxColor.electricCyan)
                .frame(width: 60, alignment: .trailing)
            Text(String(format: "%.1f%%", move * 100))
                .foregroundColor(theme.textPrimary)
                .frame(width: 60, alignment: .trailing)
            Text(dir)
                .foregroundColor(dirColor)
                .shadow(color: dirColor.opacity(0.4), radius: 3)
                .frame(width: 40, alignment: .center)
        }
        .font(LxFont.mono(10))
    }
    
    // MARK: - Accuracy
    
    private var accuracyCard: some View {
        let acc = engine.accuracy
        return GlassPanel(neon: LxColor.neonLime) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ai.accuracy"), icon: "target", color: LxColor.neonLime)
                GlassMetric(loc.t("ai.overall"), value: String(format: "%.1f%%", acc.accuracy * 100),
                            color: acc.accuracy > 0.4 ? LxColor.neonLime : LxColor.magentaPink)
                GlassMetric(loc.t("ai.rolling"), value: String(format: "%.1f%%", acc.rollingAccuracy * 100), color: LxColor.electricCyan)
                GlassMetric(loc.t("ai.predictions"), value: "\(acc.totalPredictions)", color: theme.textPrimary)
                GlassMetric(loc.t("ai.correct"), value: "\(acc.correctPredictions)", color: LxColor.neonLime)
                GlassMetric(loc.t("ai.calibrationErr"), value: String(format: "%.4f", acc.calibrationError), color: theme.textPrimary)
                GlassMetric(loc.t("ai.backend"), value: acc.usingOnnx ? loc.t("ai.onnx") : loc.t("model.native"),
                            color: acc.usingOnnx ? LxColor.electricCyan : theme.textSecondary)
            }
        }
    }
    
    private var inferenceCard: some View {
        let sys = engine.systemMonitor
        return GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ai.inference"), icon: "cpu", color: LxColor.electricCyan)
                GlassMetric(loc.t("ai.backend"), value: sys.inferenceBackend, color: LxColor.electricCyan)
                GlassMetric(loc.t("ai.modelP50"), value: String(format: "%.0f us", sys.modelLatencyP50Us), color: theme.textPrimary)
                GlassMetric(loc.t("ai.modelP99"), value: String(format: "%.0f us", sys.modelLatencyP99Us),
                            color: sys.modelLatencyP99Us > 500 ? LxColor.amber : theme.textPrimary)
                GlassMetric(loc.t("ai.gpu"), value: sys.gpuAvailable ? sys.gpuName : "N/A",
                            color: sys.gpuAvailable ? LxColor.neonLime : theme.textTertiary)
                GlassMetric(loc.t("ai.onnx"), value: engine.accuracy.usingOnnx ? loc.t("ai.onnxActive") : loc.t("ai.onnxInactive"),
                            color: engine.accuracy.usingOnnx ? LxColor.neonLime : theme.textTertiary)
            }
        }
    }
    
    // MARK: - Charts
    
    private var pnlChartCard: some View {
        GlassPanel(neon: engine.position.netPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("ai.pnlHistory"), icon: "chart.xyaxis.line",
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
    
    private var drawdownChartCard: some View {
        GlassPanel(neon: LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ai.drawdownHistory"), icon: "chart.line.downtrend.xyaxis", color: LxColor.magentaPink)
                if #available(macOS 14.0, *), !engine.drawdownHistory.isEmpty {
                    Chart {
                        ForEach(Array(engine.drawdownHistory.enumerated()), id: \.offset) { idx, val in
                            AreaMark(x: .value("T", idx), y: .value("DD", val * 100))
                                .foregroundStyle(
                                    LinearGradient(colors: [LxColor.magentaPink.opacity(0.3), LxColor.magentaPink.opacity(0.05)],
                                                   startPoint: .top, endPoint: .bottom)
                                )
                        }
                    }
                    .frame(height: 70)
                    .chartYAxis {
                        AxisMarks(position: .leading) { _ in
                            AxisValueLabel().foregroundStyle(theme.textTertiary)
                            AxisGridLine().foregroundStyle(theme.borderSubtle)
                        }
                    }
                    .chartXAxis(.hidden)
                } else {
                    Text(loc.t("ai.noData"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                        .frame(height: 60)
                        .frame(maxWidth: .infinity)
                }
            }
        }
    }
    
    private var rollingAccuracyChartCard: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("ai.rollingAccuracy"), icon: "chart.line.uptrend.xyaxis", color: LxColor.electricCyan)
                if #available(macOS 14.0, *), !engine.accuracyHistory.isEmpty {
                    Chart {
                        ForEach(Array(engine.accuracyHistory.enumerated()), id: \.offset) { idx, val in
                            LineMark(x: .value("T", idx), y: .value("Acc", val * 100))
                                .foregroundStyle(LxColor.electricCyan)
                        }
                        RuleMark(y: .value("Random", 33.3))
                            .foregroundStyle(LxColor.bloodRed.opacity(0.5))
                            .lineStyle(StrokeStyle(dash: [4, 4]))
                    }
                    .frame(height: 70)
                    .chartYAxis {
                        AxisMarks(position: .leading) { _ in
                            AxisValueLabel().foregroundStyle(theme.textTertiary)
                            AxisGridLine().foregroundStyle(theme.borderSubtle)
                        }
                    }
                    .chartXAxis(.hidden)
                    .chartYScale(domain: 0...100)
                } else {
                    Text(loc.t("ai.noData"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                        .frame(height: 60)
                        .frame(maxWidth: .infinity)
                }
            }
        }
    }
}
