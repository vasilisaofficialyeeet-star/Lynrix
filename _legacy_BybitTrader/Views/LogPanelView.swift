// LogPanelView.swift — System log viewer with filtering and export

import SwiftUI
import UniformTypeIdentifiers

struct LogPanelView: View {
    @EnvironmentObject var engine: TradingEngine
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
            // Toolbar
            HStack(spacing: 12) {
                Text("СИСТЕМНЫЙ ЖУРНАЛ")
                    .font(.system(.headline, design: .monospaced))
                
                Spacer()
                
                // Search
                HStack {
                    Image(systemName: "magnifyingglass")
                        .foregroundColor(.secondary)
                    TextField("Фильтр...", text: $searchText)
                        .textFieldStyle(.plain)
                        .font(.system(.caption, design: .monospaced))
                }
                .padding(.horizontal, 8)
                .padding(.vertical, 4)
                .background(Color(.controlBackgroundColor))
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .frame(width: 200)
                
                // Level filter
                Picker("Уровень", selection: $filterLevel) {
                    Text("Все").tag(LogEntry.LogLevel?.none)
                    Text("ИНФО").tag(LogEntry.LogLevel?.some(.info))
                    Text("ПРЕДУПР").tag(LogEntry.LogLevel?.some(.warn))
                    Text("ОШИБКА").tag(LogEntry.LogLevel?.some(.error))
                }
                .pickerStyle(.segmented)
                .frame(width: 250)
                
                Toggle("Автопрокрутка", isOn: $autoScroll)
                    .toggleStyle(.switch)
                    .controlSize(.small)
                
                Text("\(filteredLogs.count) записей")
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(.secondary)
                
                Button(action: { engine.clearLogs() }) {
                    Image(systemName: "trash")
                }
                .buttonStyle(.borderless)
                .help("Очистить логи")
                
                Button(action: exportLogs) {
                    Image(systemName: "square.and.arrow.up")
                }
                .buttonStyle(.borderless)
                .help("Экспорт логов")
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 8)
            .background(Color(.controlBackgroundColor).opacity(0.3))
            
            Divider()
            
            // Log content
            if filteredLogs.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "doc.text")
                        .font(.largeTitle)
                        .foregroundColor(.secondary.opacity(0.3))
                    Text("Записей нет")
                        .font(.system(.body, design: .monospaced))
                        .foregroundColor(.secondary)
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
        .background(Color(.textBackgroundColor).opacity(0.3))
    }
    
    private func exportLogs() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.plainText]
        panel.nameFieldStringValue = "bybit_logs.txt"
        panel.begin { result in
            if result == .OK, let url = panel.url {
                try? engine.exportLogs().write(to: url, atomically: true, encoding: .utf8)
            }
        }
    }
}

struct LogRow: View {
    let entry: LogEntry
    
    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()
    
    var body: some View {
        HStack(alignment: .top, spacing: 8) {
            Text(Self.timeFormatter.string(from: entry.timestamp))
                .foregroundColor(.secondary)
                .frame(width: 90, alignment: .leading)
            
            Text(entry.level.rawValue)
                .fontWeight(.bold)
                .foregroundColor(levelColor)
                .frame(width: 50, alignment: .leading)
            
            Text(entry.message)
                .foregroundColor(.primary)
                .frame(maxWidth: .infinity, alignment: .leading)
                .lineLimit(3)
        }
        .font(.system(.caption, design: .monospaced))
        .padding(.vertical, 2)
        .padding(.horizontal, 4)
        .background(backgroundColor)
    }
    
    private var levelColor: Color {
        switch entry.level {
        case .debug: return .secondary
        case .info:  return .blue
        case .warn:  return .orange
        case .error: return .red
        }
    }
    
    private var backgroundColor: Color {
        switch entry.level {
        case .warn:  return .orange.opacity(0.05)
        case .error: return .red.opacity(0.08)
        default:     return .clear
        }
    }
}
