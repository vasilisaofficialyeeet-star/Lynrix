// MergedViews.swift — D1: Thin wrapper views that merge removed sidebar tabs into parent views
// Dashboard + AI, ModelGovernance + MLDiagnostics, SystemMonitor + Diagnostics

import SwiftUI

// MARK: - D-R1: Dashboard + AI + Signals (merged)

struct MergedDashboardView: View {
    @State private var selectedSubTab: SubTab = .overview
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    enum SubTab: String, CaseIterable {
        case overview, aiModel, signals
        
        var locKey: String {
            switch self {
            case .overview: return "tab.dashboard"
            case .aiModel:  return "tab.ai"
            case .signals:  return "tab.signals"
            }
        }
        
        var icon: String {
            switch self {
            case .overview: return "gauge.open.with.lines.needle.33percent"
            case .aiModel:  return "brain"
            case .signals:  return "bolt.horizontal"
            }
        }
    }
    
    var body: some View {
        VStack(spacing: 0) {
            mergedTabBar
            
            Group {
                switch selectedSubTab {
                case .overview: DashboardView()
                case .aiModel:  AIDashboardView()
                case .signals:  SignalsView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
    
    private var mergedTabBar: some View {
        HStack(spacing: 2) {
            ForEach(SubTab.allCases, id: \.rawValue) { tab in
                MergedSubTabButton(
                    title: loc.t(tab.locKey),
                    icon: tab.icon,
                    isSelected: selectedSubTab == tab,
                    color: LxColor.electricCyan
                ) {
                    withAnimation(LxAnimation.snappy) { selectedSubTab = tab }
                }
            }
            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
        .background(theme.backgroundSecondary.opacity(0.3))
        .overlay(Rectangle().fill(theme.divider).frame(height: 0.5), alignment: .bottom)
    }
}

// MARK: - D1: Model Governance + ML Diagnostics (merged)

struct MergedModelGovernanceView: View {
    @State private var selectedSubTab: SubTab = .governance
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    enum SubTab: String, CaseIterable {
        case governance, mlDiagnostics
        
        var locKey: String {
            switch self {
            case .governance:     return "tab.modelGov"
            case .mlDiagnostics:  return "tab.mlDiag"
            }
        }
        
        var icon: String {
            switch self {
            case .governance:     return "checkmark.seal"
            case .mlDiagnostics:  return "waveform.path.ecg"
            }
        }
    }
    
    var body: some View {
        VStack(spacing: 0) {
            mergedTabBar
            
            Group {
                switch selectedSubTab {
                case .governance:     ModelGovernanceView()
                case .mlDiagnostics:  MLDiagnosticsView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
    
    private var mergedTabBar: some View {
        HStack(spacing: 2) {
            ForEach(SubTab.allCases, id: \.rawValue) { tab in
                MergedSubTabButton(
                    title: loc.t(tab.locKey),
                    icon: tab.icon,
                    isSelected: selectedSubTab == tab,
                    color: LxColor.electricCyan
                ) {
                    withAnimation(LxAnimation.snappy) { selectedSubTab = tab }
                }
            }
            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
        .background(theme.backgroundSecondary.opacity(0.3))
        .overlay(Rectangle().fill(theme.divider).frame(height: 0.5), alignment: .bottom)
    }
}

// MARK: - D-R1: System Monitor + Diagnostics + Incidents (merged)

struct MergedSystemMonitorView: View {
    @State private var selectedSubTab: SubTab = .monitor
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    enum SubTab: String, CaseIterable {
        case monitor, diagnostics, incidents
        
        var locKey: String {
            switch self {
            case .monitor:      return "tab.system"
            case .diagnostics:  return "tab.diagnostics"
            case .incidents:    return "tab.incidents"
            }
        }
        
        var icon: String {
            switch self {
            case .monitor:      return "cpu"
            case .diagnostics:  return "stethoscope"
            case .incidents:    return "list.bullet.clipboard"
            }
        }
    }
    
    var body: some View {
        VStack(spacing: 0) {
            mergedTabBar
            
            Group {
                switch selectedSubTab {
                case .monitor:      SystemMonitorView()
                case .diagnostics:  DiagnosticsView()
                case .incidents:    IncidentCenterView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
    
    private var mergedTabBar: some View {
        HStack(spacing: 2) {
            ForEach(SubTab.allCases, id: \.rawValue) { tab in
                MergedSubTabButton(
                    title: loc.t(tab.locKey),
                    icon: tab.icon,
                    isSelected: selectedSubTab == tab,
                    color: LxColor.coolSteel
                ) {
                    withAnimation(LxAnimation.snappy) { selectedSubTab = tab }
                }
            }
            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
        .background(theme.backgroundSecondary.opacity(0.3))
        .overlay(Rectangle().fill(theme.divider).frame(height: 0.5), alignment: .bottom)
    }
}

// MARK: - D-R1: Risk Center + VaR (merged)

struct MergedRiskCenterView: View {
    @State private var selectedSubTab: SubTab = .risk
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    enum SubTab: String, CaseIterable {
        case risk, varDashboard
        
        var locKey: String {
            switch self {
            case .risk:         return "tab.riskCenter"
            case .varDashboard: return "tab.varRisk"
            }
        }
        
        var icon: String {
            switch self {
            case .risk:         return "shield.checkerboard"
            case .varDashboard: return "shield.lefthalf.filled"
            }
        }
    }
    
    var body: some View {
        VStack(spacing: 0) {
            mergedTabBar
            Group {
                switch selectedSubTab {
                case .risk:         RiskCommandCenterView()
                case .varDashboard: VarDashboardView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
    
    private var mergedTabBar: some View {
        HStack(spacing: 2) {
            ForEach(SubTab.allCases, id: \.rawValue) { tab in
                MergedSubTabButton(
                    title: loc.t(tab.locKey),
                    icon: tab.icon,
                    isSelected: selectedSubTab == tab,
                    color: LxColor.amber
                ) {
                    withAnimation(LxAnimation.snappy) { selectedSubTab = tab }
                }
            }
            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
        .background(theme.backgroundSecondary.opacity(0.3))
        .overlay(Rectangle().fill(theme.divider).frame(height: 0.5), alignment: .bottom)
    }
}

// MARK: - D-R1: Execution Intel + Order FSM (merged)

struct MergedExecutionView: View {
    @State private var selectedSubTab: SubTab = .execution
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    enum SubTab: String, CaseIterable {
        case execution, orderFSM
        
        var locKey: String {
            switch self {
            case .execution: return "tab.executionIntel"
            case .orderFSM:  return "tab.orderFSM"
            }
        }
        
        var icon: String {
            switch self {
            case .execution: return "chart.bar.doc.horizontal"
            case .orderFSM:  return "arrow.triangle.branch"
            }
        }
    }
    
    var body: some View {
        VStack(spacing: 0) {
            mergedTabBar
            Group {
                switch selectedSubTab {
                case .execution: ExecutionIntelligenceView()
                case .orderFSM:  OrderStateMachineView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
    
    private var mergedTabBar: some View {
        HStack(spacing: 2) {
            ForEach(SubTab.allCases, id: \.rawValue) { tab in
                MergedSubTabButton(
                    title: loc.t(tab.locKey),
                    icon: tab.icon,
                    isSelected: selectedSubTab == tab,
                    color: LxColor.gold
                ) {
                    withAnimation(LxAnimation.snappy) { selectedSubTab = tab }
                }
            }
            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
        .background(theme.backgroundSecondary.opacity(0.3))
        .overlay(Rectangle().fill(theme.divider).frame(height: 0.5), alignment: .bottom)
    }
}

// MARK: - D-R1: Strategy Research + Allocation Intel (merged)

struct MergedResearchView: View {
    @State private var selectedSubTab: SubTab = .research
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    enum SubTab: String, CaseIterable {
        case research, allocation
        
        var locKey: String {
            switch self {
            case .research:   return "tab.strategyResearch"
            case .allocation: return "tab.allocIntel"
            }
        }
        
        var icon: String {
            switch self {
            case .research:   return "magnifyingglass.circle"
            case .allocation: return "slider.horizontal.below.square.and.square.filled"
            }
        }
    }
    
    var body: some View {
        VStack(spacing: 0) {
            mergedTabBar
            Group {
                switch selectedSubTab {
                case .research:   StrategyResearchView()
                case .allocation: AllocationIntelligenceView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
    
    private var mergedTabBar: some View {
        HStack(spacing: 2) {
            ForEach(SubTab.allCases, id: \.rawValue) { tab in
                MergedSubTabButton(
                    title: loc.t(tab.locKey),
                    icon: tab.icon,
                    isSelected: selectedSubTab == tab,
                    color: LxColor.electricCyan
                ) {
                    withAnimation(LxAnimation.snappy) { selectedSubTab = tab }
                }
            }
            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
        .background(theme.backgroundSecondary.opacity(0.3))
        .overlay(Rectangle().fill(theme.divider).frame(height: 0.5), alignment: .bottom)
    }
}

// MARK: - Shared Sub-Tab Button

struct MergedSubTabButton: View {
    let title: String
    let icon: String
    let isSelected: Bool
    let color: Color
    let action: () -> Void
    
    @State private var isHovered = false
    @Environment(\.theme) var theme
    
    var body: some View {
        Button(action: action) {
            HStack(spacing: 5) {
                Image(systemName: icon)
                    .font(.system(size: 10, weight: isSelected ? .bold : .medium))
                Text(title)
                    .font(LxFont.mono(11, weight: isSelected ? .bold : .medium))
            }
            .foregroundColor(isSelected ? color : (isHovered ? theme.textPrimary : theme.textSecondary))
            .padding(.horizontal, 10)
            .padding(.vertical, 5)
            .background(
                RoundedRectangle(cornerRadius: 6)
                    .fill(isSelected ? color.opacity(theme.accentTintOpacity) : (isHovered ? theme.hoverBackground : Color.clear))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(isSelected ? color.opacity(theme.accentBorderOpacity * 0.6) : Color.clear, lineWidth: 0.5)
            )
        }
        .buttonStyle(.plain)
        .onHover { hovering in
            withAnimation(LxAnimation.micro) { isHovered = hovering }
        }
    }
}
