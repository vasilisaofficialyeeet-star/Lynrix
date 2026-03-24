// OrderBookView.swift — Full order book display with color-coded depth

import SwiftUI

struct OrderBookView: View {
    @EnvironmentObject var engine: TradingEngine
    @State private var displayLevels: Int = 20
    
    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text("СТАКАН")
                    .font(.system(.headline, design: .monospaced))
                    .foregroundColor(.primary)
                
                Spacer()
                
                if engine.orderBook.valid {
                    HStack(spacing: 12) {
                        Label("Mid: \(String(format: "%.1f", engine.orderBook.midPrice))",
                              systemImage: "arrow.left.arrow.right")
                            .font(.system(.caption, design: .monospaced))
                        Label("Spread: \(String(format: "%.2f", engine.orderBook.spread))",
                              systemImage: "arrow.up.arrow.down")
                            .font(.system(.caption, design: .monospaced))
                            .foregroundColor(.yellow)
                    }
                } else {
                    Text("НЕТ ДАННЫХ")
                        .font(.system(.caption, design: .monospaced))
                        .foregroundColor(.secondary)
                }
                
                Picker("Уровни", selection: $displayLevels) {
                    Text("10").tag(10)
                    Text("20").tag(20)
                    Text("50").tag(50)
                }
                .pickerStyle(.segmented)
                .frame(width: 150)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(Color(.controlBackgroundColor).opacity(0.3))
            
            Divider()
            
            // Column headers
            HStack(spacing: 0) {
                Text("ОБЪЁМ")
                    .frame(maxWidth: .infinity, alignment: .trailing)
                Text("БИД")
                    .frame(maxWidth: .infinity, alignment: .trailing)
                Text("ЦЕНА")
                    .frame(maxWidth: .infinity, alignment: .center)
                Text("АСК")
                    .frame(maxWidth: .infinity, alignment: .leading)
                Text("ОБЪЁМ")
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .font(.system(.caption2, design: .monospaced))
            .foregroundColor(.secondary)
            .padding(.horizontal, 16)
            .padding(.vertical, 6)
            .background(Color(.controlBackgroundColor).opacity(0.2))
            
            // Order book grid
            ScrollView {
                LazyVStack(spacing: 0) {
                    // Asks (reversed — best ask at bottom)
                    ForEach(Array(askLevels.reversed().enumerated()), id: \.offset) { idx, ask in
                        OrderBookRow(
                            bidQty: nil, bidPrice: nil,
                            askPrice: ask.price, askQty: ask.qty,
                            maxQty: maxQty, depthFraction: askDepthFraction(idx)
                        )
                    }
                    
                    // Spread row
                    SpreadRow(spread: engine.orderBook.spread, midPrice: engine.orderBook.midPrice)
                    
                    // Bids (best bid at top)
                    ForEach(Array(bidLevels.enumerated()), id: \.offset) { idx, bid in
                        OrderBookRow(
                            bidQty: bid.qty, bidPrice: bid.price,
                            askPrice: nil, askQty: nil,
                            maxQty: maxQty, depthFraction: bidDepthFraction(idx)
                        )
                    }
                }
            }
        }
        .background(Color(.windowBackgroundColor))
    }
    
    private var bidLevels: [PriceLevelModel] {
        Array(engine.orderBook.bids.prefix(displayLevels))
    }
    
    private var askLevels: [PriceLevelModel] {
        Array(engine.orderBook.asks.prefix(displayLevels))
    }
    
    private var maxQty: Double {
        let bm = bidLevels.map(\.qty).max() ?? 1
        let am = askLevels.map(\.qty).max() ?? 1
        return max(bm, am, 0.001)
    }
    
    private func bidDepthFraction(_ index: Int) -> Double {
        guard !bidLevels.isEmpty else { return 0 }
        let cumQty = bidLevels.prefix(index + 1).reduce(0) { $0 + $1.qty }
        let totalQty = bidLevels.reduce(0) { $0 + $1.qty }
        return totalQty > 0 ? cumQty / totalQty : 0
    }
    
    private func askDepthFraction(_ index: Int) -> Double {
        guard !askLevels.isEmpty else { return 0 }
        let reversed = Array(askLevels.reversed())
        let cumQty = reversed.prefix(index + 1).reduce(0) { $0 + $1.qty }
        let totalQty = reversed.reduce(0) { $0 + $1.qty }
        return totalQty > 0 ? cumQty / totalQty : 0
    }
}

// MARK: - Order Book Row

struct OrderBookRow: View {
    let bidQty: Double?
    let bidPrice: Double?
    let askPrice: Double?
    let askQty: Double?
    let maxQty: Double
    let depthFraction: Double
    
    var body: some View {
        HStack(spacing: 0) {
            // Bid side
            ZStack(alignment: .trailing) {
                if let qty = bidQty {
                    Rectangle()
                        .fill(Color.green.opacity(0.08 + 0.12 * depthFraction))
                        .frame(width: barWidth(qty), alignment: .trailing)
                        .frame(maxWidth: .infinity, alignment: .trailing)
                }
                
                if let qty = bidQty {
                    Text(String(format: "%.3f", qty))
                        .foregroundColor(.green.opacity(0.8))
                }
            }
            .frame(maxWidth: .infinity, alignment: .trailing)
            
            Group {
                if let price = bidPrice {
                    Text(String(format: "%.1f", price))
                        .foregroundColor(.green)
                        .fontWeight(.medium)
                } else {
                    Text("")
                }
            }
            .frame(maxWidth: .infinity, alignment: .trailing)
            
            // Center price column
            Rectangle()
                .fill(Color.clear)
                .frame(maxWidth: .infinity)
            
            // Ask side
            Group {
                if let price = askPrice {
                    Text(String(format: "%.1f", price))
                        .foregroundColor(.red)
                        .fontWeight(.medium)
                } else {
                    Text("")
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            
            ZStack(alignment: .leading) {
                if let qty = askQty {
                    Rectangle()
                        .fill(Color.red.opacity(0.08 + 0.12 * depthFraction))
                        .frame(width: barWidth(qty), alignment: .leading)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                
                if let qty = askQty {
                    Text(String(format: "%.3f", qty))
                        .foregroundColor(.red.opacity(0.8))
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .font(.system(.caption, design: .monospaced))
        .padding(.horizontal, 16)
        .padding(.vertical, 2)
    }
    
    private func barWidth(_ qty: Double) -> CGFloat {
        return CGFloat(min(qty / maxQty, 1.0)) * 120
    }
}

// MARK: - Spread Row

struct SpreadRow: View {
    let spread: Double
    let midPrice: Double
    
    var body: some View {
        HStack {
            Spacer()
            HStack(spacing: 8) {
                Image(systemName: "arrow.up.arrow.down")
                    .font(.caption2)
                Text(String(format: "%.2f", spread))
                    .fontWeight(.semibold)
                if midPrice > 0 {
                    Text(String(format: "(%.2f bps)", (spread / midPrice) * 10000))
                        .foregroundColor(.secondary)
                }
            }
            .font(.system(.caption, design: .monospaced))
            .foregroundColor(.yellow)
            .padding(.vertical, 4)
            .padding(.horizontal, 12)
            .background(Color.yellow.opacity(0.05))
            .clipShape(RoundedRectangle(cornerRadius: 4))
            Spacer()
        }
        .padding(.vertical, 2)
    }
}
