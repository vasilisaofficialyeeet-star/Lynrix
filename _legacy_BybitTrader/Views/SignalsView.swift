// SignalsView.swift — Trade signals and recent signal history

import SwiftUI

struct SignalsView: View {
    @EnvironmentObject var engine: TradingEngine
    
    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text("СИГНАЛЫ")
                    .font(.system(.headline, design: .monospaced))
                Spacer()
                Text("\(engine.signals.count) записей")
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(.secondary)
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
                    .frame(width: 100, alignment: .trailing)
                Text("ОБЪЁМ")
                    .frame(width: 80, alignment: .trailing)
                Text("УВЕРЕННОСТЬ")
                    .frame(maxWidth: .infinity, alignment: .trailing)
            }
            .font(.system(.caption2, design: .monospaced))
            .foregroundColor(.secondary)
            .padding(.horizontal, 16)
            .padding(.vertical, 6)
            .background(Color(.controlBackgroundColor).opacity(0.2))
            
            if engine.signals.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "bolt.horizontal")
                        .font(.largeTitle)
                        .foregroundColor(.secondary.opacity(0.3))
                    Text("Сигналов пока нет")
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(engine.signals) { signal in
                    SignalRow(signal: signal)
                        .listRowInsets(EdgeInsets(top: 2, leading: 16, bottom: 2, trailing: 16))
                        .listRowSeparator(.hidden)
                }
                .listStyle(.plain)
            }
        }
        .background(Color(.windowBackgroundColor))
    }
}

struct SignalRow: View {
    let signal: SignalEntry
    
    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()
    
    var body: some View {
        HStack(spacing: 0) {
            Text(Self.timeFormatter.string(from: signal.timestamp))
                .frame(width: 100, alignment: .leading)
                .foregroundColor(.secondary)
            
            Text(signal.isBuy ? "ПОКУПКА" : "ПРОДАЖА")
                .fontWeight(.bold)
                .frame(width: 60, alignment: .center)
                .foregroundColor(signal.isBuy ? .green : .red)
            
            Text(String(format: "%.1f", signal.price))
                .frame(width: 100, alignment: .trailing)
            
            Text(String(format: "%.4f", signal.qty))
                .frame(width: 80, alignment: .trailing)
                .foregroundColor(.secondary)
            
            // Confidence bar
            HStack(spacing: 6) {
                Spacer()
                Text(String(format: "%.1f%%", signal.confidence * 100))
                    .foregroundColor(confidenceColor)
                GeometryReader { geo in
                    ZStack(alignment: .leading) {
                        RoundedRectangle(cornerRadius: 2)
                            .fill(Color(.controlBackgroundColor))
                        RoundedRectangle(cornerRadius: 2)
                            .fill(confidenceColor.opacity(0.6))
                            .frame(width: geo.size.width * CGFloat(signal.confidence))
                    }
                }
                .frame(width: 80, height: 8)
            }
            .frame(maxWidth: .infinity, alignment: .trailing)
        }
        .font(.system(.caption, design: .monospaced))
        .padding(.vertical, 3)
    }
    
    private var confidenceColor: Color {
        if signal.confidence > 0.8 { return .green }
        if signal.confidence > 0.6 { return .yellow }
        return .orange
    }
}
