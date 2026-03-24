// DashboardView.swift — Main dashboard overview

import SwiftUI

struct DashboardView: View {
    @EnvironmentObject var engine: TradingEngine
    
    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                // Status header
                HStack(spacing: 20) {
                    StatusCard(title: "Статус", value: engine.status.label,
                               color: statusColor)
                    StatusCard(title: "Mid Price",
                               value: String(format: "%.2f", engine.orderBook.midPrice),
                               color: .blue)
                    StatusCard(title: "Spread",
                               value: String(format: "%.4f", engine.orderBook.spread),
                               color: .orange)
                    StatusCard(title: "Режим",
                               value: engine.regime.regimeName,
                               color: regimeColor)
                }
                .padding(.horizontal)
                
                // Position & PnL
                HStack(spacing: 20) {
                    StatusCard(title: "Позиция",
                               value: String(format: "%.4f", engine.position.size),
                               color: engine.position.isLong ? .green : .red)
                    StatusCard(title: "Unrealized PnL",
                               value: String(format: "%.2f $", engine.position.unrealizedPnl),
                               color: engine.position.unrealizedPnl >= 0 ? .green : .red)
                    StatusCard(title: "Realized PnL",
                               value: String(format: "%.2f $", engine.position.realizedPnl),
                               color: engine.position.realizedPnl >= 0 ? .green : .red)
                    StatusCard(title: "Net PnL",
                               value: String(format: "%.2f $", engine.position.netPnl),
                               color: engine.position.netPnl >= 0 ? .green : .red)
                }
                .padding(.horizontal)
                
                // Metrics
                HStack(spacing: 20) {
                    StatusCard(title: "OB Updates",
                               value: "\(engine.metrics.obUpdates)",
                               color: .secondary)
                    StatusCard(title: "Trades",
                               value: "\(engine.metrics.tradesTotal)",
                               color: .secondary)
                    StatusCard(title: "Signals",
                               value: "\(engine.metrics.signalsTotal)",
                               color: .secondary)
                    StatusCard(title: "Orders Sent",
                               value: "\(engine.metrics.ordersSent)",
                               color: .secondary)
                }
                .padding(.horizontal)
                
                // Latency
                HStack(spacing: 20) {
                    StatusCard(title: "E2E p50",
                               value: String(format: "%.0f µs", engine.metrics.e2eLatencyP50Us),
                               color: .purple)
                    StatusCard(title: "E2E p99",
                               value: String(format: "%.0f µs", engine.metrics.e2eLatencyP99Us),
                               color: .purple)
                    StatusCard(title: "Model p50",
                               value: String(format: "%.0f µs", engine.metrics.modelLatencyP50Us),
                               color: .purple)
                    StatusCard(title: "Model p99",
                               value: String(format: "%.0f µs", engine.metrics.modelLatencyP99Us),
                               color: .purple)
                }
                .padding(.horizontal)
                
                // AI Prediction
                GroupBox("AI Предсказание") {
                    HStack(spacing: 40) {
                        VStack {
                            Text("Confidence")
                                .font(.caption)
                                .foregroundColor(.secondary)
                            Text(String(format: "%.1f%%", engine.prediction.modelConfidence * 100))
                                .font(.title2)
                                .fontWeight(.bold)
                        }
                        VStack {
                            Text("P(Up)")
                                .font(.caption)
                                .foregroundColor(.green)
                            Text(String(format: "%.1f%%", engine.prediction.probUp * 100))
                                .font(.title2)
                                .fontWeight(.bold)
                                .foregroundColor(.green)
                        }
                        VStack {
                            Text("P(Down)")
                                .font(.caption)
                                .foregroundColor(.red)
                            Text(String(format: "%.1f%%", engine.prediction.probDown * 100))
                                .font(.title2)
                                .fontWeight(.bold)
                                .foregroundColor(.red)
                        }
                        VStack {
                            Text("Circuit Breaker")
                                .font(.caption)
                                .foregroundColor(.secondary)
                            Text(engine.circuitBreaker.tripped ? "TRIPPED" : "OK")
                                .font(.title2)
                                .fontWeight(.bold)
                                .foregroundColor(engine.circuitBreaker.tripped ? .red : .green)
                        }
                    }
                    .padding()
                }
                .padding(.horizontal)
                
                // Recent logs
                GroupBox("Последние логи") {
                    VStack(alignment: .leading, spacing: 4) {
                        ForEach(engine.logs.suffix(10).reversed()) { log in
                            HStack {
                                Text(log.level.rawValue.uppercased())
                                    .font(.system(.caption2, design: .monospaced))
                                    .fontWeight(.bold)
                                    .foregroundColor(logColor(log.level))
                                    .frame(width: 50, alignment: .leading)
                                Text(log.message)
                                    .font(.system(.caption, design: .monospaced))
                                    .lineLimit(1)
                                Spacer()
                            }
                        }
                    }
                    .padding(8)
                }
                .padding(.horizontal)
                
                Spacer()
            }
            .padding(.vertical)
        }
        .navigationTitle("Dashboard")
    }
    
    private var statusColor: Color {
        switch engine.status {
        case .idle: return .secondary
        case .connecting: return .yellow
        case .connected: return .blue
        case .trading: return .green
        case .error: return .red
        case .stopping: return .orange
        }
    }
    
    private var regimeColor: Color {
        switch engine.regime.current {
        case 0: return .green
        case 1: return .red
        case 2: return .blue
        case 3: return .purple
        case 4: return .orange
        default: return .gray
        }
    }
    
    private func logColor(_ level: LogEntry.LogLevel) -> Color {
        switch level {
        case .debug: return .gray
        case .info: return .blue
        case .warn: return .orange
        case .error: return .red
        }
    }
}

// MARK: - Status Card

struct StatusCard: View {
    let title: String
    let value: String
    let color: Color
    
    var body: some View {
        VStack(spacing: 6) {
            Text(title)
                .font(.system(.caption, design: .monospaced))
                .foregroundColor(.secondary)
            Text(value)
                .font(.system(.body, design: .monospaced))
                .fontWeight(.semibold)
                .foregroundColor(color)
        }
        .frame(maxWidth: .infinity)
        .padding(12)
        .background(Color(nsColor: .controlBackgroundColor))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
}
