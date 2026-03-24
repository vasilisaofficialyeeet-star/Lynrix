// PortfolioView.swift — Position, PnL, and risk metrics

import SwiftUI

struct PortfolioView: View {
    @EnvironmentObject var engine: TradingEngine
    
    var body: some View {
        ScrollView {
            VStack(spacing: 20) {
                // Position header
                GroupBox {
                    VStack(spacing: 16) {
                        HStack {
                            Text("ТЕКУЩАЯ ПОЗИЦИЯ")
                                .font(.system(.headline, design: .monospaced))
                            Spacer()
                            if engine.position.hasPosition {
                                Text(engine.position.isLong ? "ЛОНГ" : "ШОРТ")
                                    .font(.system(.caption, design: .monospaced))
                                    .fontWeight(.bold)
                                    .padding(.horizontal, 8)
                                    .padding(.vertical, 3)
                                    .background(engine.position.isLong ? Color.green.opacity(0.2) : Color.red.opacity(0.2))
                                    .foregroundColor(engine.position.isLong ? .green : .red)
                                    .clipShape(RoundedRectangle(cornerRadius: 4))
                            } else {
                                Text("БЕЗ ПОЗИЦИИ")
                                    .font(.system(.caption, design: .monospaced))
                                    .foregroundColor(.secondary)
                            }
                        }
                        
                        LazyVGrid(columns: [
                            GridItem(.flexible()),
                            GridItem(.flexible()),
                            GridItem(.flexible()),
                        ], spacing: 16) {
                            PortfolioMetric(label: "Размер", value: String(format: "%.4f", engine.position.size),
                                          unit: engine.config.symbol)
                            PortfolioMetric(label: "Цена входа", value: String(format: "%.1f", engine.position.entryPrice))
                            PortfolioMetric(label: "Марк цена", value: String(format: "%.1f", engine.orderBook.midPrice))
                        }
                    }
                }
                
                // PnL section
                GroupBox {
                    VStack(spacing: 16) {
                        Text("ПРИБЫЛЬ И УБЫТОК")
                            .font(.system(.headline, design: .monospaced))
                            .frame(maxWidth: .infinity, alignment: .leading)
                        
                        LazyVGrid(columns: [
                            GridItem(.flexible()),
                            GridItem(.flexible()),
                            GridItem(.flexible()),
                            GridItem(.flexible()),
                        ], spacing: 16) {
                            PnlMetric(label: "Нереализ.", value: engine.position.unrealizedPnl)
                            PnlMetric(label: "Реализ.", value: engine.position.realizedPnl)
                            PnlMetric(label: "Фандинг", value: engine.position.fundingImpact)
                            PnlMetric(label: "Чистый PnL", value: engine.position.netPnl, isBold: true)
                        }
                    }
                }
                
                // Execution stats
                GroupBox {
                    VStack(spacing: 16) {
                        Text("СТАТИСТИКА ИСПОЛНЕНИЯ")
                            .font(.system(.headline, design: .monospaced))
                            .frame(maxWidth: .infinity, alignment: .leading)
                        
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 4), spacing: 16) {
                            StatMetric(label: "Ордеров отпр.", value: "\(engine.metrics.ordersSent)")
                            StatMetric(label: "Исполнено", value: "\(engine.metrics.ordersFilled)", color: .green)
                            StatMetric(label: "Отменено", value: "\(engine.metrics.ordersCancelled)", color: .orange)
                            StatMetric(label: "% исполн.",
                                      value: fillRate,
                                      color: .cyan)
                        }
                    }
                }
                
                // Latency
                GroupBox {
                    VStack(spacing: 16) {
                        Text("ЛАТЕНСИ")
                            .font(.system(.headline, design: .monospaced))
                            .frame(maxWidth: .infinity, alignment: .leading)
                        
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 4), spacing: 16) {
                            StatMetric(label: "E2E p50", value: String(format: "%.1f мкс", engine.metrics.e2eLatencyP50Us), color: .cyan)
                            StatMetric(label: "E2E p99", value: String(format: "%.1f мкс", engine.metrics.e2eLatencyP99Us), color: .cyan)
                            StatMetric(label: "Фичи p50", value: String(format: "%.1f мкс", engine.metrics.featLatencyP50Us), color: .blue)
                            StatMetric(label: "Фичи p99", value: String(format: "%.1f мкс", engine.metrics.featLatencyP99Us), color: .blue)
                        }
                    }
                }
                
                Spacer()
            }
            .padding(20)
        }
        .background(Color(.windowBackgroundColor))
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
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(.caption2)
                .foregroundColor(.secondary)
            HStack(alignment: .firstTextBaseline, spacing: 4) {
                Text(value)
                    .font(.system(.body, design: .monospaced))
                    .fontWeight(.medium)
                if !unit.isEmpty {
                    Text(unit)
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
            }
        }
    }
}

struct PnlMetric: View {
    let label: String
    let value: Double
    var isBold: Bool = false
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(.caption2)
                .foregroundColor(.secondary)
            Text(String(format: "%+.4f", value))
                .font(.system(isBold ? .title3 : .body, design: .monospaced))
                .fontWeight(isBold ? .bold : .medium)
                .foregroundColor(value > 0.0001 ? .green : (value < -0.0001 ? .red : .secondary))
        }
    }
}

struct StatMetric: View {
    let label: String
    let value: String
    var color: Color = .primary
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(.caption2)
                .foregroundColor(.secondary)
            Text(value)
                .font(.system(.body, design: .monospaced))
                .fontWeight(.medium)
                .foregroundColor(color)
        }
    }
}
