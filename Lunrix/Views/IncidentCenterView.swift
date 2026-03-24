// IncidentCenterView.swift — Incident Center timeline (Lynrix v2.5)

import SwiftUI

struct IncidentCenterView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @ObservedObject var store = IncidentStore.shared
    @Environment(\.theme) var theme
    
    @State private var filterSeverity: IncidentSeverity? = nil
    @State private var filterCategory: IncidentCategory? = nil
    
    private var filteredIncidents: [IncidentEntry] {
        store.incidents.filter { entry in
            if let sev = filterSeverity, entry.severity != sev { return false }
            if let cat = filterCategory, entry.category != cat { return false }
            return true
        }
    }
    
    var body: some View {
        VStack(spacing: 0) {
            // Header
            headerPanel
            
            // Filters
            filterBar
            
            // Timeline
            if filteredIncidents.isEmpty {
                emptyState
            } else {
                ScrollView {
                    LazyVStack(spacing: 6) {
                        ForEach(filteredIncidents) { incident in
                            IncidentRow(incident: incident)
                        }
                    }
                    .padding(16)
                }
            }
        }
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - Header
    
    private var headerPanel: some View {
        GlassPanel(neon: store.criticalUnresolved.isEmpty ? LxColor.coolSteel : LxColor.bloodRed) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 8) {
                        Image(systemName: "list.bullet.clipboard")
                            .font(.system(size: 16, weight: .bold))
                            .foregroundColor(store.criticalUnresolved.isEmpty ? LxColor.coolSteel : LxColor.bloodRed)
                        Text(loc.t("incident.title"))
                            .font(LxFont.mono(14, weight: .bold))
                            .foregroundColor(theme.textPrimary)
                    }
                    Text(loc.t("incident.subtitle"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                }
                Spacer()
                
                HStack(spacing: 12) {
                    IncidentCountBadge(
                        count: store.incidents.filter { $0.severity == .critical && !$0.resolved }.count,
                        label: loc.t("incident.critical"),
                        color: LxColor.bloodRed
                    )
                    IncidentCountBadge(
                        count: store.incidents.filter { $0.severity == .warning && !$0.resolved }.count,
                        label: loc.t("incident.warning"),
                        color: LxColor.amber
                    )
                    IncidentCountBadge(
                        count: store.incidents.count,
                        label: loc.t("incident.total"),
                        color: LxColor.coolSteel
                    )
                }
                
                NeonButton(loc.t("incident.clearAll"), icon: "trash", color: LxColor.coolSteel) {
                    store.clear()
                }
                .disabled(store.incidents.isEmpty)
            }
        }
        .padding(.horizontal, 16)
        .padding(.top, 16)
    }
    
    // MARK: - Filter Bar
    
    private var filterBar: some View {
        HStack(spacing: 6) {
            Text(loc.t("incident.filter"))
                .font(LxFont.micro)
                .foregroundColor(theme.textTertiary)
            
            FilterPill(label: loc.t("common.all"), isSelected: filterSeverity == nil && filterCategory == nil) {
                filterSeverity = nil
                filterCategory = nil
            }
            
            ForEach(IncidentSeverity.allCases, id: \.rawValue) { sev in
                FilterPill(label: loc.t(sev.locKey), isSelected: filterSeverity == sev, color: sev.color) {
                    filterSeverity = filterSeverity == sev ? nil : sev
                }
            }
            
            Rectangle().fill(theme.divider).frame(width: 0.5, height: 14)
            
            ForEach(IncidentCategory.allCases.prefix(5), id: \.rawValue) { cat in
                FilterPill(label: loc.t(cat.locKey), isSelected: filterCategory == cat) {
                    filterCategory = filterCategory == cat ? nil : cat
                }
            }
            
            Spacer()
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
    }
    
    // MARK: - Empty State
    
    private var emptyState: some View {
        VStack(spacing: 12) {
            Spacer()
            Image(systemName: "checkmark.shield")
                .font(.system(size: 36))
                .foregroundColor(LxColor.neonLime.opacity(0.4))
            Text(loc.t("incident.noIncidents"))
                .font(LxFont.mono(13))
                .foregroundColor(theme.textTertiary)
            Text(loc.t("incident.noIncidentsDetail"))
                .font(LxFont.label)
                .foregroundColor(theme.textTertiary)
            Spacer()
        }
        .frame(maxWidth: .infinity)
    }
}

