// WidgetLayoutManager.swift — Centralized widget visibility preferences (Lynrix v2.5 Sprint 7A)
// Provides per-screen widget visibility controls with UserDefaults persistence and reset-to-defaults.

import SwiftUI
import Combine

// MARK: - Widget Identifier

enum WidgetScreen: String, CaseIterable {
    case dashboard
    case systemMonitor
    case diagnostics
    case executionIntel
    case riskCenter
    case tradeReview
    case modelGovernance
    case regimeCenter
    case allocationIntel
    case strategyResearch
    
    var locKey: String {
        switch self {
        case .dashboard:       return "layout.screen.dashboard"
        case .systemMonitor:   return "layout.screen.sysmon"
        case .diagnostics:     return "layout.screen.diagnostics"
        case .executionIntel:  return "layout.screen.execIntel"
        case .riskCenter:      return "layout.screen.riskCenter"
        case .tradeReview:     return "layout.screen.tradeReview"
        case .modelGovernance: return "layout.screen.modelGov"
        case .regimeCenter:    return "layout.screen.regimeCenter"
        case .allocationIntel: return "layout.screen.allocIntel"
        case .strategyResearch: return "layout.screen.stratResearch"
        }
    }
}

struct WidgetID: Hashable, Equatable {
    let screen: WidgetScreen
    let key: String
    
    var persistenceKey: String { "lynrix.widget.\(screen.rawValue).\(key)" }
}

// MARK: - Widget Metadata

struct WidgetMeta {
    let id: WidgetID
    let locKey: String
    let isSafetyCritical: Bool
    let defaultVisible: Bool
    
    init(_ screen: WidgetScreen, _ key: String, locKey: String, safety: Bool = false, defaultVisible: Bool = true) {
        self.id = WidgetID(screen: screen, key: key)
        self.locKey = locKey
        self.isSafetyCritical = safety
        self.defaultVisible = defaultVisible
    }
}

// MARK: - Widget Layout Manager

final class WidgetLayoutManager: ObservableObject {
    static let shared = WidgetLayoutManager()
    
    @Published private var visibility: [String: Bool] = [:]
    
    private init() {
        loadAll()
    }
    
    // MARK: - Public API
    
    func isVisible(_ widget: WidgetID) -> Bool {
        let key = widget.persistenceKey
        return visibility[key] ?? true
    }
    
    func setVisible(_ widget: WidgetID, visible: Bool) {
        let key = widget.persistenceKey
        visibility[key] = visible
        UserDefaults.standard.set(visible, forKey: key)
    }
    
    func toggle(_ widget: WidgetID) {
        setVisible(widget, visible: !isVisible(widget))
    }
    
    func resetScreen(_ screen: WidgetScreen) {
        let registry = Self.widgetRegistry[screen] ?? []
        for meta in registry {
            let key = meta.id.persistenceKey
            visibility[key] = meta.defaultVisible
            UserDefaults.standard.set(meta.defaultVisible, forKey: key)
        }
    }
    
    func resetAll() {
        for (_, metas) in Self.widgetRegistry {
            for meta in metas {
                let key = meta.id.persistenceKey
                visibility[key] = meta.defaultVisible
                UserDefaults.standard.set(meta.defaultVisible, forKey: key)
            }
        }
    }
    
    func widgets(for screen: WidgetScreen) -> [WidgetMeta] {
        Self.widgetRegistry[screen] ?? []
    }
    
    func hiddenCount(for screen: WidgetScreen) -> Int {
        let metas = Self.widgetRegistry[screen] ?? []
        return metas.filter { !isVisible($0.id) }.count
    }
    
    // MARK: - Persistence
    
    private func loadAll() {
        for (_, metas) in Self.widgetRegistry {
            for meta in metas {
                let key = meta.id.persistenceKey
                if let stored = UserDefaults.standard.object(forKey: key) as? Bool {
                    visibility[key] = stored
                } else {
                    visibility[key] = meta.defaultVisible
                }
            }
        }
    }
    
    // MARK: - Widget Registry
    
