// ContentView.swift — Main layout with sidebar navigation

import SwiftUI

struct ContentView: View {
    @EnvironmentObject var engine: TradingEngine
    @State private var selectedTab: SidebarTab = .dashboard
    
    enum SidebarTab: String, CaseIterable, Identifiable {
        case dashboard = "Панель"
        case ai = "AI Модель"
        case orderbook = "Стакан"
        case trades = "Лента сделок"
        case portfolio = "Портфель"
        case signals = "Сигналы"
        case strategyPerf = "Стратегия"
        case systemMonitor = "Система"
        case mlDiagnostics = "ML Диагностика"
        case settings = "Настройки"
        case logs = "Логи"
        case diagnostics = "Диагностика"
        
        var id: String { rawValue }
        
        var icon: String {
            switch self {
            case .dashboard: return "gauge.open.with.lines.needle.33percent"
            case .ai: return "brain"
            case .orderbook: return "list.number"
            case .trades: return "chart.line.uptrend.xyaxis"
            case .portfolio: return "briefcase"
            case .signals: return "bolt.horizontal"
            case .strategyPerf: return "chart.bar.xaxis"
            case .systemMonitor: return "cpu"
            case .mlDiagnostics: return "brain.head.profile"
            case .settings: return "gearshape"
            case .logs: return "doc.text"
            case .diagnostics: return "stethoscope"
            }
        }
    }
    
    var body: some View {
        NavigationSplitView {
            List(SidebarTab.allCases, selection: $selectedTab) { tab in
                Label(tab.rawValue, systemImage: tab.icon)
                    .tag(tab)
            }
            .listStyle(.sidebar)
            .frame(minWidth: 180)
            
            // Status bar at bottom of sidebar
            VStack(spacing: 8) {
                Divider()
                StatusBadge(status: engine.status)
                
                if engine.isReconnecting {
                    ReconnectBadge()
                }
                
                HStack(spacing: 6) {
                    if engine.status.isActive {
                        Button(action: { engine.stop() }) {
                            Label("Стоп", systemImage: "stop.fill")
                                .font(.caption)
                        }
                        .buttonStyle(.borderedProminent)
                        .tint(.red)
                        .controlSize(.small)
                        
                        Button(action: { engine.emergencyStop() }) {
                            Label("SOS", systemImage: "exclamationmark.octagon.fill")
                                .font(.caption)
                        }
                        .buttonStyle(.borderedProminent)
                        .tint(.pink)
                        .controlSize(.small)
                        .help("Аварийная остановка — отмена всех ордеров")
                    } else {
                        Button(action: { startEngine() }) {
                            Label("Старт", systemImage: "play.fill")
                                .font(.caption)
                        }
                        .buttonStyle(.borderedProminent)
                        .tint(.green)
                        .controlSize(.small)
                        
                        if engine.status == .error {
                            Button(action: { engine.restart() }) {
                                Label("Рестарт", systemImage: "arrow.counterclockwise")
                                    .font(.caption)
                            }
                            .buttonStyle(.borderedProminent)
                            .tint(.orange)
                            .controlSize(.small)
                        }
                    }
                }
                
                HStack(spacing: 6) {
                    ModeBadge(isPaper: engine.paperMode)
                    
                    if engine.status.isActive {
                        Button(action: { engine.reloadModel() }) {
                            Image(systemName: "arrow.triangle.2.circlepath")
                                .font(.caption2)
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        .help("Перезагрузить ML модель")
                    }
                }
                .padding(.bottom, 8)
            }
            .padding(.horizontal, 12)
        } detail: {
            Group {
                switch selectedTab {
                case .dashboard:  DashboardView()
                case .ai:         AIDashboardView()
                case .orderbook:  OrderBookView()
                case .trades:     TradeTapeView()
                case .portfolio:  PortfolioView()
                case .signals:    SignalsView()
                case .strategyPerf: StrategyPerformanceView()
                case .systemMonitor: SystemMonitorView()
                case .mlDiagnostics: MLDiagnosticsView()
                case .settings:   SettingsView()
                case .logs:       LogPanelView()
                case .diagnostics: DiagnosticsView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .alert("ПАНИКА", isPresented: $engine.showPanicAlert) {
            Button("Аварийная остановка", role: .destructive) {
                engine.emergencyStop()
            }
            Button("Перезапуск", role: .none) {
                engine.restart()
            }
            Button("Закрыть", role: .cancel) {}
        } message: {
            Text(engine.panicMessage)
        }
    }
    
    private func startEngine() {
        let key = KeychainManager.shared.loadAPIKey()
        let secret = KeychainManager.shared.loadAPISecret()
        engine.start(apiKey: key, apiSecret: secret)
    }
}

// MARK: - Status Badge

struct StatusBadge: View {
    let status: EngineStatus
    
    var color: Color {
        switch status {
        case .idle:       return .secondary
        case .connecting: return .yellow
        case .connected:  return .blue
        case .trading:    return .green
        case .error:      return .red
        case .stopping:   return .orange
        }
    }
    
    var body: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(color)
                .frame(width: 8, height: 8)
                .shadow(color: color.opacity(0.6), radius: 3)
            Text(status.label)
                .font(.system(.caption, design: .monospaced))
                .foregroundColor(color)
        }
    }
}

struct ModeBadge: View {
    let isPaper: Bool
    
    var body: some View {
        Text(isPaper ? "БУМАЖНАЯ" : "РЕАЛЬНАЯ")
            .font(.system(.caption2, design: .monospaced))
            .fontWeight(.bold)
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(isPaper ? Color.blue.opacity(0.2) : Color.red.opacity(0.2))
            .foregroundColor(isPaper ? .blue : .red)
            .clipShape(RoundedRectangle(cornerRadius: 4))
    }
}

struct ReconnectBadge: View {
    @State private var isAnimating = false
    
    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: "arrow.triangle.2.circlepath")
                .rotationEffect(.degrees(isAnimating ? 360 : 0))
                .animation(.linear(duration: 1.5).repeatForever(autoreverses: false), value: isAnimating)
            Text("ПЕРЕПОДКЛЮЧЕНИЕ")
        }
        .font(.system(.caption2, design: .monospaced))
        .fontWeight(.bold)
        .foregroundColor(.orange)
        .padding(.horizontal, 6)
        .padding(.vertical, 2)
        .background(Color.orange.opacity(0.15))
        .clipShape(RoundedRectangle(cornerRadius: 4))
        .onAppear { isAnimating = true }
    }
}