// MARK: - Incident Row

struct IncidentRow: View {
    let incident: IncidentEntry
    @Environment(\.theme) var theme
    @EnvironmentObject var loc: LocalizationManager
    
    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()
    
    private static let dateFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "yyyy-MM-dd"
        return f
    }()
    
    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            // Timeline dot
            VStack(spacing: 0) {
                Circle()
                    .fill(incident.severity.color)
                    .frame(width: 8, height: 8)
                    .shadow(color: incident.severity.color.opacity(0.5), radius: 3)
                Rectangle()
                    .fill(theme.divider)
                    .frame(width: 1)
                    .frame(maxHeight: .infinity)
            }
            .frame(width: 8)
            
            // Content
            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: 6) {
                    Image(systemName: incident.category.icon)
                        .font(.system(size: 10))
                        .foregroundColor(incident.severity.color)
                    Text(loc.t(incident.titleKey))
                        .font(LxFont.mono(11, weight: .semibold))
                        .foregroundColor(theme.textPrimary)
                    Spacer()
                    Text(Self.timeFormatter.string(from: incident.timestamp))
                        .font(LxFont.mono(9))
                        .foregroundColor(theme.textTertiary)
                    Text(Self.dateFormatter.string(from: incident.timestamp))
                        .font(LxFont.mono(8))
                        .foregroundColor(theme.textTertiary)
                }
                
                if !incident.detail.isEmpty {
                    Text(incident.detail)
                        .font(LxFont.mono(9))
                        .foregroundColor(theme.textSecondary)
                        .lineLimit(2)
                }
                
                HStack(spacing: 6) {
                    Image(systemName: incident.severity.icon)
                        .font(.system(size: 8))
                        .foregroundColor(incident.severity.color)
                    Text(loc.t(incident.severity.locKey))
                        .font(LxFont.mono(8, weight: .bold))
                        .foregroundColor(incident.severity.color)
                    Text("·")
                        .foregroundColor(theme.textTertiary)
                    Text(loc.t(incident.category.locKey))
                        .font(LxFont.mono(8))
                        .foregroundColor(theme.textTertiary)
                    if incident.resolved {
                        Text("·")
                            .foregroundColor(theme.textTertiary)
                        Text(loc.t("incident.resolved"))
                            .font(LxFont.mono(8, weight: .bold))
                            .foregroundColor(LxColor.neonLime)
                    }
                }
            }
        }
        .padding(8)
        .background(
            RoundedRectangle(cornerRadius: 7)
                .fill(incident.severity == .critical && !incident.resolved
                      ? incident.severity.color.opacity(0.04) : Color.clear)
                .overlay(
                    RoundedRectangle(cornerRadius: 7)
                        .stroke(incident.severity == .critical && !incident.resolved
                                ? incident.severity.color.opacity(0.1) : theme.borderSubtle.opacity(0.3),
                                lineWidth: 0.5)
                )
        )
    }
}

// MARK: - Supporting Components

struct IncidentCountBadge: View {
    let count: Int
    let label: String
    let color: Color
    @Environment(\.theme) var theme
    
    var body: some View {
        VStack(spacing: 1) {
            Text("\(count)")
                .font(LxFont.mono(13, weight: .bold))
                .foregroundColor(count > 0 ? color : theme.textTertiary)
            Text(label)
                .font(LxFont.mono(7))
                .foregroundColor(theme.textTertiary)
        }
    }
}

struct FilterPill: View {
    let label: String
    let isSelected: Bool
    var color: Color = LxColor.electricCyan
    let action: () -> Void
    @Environment(\.theme) var theme
    
    var body: some View {
        Button(action: action) {
            Text(label)
                .font(LxFont.mono(9, weight: isSelected ? .bold : .regular))
                .foregroundColor(isSelected ? color : theme.textSecondary)
                .padding(.horizontal, 8)
                .padding(.vertical, 3)
                .background(
                    Capsule().fill(isSelected ? color.opacity(0.1) : Color.clear)
                )
                .overlay(
                    Capsule().stroke(isSelected ? color.opacity(0.25) : theme.borderSubtle, lineWidth: 0.5)
                )
        }
        .buttonStyle(.plain)
    }
}
