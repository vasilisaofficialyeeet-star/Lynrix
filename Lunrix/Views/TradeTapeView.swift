// TradeTapeView.swift — Glassmorphism 2026 Trade Tape (Lynrix v2.5)

import SwiftUI
import UniformTypeIdentifiers

struct TradeTapeView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    @State private var autoScroll: Bool = true
    
    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 12) {
                HStack(spacing: 6) {
                    Image(systemName: "chart.line.uptrend.xyaxis")
                        .foregroundColor(LxColor.neonLime)
                        .shadow(color: LxColor.neonLime.opacity(0.4), radius: 3)
                    Text(loc.t("tradeTape.title"))
                        .font(LxFont.mono(14, weight: .bold))
                        .foregroundColor(theme.textPrimary)
                }
                Spacer()
                Text("\(engine.trades.count) \(loc.t("tradeTape.trades"))")
                    .font(LxFont.micro)
                    .foregroundColor(theme.textTertiary)
                Toggle(loc.t("tradeTape.autoScroll"), isOn: $autoScroll)
                    .toggleStyle(.switch)
                    .controlSize(.small)
                    .foregroundColor(theme.textSecondary)
                Button(action: exportCSV) {
                    Label("CSV", systemImage: "square.and.arrow.up")
                        .font(LxFont.micro)
                        .foregroundColor(LxColor.electricCyan.opacity(0.7))
                }
                .buttonStyle(.borderless)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(theme.glassHighlight)
            
            Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
            
            HStack(spacing: 0) {
                Text(loc.t("tradeTape.time")).frame(width: 100, alignment: .leading)
                Text(loc.t("tradeTape.side")).frame(width: 60, alignment: .center)
                Text(loc.t("tradeTape.price")).frame(width: 120, alignment: .trailing)
                Text(loc.t("tradeTape.qty")).frame(width: 100, alignment: .trailing)
                Text(loc.t("tradeTape.notional")).frame(maxWidth: .infinity, alignment: .trailing)
            }
            .font(LxFont.mono(9, weight: .bold))
            .foregroundColor(theme.textTertiary)
            .padding(.horizontal, 16)
            .padding(.vertical, 6)
            .background(theme.glassHighlight.opacity(0.5))
            
            if engine.trades.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "chart.line.uptrend.xyaxis")
                        .font(.largeTitle)
                        .foregroundColor(theme.textTertiary.opacity(0.3))
                    Text(loc.t("tradeTape.noTrades"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                    Text(loc.t("tradeTape.waitingForTrades"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary.opacity(0.6))
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
            
            if !engine.trades.isEmpty {
                Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                HStack(spacing: 20) {
                    HStack(spacing: 4) {
                        StatusDot(LxColor.neonLime)
                        Text("\(loc.t("tradeTape.buy")) \(buyCount)")
                            .foregroundColor(LxColor.neonLime)
                    }
                    HStack(spacing: 4) {
                        StatusDot(LxColor.magentaPink)
                        Text("\(loc.t("tradeTape.sell")) \(sellCount)")
                            .foregroundColor(LxColor.magentaPink)
                    }
                    Spacer()
                    Text("\(loc.t("tradeTape.volume")) \(String(format: "%.4f", totalVolume))")
                        .foregroundColor(theme.textTertiary)
                    Text("\(loc.t("tradeTape.avgPrice")) \(String(format: "%.1f", avgPrice))")
                        .foregroundColor(theme.textTertiary)
                        .foregroundColor(theme.textTertiary)
                }
                .font(LxFont.mono(10))
                .padding(.horizontal, 16)
                .padding(.vertical, 6)
                .background(theme.glassHighlight)
            }
        }
        .background(theme.backgroundPrimary)
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
    @Environment(\.theme) var theme
    let trade: TradeEntry
    
    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()
    
    private var sideColor: Color {
        trade.isBuyerMaker ? LxColor.magentaPink : LxColor.neonLime
    }
    
    var body: some View {
        HStack(spacing: 0) {
            Text(Self.timeFormatter.string(from: trade.timestamp))
                .frame(width: 100, alignment: .leading)
                .foregroundColor(theme.textTertiary)
            Text(trade.side)
                .fontWeight(.bold)
                .frame(width: 60, alignment: .center)
                .foregroundColor(sideColor)
                .shadow(color: sideColor.opacity(0.3), radius: 2)
            Text(String(format: "%.1f", trade.price))
                .frame(width: 120, alignment: .trailing)
                .foregroundColor(sideColor)
            Text(String(format: "%.4f", trade.qty))
                .frame(width: 100, alignment: .trailing)
                .foregroundColor(theme.textSecondary)
            Text(String(format: "%.2f", trade.price * trade.qty))
                .frame(maxWidth: .infinity, alignment: .trailing)
                .foregroundColor(theme.textTertiary)
        }
        .font(LxFont.mono(10))
        .padding(.vertical, 2)
        .padding(.horizontal, 4)
        .background(sideColor.opacity(0.02))
    }
}
