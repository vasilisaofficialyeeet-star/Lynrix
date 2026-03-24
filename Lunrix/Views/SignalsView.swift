// SignalsView.swift — Glassmorphism 2026 Trade Signals (Lynrix v2.5)

import SwiftUI

struct SignalsView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    var body: some View {
        VStack(spacing: 0) {
            HStack {
                HStack(spacing: 8) {
                    Image(systemName: "bolt.horizontal.fill")
                        .foregroundColor(LxColor.electricCyan)
                        .shadow(color: LxColor.electricCyan.opacity(0.4), radius: 3)
                    Text(loc.t("signals.title"))
                        .font(LxFont.mono(14, weight: .bold))
                        .foregroundColor(theme.textPrimary)
                }
                Spacer()
                Text("\(engine.signals.count) \(loc.t("common.entries"))")
                    .font(LxFont.micro)
                    .foregroundColor(theme.textTertiary)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)
            .background(theme.glassHighlight)
            
            Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
            
            HStack(spacing: 0) {
                Text(loc.t("signals.time")).frame(width: 100, alignment: .leading)
                Text(loc.t("signals.side")).frame(width: 60, alignment: .center)
                Text(loc.t("signals.price")).frame(width: 100, alignment: .trailing)
                Text(loc.t("signals.qty")).frame(width: 80, alignment: .trailing)
                Text(loc.t("signals.confidence")).frame(maxWidth: .infinity, alignment: .trailing)
            }
            .font(LxFont.mono(9, weight: .bold))
            .foregroundColor(theme.textTertiary)
            .padding(.horizontal, 16)
            .padding(.vertical, 6)
            .background(theme.glassHighlight.opacity(0.5))
            
            if engine.signals.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "bolt.horizontal")
                        .font(.largeTitle)
                        .foregroundColor(theme.textTertiary.opacity(0.3))
                    Text(loc.t("signals.noSignals"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(engine.signals) { signal in
                    SignalRow(signal: signal)
                        .listRowInsets(EdgeInsets(top: 2, leading: 16, bottom: 2, trailing: 16))
                        .listRowSeparator(.hidden)
                        .listRowBackground(Color.clear)
                }
                .listStyle(.plain)
                .scrollContentBackground(.hidden)
            }
        }
        .background(theme.backgroundPrimary)
    }
}

struct SignalRow: View {
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
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
                .foregroundColor(theme.textTertiary)
            Text(signal.isBuy ? loc.t("signals.buy") : loc.t("signals.sell"))
                .fontWeight(.bold)
                .frame(width: 60, alignment: .center)
                .foregroundColor(signal.isBuy ? LxColor.neonLime : LxColor.magentaPink)
                .shadow(color: (signal.isBuy ? LxColor.neonLime : LxColor.magentaPink).opacity(0.3), radius: 2)
            Text(String(format: "%.1f", signal.price))
                .frame(width: 100, alignment: .trailing)
                .foregroundColor(theme.textPrimary)
            Text(String(format: "%.4f", signal.qty))
                .frame(width: 80, alignment: .trailing)
                .foregroundColor(theme.textSecondary)
            HStack(spacing: 6) {
                Spacer()
                Text(String(format: "%.1f%%", signal.confidence * 100))
                    .foregroundColor(confidenceColor)
                    .shadow(color: confidenceColor.opacity(0.3), radius: 2)
                GeometryReader { geo in
                    ZStack(alignment: .leading) {
                        RoundedRectangle(cornerRadius: 2)
                            .fill(confidenceColor.opacity(0.08))
                        RoundedRectangle(cornerRadius: 2)
                            .fill(
                                LinearGradient(colors: [confidenceColor.opacity(0.5), confidenceColor.opacity(0.2)],
                                               startPoint: .leading, endPoint: .trailing)
                            )
                            .frame(width: geo.size.width * CGFloat(signal.confidence))
                            .shadow(color: confidenceColor.opacity(0.2), radius: 2)
                    }
                }
                .frame(width: 80, height: 8)
            }
            .frame(maxWidth: .infinity, alignment: .trailing)
        }
        .font(LxFont.mono(10))
        .padding(.vertical, 3)
    }
    
    private var confidenceColor: Color {
        if signal.confidence > 0.8 { return LxColor.neonLime }
        if signal.confidence > 0.6 { return LxColor.gold }
        return LxColor.amber
    }
}
