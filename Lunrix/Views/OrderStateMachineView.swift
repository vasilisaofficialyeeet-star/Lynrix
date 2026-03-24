// OrderStateMachineView.swift — Glassmorphism 2026 FSM Neon Nodes (Lynrix v2.5)

import SwiftUI

struct OrderStateMachineView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                osmHeader
                
                // Overview metrics
                HStack(spacing: 12) {
                    NeonMetricCard(loc.t("osm.activeOrders"), value: "\(engine.osmSummary.activeOrders)", color: LxColor.electricCyan)
                    NeonMetricCard(loc.t("osm.transitions"), value: "\(engine.osmSummary.totalTransitions)", color: LxColor.magentaPink)
                    NeonMetricCard(loc.t("osm.fillTime"), value: String(format: "%.0f us", engine.osmSummary.avgFillTimeUs), color: LxColor.neonLime)
                    NeonMetricCard(loc.t("osm.avgSlippage"), value: String(format: "%.4f", engine.osmSummary.avgSlippage),
                                   color: engine.osmSummary.avgSlippage > 0.01 ? LxColor.bloodRed : LxColor.neonLime)
                    NeonMetricCard(loc.t("osm.marketImpact"), value: String(format: "%.2f bps", engine.osmSummary.marketImpactBps),
                                   color: engine.osmSummary.marketImpactBps > 1 ? LxColor.amber : LxColor.neonLime)
                }
                
                if engine.osmSummary.icebergActive { icebergCard }
                if engine.osmSummary.twapActive { twapCard }
                
                fsmDiagramCard
                
                if !engine.osmSummary.orders.isEmpty { ordersTableCard }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - Header
    
    private var osmHeader: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 8) {
                        Image(systemName: "arrow.triangle.swap")
                            .font(.system(size: 18, weight: .bold))
                            .foregroundColor(LxColor.electricCyan)
                            .shadow(color: LxColor.electricCyan.opacity(0.5), radius: 6)
                        Text(loc.t("osm.title"))
                            .font(LxFont.mono(16, weight: .bold))
                            .foregroundColor(theme.textPrimary)
                    }
                    Text(loc.t("osm.subtitle"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                }
                Spacer()
                HStack(spacing: 6) {
                    GlowText("\(engine.osmSummary.activeOrders)",
                             font: LxFont.bigMetric, color: LxColor.electricCyan, glow: 5)
                    Text(loc.t("common.active"))
                        .font(LxFont.mono(10))
                        .foregroundColor(theme.textTertiary)
                }
            }
        }
    }
    
    // MARK: - Iceberg
    
    private var icebergCard: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Image(systemName: "triangle.fill")
                        .foregroundColor(LxColor.electricCyan)
                        .rotationEffect(.degrees(180))
                        .shadow(color: LxColor.electricCyan.opacity(0.4), radius: 3)
                    GlassSectionHeader(loc.t("osm.icebergOrder"), icon: "", color: LxColor.electricCyan)
                    Spacer()
                    GlowText("\(engine.osmSummary.icebergSlicesDone)/\(engine.osmSummary.icebergSlicesTotal)",
                             font: LxFont.mono(11, weight: .bold), color: LxColor.electricCyan, glow: 3)
                    Text(loc.t("osm.slices"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
                
                NeonProgressBar(
                    value: engine.osmSummary.icebergSlicesTotal > 0
                        ? Double(engine.osmSummary.icebergSlicesDone) / Double(engine.osmSummary.icebergSlicesTotal) : 0,
                    color: LxColor.electricCyan, height: 5
                )
                
                HStack {
                    Text(String(format: "%@: %.4f / %.4f", loc.t("osm.filled"), engine.osmSummary.icebergFilledQty, engine.osmSummary.icebergTotalQty))
                        .font(LxFont.mono(10))
                        .foregroundColor(theme.textSecondary)
                    Spacer()
                    let pct = engine.osmSummary.icebergTotalQty > 0
                        ? engine.osmSummary.icebergFilledQty / engine.osmSummary.icebergTotalQty * 100 : 0
                    GlowText(String(format: "%.1f%%", pct),
                             font: LxFont.mono(11, weight: .bold), color: LxColor.electricCyan, glow: 3)
                }
            }
        }
    }
    
    // MARK: - TWAP
    
    private var twapCard: some View {
        GlassPanel(neon: LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("osm.twapSchedule"), icon: "clock.arrow.circlepath", color: LxColor.magentaPink)
                    Spacer()
                    GlowText("\(engine.osmSummary.twapSlicesDone)/\(engine.osmSummary.twapSlicesTotal)",
                             font: LxFont.mono(11, weight: .bold), color: LxColor.magentaPink, glow: 3)
                    Text(loc.t("osm.slices"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
                
                NeonProgressBar(
                    value: engine.osmSummary.twapSlicesTotal > 0
                        ? Double(engine.osmSummary.twapSlicesDone) / Double(engine.osmSummary.twapSlicesTotal) : 0,
                    color: LxColor.magentaPink, height: 5
                )
                
                HStack {
                    Text(loc.t("osm.marketImpactLabel"))
                        .font(LxFont.mono(10))
                        .foregroundColor(theme.textTertiary)
                    GlowText(String(format: "%.2f bps", engine.osmSummary.marketImpactBps),
                             font: LxFont.mono(10, weight: .bold),
                             color: engine.osmSummary.marketImpactBps > 1 ? LxColor.amber : LxColor.neonLime, glow: 2)
                    Spacer()
                    let pct = engine.osmSummary.twapSlicesTotal > 0
                        ? Double(engine.osmSummary.twapSlicesDone) / Double(engine.osmSummary.twapSlicesTotal) * 100 : 0
                    GlowText(String(format: "%.1f%%", pct),
                             font: LxFont.mono(11, weight: .bold), color: LxColor.magentaPink, glow: 3)
                }
            }
        }
    }
    
    // MARK: - FSM Diagram
    
    private var fsmDiagramCard: some View {
        let stateNames = [loc.t("osm.stNew"), loc.t("osm.stPending"), loc.t("osm.stOpen"), loc.t("osm.stPartialFill"), loc.t("osm.stFilled"), loc.t("osm.stCancelling"), loc.t("osm.stCancelled"), loc.t("osm.stExpired")]
        let stateIcons = ["plus.circle", "hourglass", "checkmark.circle", "circle.lefthalf.filled",
                          "checkmark.circle.fill", "xmark.circle", "minus.circle", "clock.badge.xmark"]
        let stateColors: [Color] = [LxColor.electricCyan, LxColor.gold, LxColor.neonLime, LxColor.electricCyan,
                                     LxColor.neonLime, LxColor.amber, LxColor.coolSteel, LxColor.coolSteel]
        let stateCounts = Dictionary(grouping: engine.osmSummary.orders, by: { $0.state })
        
        return GlassPanel(neon: LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 10) {
                GlassSectionHeader(loc.t("osm.fsmStatesLut"), icon: "arrow.triangle.branch", color: LxColor.magentaPink)
                
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 8), count: 4), spacing: 8) {
                    ForEach(0..<8, id: \.self) { i in
                        let count = stateCounts[i]?.count ?? 0
                        let active = count > 0
                        VStack(spacing: 4) {
                            Image(systemName: stateIcons[i])
                                .font(.system(size: 18, weight: .medium))
                                .foregroundColor(active ? stateColors[i] : theme.textTertiary.opacity(0.3))
                                .shadow(color: active ? stateColors[i].opacity(0.5) : .clear, radius: active ? 6 : 0)
                            Text(stateNames[i])
                                .font(LxFont.mono(9))
                                .foregroundColor(active ? theme.textSecondary : theme.textTertiary.opacity(0.4))
                            Text("\(count)")
                                .font(LxFont.mono(12, weight: .bold))
                                .foregroundColor(active ? stateColors[i] : theme.textTertiary.opacity(0.3))
                                .shadow(color: active ? stateColors[i].opacity(0.3) : .clear, radius: 2)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(8)
                        .background(
                            RoundedRectangle(cornerRadius: 8)
                                .fill(active ? stateColors[i].opacity(0.06) : theme.glassHighlight)
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(active ? stateColors[i].opacity(0.2) : theme.borderSubtle, lineWidth: 0.5)
                        )
                    }
                }
            }
        }
    }
    
    // MARK: - Orders Table
    
    private var ordersTableCard: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("osm.activeOrders"), icon: "list.bullet.rectangle", color: LxColor.electricCyan)
                    Spacer()
                    Text("\(engine.osmSummary.orders.count) \(loc.t("dashboard.orders"))")
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
                
                Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                
                // Header
                HStack {
                    Text(loc.t("osm.orderId")).frame(width: 80, alignment: .leading)
                    Text(loc.t("osm.side")).frame(width: 40)
                    Text(loc.t("osm.state")).frame(width: 70)
                    Text(loc.t("osm.price")).frame(width: 80, alignment: .trailing)
                    Text(loc.t("osm.qty")).frame(width: 60, alignment: .trailing)
                    Text(loc.t("osm.filled")).frame(width: 60, alignment: .trailing)
                    Text(loc.t("osm.fillPct")).frame(width: 55, alignment: .trailing)
                    Text(loc.t("osm.cx")).frame(width: 30, alignment: .trailing)
                }
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(theme.textTertiary)
                
                ForEach(engine.osmSummary.orders.prefix(20)) { order in
                    HStack {
                        Text(String(order.id.prefix(8)))
                            .frame(width: 80, alignment: .leading)
                            .foregroundColor(theme.textSecondary)
                        Text(order.isBuy ? loc.t("signals.buy") : loc.t("signals.sell"))
                            .foregroundColor(order.isBuy ? LxColor.neonLime : LxColor.magentaPink)
                            .frame(width: 40)
                        Text(loc.t(order.stateLocKey))
                            .foregroundColor(neonColorForState(order.state))
                            .frame(width: 70)
                        Text(String(format: "%.2f", order.price))
                            .frame(width: 80, alignment: .trailing)
                            .foregroundColor(theme.textPrimary)
                        Text(String(format: "%.4f", order.qty))
                            .frame(width: 60, alignment: .trailing)
                            .foregroundColor(theme.textSecondary)
                        Text(String(format: "%.4f", order.filledQty))
                            .frame(width: 60, alignment: .trailing)
                            .foregroundColor(theme.textSecondary)
                        Text(String(format: "%.0f%%", order.fillProbability * 100))
                            .foregroundColor(order.fillProbability > 0.5 ? LxColor.neonLime : LxColor.amber)
                            .frame(width: 55, alignment: .trailing)
                        Text("\(order.cancelAttempts)")
                            .foregroundColor(order.cancelAttempts > 0 ? LxColor.amber : theme.textTertiary)
                            .frame(width: 30, alignment: .trailing)
                    }
                    .font(LxFont.mono(10))
                }
            }
        }
    }
    
    // MARK: - Helpers
    
    private func neonColorForState(_ state: Int) -> Color {
        switch state {
        case 0: return LxColor.electricCyan
        case 1: return LxColor.gold
        case 2: return LxColor.neonLime
        case 3: return LxColor.electricCyan
        case 4: return LxColor.neonLime
        case 5: return LxColor.amber
        case 6: return LxColor.coolSteel
        case 7: return LxColor.coolSteel
        default: return theme.textTertiary
        }
    }
}
