// TradeReviewStudioView.swift — Trade Review Studio (Lynrix v2.5 Sprint 4)
// Post-trade analysis, journaling, filtering, statistics, and replay linkage.

import SwiftUI

struct TradeReviewStudioView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @ObservedObject var journal = TradeJournalStore.shared
    @Environment(\.theme) var theme

    @State private var selectedTradeId: UUID? = nil
    @State private var filter = TradeJournalFilter()
    @State private var sortOrder: TradeJournalSort = .newest
    @State private var showFilters = false
    @State private var editingNotes: String = ""

    private var filteredTrades: [CompletedTrade] {
        let filtered = filter.isActive ? journal.trades.filter { filter.matches($0) } : journal.trades
        return sortOrder.apply(filtered)
    }

    private var selectedTrade: CompletedTrade? {
        guard let id = selectedTradeId else { return nil }
        return journal.trades.first { $0.id == id }
    }

    var body: some View {
        HSplitView {
            // Left: Trade list + stats
            VStack(spacing: 0) {
                statsHeader
                filterBar
                tradeList
            }
            .frame(minWidth: 380, idealWidth: 440)

            // Right: Trade detail
            if let trade = selectedTrade {
                tradeDetailPanel(trade)
                    .frame(minWidth: 400)
            } else {
                emptyDetail
                    .frame(minWidth: 400)
            }
        }
        .background(theme.backgroundPrimary)
    }

    // MARK: - Stats Header

    private var statsHeader: some View {
        let stats = journal.computeStats(for: filter.isActive ? filteredTrades : nil)
        return GlassPanel(neon: stats.totalPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink, padding: 10) {
            VStack(spacing: 8) {
                HStack {
                    GlassSectionHeader(loc.t("trade.studio"), icon: "doc.text.magnifyingglass",
                                       color: LxColor.gold)
                    Spacer()
                    Text("\(stats.totalTrades) \(loc.t("trade.trades"))")
                        .font(LxFont.mono(10))
                        .foregroundColor(theme.textTertiary)
                }
                HStack(spacing: 12) {
                    miniStat(loc.t("trade.totalPnl"), value: String(format: "%+.4f", stats.totalPnl),
                             color: stats.totalPnl >= 0 ? LxColor.neonLime : LxColor.magentaPink)
                    miniStat(loc.t("trade.winRate"), value: String(format: "%.1f%%", stats.winRate * 100),
                             color: stats.winRate > 0.5 ? LxColor.neonLime : LxColor.magentaPink)
                    miniStat(loc.t("trade.profitFactor"), value: String(format: "%.2f", stats.profitFactor),
                             color: stats.profitFactor > 1.0 ? LxColor.neonLime : LxColor.magentaPink)
                    miniStat(loc.t("trade.avgExecScore"), value: String(format: "%.0f", stats.avgExecutionScore),
                             color: stats.avgExecutionScore > 70 ? LxColor.neonLime : LxColor.amber)
                    miniStat(loc.t("trade.avgSlippage"), value: String(format: "%.2f bps", stats.avgSlippage),
                             color: stats.avgSlippage < 3.0 ? LxColor.neonLime : LxColor.amber)
                }
            }
        }
        .padding(.horizontal, 10)
        .padding(.top, 10)
    }

    private func miniStat(_ label: String, value: String, color: Color) -> some View {
        VStack(spacing: 2) {
            Text(label)
                .font(LxFont.mono(8, weight: .bold))
                .foregroundColor(theme.textTertiary)
                .lineLimit(1)
            Text(value)
                .font(LxFont.mono(11, weight: .bold))
                .foregroundColor(color)
        }
        .frame(maxWidth: .infinity)
    }

    // MARK: - Filter Bar

    private var filterBar: some View {
        HStack(spacing: 6) {
            // Sort
            Menu {
                ForEach(TradeJournalSort.allCases, id: \.self) { sort in
                    Button(loc.t(sort.locKey)) { sortOrder = sort }
                }
            } label: {
                HStack(spacing: 3) {
                    Image(systemName: "arrow.up.arrow.down")
                        .font(.system(size: 9))
                    Text(loc.t(sortOrder.locKey))
                        .font(LxFont.mono(9))
                }
                .foregroundColor(theme.textSecondary)
                .padding(.horizontal, 6)
                .padding(.vertical, 3)
                .background(RoundedRectangle(cornerRadius: 5).fill(theme.panelBackground))
            }
            .menuStyle(.borderlessButton)

            // Filter toggles
            filterPill(loc.t("common.long"), active: filter.side == .long) {
                filter.side = filter.side == .long ? nil : .long
            }
            filterPill(loc.t("common.short"), active: filter.side == .short) {
                filter.side = filter.side == .short ? nil : .short
            }
            filterPill("✓", active: filter.outcome == .win) {
                filter.outcome = filter.outcome == .win ? nil : .win
            }
            filterPill("✗", active: filter.outcome == .loss) {
                filter.outcome = filter.outcome == .loss ? nil : .loss
            }

            Spacer()

            if filter.isActive {
                Button(action: { filter = TradeJournalFilter() }) {
                    Image(systemName: "xmark.circle.fill")
                        .font(.system(size: 10))
                        .foregroundColor(theme.textTertiary)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 6)
    }

    private func filterPill(_ label: String, active: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(label)
                .font(LxFont.mono(9, weight: active ? .bold : .regular))
                .foregroundColor(active ? LxColor.electricCyan : theme.textTertiary)
                .padding(.horizontal, 6)
                .padding(.vertical, 2)
                .background(
                    RoundedRectangle(cornerRadius: 4)
                        .fill(active ? LxColor.electricCyan.opacity(0.1) : Color.clear)
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 4)
                        .stroke(active ? LxColor.electricCyan.opacity(0.3) : theme.borderSubtle, lineWidth: 0.5)
                )
        }
        .buttonStyle(.plain)
    }

    // MARK: - Trade List

    private var tradeList: some View {
        ScrollView {
            LazyVStack(spacing: 2) {
                if filteredTrades.isEmpty {
                    emptyTradeList
                } else {
                    ForEach(filteredTrades) { trade in
                        tradeRow(trade)
                    }
                }
            }
            .padding(.horizontal, 10)
            .padding(.bottom, 10)
        }
    }

    private var emptyTradeList: some View {
        VStack(spacing: 12) {
            Image(systemName: "doc.text.magnifyingglass")
                .font(.system(size: 28))
                .foregroundColor(theme.textTertiary.opacity(0.5))
            Text(loc.t("trade.empty"))
                .font(LxFont.mono(11))
                .foregroundColor(theme.textTertiary)
            if filter.isActive {
                Text(loc.t("trade.noFilterMatch"))
                    .font(LxFont.micro)
                    .foregroundColor(theme.textTertiary.opacity(0.6))
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.top, 60)
    }

    private func tradeRow(_ trade: CompletedTrade) -> some View {
        let isSelected = selectedTradeId == trade.id
        return Button(action: {
            withAnimation(LxAnimation.micro) {
                selectedTradeId = trade.id
                editingNotes = trade.userNotes
            }
        }) {
            HStack(spacing: 8) {
                // Trade number + side
                VStack(spacing: 2) {
                    Text("#\(trade.tradeNumber)")
                        .font(LxFont.mono(10, weight: .bold))
                        .foregroundColor(theme.textPrimary)
                    Image(systemName: trade.side.icon)
                        .font(.system(size: 9))
                        .foregroundColor(trade.side.color)
                }
                .frame(width: 36)

                // Outcome dot
                Image(systemName: trade.outcome.icon)
                    .font(.system(size: 10))
                    .foregroundColor(trade.outcome.color)

                // PnL
                VStack(alignment: .leading, spacing: 1) {
                    Text(String(format: "%+.6f", trade.realizedPnl))
                        .font(LxFont.mono(11, weight: .bold))
                        .foregroundColor(trade.outcome.color)
                    Text(trade.durationFormatted)
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }

                Spacer()

                // Rating stars
                HStack(spacing: 1) {
                    ForEach(1...5, id: \.self) { star in
                        Image(systemName: star <= trade.effectiveRating.rawValue ? "star.fill" : "star")
                            .font(.system(size: 7))
                            .foregroundColor(star <= trade.effectiveRating.rawValue ?
                                            trade.effectiveRating.color : theme.textTertiary.opacity(0.3))
                    }
                }

                // Exec score
                Text(String(format: "%.0f", trade.executionScore))
                    .font(LxFont.mono(9, weight: .bold))
                    .foregroundColor(trade.executionScore > 70 ? LxColor.neonLime :
                                    (trade.executionScore > 50 ? LxColor.amber : LxColor.bloodRed))
                    .frame(width: 24)

                // Notes indicator
                if !trade.userNotes.isEmpty {
                    Image(systemName: "note.text")
                        .font(.system(size: 8))
                        .foregroundColor(LxColor.gold)
                }
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(
                RoundedRectangle(cornerRadius: 6)
                    .fill(isSelected ? trade.side.color.opacity(theme.accentTintOpacity) : Color.clear)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(isSelected ? trade.side.color.opacity(0.3) : Color.clear, lineWidth: 0.5)
            )
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }

    // MARK: - Trade Detail Panel

    private func tradeDetailPanel(_ trade: CompletedTrade) -> some View {
        ScrollView {
            VStack(spacing: 12) {
                // Header
                tradeDetailHeader(trade)

                // PnL + Execution
                HStack(spacing: 12) {
                    pnlDetailCard(trade)
                    executionDetailCard(trade)
                }

                // Entry / Exit
                HStack(spacing: 12) {
                    legCard(loc.t("trade.entry"), leg: trade.entry, color: trade.side.color)
                    legCard(loc.t("trade.exit"), leg: trade.exit, color: trade.outcome.color)
                }

                // Risk Context
                riskContextCard(trade)

                // Rating + Notes
                journalCard(trade)

                // Incidents
                if !trade.incidentIds.isEmpty {
                    incidentsCard(trade)
                }
            }
            .padding(12)
        }
        .background(theme.backgroundPrimary)
    }

    private func tradeDetailHeader(_ trade: CompletedTrade) -> some View {
        GlassPanel(neon: trade.outcome.color, padding: 10) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 8) {
                        Text("#\(trade.tradeNumber)")
                            .font(LxFont.mono(16, weight: .bold))
                            .foregroundColor(theme.textPrimary)
                        Image(systemName: trade.side.icon)
                            .foregroundColor(trade.side.color)
                        Text(loc.t(trade.side.locKey).uppercased())
                            .font(LxFont.mono(11, weight: .bold))
                            .foregroundColor(trade.side.color)
                        Text(trade.symbol)
                            .font(LxFont.mono(11))
                            .foregroundColor(theme.textSecondary)
                    }
                    HStack(spacing: 6) {
                        Image(systemName: trade.outcome.icon)
                            .foregroundColor(trade.outcome.color)
                        Text(loc.t(trade.outcome.locKey))
                            .font(LxFont.mono(10, weight: .bold))
                            .foregroundColor(trade.outcome.color)
                        Text("•")
                            .foregroundColor(theme.textTertiary)
                        Text(trade.durationFormatted)
                            .font(LxFont.mono(10))
                            .foregroundColor(theme.textSecondary)
                    }
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 4) {
                    Text(String(format: "%+.6f", trade.realizedPnl))
                        .font(LxFont.mono(18, weight: .bold))
                        .foregroundColor(trade.outcome.color)
                        .shadow(color: trade.outcome.color.opacity(0.3), radius: 4)
                    Text(String(format: "%+.2f%%", trade.pnlPercent))
                        .font(LxFont.mono(10))
                        .foregroundColor(trade.outcome.color.opacity(0.7))
                }
            }
        }
    }

    private func pnlDetailCard(_ trade: CompletedTrade) -> some View {
        GlassPanel(neon: trade.outcome.color) {
            VStack(alignment: .leading, spacing: 6) {
                GlassSectionHeader(loc.t("trade.pnlAnalysis"), icon: "chart.bar.fill", color: trade.outcome.color)
                GlassMetric(loc.t("trade.realizedPnl"), value: String(format: "%+.6f", trade.realizedPnl), color: trade.outcome.color)
                GlassMetric(loc.t("trade.pnlPercent"), value: String(format: "%+.2f%%", trade.pnlPercent), color: trade.outcome.color)
                GlassMetric(loc.t("trade.holdTime"), value: trade.durationFormatted, color: theme.textPrimary)
                GlassMetric(loc.t("trade.signalConf"), value: String(format: "%.1f%%", trade.signalConfidence * 100),
                            color: trade.signalConfidence > 0.7 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("trade.predCorrect"), value: trade.predictionCorrect ? loc.t("common.yes") : loc.t("common.no"),
                            color: trade.predictionCorrect ? LxColor.neonLime : LxColor.magentaPink)
            }
        }
    }

    private func executionDetailCard(_ trade: CompletedTrade) -> some View {
        let scoreColor = trade.executionScore > 70 ? LxColor.neonLime :
                        (trade.executionScore > 50 ? LxColor.amber : LxColor.bloodRed)
        return GlassPanel(neon: scoreColor) {
            VStack(alignment: .leading, spacing: 6) {
                GlassSectionHeader(loc.t("trade.execQuality"), icon: "gauge.open.with.lines.needle.33percent", color: scoreColor)
                GlassMetric(loc.t("trade.execScore"), value: String(format: "%.0f/100", trade.executionScore), color: scoreColor)
                GlassMetric(loc.t("trade.entrySlippage"), value: String(format: "%.2f bps", trade.entrySlippageBps),
                            color: trade.entrySlippageBps < 3 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("trade.exitSlippage"), value: String(format: "%.2f bps", trade.exitSlippageBps),
                            color: trade.exitSlippageBps < 3 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("trade.totalSlippage"), value: String(format: "%.2f bps", trade.totalSlippageBps),
                            color: trade.totalSlippageBps < 5 ? LxColor.neonLime : LxColor.amber)
            }
        }
    }

    private func legCard(_ title: String, leg: TradeExecutionSnapshot, color: Color) -> some View {
        GlassPanel(neon: color) {
            VStack(alignment: .leading, spacing: 6) {
                GlassSectionHeader(title, icon: "arrow.right.circle", color: color)
                GlassMetric(loc.t("trade.price"), value: String(format: "%.6f", leg.fillPrice), color: theme.textPrimary)
                GlassMetric(loc.t("trade.qty"), value: String(format: "%.6f", leg.qty), color: theme.textPrimary)
                GlassMetric(loc.t("trade.slippage"), value: String(format: "%.2f bps", leg.slippageBps),
                            color: leg.slippageBps < 3 ? LxColor.neonLime : LxColor.amber)
                GlassMetric(loc.t("trade.fillTime"), value: String(format: "%.0f µs", leg.fillTimeUs), color: theme.textPrimary)
                GlassMetric(loc.t("trade.time"), value: formatTime(leg.timestamp), color: theme.textTertiary)
            }
        }
    }

    private func riskContextCard(_ trade: CompletedTrade) -> some View {
        GlassPanel(neon: LxColor.amber) {
            VStack(alignment: .leading, spacing: 6) {
                GlassSectionHeader(loc.t("trade.riskContext"), icon: "shield.lefthalf.filled", color: LxColor.amber)
                HStack(spacing: 20) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(loc.t("trade.atEntry")).font(LxFont.mono(9, weight: .bold)).foregroundColor(theme.textTertiary)
                        GlassMetric(loc.t("trade.regime"), value: regimeName(trade.riskAtEntry.regime),
                                    color: LxColor.regime(trade.riskAtEntry.regime))
                        GlassMetric(loc.t("trade.volatility"), value: String(format: "%.4f", trade.riskAtEntry.volatility), color: theme.textPrimary)
                        GlassMetric(loc.t("trade.drawdown"), value: String(format: "%.2f%%", trade.riskAtEntry.drawdownPct * 100),
                                    color: trade.riskAtEntry.drawdownPct > 0.03 ? LxColor.bloodRed : theme.textPrimary)
                        GlassMetric(loc.t("trade.var99"), value: String(format: "$%.2f", abs(trade.riskAtEntry.var99AtEntry)), color: LxColor.magentaPink)
                    }
                    VStack(alignment: .leading, spacing: 4) {
                        Text(loc.t("trade.atExit")).font(LxFont.mono(9, weight: .bold)).foregroundColor(theme.textTertiary)
                        GlassMetric(loc.t("trade.regime"), value: regimeName(trade.riskAtExit.regime),
                                    color: LxColor.regime(trade.riskAtExit.regime))
                        GlassMetric(loc.t("trade.volatility"), value: String(format: "%.4f", trade.riskAtExit.volatility), color: theme.textPrimary)
                        GlassMetric(loc.t("trade.drawdown"), value: String(format: "%.2f%%", trade.riskAtExit.drawdownPct * 100),
                                    color: trade.riskAtExit.drawdownPct > 0.03 ? LxColor.bloodRed : theme.textPrimary)
                        GlassMetric(loc.t("trade.var99"), value: String(format: "$%.2f", abs(trade.riskAtExit.var99AtEntry)), color: LxColor.magentaPink)
                    }
                }
            }
        }
    }

    private func journalCard(_ trade: CompletedTrade) -> some View {
        GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("trade.journal"), icon: "pencil.and.outline", color: LxColor.gold)

                // Star rating
                HStack(spacing: 8) {
                    Text(loc.t("trade.rating"))
                        .font(LxFont.mono(10))
                        .foregroundColor(theme.textTertiary)
                    HStack(spacing: 2) {
                        ForEach(TradeRating.allCases.reversed(), id: \.self) { rating in
                            Button(action: {
                                if trade.userRating == rating {
                                    journal.clearUserRating(trade.id)
                                } else {
                                    journal.setUserRating(trade.id, rating: rating)
                                }
                            }) {
                                Image(systemName: rating.rawValue <= trade.effectiveRating.rawValue ? "star.fill" : "star")
                                    .font(.system(size: 12))
                                    .foregroundColor(rating.rawValue <= trade.effectiveRating.rawValue ?
                                                    trade.effectiveRating.color : theme.textTertiary.opacity(0.3))
                            }
                            .buttonStyle(.plain)
                        }
                    }
                    Spacer()
                    Text(loc.t(trade.effectiveRating.locKey))
                        .font(LxFont.mono(9, weight: .bold))
                        .foregroundColor(trade.effectiveRating.color)
                    if trade.userRating != nil {
                        Text("(\(loc.t("trade.userOverride")))")
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                    }
                }

                // Notes
                VStack(alignment: .leading, spacing: 4) {
                    Text(loc.t("trade.notes"))
                        .font(LxFont.mono(9, weight: .bold))
                        .foregroundColor(theme.textTertiary)
                    TextEditor(text: $editingNotes)
                        .font(LxFont.mono(11))
                        .frame(minHeight: 60, maxHeight: 120)
                        .scrollContentBackground(.hidden)
                        .background(
                            RoundedRectangle(cornerRadius: 6)
                                .fill(theme.panelBackground)
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 6)
                                .stroke(theme.borderSubtle, lineWidth: 0.5)
                        )
                        .onChange(of: editingNotes) { newValue in
                            journal.setUserNotes(trade.id, notes: newValue)
                        }
                }
            }
        }
    }

    private func incidentsCard(_ trade: CompletedTrade) -> some View {
        let incidents = IncidentStore.shared.incidents.filter { trade.incidentIds.contains($0.id) }
        return GlassPanel(neon: LxColor.bloodRed) {
            VStack(alignment: .leading, spacing: 6) {
                GlassSectionHeader(loc.t("trade.incidents"), icon: "exclamationmark.triangle.fill", color: LxColor.bloodRed)
                ForEach(incidents) { incident in
                    HStack(spacing: 6) {
                        Image(systemName: incident.severity.icon)
                            .font(.system(size: 9))
                            .foregroundColor(incident.severity.color)
                        Text(loc.t(incident.titleKey))
                            .font(LxFont.mono(10))
                            .foregroundColor(theme.textPrimary)
                        Spacer()
                        Text(formatTime(incident.timestamp))
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                    }
                }
            }
        }
    }

    // MARK: - Empty Detail

    private var emptyDetail: some View {
        VStack(spacing: 16) {
            Image(systemName: "doc.text.magnifyingglass")
                .font(.system(size: 40))
                .foregroundColor(theme.textTertiary.opacity(0.3))
            Text(loc.t("trade.selectTrade"))
                .font(LxFont.mono(13))
                .foregroundColor(theme.textTertiary)
            Text(loc.t("trade.studioDescription"))
                .font(LxFont.label)
                .foregroundColor(theme.textTertiary.opacity(0.6))
                .multilineTextAlignment(.center)
                .frame(maxWidth: 300)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(theme.backgroundPrimary)
    }

    // MARK: - Helpers

    private func formatTime(_ date: Date) -> String {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f.string(from: date)
    }

    private func regimeName(_ regime: Int) -> String {
        loc.t(LxColor.regimeLocKey(regime))
    }
}
