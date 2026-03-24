// LogPanelView.swift — Glassmorphism 2026 System Log (Lynrix v2.5)

import SwiftUI
import UniformTypeIdentifiers

struct LogPanelView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    @State private var filterLevel: LogEntry.LogLevel? = nil
    @State private var searchText: String = ""
    @State private var autoScroll: Bool = true
    
    var filteredLogs: [LogEntry] {
        engine.logs.filter { entry in
            let levelMatch = filterLevel == nil || entry.level == filterLevel
            let textMatch = searchText.isEmpty || entry.message.localizedCaseInsensitiveContains(searchText)
            return levelMatch && textMatch
        }
    }
    
    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 12) {
                HStack(spacing: 6) {
                    Image(systemName: "doc.text.fill")
                        .foregroundColor(LxColor.electricCyan)
                        .shadow(color: LxColor.electricCyan.opacity(0.4), radius: 3)
                    Text(loc.t("log.title"))
                        .font(LxFont.mono(14, weight: .bold))
                        .foregroundColor(theme.textPrimary)
                }
                Spacer()
                HStack {
                    Image(systemName: "magnifyingglass").foregroundColor(theme.textTertiary)
                    TextField(loc.t("logs.filter"), text: $searchText)
                        .textFieldStyle(.plain)
                        .font(LxFont.mono(10))
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 4)
                .background(theme.glassHighlight)
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .frame(width: 200)
                
                Picker(loc.t("logs.level"), selection: $filterLevel) {
                    Text(loc.t("logs.all")).tag(LogEntry.LogLevel?.none)
                    Text(loc.t("logs.info")).tag(LogEntry.LogLevel?.some(.info))
                    Text(loc.t("logs.warn")).tag(LogEntry.LogLevel?.some(.warn))
                    Text(loc.t("logs.error")).tag(LogEntry.LogLevel?.some(.error))
                }
                .pickerStyle(.segmented)
                .frame(width: 250)
                
                Toggle(loc.t("tradeTape.autoScroll"), isOn: $autoScroll)
                    .toggleStyle(.switch)
                    .controlSize(.small)
                    .foregroundColor(theme.textSecondary)
                
                Text("\(filteredLogs.count) \(loc.t("common.entries"))")
                    .font(LxFont.micro)
                    .foregroundColor(theme.textTertiary)
                
                Button(action: { engine.clearLogs() }) {
                    Image(systemName: "trash")
                        .foregroundColor(LxColor.bloodRed.opacity(0.7))
                }
                .buttonStyle(.borderless)
                
                Button(action: exportLogs) {
                    Image(systemName: "square.and.arrow.up")
                        .foregroundColor(LxColor.electricCyan.opacity(0.7))
                }
                .buttonStyle(.borderless)
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 8)
            .background(theme.glassHighlight)
            
            Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
            
            if filteredLogs.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "doc.text")
                        .font(.largeTitle)
                        .foregroundColor(theme.textTertiary.opacity(0.3))
                    Text(loc.t("log.noEntries"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollViewReader { proxy in
                    ScrollView {
                        LazyVStack(alignment: .leading, spacing: 0) {
                            ForEach(filteredLogs) { entry in
                                LogRow(entry: entry)
                                    .id(entry.id)
                            }
                        }
                        .padding(.horizontal, 12)
                        .padding(.vertical, 4)
                    }
                    .onChange(of: filteredLogs.count) { _ in
                        if autoScroll, let last = filteredLogs.last {
                            withAnimation(.none) {
                                proxy.scrollTo(last.id, anchor: .bottom)
                            }
                        }
                    }
                }
            }
        }
        .background(theme.backgroundPrimary)
    }
    
    private func exportLogs() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.plainText]
        panel.nameFieldStringValue = "lynrix_logs.txt"
        panel.begin { result in
            if result == .OK, let url = panel.url {
                try? engine.exportLogs().write(to: url, atomically: true, encoding: .utf8)
            }
        }
    }
}

struct LogRow: View {
    @Environment(\.theme) var theme
    let entry: LogEntry
    
    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()
    
    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            Text(Self.timeFormatter.string(from: entry.timestamp))
                .foregroundColor(theme.textTertiary)
                .frame(width: 90, alignment: .leading)
            Text(entry.level.rawValue)
                .fontWeight(.bold)
                .foregroundColor(levelColor)
                .shadow(color: levelColor.opacity(0.3), radius: 2)
                .frame(width: 50, alignment: .leading)
            Text(entry.message)
                .foregroundColor(theme.textPrimary)
                .frame(maxWidth: .infinity, alignment: .leading)
                .lineLimit(3)
        }
        .font(LxFont.mono(10))
        .padding(.vertical, 2)
        .padding(.horizontal, 4)
        .background(backgroundColor)
    }
    
    private var levelColor: Color {
        switch entry.level {
        case .debug: return LxColor.coolSteel
        case .info:  return LxColor.electricCyan
        case .warn:  return LxColor.amber
        case .error: return LxColor.bloodRed
        }
    }
    
    private var backgroundColor: Color {
        switch entry.level {
        case .warn:  return LxColor.amber.opacity(0.03)
        case .error: return LxColor.bloodRed.opacity(0.05)
        default:     return .clear
        }
    }
}
