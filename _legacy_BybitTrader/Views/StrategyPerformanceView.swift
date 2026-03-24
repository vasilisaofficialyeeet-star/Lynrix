// StrategyPerformanceView.swift — Strategy metrics + health monitoring tab

import SwiftUI
import Charts

struct StrategyPerformanceView: View {
    @EnvironmentObject var engine: TradingEngine
    
    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                // Health Status Banner
                healthBanner
                
                HStack(spacing: 16) {
                    // Key Metrics Card
                    keyMetricsCard
                    
                    // Risk Metrics Card
                    riskMetricsCard
                }
                
                HStack(spacing: 16) {
                    // Trade Statistics
                    tradeStatsCard
                    
                    // Health Components
                    healthComponentsCard
                }
                
                // PnL Chart
                pnlChartCard
                
                // RL Optimizer Status
                rlOptimizerCard
            }
            .padding()
        }
        .background(Color(.windowBackgroundColor))
    }
    
    // MARK: - Health Banner
    
    private var healthBanner: some View {
        let h = engine.strategyHealth
        return HStack {
            Circle()
                .fill(Color(h.levelColor))
                .frame(width: 12, height: 12)
            Text(h.levelName)
                .font(.headline)
            
            Spacer()
            
            Text("Score: \(h.healthScore, specifier: "%.2f")")
                .font(.subheadline)
                .foregroundColor(.secondary)
            
            Text("Activity: \(h.activityScale, specifier: "%.0f%%")")
                .font(.subheadline)
                .padding(.horizontal, 8)
                .padding(.vertical, 2)
                .background(h.activityScale < 0.5 ? Color.red.opacity(0.2) : Color.green.opacity(0.2))
                .cornerRadius(4)
            
            if h.accuracyDeclining {
                Label("Acc\u{2193}", systemImage: "arrow.down.circle.fill")
                    .foregroundColor(.orange)
                    .font(.caption)
            }
            if h.drawdownWarning {
                Label("DD!", systemImage: "exclamationmark.triangle.fill")
                    .foregroundColor(.red)
                    .font(.caption)
            }
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Key Metrics
    
    private var keyMetricsCard: some View {
        let sm = engine.strategyMetrics
        return VStack(alignment: .leading, spacing: 8) {
            Label("Ключевые метрики", systemImage: "chart.bar.fill")
                .font(.headline)
            
            Divider()
            
            metricRow("Sharpe Ratio", value: String(format: "%.2f", sm.sharpeRatio),
                       color: sm.sharpeRatio > 1 ? .green : (sm.sharpeRatio < 0 ? .red : .primary))
            metricRow("Sortino Ratio", value: String(format: "%.2f", sm.sortinoRatio),
                       color: sm.sortinoRatio > 1 ? .green : .primary)
            metricRow("Profit Factor", value: String(format: "%.2f", sm.profitFactor),
                       color: sm.profitFactor > 1.5 ? .green : (sm.profitFactor < 1 ? .red : .primary))
            metricRow("Win Rate", value: String(format: "%.1f%%", sm.winRate * 100),
                       color: sm.winRate > 0.55 ? .green : (sm.winRate < 0.45 ? .red : .primary))
            metricRow("Expectancy", value: String(format: "$%.4f", sm.expectancy),
                       color: sm.expectancy > 0 ? .green : .red)
            metricRow("Total PnL", value: String(format: "$%.4f", sm.totalPnl),
                       color: sm.totalPnl > 0 ? .green : .red)
            metricRow("Calmar Ratio", value: String(format: "%.2f", sm.calmarRatio))
            metricRow("Recovery Factor", value: String(format: "%.2f", sm.recoveryFactor))
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Risk Metrics
    
    private var riskMetricsCard: some View {
        let sm = engine.strategyMetrics
        return VStack(alignment: .leading, spacing: 8) {
            Label("Риск-метрики", systemImage: "shield.fill")
                .font(.headline)
            
            Divider()
            
            metricRow("Max Drawdown", value: String(format: "%.2f%%", sm.maxDrawdownPct * 100),
                       color: sm.maxDrawdownPct > 0.05 ? .red : .primary)
            metricRow("Current DD", value: String(format: "%.2f%%", sm.currentDrawdown * 100),
                       color: sm.currentDrawdown > 0.03 ? .orange : .primary)
            metricRow("Best Trade", value: String(format: "$%.4f", sm.bestTrade), color: .green)
            metricRow("Worst Trade", value: String(format: "$%.4f", sm.worstTrade), color: .red)
            metricRow("Avg Win", value: String(format: "$%.4f", sm.avgWin), color: .green)
            metricRow("Avg Loss", value: String(format: "$%.4f", sm.avgLoss), color: .red)
            metricRow("Daily PnL", value: String(format: "$%.4f", sm.dailyPnl),
                       color: sm.dailyPnl > 0 ? .green : .red)
            metricRow("Hourly PnL", value: String(format: "$%.6f", sm.hourlyPnl),
                       color: sm.hourlyPnl > 0 ? .green : .red)
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Trade Stats
    
    private var tradeStatsCard: some View {
        let sm = engine.strategyMetrics
        return VStack(alignment: .leading, spacing: 8) {
            Label("Статистика сделок", systemImage: "arrow.left.arrow.right")
                .font(.headline)
            
            Divider()
            
            metricRow("Total Trades", value: "\(sm.totalTrades)")
            metricRow("Winning", value: "\(sm.winningTrades)", color: .green)
            metricRow("Losing", value: "\(sm.losingTrades)", color: .red)
            metricRow("Consec Wins", value: "\(sm.consecutiveWins) (max \(sm.maxConsecutiveWins))", color: .green)
            metricRow("Consec Losses", value: "\(sm.consecutiveLosses) (max \(sm.maxConsecutiveLosses))", color: .red)
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Health Components
    
    private var healthComponentsCard: some View {
        let h = engine.strategyHealth
        return VStack(alignment: .leading, spacing: 8) {
            Label("Компоненты здоровья", systemImage: "heart.fill")
                .font(.headline)
            
            Divider()
            
            healthBar("Accuracy", score: h.accuracyScore, declining: h.accuracyDeclining)
            healthBar("PnL", score: h.pnlScore, declining: h.pnlDeclining)
            healthBar("Drawdown", score: h.drawdownScore, declining: false)
            healthBar("Sharpe", score: h.sharpeScore, declining: false)
            healthBar("Consistency", score: h.consistencyScore, declining: false)
            healthBar("Fill Rate", score: h.fillRateScore, declining: false)
            
            Divider()
            
            metricRow("Regime Changes/1h", value: "\(h.regimeChanges1h)",
                       color: h.regimeChanges1h > 5 ? .orange : .primary)
            metricRow("Threshold Offset", value: String(format: "+%.3f", h.thresholdOffset))
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - PnL Chart
    
    private var pnlChartCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            Label("PnL History", systemImage: "chart.xyaxis.line")
                .font(.headline)
            
            if #available(macOS 14.0, *), !engine.pnlHistory.isEmpty {
                Chart {
                    ForEach(Array(engine.pnlHistory.enumerated()), id: \.offset) { idx, val in
                        LineMark(
                            x: .value("Tick", idx),
                            y: .value("PnL", val)
                        )
                        .foregroundStyle(val >= 0 ? Color.green : Color.red)
                    }
                }
                .frame(height: 120)
                .chartYAxis {
                    AxisMarks(position: .leading)
                }
            } else {
                Text("Нет данных")
                    .foregroundColor(.secondary)
                    .frame(height: 80)
                    .frame(maxWidth: .infinity)
            }
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - RL Optimizer
    
    private var rlOptimizerCard: some View {
        let rl = engine.rlState
        return VStack(alignment: .leading, spacing: 8) {
            HStack {
                Label("RL Optimizer (PPO)", systemImage: "brain")
                    .font(.headline)
                Spacer()
                Text(rl.exploring ? "Exploring" : "Exploiting")
                    .font(.caption)
                    .padding(.horizontal, 6)
                    .padding(.vertical, 2)
                    .background(rl.exploring ? Color.blue.opacity(0.2) : Color.green.opacity(0.2))
                    .cornerRadius(4)
            }
            
            Divider()
            
            HStack(spacing: 24) {
                VStack(alignment: .leading, spacing: 4) {
                    metricRow("Steps", value: "\(rl.totalSteps)")
                    metricRow("Updates", value: "\(rl.totalUpdates)")
                    metricRow("Avg Reward", value: String(format: "%.4f", rl.avgReward),
                               color: rl.avgReward > 0 ? .green : .red)
                }
                VStack(alignment: .leading, spacing: 4) {
                    metricRow("\u{0394} Threshold", value: String(format: "%+.4f", rl.signalThresholdDelta))
                    metricRow("Size Scale", value: String(format: "%.2fx", rl.positionSizeScale))
                    metricRow("Offset BPS", value: String(format: "%+.2f", rl.orderOffsetBps))
                }
                VStack(alignment: .leading, spacing: 4) {
                    metricRow("Policy Loss", value: String(format: "%.4f", rl.policyLoss))
                    metricRow("Value Loss", value: String(format: "%.4f", rl.valueLoss))
                    metricRow("Value Est", value: String(format: "%.4f", rl.valueEstimate))
                }
            }
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Helpers
    
    private func metricRow(_ label: String, value: String, color: Color = .primary) -> some View {
        HStack {
            Text(label)
                .font(.caption)
                .foregroundColor(.secondary)
            Spacer()
            Text(value)
                .font(.caption.monospacedDigit())
                .foregroundColor(color)
        }
    }
    
    private func healthBar(_ label: String, score: Double, declining: Bool) -> some View {
        HStack(spacing: 6) {
            Text(label)
                .font(.caption)
                .foregroundColor(.secondary)
                .frame(width: 80, alignment: .leading)
            
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Rectangle()
                        .fill(Color.gray.opacity(0.2))
                        .frame(height: 8)
                        .cornerRadius(4)
                    Rectangle()
                        .fill(barColor(score))
                        .frame(width: max(geo.size.width * score, 2), height: 8)
                        .cornerRadius(4)
                }
            }
            .frame(height: 8)
            
            Text(String(format: "%.0f%%", score * 100))
                .font(.caption2.monospacedDigit())
                .frame(width: 32, alignment: .trailing)
            
            if declining {
                Image(systemName: "arrow.down")
                    .font(.caption2)
                    .foregroundColor(.orange)
            }
        }
    }
    
    private func barColor(_ score: Double) -> Color {
        if score > 0.7 { return .green }
        if score > 0.4 { return .yellow }
        return .red
    }
}
