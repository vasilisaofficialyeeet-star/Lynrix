// OrderBookView.swift — Glassmorphism 2026 Glass Heatmap order book (Lynrix v2.5)

import SwiftUI

struct OrderBookView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    @State private var displayLevels: Int = 20
    
    var body: some View {
        VStack(spacing: 0) {
            // Glass header
            HStack(spacing: 12) {
                GlassSectionHeader(loc.t("tab.orderbook"), icon: "list.number", color: LxColor.electricCyan)
                
                Spacer()
                
                if engine.orderBook.valid {
                    HStack(spacing: 14) {
                        NeonLabel(String(format: "%.1f", engine.orderBook.midPrice), icon: "arrow.left.arrow.right", color: LxColor.electricCyan)
                        NeonLabel(String(format: "%.2f", engine.orderBook.spread), icon: "arrow.up.arrow.down", color: LxColor.amber)
                    }
                } else {
                    StatusBadge(loc.t("orderbook.noData"), color: theme.textTertiary)
                }
                
                HStack(spacing: 4) {
                    ForEach([10, 20, 50], id: \.self) { n in
                        Button {
                            withAnimation(LxAnimation.snappy) { displayLevels = n }
                        } label: {
                            Text("\(n)")
                                .font(LxFont.mono(10, weight: displayLevels == n ? .bold : .regular))
                                .foregroundColor(displayLevels == n ? LxColor.electricCyan : theme.textTertiary)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 3)
                                .background(
                                    RoundedRectangle(cornerRadius: 5)
                                        .fill(displayLevels == n ? LxColor.electricCyan.opacity(0.1) : Color.clear)
                                )
                                .overlay(
                                    RoundedRectangle(cornerRadius: 5)
                                        .stroke(displayLevels == n ? LxColor.electricCyan.opacity(0.2) : Color.clear, lineWidth: 0.5)
                                )
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(Color(white: 0.11, opacity: 0.92))
            
            // Column headers
            HStack(spacing: 0) {
                Text(loc.t("orderbook.size"))
                    .frame(maxWidth: .infinity, alignment: .trailing)
                Text(loc.t("orderbook.bidSide"))
                    .frame(maxWidth: .infinity, alignment: .trailing)
                    .foregroundColor(LxColor.neonLime.opacity(0.5))
                Text(loc.t("dashboard.spread"))
                    .frame(maxWidth: .infinity, alignment: .center)
                Text(loc.t("orderbook.askSide"))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .foregroundColor(LxColor.magentaPink.opacity(0.5))
                Text(loc.t("orderbook.size"))
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .font(LxFont.mono(9, weight: .bold))
            .foregroundColor(theme.textTertiary)
            .padding(.horizontal, 16)
            .padding(.vertical, 5)
            .background(theme.backgroundPrimary.opacity(0.8))
            
            Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
            
            // Order book grid
            ScrollView {
                LazyVStack(spacing: 0) {
                    ForEach(Array(askLevels.reversed().enumerated()), id: \.offset) { idx, ask in
                        GlassOBRow(
                            bidQty: nil, bidPrice: nil,
                            askPrice: ask.price, askQty: ask.qty,
                            maxQty: maxQty, depthFraction: askDepthFraction(idx)
                        )
                    }
                    
                    GlassSpreadRow(spread: engine.orderBook.spread, midPrice: engine.orderBook.midPrice)
                    
                    ForEach(Array(bidLevels.enumerated()), id: \.offset) { idx, bid in
                        GlassOBRow(
                            bidQty: bid.qty, bidPrice: bid.price,
                            askPrice: nil, askQty: nil,
                            maxQty: maxQty, depthFraction: bidDepthFraction(idx)
                        )
                    }
                }
            }
        }
        .background(theme.backgroundPrimary)
    }
    
    private var bidLevels: [PriceLevelModel] { Array(engine.orderBook.bids.prefix(displayLevels)) }
    private var askLevels: [PriceLevelModel] { Array(engine.orderBook.asks.prefix(displayLevels)) }
    
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

// MARK: - Glass Order Book Row

struct GlassOBRow: View {
    let bidQty: Double?
    let bidPrice: Double?
    let askPrice: Double?
    let askQty: Double?
    let maxQty: Double
    let depthFraction: Double
    
    @State private var isHovered = false
    
    var body: some View {
        HStack(spacing: 0) {
            // Bid qty + heatmap
            ZStack(alignment: .trailing) {
                if let qty = bidQty {
                    GeometryReader { geo in
                        Rectangle()
                            .fill(
                                LinearGradient(
                                    colors: [LxColor.neonLime.opacity(0.02), LxColor.neonLime.opacity(0.05 + 0.15 * depthFraction)],
                                    startPoint: .leading,
                                    endPoint: .trailing
                                )
                            )
                            .frame(width: max(0, geo.size.width * CGFloat(min(qty / maxQty, 1.0))))
                            .frame(maxWidth: .infinity, alignment: .trailing)
                    }
                }
                if let qty = bidQty {
                    Text(String(format: "%.3f", qty))
                        .foregroundColor(LxColor.neonLime.opacity(0.7))
                        .padding(.trailing, 4)
                }
            }
            .frame(maxWidth: .infinity, alignment: .trailing)
            
            Group {
                if let price = bidPrice {
                    Text(String(format: "%.1f", price))
                        .foregroundColor(LxColor.neonLime)
                        .fontWeight(.medium)
                        .shadow(color: isHovered ? LxColor.neonLime.opacity(0.3) : .clear, radius: 3)
                } else {
                    Text("")
                }
            }
            .frame(maxWidth: .infinity, alignment: .trailing)
            
            Rectangle().fill(Color.clear).frame(maxWidth: .infinity)
            
            Group {
                if let price = askPrice {
                    Text(String(format: "%.1f", price))
                        .foregroundColor(LxColor.magentaPink)
                        .fontWeight(.medium)
                        .shadow(color: isHovered ? LxColor.magentaPink.opacity(0.3) : .clear, radius: 3)
                } else {
                    Text("")
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            
            // Ask qty + heatmap
            ZStack(alignment: .leading) {
                if let qty = askQty {
                    GeometryReader { geo in
                        Rectangle()
                            .fill(
                                LinearGradient(
                                    colors: [LxColor.magentaPink.opacity(0.05 + 0.15 * depthFraction), LxColor.magentaPink.opacity(0.02)],
                                    startPoint: .leading,
                                    endPoint: .trailing
                                )
                            )
                            .frame(width: max(0, geo.size.width * CGFloat(min(qty / maxQty, 1.0))))
                    }
                }
                if let qty = askQty {
                    Text(String(format: "%.3f", qty))
                        .foregroundColor(LxColor.magentaPink.opacity(0.7))
                        .padding(.leading, 4)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .font(LxFont.mono(11))
        .padding(.horizontal, 16)
        .padding(.vertical, 1.5)
        .background(isHovered ? Color.white.opacity(0.02) : Color.clear)
        .onHover { hovering in isHovered = hovering }
    }
}

// MARK: - Glass Spread Row

struct GlassSpreadRow: View {
    @Environment(\.theme) var theme
    let spread: Double
    let midPrice: Double
    
    var body: some View {
        HStack {
            Spacer()
            HStack(spacing: 8) {
                Image(systemName: "arrow.up.arrow.down")
                    .font(.system(size: 9, weight: .bold))
                GlowText(String(format: "%.2f", spread), font: LxFont.mono(11, weight: .bold), color: LxColor.amber, glow: 4)
                if midPrice > 0 {
                    Text(String(format: "%.2f bps", (spread / midPrice) * 10000))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
            }
            .foregroundColor(LxColor.amber)
            .padding(.vertical, 5)
            .padding(.horizontal, 14)
            .background(
                Capsule().fill(LxColor.amber.opacity(0.06))
            )
            .overlay(
                Capsule().stroke(LxColor.amber.opacity(0.15), lineWidth: 0.5)
            )
            .shadow(color: LxColor.amber.opacity(0.1), radius: 4)
            Spacer()
        }
        .padding(.vertical, 3)
    }
}