    static let widgetRegistry: [WidgetScreen: [WidgetMeta]] = [
        .dashboard: [
            WidgetMeta(.dashboard, "hero",           locKey: "layout.widget.hero", safety: false),
            WidgetMeta(.dashboard, "pnl",            locKey: "layout.widget.pnl", safety: false),
            WidgetMeta(.dashboard, "quickBadges",    locKey: "layout.widget.quickBadges"),
            WidgetMeta(.dashboard, "equityCurve",    locKey: "layout.widget.equityCurve"),
            WidgetMeta(.dashboard, "throughput",      locKey: "layout.widget.throughput"),
            WidgetMeta(.dashboard, "latency",         locKey: "layout.widget.latency"),
            WidgetMeta(.dashboard, "aiPrediction",    locKey: "layout.widget.aiPrediction"),
            WidgetMeta(.dashboard, "circuitBreaker",  locKey: "layout.widget.circuitBreaker", safety: true),
            WidgetMeta(.dashboard, "recentLogs",      locKey: "layout.widget.recentLogs"),
        ],
        .systemMonitor: [
            WidgetMeta(.systemMonitor, "resources",       locKey: "layout.widget.resources"),
            WidgetMeta(.systemMonitor, "throughput",       locKey: "layout.widget.throughput"),
            WidgetMeta(.systemMonitor, "latency",          locKey: "layout.widget.latency"),
            WidgetMeta(.systemMonitor, "network",          locKey: "layout.widget.network"),
            WidgetMeta(.systemMonitor, "inference",        locKey: "layout.widget.inference"),
            WidgetMeta(.systemMonitor, "stageHistograms",  locKey: "layout.widget.stageHistograms"),
            WidgetMeta(.systemMonitor, "jitterAlerts",     locKey: "layout.widget.jitterAlerts", safety: true),
        ],
        .diagnostics: [
            WidgetMeta(.diagnostics, "system",      locKey: "layout.widget.systemInfo"),
            WidgetMeta(.diagnostics, "engine",      locKey: "layout.widget.engineState"),
            WidgetMeta(.diagnostics, "data",        locKey: "layout.widget.dataState"),
            WidgetMeta(.diagnostics, "performance", locKey: "layout.widget.perfMetrics"),
        ],
        .executionIntel: [
            WidgetMeta(.executionIntel, "kpiSummary",     locKey: "layout.widget.kpiSummary"),
            WidgetMeta(.executionIntel, "execScore",      locKey: "layout.widget.execScore"),
            WidgetMeta(.executionIntel, "slippage",       locKey: "layout.widget.slippage"),
            WidgetMeta(.executionIntel, "fillQuality",    locKey: "layout.widget.fillQuality"),
            WidgetMeta(.executionIntel, "latencyDecomp",  locKey: "layout.widget.latencyDecomp"),
            WidgetMeta(.executionIntel, "poorExecs",      locKey: "layout.widget.poorExecs"),
            WidgetMeta(.executionIntel, "insights",       locKey: "layout.widget.insights"),
        ],
        .riskCenter: [
            WidgetMeta(.riskCenter, "position",       locKey: "layout.widget.position"),
            WidgetMeta(.riskCenter, "riskUtil",       locKey: "layout.widget.riskUtil"),
            WidgetMeta(.riskCenter, "varSummary",     locKey: "layout.widget.varSummary"),
            WidgetMeta(.riskCenter, "circuitBreaker", locKey: "layout.widget.circuitBreaker", safety: true),
            WidgetMeta(.riskCenter, "killSwitch",     locKey: "layout.widget.killSwitch", safety: true),
        ],
    ]
}

// MARK: - Widget Visibility Toggle Toolbar

struct WidgetVisibilityMenu: View {
    let screen: WidgetScreen
    @ObservedObject var layout = WidgetLayoutManager.shared
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    var body: some View {
        Menu {
            ForEach(layout.widgets(for: screen), id: \.id) { meta in
                Button(action: { layout.toggle(meta.id) }) {
                    HStack {
                        if layout.isVisible(meta.id) {
                            Image(systemName: "checkmark")
                        }
                        Text(loc.t(meta.locKey))
                        if meta.isSafetyCritical {
                            Image(systemName: "shield.fill")
                        }
                    }
                }
            }
            Divider()
            Button(action: { layout.resetScreen(screen) }) {
                Label(loc.t("layout.resetDefaults"), systemImage: "arrow.counterclockwise")
            }
        } label: {
            HStack(spacing: 3) {
                Image(systemName: "square.grid.2x2")
                    .font(.system(size: 10))
                if layout.hiddenCount(for: screen) > 0 {
                    Text("\(layout.hiddenCount(for: screen))")
                        .font(LxFont.mono(9, weight: .bold))
                }
            }
            .foregroundColor(layout.hiddenCount(for: screen) > 0 ? LxColor.amber : theme.textTertiary)
            .padding(.horizontal, 6)
            .padding(.vertical, 3)
            .background(Capsule().fill(theme.glassHighlight))
            .overlay(Capsule().stroke(theme.borderSubtle, lineWidth: 0.5))
        }
        .menuStyle(.borderlessButton)
        .fixedSize()
    }
}
