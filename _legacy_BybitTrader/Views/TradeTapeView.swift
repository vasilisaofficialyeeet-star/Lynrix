// TradeTapeView.swift — Recent trades with aggressor side and CSV export

import SwiftUI
import UniformTypeIdentifiers

struct TradeTapeView: View {
    @EnvironmentObject var engine: TradingEngine
    @State private var autoScroll: Bool = true
    
    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack(spacing: 12) {
                Text("ЛЕНТА СДЕЛОК")
                    .font(.system(.headline, design: .monospaced))
                
                Spacer()
                
                Text("\(engine.trades.count) сделок")
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(.secondary)
                
                Toggle("Автопрокрутка", isOn: $autoScroll)
                    .toggleStyle(.switch)
                    .controlSize(.small)
                
                Button(action: exportCSV) {
                    Label("CSV", systemImage: "square.and.arrow.up")
                        .font(.caption)
                }
                .buttonStyle(.borderless)
                .help("Экспорт сделок в CSV")
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(Color(.controlBackgroundColor).opacity(0.3))
            
            Divider()
            
            // Column headers
            HStack(spacing: 0) {
                Text("ВРЕМЯ")
                    .frame(width: 100, alignment: .leading)
                Text("СТОРОНА")
                    .frame(width: 60, alignment: .center)
                Text("ЦЕНА")
                    .frame(width: 120, alignment: .trailing)
                Text("ОБЪЁМ")
                    .frame(width: 100, alignment: .trailing)
                Text("СУММА")
                    .frame(maxWidth: .infinity, alignment: .trailing)
            }
            .font(.system(.caption2, design: .monospaced))
            .foregroundColor(.secondary)
            .padding(.horizontal, 16)
            .padding(.vertical, 6)
            .background(Color(.controlBackgroundColor).opacity(0.2))
            
            // Trade list
            if engine.trades.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "chart.line.uptrend.xyaxis")
                        .font(.largeTitle)
                        .foregroundColor(.secondary.opacity(0.3))
                    Text("Сделок пока нет")
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(.secondary)
                    Text("Сделки появятся после запуска движка")
                        .font(.caption)
                        .foregroundColor(.secondary.opacity(0.6))
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollViewReader { proxy in
                    ScrollView {
                        LazyVStack(spacing: 0) {
                            ForEach(engine.trades) { trade in
                                TradeRow(trade: trade)
                                    .id(trade.id)
                            }
                        }
                        .padding(.horizontal, 12)
                    }
                    .onChange(of: engine.trades.count) { _ in
                        if autoScroll, let first = engine.trades.first {
                            withAnimation(.none) {
                                proxy.scrollTo(first.id, anchor: .top)
                            }
                        }
                    }
                }
            }
            
            // Summary bar
            if !engine.trades.isEmpty {
                Divider()
                HStack(spacing: 20) {
                    HStack(spacing: 4) {
                        Circle().fill(Color.green).frame(width: 6, height: 6)
                        Text("Покупка: \(buyCount)")
                            .foregroundColor(.green)
                    }
                    HStack(spacing: 4) {
                        Circle().fill(Color.red).frame(width: 6, height: 6)
                        Text("Продажа: \(sellCount)")
                            .foregroundColor(.red)
                    }
                    
                    Spacer()
                    
                    Text("Объём: \(String(format: "%.4f", totalVolume))")
                        .foregroundColor(.secondary)
                    Text("Ср. цена: \(String(format: "%.1f", avgPrice))")
                        .foregroundColor(.secondary)
                }
                .font(.system(.caption, design: .monospaced))
                .padding(.horizontal, 16)
                .padding(.vertical, 6)
                .background(Color(.controlBackgroundColor).opacity(0.3))
            }
        }
        .background(Color(.windowBackgroundColor))
    }
    
    private var buyCount: Int { engine.trades.filter { !$0.isBuyerMaker }.count }
    private var sellCount: Int { engine.trades.filter { $0.isBuyerMaker }.count }
    private var totalVolume: Double { engine.trades.reduce(0) { $0 + $1.qty } }
    private var avgPrice: Double {
        guard !engine.trades.isEmpty else { return 0 }
        let totalNotional = engine.trades.reduce(0.0) { $0 + $1.price * $1.qty }
        return totalNotional / totalVolume
    }
    
    private func exportCSV() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.commaSeparatedText]
        panel.nameFieldStringValue = "trades_\(ISO8601DateFormatter().string(from: Date())).csv"
        panel.begin { result in
            if result == .OK, let url = panel.url {
                try? engine.exportTradesCSV().write(to: url, atomically: true, encoding: .utf8)
            }
        }
    }
}

struct TradeRow: View {
    let trade: TradeEntry
    
    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()
    
    var body: some View {
        HStack(spacing: 0) {
            Text(Self.timeFormatter.string(from: trade.timestamp))
                .frame(width: 100, alignment: .leading)
                .foregroundColor(.secondary)
            
            Text(trade.side)
                .fontWeight(.bold)
                .frame(width: 60, alignment: .center)
                .foregroundColor(trade.isBuyerMaker ? .red : .green)
            
            Text(String(format: "%.1f", trade.price))
                .frame(width: 120, alignment: .trailing)
                .foregroundColor(trade.isBuyerMaker ? .red : .green)
            
            Text(String(format: "%.4f", trade.qty))
                .frame(width: 100, alignment: .trailing)
                .foregroundColor(.secondary)
            
            Text(String(format: "%.2f", trade.price * trade.qty))
                .frame(maxWidth: .infinity, alignment: .trailing)
                .foregroundColor(.secondary.opacity(0.8))
        }
        .font(.system(.caption, design: .monospaced))
        .padding(.vertical, 2)
        .padding(.horizontal, 4)
        .background(trade.isBuyerMaker ? Color.red.opacity(0.03) : Color.green.opacity(0.03))
    }
}
