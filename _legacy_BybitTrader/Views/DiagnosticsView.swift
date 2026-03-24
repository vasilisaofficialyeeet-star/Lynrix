// DiagnosticsView.swift — Runtime diagnostics panel for debugging

import SwiftUI
import os.log

struct DiagnosticsView: View {
    @EnvironmentObject var engine: TradingEngine
    @State private var refreshCount: Int = 0
    
    private let timer = Timer.publish(every: 1.0, on: .main, in: .common).autoconnect()
    
    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                GroupBox {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Image(systemName: "cpu")
                                .foregroundColor(.cyan)
                            Text("СИСТЕМНАЯ ДИАГНОСТИКА")
                                .font(.system(.headline, design: .monospaced))
                        }
                        DiagRow(label: "Версия", value: appVersion)
                        DiagRow(label: "macOS", value: ProcessInfo.processInfo.operatingSystemVersionString)
                        DiagRow(label: "Ядра CPU", value: "\(ProcessInfo.processInfo.processorCount)")
                        DiagRow(label: "Оперативная память", value: "\(ProcessInfo.processInfo.physicalMemory / 1_073_741_824) ГБ")
                        DiagRow(label: "ID процесса", value: "\(ProcessInfo.processInfo.processIdentifier)")
                        DiagRow(label: "Аптайм", value: formatUptime(ProcessInfo.processInfo.systemUptime))
                    }
                }
                
                GroupBox {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Image(systemName: "gearshape.2")
                                .foregroundColor(.green)
                            Text("СОСТОЯНИЕ ДВИЖКА")
                                .font(.system(.headline, design: .monospaced))
                        }
                        DiagRow(label: "Статус", value: engine.status.label, color: statusColor)
                        DiagRow(label: "Режим", value: engine.paperMode ? "БУМАЖНАЯ" : "РЕАЛЬНАЯ",
                                color: engine.paperMode ? .blue : .red)
                        DiagRow(label: "Символ", value: engine.config.symbol)
                        DiagRow(label: "Объём ордера", value: String(format: "%.4f", engine.config.orderQty))
                        DiagRow(label: "Порог сигнала", value: String(format: "%.2f", engine.config.signalThreshold))
                        DiagRow(label: "Переподключение", value: engine.isReconnecting ? "ДА" : "НЕТ",
                                color: engine.isReconnecting ? .orange : .green)
                    }
                }
                
                GroupBox {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Image(systemName: "chart.bar.doc.horizontal")
                                .foregroundColor(.purple)
                            Text("ДАННЫЕ")
                                .font(.system(.headline, design: .monospaced))
                        }
                        DiagRow(label: "Стакан валиден", value: engine.orderBook.valid ? "ДА" : "НЕТ",
                                color: engine.orderBook.valid ? .green : .red)
                        DiagRow(label: "Уровни бид", value: "\(engine.orderBook.bids.count)")
                        DiagRow(label: "Уровни аск", value: "\(engine.orderBook.asks.count)")
                        DiagRow(label: "Лучший бид", value: String(format: "%.2f", engine.orderBook.bestBid))
                        DiagRow(label: "Лучший аск", value: String(format: "%.2f", engine.orderBook.bestAsk))
                        DiagRow(label: "Записей логов", value: "\(engine.logs.count)")
                        DiagRow(label: "Сигналов", value: "\(engine.signals.count)")
                        DiagRow(label: "Сделок", value: "\(engine.trades.count)")
                    }
                }
                
                GroupBox {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Image(systemName: "gauge.with.dots.needle.67percent")
                                .foregroundColor(.orange)
                            Text("ПРОИЗВОДИТЕЛЬНОСТЬ")
                                .font(.system(.headline, design: .monospaced))
                        }
                        DiagRow(label: "Обновл. стакана", value: "\(engine.metrics.obUpdates)")
                        DiagRow(label: "Сделок всего", value: "\(engine.metrics.tradesTotal)")
                        DiagRow(label: "Сигналов всего", value: "\(engine.metrics.signalsTotal)")
                        DiagRow(label: "Ордеров отпр.", value: "\(engine.metrics.ordersSent)")
                        DiagRow(label: "WS переподкл.", value: "\(engine.metrics.wsReconnects)",
                                color: engine.metrics.wsReconnects > 0 ? .orange : .green)
                        DiagRow(label: "E2E p50", value: String(format: "%.1f мкс", engine.metrics.e2eLatencyP50Us))
                        DiagRow(label: "E2E p99", value: String(format: "%.1f мкс", engine.metrics.e2eLatencyP99Us))
                    }
                }
                
                Spacer()
            }
            .padding(20)
        }
        .background(Color(.windowBackgroundColor))
        .onReceive(timer) { _ in refreshCount += 1 }
    }
    
    private var appVersion: String {
        let v = Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.0"
        let b = Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "1"
        return "\(v) (\(b))"
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
    
    private func formatUptime(_ seconds: TimeInterval) -> String {
        let h = Int(seconds) / 3600
        let m = (Int(seconds) % 3600) / 60
        return "\(h)h \(m)m"
    }
}

struct DiagRow: View {
    let label: String
    let value: String
    var color: Color = .primary
    
    var body: some View {
        HStack {
            Text(label)
                .font(.system(.caption, design: .monospaced))
                .foregroundColor(.secondary)
                .frame(width: 150, alignment: .leading)
            Spacer()
            Text(value)
                .font(.system(.caption, design: .monospaced))
                .fontWeight(.medium)
                .foregroundColor(color)
        }
    }
}
