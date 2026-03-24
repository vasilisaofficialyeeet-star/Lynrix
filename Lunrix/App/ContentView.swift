// ContentView.swift — Glassmorphism 2026 sidebar + navigation (Lynrix v2.5)

import SwiftUI

struct ContentView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @EnvironmentObject var themeManager: ThemeManager
    @Environment(\.theme) var theme
    @State private var selectedTab: SidebarTab = .dashboard
    @State private var devToolsExpanded: Bool = false
    @State private var showCommandPalette: Bool = false  // D-A1
    
    // D-R1: Reduced from 24 → 17 tabs. 8 merged into parent views via sub-tabs.
    // Dev tools (chaos, replay, logs) collapsed by default → 14 visible.
    enum SidebarTab: String, CaseIterable, Identifiable {
        // Trading (4) — signals merged into dashboard
        case dashboard, orderbook, trades, portfolio
        // Analytics (3) — varDashboard→riskCenter, orderFSM→executionIntel
        case strategyPerf, riskCenter, executionIntel, tradeReview
        // Research (3) — allocationIntel→strategyResearch
        case rlTraining, modelGovernance, regimeCenter, strategyResearch
        // System (2) — incidentCenter→systemMonitor
        case systemMonitor, settings
        // Developer Tools (3) — collapsed by default
        case chaosControl, replayControl, logs
        
        var id: String { rawValue }
        
        var locKey: String {
            switch self {
            case .dashboard:       return "tab.dashboard"
            case .orderbook:       return "tab.orderbook"
            case .trades:          return "tab.trades"
            case .portfolio:       return "tab.portfolio"
            case .strategyPerf:    return "tab.strategy"
            case .riskCenter:      return "tab.riskCenter"
            case .executionIntel:  return "tab.executionIntel"
            case .tradeReview:     return "tab.tradeReview"
            case .rlTraining:      return "tab.rlTraining"
            case .modelGovernance: return "tab.modelGov"
            case .regimeCenter:    return "tab.regimeCenter"
            case .strategyResearch: return "tab.strategyResearch"
            case .systemMonitor:   return "tab.system"
            case .settings:        return "tab.settings"
            case .chaosControl:    return "tab.chaos"
            case .replayControl:   return "tab.replay"
            case .logs:            return "tab.logs"
            }
        }
        
        var icon: String {
            switch self {
            case .dashboard:       return "gauge.open.with.lines.needle.33percent"
            case .orderbook:       return "list.number"
            case .trades:          return "chart.line.uptrend.xyaxis"
            case .portfolio:       return "briefcase"
            case .strategyPerf:    return "chart.bar.xaxis"
            case .riskCenter:      return "shield.checkerboard"
            case .executionIntel:  return "chart.bar.doc.horizontal"
            case .tradeReview:     return "doc.text.magnifyingglass"
            case .rlTraining:      return "brain.head.profile"
            case .modelGovernance: return "checkmark.seal"
            case .regimeCenter:    return "waveform.path.ecg.rectangle"
            case .strategyResearch: return "magnifyingglass.circle"
            case .systemMonitor:   return "cpu"
            case .settings:        return "gearshape"
            case .chaosControl:    return "tornado"
            case .replayControl:   return "play.rectangle"
            case .logs:            return "doc.text"
            }
        }
        
        var neonColor: Color {
            switch self {
            case .dashboard:       return LxColor.electricCyan
            case .orderbook:       return LxColor.electricCyan
            case .trades:          return LxColor.neonLime
            case .portfolio:       return LxColor.neonLime
            case .strategyPerf:    return LxColor.gold
            case .riskCenter:      return LxColor.amber
            case .executionIntel:  return LxColor.gold
            case .tradeReview:     return LxColor.gold
            case .rlTraining:      return LxColor.electricCyan
            case .modelGovernance: return LxColor.electricCyan
            case .regimeCenter:    return LxColor.gold
            case .strategyResearch: return LxColor.electricCyan
            case .systemMonitor:   return LxColor.coolSteel
            case .settings:        return LxColor.coolSteel
            case .chaosControl:    return LxColor.bloodRed
            case .replayControl:   return LxColor.gold
            case .logs:            return LxColor.coolSteel
            }
        }
        
        var section: SidebarSection {
            switch self {
            case .dashboard, .orderbook, .trades, .portfolio:
                return .trading
            case .strategyPerf, .riskCenter, .executionIntel, .tradeReview:
                return .analytics
            case .rlTraining, .modelGovernance, .regimeCenter, .strategyResearch:
                return .research
            case .systemMonitor, .settings:
                return .system
            case .chaosControl, .replayControl, .logs:
                return .devTools
            }
        }
    }
    
    // D1: Reorganized from 5 → 4 main sections + collapsible dev tools
    enum SidebarSection: String, CaseIterable {
        case trading, analytics, research, system, devTools
        
        // PERF P3: Pre-computed tab lists — avoid .filter{} on every body eval
        static let mainSections: [SidebarSection] = allCases.filter { $0.isMainSection }
        static let tabsBySection: [SidebarSection: [SidebarTab]] = {
            var dict: [SidebarSection: [SidebarTab]] = [:]
            for tab in SidebarTab.allCases {
                dict[tab.section, default: []].append(tab)
            }
            return dict
        }()
        
        var locKey: String {
            switch self {
            case .trading:   return "section.trading"
            case .analytics: return "section.analytics"
            case .research:  return "section.research"
            case .system:    return "section.system"
            case .devTools:  return "section.devTools"
            }
        }
        
        var color: Color {
            switch self {
            case .trading:   return LxColor.electricCyan
            case .analytics: return LxColor.amber
            case .research:  return LxColor.gold
            case .system:    return LxColor.coolSteel
            case .devTools:  return LxColor.magentaPink
            }
        }
        
        /// Main sections shown always; devTools is collapsible
        var isMainSection: Bool { self != .devTools }
    }
    
    var body: some View {
        HStack(spacing: 0) {
            // Glass Sidebar
            sidebarView
            
            // Neon separator
            Rectangle()
                .fill(
                    LinearGradient(
                        colors: [
                            selectedTab.neonColor.opacity(0.0),
                            selectedTab.neonColor.opacity(0.3),
                            selectedTab.neonColor.opacity(0.3),
                            selectedTab.neonColor.opacity(0.0)
                        ],
                        startPoint: .top,
                        endPoint: .bottom
                    )
                )
                .frame(width: 0.5)
                .shadow(color: selectedTab.neonColor.opacity(0.3 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
            
            // Detail content with unified status bar
            VStack(spacing: 0) {
                UnifiedStatusBar()
                detailView
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .background(
            // PERF P1: Removed VisualEffectBackground — behind-window blur was compositing every frame
            theme.backgroundPrimary
        )
        // D-A4: Toast notifications
        .overlay { ToastOverlay() }
        // D-A1: Command palette overlay
        .overlay {
            if showCommandPalette {
                ZStack {
                    Color.black.opacity(0.3)
                        .ignoresSafeArea()
                        .onTapGesture { showCommandPalette = false }
                    
                    VStack {
                        CommandPalette(
                            isPresented: $showCommandPalette,
                            commands: buildCommandList()
                        )
                        .padding(.top, 60)
                        Spacer()
                    }
                }
                .transition(.opacity.animation(LxAnimation.micro))
            }
        }
        // D-A2: Keyboard shortcuts (macOS 14+)
        .modifier(KeyboardShortcutModifier(
            onCommand: { key in handleKeyboardShortcutChar(key) },
            showPalette: $showCommandPalette
        ))
        .alert(loc.t("engine.panic"), isPresented: $engine.showPanicAlert) {
            Button(loc.t("menu.emergencyStop"), role: .destructive) { engine.emergencyStop() }
            Button(loc.t("common.restart"), role: .none) { engine.restart() }
            Button(loc.t("common.dismiss"), role: .cancel) { }
        } message: {
            Text(engine.panicMessage)
        }
    }
    
    // MARK: - Sidebar
    
    private var sidebarView: some View {
        VStack(spacing: 0) {
            // Logo area (draggable)
            logoBar
            
            // Navigation items
            ScrollView(.vertical, showsIndicators: false) {
                VStack(alignment: .leading, spacing: 2) {
                    // PERF P3: Use pre-computed arrays instead of .filter{} per render
                    ForEach(SidebarSection.mainSections, id: \.self) { section in
                        sectionHeader(section, loc: loc)
                        ForEach(SidebarSection.tabsBySection[section] ?? []) { tab in
                            SidebarRow(tab: tab, isSelected: selectedTab == tab) {
                                withAnimation(LxAnimation.spring) {
                                    selectedTab = tab
                                }
                            }
                        }
                        Spacer().frame(height: 8)
                    }
                    
                    // D1: Collapsible Developer Tools section
                    devToolsSectionHeader
                    if devToolsExpanded {
                        ForEach(SidebarSection.tabsBySection[.devTools] ?? []) { tab in
                            SidebarRow(tab: tab, isSelected: selectedTab == tab) {
                                withAnimation(LxAnimation.spring) {
                                    selectedTab = tab
                                }
                            }
                        }
                    }
                }
                .padding(.horizontal, 10)
                .padding(.top, 4)
            }
            
            Spacer(minLength: 0)
            
            // D6: Theme toggle in sidebar footer
            themeToggleBar
            
            // Engine controls
            engineControlBar
        }
        .frame(width: 200)
        .background(
            // PERF P1: Removed VisualEffectBackground — sidebar blur was compositing every frame
            theme.sidebarBackground
        )
    }
    
    // D1: Collapsible dev tools header with chevron
    private var devToolsSectionHeader: some View {
        Button(action: {
            withAnimation(LxAnimation.snappy) {
                devToolsExpanded.toggle()
            }
        }) {
            HStack(spacing: 4) {
                Image(systemName: devToolsExpanded ? "chevron.down" : "chevron.right")
                    .font(.system(size: 7, weight: .bold))
                    .foregroundColor(SidebarSection.devTools.color.opacity(0.5))
                    .frame(width: 10)
                Rectangle()
                    .fill(SidebarSection.devTools.color.opacity(0.4))
                    .frame(width: 8, height: 0.5)
                Text(loc.t(SidebarSection.devTools.locKey))
                    .font(LxFont.mono(9, weight: .bold))
                    .foregroundColor(SidebarSection.devTools.color.opacity(0.5))
                    .tracking(1.5)
                Rectangle()
                    .fill(SidebarSection.devTools.color.opacity(0.15))
                    .frame(height: 0.5)
            }
            .padding(.vertical, 4)
            .padding(.horizontal, 4)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
    
    // D6: Visible theme toggle in sidebar footer
    private var themeToggleBar: some View {
        HStack(spacing: 2) {
            ForEach(AppearanceMode.allCases) { mode in
                Button(action: { themeManager.mode = mode }) {
                    Image(systemName: mode.icon)
                        .font(.system(size: 10, weight: themeManager.mode == mode ? .bold : .regular))
                        .foregroundColor(themeManager.mode == mode ? LxColor.electricCyan : theme.textTertiary)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 5)
                        .background(
                            RoundedRectangle(cornerRadius: 5)
                                .fill(themeManager.mode == mode ? LxColor.electricCyan.opacity(theme.accentTintOpacity) : Color.clear)
                        )
                }
                .buttonStyle(.plain)
                .help(loc.t(mode.locKey))
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 4)
    }
    
    // MARK: - Logo

    private var logoBar: some View {
        VStack(spacing: 4) {
            HStack(spacing: 8) {
                ZStack {
                    Circle()
                        .fill(LxColor.electricCyan.opacity(theme.accentTintOpacity))
                        .frame(width: 28, height: 28)
                    Image(systemName: "diamond.fill")
                        .font(.system(size: 13, weight: .bold))
                        .foregroundColor(LxColor.electricCyan)
                        .shadow(color: LxColor.electricCyan.opacity(0.5 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
                }
                VStack(alignment: .leading, spacing: 0) {
                    Text(loc.t("app.name"))
                        .font(LxFont.mono(14, weight: .bold))
                        .foregroundColor(theme.textPrimary)
                        .shadow(color: LxColor.electricCyan.opacity(0.2 * theme.glowOpacity), radius: 3 * theme.glowIntensity)
                    Text(loc.t("app.version"))
                        .font(LxFont.micro)
                        .foregroundColor(theme.textTertiary)
                }
                Spacer()
            }
            .padding(.horizontal, 14)
            .padding(.top, 24)
            .padding(.bottom, 10)
        }
    }
    
    // MARK: - Section Header
    
    private func sectionHeader(_ section: SidebarSection, loc: LocalizationManager) -> some View {
        HStack(spacing: 4) {
            Rectangle()
                .fill(section.color.opacity(0.4))
                .frame(width: 12, height: 0.5)
            Text(loc.t(section.locKey))
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(section.color.opacity(0.5))
                .tracking(1.5)
            Rectangle()
                .fill(section.color.opacity(0.15))
                .frame(height: 0.5)
        }
        .padding(.vertical, 4)
        .padding(.horizontal, 4)
    }
    
    // MARK: - Engine Controls
    
    private var engineControlBar: some View {
        VStack(spacing: 8) {
            Rectangle()
                .fill(theme.divider)
                .frame(height: 0.5)
            
            // Engine status
            EngineStatusBadge(status: engine.status)
            
            if engine.isReconnecting {
                GlassReconnectBadge()
            }
            
            // Control buttons
            HStack(spacing: 6) {
                if engine.status.isActive {
                    NeonButton(loc.t("common.stop"), icon: "stop.fill", color: LxColor.bloodRed) {
                        engine.stop()
                    }
                    NeonIconButton("exclamationmark.octagon.fill", color: LxColor.magentaPink, size: 26) {
                        engine.emergencyStop()
                    }
                } else {
                    NeonButton(loc.t("common.start"), icon: "play.fill", color: LxColor.neonLime) {
                        engine.start()
                    }
                    if engine.status == .error {
                        NeonButton(loc.t("common.restart"), icon: "arrow.counterclockwise", color: LxColor.amber) {
                            engine.restart()
                        }
                    }
                }
            }
            
            // Kill switch indicator
            if engine.killSwitch.isAnyHaltActive {
                GlassHaltBadge(reason: engine.killSwitch.primaryReason)
            }
            
            // Mode + Chaos badges
            HStack(spacing: 5) {
                TradingModeBadge(mode: engine.tradingMode)
                if engine.chaosState.enabled {
                    GlassChaosBadge()
                }
            }
            
            // D-A3: PnL sparkline
            if !engine.pnlHistory.isEmpty {
                PnlSparkline(data: engine.pnlHistory, width: 70, height: 18)
                    .padding(.top, 4)
            }
            
            Spacer().frame(height: 10)
        }
        .padding(.horizontal, 12)
    }
    
    // MARK: - D-A1: Command Palette Builder
    
    private func buildCommandList() -> [CommandItem] {
        var items: [CommandItem] = []
        
        // Navigation commands — one per sidebar tab
        let tabShortcuts: [SidebarTab: String] = [
            .dashboard: "⌘1", .orderbook: "⌘2", .trades: "⌘3", .portfolio: "⌘4",
            .strategyPerf: "⌘5", .riskCenter: "⌘6", .executionIntel: "⌘7",
            .systemMonitor: "⌘8", .settings: "⌘9"
        ]
        
        for tab in SidebarTab.allCases {
            items.append(CommandItem(
                title: loc.t(tab.locKey),
                subtitle: loc.t("palette.goTo"),
                icon: tab.icon,
                color: tab.neonColor,
                shortcut: tabShortcuts[tab]
            ) {
                withAnimation(LxAnimation.spring) { selectedTab = tab }
                if tab.section == .devTools { devToolsExpanded = true }
            })
        }
        
        // Engine actions
        if engine.status == .trading {
            items.append(CommandItem(
                title: loc.t("palette.stopEngine"),
                subtitle: loc.t("palette.engine"),
                icon: "stop.fill",
                color: LxColor.amber,
                shortcut: nil
            ) { engine.stop() })
            
            items.append(CommandItem(
                title: loc.t("palette.emergencyStop"),
                subtitle: loc.t("palette.engine"),
                icon: "exclamationmark.octagon.fill",
                color: LxColor.bloodRed,
                shortcut: "⌘E"
            ) { engine.emergencyStop() })
        } else {
            items.append(CommandItem(
                title: loc.t("palette.startEngine"),
                subtitle: loc.t("palette.engine"),
                icon: "play.fill",
                color: LxColor.neonLime,
                shortcut: nil
            ) {
                engine.start()
            })
        }
        
        // Theme commands
        for mode in AppearanceMode.allCases {
            items.append(CommandItem(
                title: loc.t(mode.locKey),
                subtitle: loc.t("palette.theme"),
                icon: mode.icon,
                color: LxColor.electricCyan,
                shortcut: nil
            ) { themeManager.mode = mode })
        }
        
        return items
    }
    
    // MARK: - D-A2: Keyboard Shortcuts
    
    private func handleKeyboardShortcutChar(_ key: String) -> Bool {
        // Cmd+K — toggle command palette
        if key == "k" {
            withAnimation(LxAnimation.micro) { showCommandPalette.toggle() }
            return true
        }
        
        // Cmd+E — emergency stop
        if key == "e" && engine.status == .trading {
            engine.emergencyStop()
            return true
        }
        
        // Cmd+1-9 — quick tab switch
        let quickTabs: [String: SidebarTab] = [
            "1": .dashboard, "2": .orderbook, "3": .trades, "4": .portfolio,
            "5": .strategyPerf, "6": .riskCenter, "7": .executionIntel,
            "8": .systemMonitor, "9": .settings
        ]
        if let tab = quickTabs[key] {
            withAnimation(LxAnimation.spring) { selectedTab = tab }
            return true
        }
        
        return false
    }
    
    // MARK: - Detail View
    // D-R1: 5 more merges — signals→dashboard, var→risk, orderFSM→execution,
    //       incidents→sysMon, allocIntel→research
    
    private var detailView: some View {
        Group {
            switch selectedTab {
            case .dashboard:       MergedDashboardView()
            case .orderbook:       OrderBookView()
            case .trades:          TradeTapeView()
            case .portfolio:       PortfolioView()
            case .strategyPerf:    StrategyPerformanceView()
            case .riskCenter:      MergedRiskCenterView()
            case .executionIntel:  MergedExecutionView()
            case .tradeReview:     TradeReviewStudioView()
            case .rlTraining:      RLTrainingView()
            case .modelGovernance: MergedModelGovernanceView()
            case .regimeCenter:    MarketRegimeCenterView()
            case .strategyResearch: MergedResearchView()
            case .systemMonitor:   MergedSystemMonitorView()
            case .settings:        SettingsView()
            case .chaosControl:    ChaosControlView()
            case .replayControl:   ReplayControlView()
            case .logs:            LogPanelView()
            }
        }
        .background(theme.backgroundPrimary)
    }
}

// MARK: - Sidebar Row

struct SidebarRow: View {
    let tab: ContentView.SidebarTab
    let isSelected: Bool
    let action: () -> Void
    
    @State private var isHovered = false
    @Environment(\.theme) var theme
    @EnvironmentObject var loc: LocalizationManager
    
    var body: some View {
        Button(action: action) {
            HStack(spacing: 8) {
                // Neon selection indicator
                RoundedRectangle(cornerRadius: 1)
                    .fill(isSelected ? tab.neonColor : Color.clear)
                    .frame(width: 2, height: 16)
                    .shadow(color: isSelected ? tab.neonColor.opacity(0.6 * theme.glowOpacity) : .clear, radius: 4 * theme.glowIntensity)
                
                Image(systemName: tab.icon)
                    .font(.system(size: 12, weight: isSelected ? .semibold : .regular))
                    .foregroundColor(isSelected ? tab.neonColor : (isHovered ? theme.textPrimary : theme.textSecondary))
                    .shadow(color: isSelected ? tab.neonColor.opacity(0.4 * theme.glowOpacity) : .clear, radius: 3 * theme.glowIntensity)
                    .frame(width: 18)
                
                Text(loc.t(tab.locKey))
                    .font(LxFont.sidebarItem)
                    .foregroundColor(isSelected ? theme.textPrimary : (isHovered ? theme.textPrimary : theme.textSecondary))
                    .lineLimit(1)
                
                Spacer()
            }
            .padding(.horizontal, 6)
            .padding(.vertical, 5)
            .background(
                Group {
                    if isSelected {
                        RoundedRectangle(cornerRadius: 7)
                            .fill(tab.neonColor.opacity(theme.accentTintOpacity))
                            .overlay(
                                RoundedRectangle(cornerRadius: 7)
                                    .stroke(tab.neonColor.opacity(theme.accentBorderOpacity * 0.8), lineWidth: 0.5)
                            )
                    } else if isHovered {
                        RoundedRectangle(cornerRadius: 7)
                            .fill(theme.hoverBackground)
                    }
                }
            )
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { hovering in
            withAnimation(LxAnimation.micro) {
                isHovered = hovering
            }
        }
    }
}

// MARK: - Glass Badges

struct GlassModeBadge: View {
    let isPaper: Bool
    @EnvironmentObject var loc: LocalizationManager
    
    var color: Color { isPaper ? LxColor.electricCyan : LxColor.bloodRed }
    
    var body: some View {
        StatusBadge(isPaper ? loc.t("common.paper") : loc.t("common.live"), color: color, pulse: !isPaper)
    }
}

struct GlassHaltBadge: View {
    let reason: String
    @EnvironmentObject var loc: LocalizationManager
    @State private var isPulsing = false
    @Environment(\.theme) var theme
    
    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: "exclamationmark.octagon.fill")
                .font(.system(size: 9))
                .scaleEffect(isPulsing ? 1.15 : 1.0)
                .animation(.easeInOut(duration: 0.8).repeatForever(autoreverses: true), value: isPulsing)
            Text(loc.t("risk.halt"))
                .font(LxFont.mono(9, weight: .bold))
        }
        .foregroundColor(LxColor.bloodRed)
        .padding(.horizontal, 7)
        .padding(.vertical, 3)
        .background(Capsule().fill(LxColor.bloodRed.opacity(0.12)))
        .overlay(Capsule().stroke(LxColor.bloodRed.opacity(0.3), lineWidth: 0.5))
        .shadow(color: LxColor.bloodRed.opacity(0.25 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
        .onAppear { isPulsing = true }
        .help(reason)
    }
}

struct GlassChaosBadge: View {
    @EnvironmentObject var loc: LocalizationManager
    @State private var isAnimating = false
    @Environment(\.theme) var theme
    
    var body: some View {
        HStack(spacing: 3) {
            Image(systemName: "tornado")
                .rotationEffect(.degrees(isAnimating ? 15 : -15))
                .animation(.easeInOut(duration: 0.5).repeatForever(autoreverses: true), value: isAnimating)
            Text(loc.t("engine.chaos"))
        }
        .font(LxFont.mono(9, weight: .bold))
        .foregroundColor(LxColor.bloodRed)
        .padding(.horizontal, 7)
        .padding(.vertical, 3)
        .background(
            Capsule().fill(LxColor.bloodRed.opacity(0.1))
        )
        .overlay(
            Capsule().stroke(LxColor.bloodRed.opacity(0.25), lineWidth: 0.5)
        )
        .shadow(color: LxColor.bloodRed.opacity(0.2 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
        .onAppear { isAnimating = true }
    }
}

struct GlassReconnectBadge: View {
    @EnvironmentObject var loc: LocalizationManager
    @State private var isAnimating = false
    @Environment(\.theme) var theme
    
    var body: some View {
        HStack(spacing: 4) {
            Image(systemName: "arrow.triangle.2.circlepath")
                .rotationEffect(.degrees(isAnimating ? 360 : 0))
                .animation(.linear(duration: 1.5).repeatForever(autoreverses: false), value: isAnimating)
            Text(loc.t("engine.reconnecting"))
        }
        .font(LxFont.mono(9, weight: .bold))
        .foregroundColor(LxColor.amber)
        .padding(.horizontal, 7)
        .padding(.vertical, 3)
        .background(Capsule().fill(LxColor.amber.opacity(0.1)))
        .overlay(Capsule().stroke(LxColor.amber.opacity(0.2), lineWidth: 0.5))
        .shadow(color: LxColor.amber.opacity(0.15 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
        .onAppear { isAnimating = true }
    }
}
