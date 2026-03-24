// SettingsView.swift — Glassmorphism 2026 Settings (Lynrix v2.5)

import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @EnvironmentObject var themeManager: ThemeManager
    @Environment(\.theme) var theme
    
    @State private var apiKey: String = ""
    @State private var apiSecret: String = ""
    @State private var showSecret: Bool = false
    @State private var keySaved: Bool = false
    @State private var showDeleteConfirm: Bool = false
    @State private var showLiveConfirm: Bool = false
    @State private var showSignOutConfirm: Bool = false
    @ObservedObject private var auth = AuthManager.shared
    @State private var settingsApplied: Bool = false
    @State private var profileName: String = ""
    @State private var showSaveProfile: Bool = false
    @ObservedObject private var profileMgr = ProfileManager.shared
    @ObservedObject private var notifyMgr = NotificationManager.shared
    
    // D-A5: Settings restructure — tab-based navigation
    enum SettingsSection: String, CaseIterable, Identifiable {
        case general, trading, ai, preferences
        var id: String { rawValue }
        
        var icon: String {
            switch self {
            case .general:     return "person.crop.circle"
            case .trading:     return "chart.line.uptrend.xyaxis"
            case .ai:          return "brain"
            case .preferences: return "gearshape.2"
            }
        }
        
        var locKey: String {
            switch self {
            case .general:     return "settings.section.general"
            case .trading:     return "settings.section.trading"
            case .ai:          return "settings.section.ai"
            case .preferences: return "settings.section.prefs"
            }
        }
    }
    @State private var selectedSection: SettingsSection = .general
    
    @State private var symbol: String = "BTCUSDT"
    @State private var orderQty: String = "0.001"
    @State private var signalThreshold: String = "0.6"
    @State private var entryOffsetBps: String = "1.0"
    @State private var maxPositionSize: String = "0.1"
    @State private var maxLeverage: String = "10.0"
    @State private var maxDailyLoss: String = "500.0"
    @State private var maxDrawdown: String = "0.1"
    @State private var maxOrdersPerSec: String = "5"
    @State private var obLevels: String = "500"
    @State private var ioThreads: String = "2"
    
    // AI Edition
    @State private var mlModelEnabled: Bool = true
    @State private var adaptiveThresholdEnabled: Bool = true
    @State private var regimeDetectionEnabled: Bool = true
    @State private var requoteEnabled: Bool = true
    @State private var adaptiveSizingEnabled: Bool = true
    @State private var cbEnabled: Bool = true
    @State private var cbLossThreshold: String = "200.0"
    @State private var cbDrawdownThreshold: String = "0.05"
    @State private var cbConsecutiveLosses: String = "10"
    @State private var cbCooldownSec: String = "300"
    @State private var featureTickMs: String = "10"
    @State private var onnxEnabled: Bool = false
    @State private var onnxModelPath: String = ""
    @State private var onnxIntraThreads: String = "4"
    
    var body: some View {
        VStack(spacing: 0) {
            // D-A5: Section picker
            HStack(spacing: 2) {
                ForEach(SettingsSection.allCases) { section in
                    Button(action: {
                        withAnimation(.easeInOut(duration: 0.15)) { selectedSection = section }
                    }) {
                        HStack(spacing: 5) {
                            Image(systemName: section.icon)
                                .font(.system(size: 11))
                            Text(loc.t(section.locKey))
                                .font(LxFont.mono(11, weight: selectedSection == section ? .bold : .medium))
                        }
                        .padding(.horizontal, 12)
                        .padding(.vertical, 7)
                        .background(
                            RoundedRectangle(cornerRadius: 8)
                                .fill(selectedSection == section ? LxColor.electricCyan.opacity(0.10) : Color.clear)
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(selectedSection == section ? LxColor.electricCyan.opacity(0.25) : Color.clear, lineWidth: 0.5)
                        )
                        .foregroundColor(selectedSection == section ? LxColor.electricCyan : theme.textTertiary)
                    }
                    .buttonStyle(.plain)
                }
                Spacer()
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 8)
            
            Divider().background(theme.borderSubtle)
            
            // Section content
            ScrollView {
                VStack(spacing: 14) {
                    switch selectedSection {
                    case .general:     generalSection
                    case .trading:     tradingSection
                    case .ai:          aiSection
                    case .preferences: preferencesSection
                    }
                    
                    // Config Validation Warnings (always visible)
                    configValidationPanel
                    
                    // Apply button
                    applyButtonBar
                }
                .padding(16)
            }
        }
        .background(theme.backgroundPrimary)
        .onAppear { loadCurrentConfig() }
        .onReceive(engine.$config.receive(on: RunLoop.main)) { _ in
            loadCurrentConfig()
        }
    }
    
    // MARK: - D-A5: General Section
    
    private var generalSection: some View {
        VStack(spacing: 14) {
            accountSection
            apiCredentialsPanel
            tradingModePanel
            profilesPanel
            aboutPanel
        }
    }
    
    // MARK: - D-A5: Trading Section
    
    private var tradingSection: some View {
        VStack(spacing: 14) {
            tradingParamsPanel
            riskParamsPanel
            circuitBreakerPanel
        }
    }
    
    // MARK: - D-A5: AI Section
    
    private var aiSection: some View {
        VStack(spacing: 14) {
            aiSettingsPanel
        }
    }
    
    // MARK: - D-A5: Preferences Section
    
    private var preferencesSection: some View {
        VStack(spacing: 14) {
            languagePanel
            appearancePanel
            notificationsPanel
        }
    }
    
    // MARK: - Apply Button Bar
    
    private var applyButtonBar: some View {
        HStack {
            Spacer()
            NeonButton(loc.t("settings.applySettings"), icon: "checkmark.circle", color: LxColor.neonLime) {
                applySettings()
                settingsApplied = true
                ToastManager.shared.show(.success, title: loc.t("settings.applied"))
                DispatchQueue.main.asyncAfter(deadline: .now() + 3) { settingsApplied = false }
            }
            .disabled(engine.status.isActive)
            
            if settingsApplied {
                StatusBadge(loc.t("settings.applied"), color: LxColor.neonLime)
            }
            
            if engine.status.isActive {
                Text(loc.t("settings.stopEngineWarning"))
                    .font(LxFont.label)
                    .foregroundColor(theme.textTertiary)
            }
        }
    }
    
    // MARK: - Panel Extractors
    
    private var apiCredentialsPanel: some View {
        GlassPanel(neon: LxColor.gold.opacity(1.0)) {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Image(systemName: "key.fill")
                                .foregroundColor(LxColor.gold.opacity(1.0))
                                .shadow(color: LxColor.gold.opacity(0.4), radius: 3)
                            Text(loc.t("settings.apiKeys"))
                                .font(LxFont.mono(14, weight: .bold))
                                .foregroundColor(theme.textPrimary)
                            Spacer()
                            if KeychainManager.shared.hasCredentials {
                                StatusBadge(loc.t("settings.keychain"), color: LxColor.neonLime)
                            }
                        }
                        
                        Text(loc.t("settings.keychainNote"))
                            .font(LxFont.label)
                            .foregroundColor(theme.textTertiary)
                        
                        HStack(spacing: 12) {
                            VStack(alignment: .leading, spacing: 4) {
                                Text(loc.t("settings.apiKey")).font(LxFont.micro).foregroundColor(theme.textTertiary)
                                SecureField(loc.t("settings.apiKey"), text: $apiKey)
                                    .textFieldStyle(.roundedBorder)
                                    .font(LxFont.mono(12))
                            }
                            VStack(alignment: .leading, spacing: 4) {
                                Text(loc.t("settings.apiSecret")).font(LxFont.micro).foregroundColor(theme.textTertiary)
                                if showSecret {
                                    TextField(loc.t("settings.apiSecret"), text: $apiSecret)
                                        .textFieldStyle(.roundedBorder)
                                        .font(LxFont.mono(12))
                                } else {
                                    SecureField(loc.t("settings.apiSecret"), text: $apiSecret)
                                        .textFieldStyle(.roundedBorder)
                                        .font(LxFont.mono(12))
                                }
                            }
                        }
                        
                        HStack {
                            NeonButton(loc.t("settings.saveToKeychain"), icon: "lock.fill", color: LxColor.gold) {
                                saveCredentials()
                            }
                            .disabled(apiKey.isEmpty || apiSecret.isEmpty)
                            
                            Toggle(loc.t("settings.showKey"), isOn: $showSecret)
                                .toggleStyle(.switch)
                                .controlSize(.small)
                                .foregroundColor(theme.textSecondary)
                            
                            Spacer()
                            
                            if keySaved {
                                StatusBadge(loc.t("common.save"), color: LxColor.neonLime)
                            }
                            
                            Button(role: .destructive, action: { showDeleteConfirm = true }) {
                                Image(systemName: "trash")
                            }
                            .foregroundColor(LxColor.bloodRed)
                            .confirmationDialog(loc.t("settings.deleteKeys"), isPresented: $showDeleteConfirm) {
                                Button(loc.t("settings.delete"), role: .destructive) {
                                    KeychainManager.shared.deleteCredentials()
                                    apiKey = ""
                                    apiSecret = ""
                                    keySaved = false
                                }
                            }
                        }
                    }
                }
                
    }
    
    private var tradingModePanel: some View {
        GlassPanel(neon: engine.paperMode ? LxColor.electricCyan : LxColor.bloodRed) {
            VStack(alignment: .leading, spacing: 12) {
                GlassSectionHeader(loc.t("settings.tradingMode"), icon: "arrow.triangle.swap",
                                   color: engine.paperMode ? LxColor.electricCyan : LxColor.bloodRed)
                HStack(spacing: 16) {
                    VStack(alignment: .leading) {
                        GlowText(engine.paperMode ? loc.t("settings.paperMode") : loc.t("common.live"),
                                 font: LxFont.mono(16, weight: .bold),
                                 color: engine.paperMode ? LxColor.electricCyan : LxColor.bloodRed, glow: 4)
                        Text(engine.paperMode ? loc.t("settings.paperDescription") : loc.t("settings.liveWarning"))
                            .font(LxFont.label).foregroundColor(theme.textTertiary)
                    }
                    Spacer()
                    NeonButton(engine.paperMode ? loc.t("settings.toLive") : loc.t("settings.toPaper"),
                               color: engine.paperMode ? LxColor.bloodRed : LxColor.electricCyan) {
                        if engine.paperMode { showLiveConfirm = true } else { engine.togglePaperMode() }
                    }
                    .confirmationDialog(loc.t("settings.switchToLive"), isPresented: $showLiveConfirm, titleVisibility: .visible) {
                        Button(loc.t("settings.toLive"), role: .destructive) { engine.togglePaperMode() }
                    } message: { Text(loc.t("settings.liveWarning")) }
                }
            }
        }
    }
    
    private var tradingParamsPanel: some View {
        GlassPanel(neon: LxColor.magentaPink) {
            VStack(alignment: .leading, spacing: 12) {
                GlassSectionHeader(loc.t("settings.tradingParams"), icon: "slider.horizontal.3", color: LxColor.magentaPink)
                LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                    SettingsField(label: loc.t("settings.symbol"), value: $symbol)
                    SettingsField(label: loc.t("settings.orderQty"), value: $orderQty)
                    SettingsField(label: loc.t("settings.signalThreshold"), value: $signalThreshold)
                    SettingsField(label: loc.t("settings.entryOffset"), value: $entryOffsetBps)
                    SettingsField(label: loc.t("settings.obLevels"), value: $obLevels)
                    SettingsField(label: loc.t("settings.ioThreads"), value: $ioThreads)
                }
            }
        }
    }
    
    private var riskParamsPanel: some View {
        GlassPanel(neon: LxColor.amber) {
            VStack(alignment: .leading, spacing: 12) {
                GlassSectionHeader(loc.t("settings.riskLimits"), icon: "shield.lefthalf.filled", color: LxColor.amber)
                LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                    SettingsField(label: loc.t("settings.maxPosition"), value: $maxPositionSize)
                    SettingsField(label: loc.t("settings.maxLeverage"), value: $maxLeverage)
                    SettingsField(label: loc.t("settings.maxLossDay"), value: $maxDailyLoss)
                    SettingsField(label: loc.t("settings.maxDrawdown"), value: $maxDrawdown)
                    SettingsField(label: loc.t("settings.maxOrdersSec"), value: $maxOrdersPerSec)
                }
            }
        }
    }
    
    private var aiSettingsPanel: some View {
        VStack(spacing: 14) {
            GlassPanel(neon: LxColor.electricCyan) {
                VStack(alignment: .leading, spacing: 12) {
                    GlassSectionHeader(loc.t("settings.aiSettings"), icon: "brain", color: LxColor.electricCyan)
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                        Toggle(loc.t("settings.mlModel"), isOn: $mlModelEnabled).toggleStyle(.switch).foregroundColor(theme.textSecondary)
                        Toggle(loc.t("settings.adaptiveThreshold"), isOn: $adaptiveThresholdEnabled).toggleStyle(.switch).foregroundColor(theme.textSecondary)
                        Toggle(loc.t("settings.regimeDetection"), isOn: $regimeDetectionEnabled).toggleStyle(.switch).foregroundColor(theme.textSecondary)
                        Toggle(loc.t("settings.requoting"), isOn: $requoteEnabled).toggleStyle(.switch).foregroundColor(theme.textSecondary)
                        Toggle(loc.t("settings.adaptiveSizing"), isOn: $adaptiveSizingEnabled).toggleStyle(.switch).foregroundColor(theme.textSecondary)
                        SettingsField(label: loc.t("settings.featureTick"), value: $featureTickMs)
                    }
                    Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                    Text(loc.t("settings.onnxRuntime")).font(LxFont.mono(11, weight: .bold)).foregroundColor(theme.textSecondary)
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                        Toggle(loc.t("settings.onnxModel"), isOn: $onnxEnabled).toggleStyle(.switch).foregroundColor(theme.textSecondary)
                        SettingsField(label: loc.t("settings.onnxPath"), value: $onnxModelPath)
                        SettingsField(label: loc.t("settings.onnxThreads"), value: $onnxIntraThreads)
                    }
                }
            }
            circuitBreakerPanel
        }
    }
    
    private var circuitBreakerPanel: some View {
        GlassPanel(neon: LxColor.bloodRed) {
            VStack(alignment: .leading, spacing: 12) {
                HStack {
                    GlassSectionHeader(loc.t("settings.circuitBreaker"), icon: "exclamationmark.triangle.fill", color: LxColor.bloodRed)
                    Spacer()
                    Toggle(loc.t("common.enabled"), isOn: $cbEnabled).toggleStyle(.switch).labelsHidden()
                }
                if cbEnabled {
                    LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                        SettingsField(label: loc.t("settings.lossThreshold"), value: $cbLossThreshold)
                        SettingsField(label: loc.t("settings.cbMaxDrawdown"), value: $cbDrawdownThreshold)
                        SettingsField(label: loc.t("settings.cbMaxConsecLosses"), value: $cbConsecutiveLosses)
                        SettingsField(label: loc.t("settings.cbCooldown"), value: $cbCooldownSec)
                    }
                }
            }
        }
    }
    
    private var languagePanel: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 12) {
                GlassSectionHeader(loc.t("settings.language"), icon: "globe", color: LxColor.electricCyan)
                Text(loc.t("settings.languageNote")).font(LxFont.label).foregroundColor(theme.textTertiary)
                HStack(spacing: 8) {
                    ForEach(LocalizationManager.Language.allCases) { lang in
                        Button(action: { loc.currentLanguage = lang }) {
                            HStack(spacing: 4) {
                                Text(lang.flag)
                                Text(lang.displayName).font(LxFont.mono(11, weight: loc.currentLanguage == lang ? .bold : .regular))
                            }
                            .padding(.horizontal, 10).padding(.vertical, 6)
                            .background(RoundedRectangle(cornerRadius: 8).fill(loc.currentLanguage == lang ? LxColor.electricCyan.opacity(0.12) : Color.clear))
                            .overlay(RoundedRectangle(cornerRadius: 8).stroke(loc.currentLanguage == lang ? LxColor.electricCyan.opacity(0.3) : theme.borderSubtle, lineWidth: 0.5))
                            .foregroundColor(loc.currentLanguage == lang ? LxColor.electricCyan : theme.textSecondary)
                        }
                        .buttonStyle(.plain)
                    }
                    Spacer()
                }
            }
        }
    }
    
    private var appearancePanel: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 12) {
                GlassSectionHeader(loc.t("settings.appearance"), icon: "sun.max.fill", color: LxColor.electricCyan)
                Text(loc.t("settings.appearanceNote")).font(LxFont.label).foregroundColor(theme.textTertiary)
                HStack(spacing: 8) {
                    ForEach(AppearanceMode.allCases) { mode in
                        Button(action: { themeManager.mode = mode }) {
                            HStack(spacing: 4) {
                                Image(systemName: mode.icon).font(.system(size: 11))
                                Text(loc.t(mode.locKey)).font(LxFont.mono(11, weight: themeManager.mode == mode ? .bold : .regular))
                            }
                            .padding(.horizontal, 10).padding(.vertical, 6)
                            .background(RoundedRectangle(cornerRadius: 8).fill(themeManager.mode == mode ? LxColor.electricCyan.opacity(0.12) : Color.clear))
                            .overlay(RoundedRectangle(cornerRadius: 8).stroke(themeManager.mode == mode ? LxColor.electricCyan.opacity(0.3) : theme.borderSubtle, lineWidth: 0.5))
                            .foregroundColor(themeManager.mode == mode ? LxColor.electricCyan : theme.textSecondary)
                        }
                        .buttonStyle(.plain)
                    }
                    Spacer()
                }
            }
        }
    }
    
    private var profilesPanel: some View {
        GlassPanel(neon: LxColor.gold) {
            VStack(alignment: .leading, spacing: 12) {
                HStack {
                    GlassSectionHeader(loc.t("profiles.title"), icon: "slider.horizontal.2.square", color: LxColor.gold)
                    Spacer()
                    if !profileMgr.activeProfileName.isEmpty { StatusBadge(profileMgr.activeProfileName, color: LxColor.gold) }
                }
                Text(loc.t("profiles.description")).font(LxFont.label).foregroundColor(theme.textTertiary)
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 8) {
                        ForEach(profileMgr.allProfiles) { profile in
                            Button(action: { profileMgr.applyProfile(profile, to: engine); loadCurrentConfig() }) {
                                VStack(spacing: 4) {
                                    Text(loc.t("profiles.\(profile.name.lowercased())"))
                                        .font(LxFont.mono(11, weight: profileMgr.activeProfileName == profile.name ? .bold : .regular))
                                    if profile.isBuiltIn {
                                        Text(loc.t("profiles.preset")).font(LxFont.micro).foregroundColor(theme.textTertiary)
                                    }
                                }
                                .padding(.horizontal, 12).padding(.vertical, 8)
                                .background(RoundedRectangle(cornerRadius: 8).fill(profileMgr.activeProfileName == profile.name ? LxColor.gold.opacity(0.12) : Color.clear))
                                .overlay(RoundedRectangle(cornerRadius: 8).stroke(profileMgr.activeProfileName == profile.name ? LxColor.gold.opacity(0.3) : theme.borderSubtle, lineWidth: 0.5))
                                .foregroundColor(profileMgr.activeProfileName == profile.name ? LxColor.gold : theme.textSecondary)
                            }
                            .buttonStyle(.plain).disabled(engine.status.isActive)
                        }
                    }
                }
                HStack(spacing: 8) {
                    NeonButton(loc.t("profiles.saveCurrent"), icon: "square.and.arrow.down", color: LxColor.gold) { showSaveProfile = true }
                        .disabled(engine.status.isActive)
                }
                .sheet(isPresented: $showSaveProfile) { saveProfileSheet }
            }
        }
    }
    
    private var notificationsPanel: some View {
        GlassPanel(neon: LxColor.amber) {
            VStack(alignment: .leading, spacing: 12) {
                HStack {
                    GlassSectionHeader(loc.t("notify.title"), icon: "bell.badge", color: LxColor.amber)
                    Spacer()
                    Toggle("", isOn: $notifyMgr.isEnabled).toggleStyle(.switch).labelsHidden()
                }
                if notifyMgr.isEnabled {
                    if !notifyMgr.isAuthorized {
                        HStack(spacing: 6) {
                            Image(systemName: "exclamationmark.triangle").font(.system(size: 10)).foregroundColor(LxColor.amber)
                            Text(loc.t("notify.notAuthorized")).font(LxFont.label).foregroundColor(LxColor.amber)
                            NeonButton(loc.t("notify.authorize"), color: LxColor.amber) { notifyMgr.requestAuthorization() }
                        }
                    }
                    HStack(spacing: 4) {
                        Text(loc.t("notify.minSeverity")).font(LxFont.mono(11)).foregroundColor(theme.textSecondary)
                        Spacer()
                        ForEach(NotificationManager.MinSeverity.allCases) { sev in
                            Button(action: { notifyMgr.minSeverity = sev }) {
                                Text(loc.t(sev.locKey))
                                    .font(LxFont.mono(10, weight: notifyMgr.minSeverity == sev ? .bold : .regular))
                                    .padding(.horizontal, 10).padding(.vertical, 5)
                                    .background(RoundedRectangle(cornerRadius: 6).fill(notifyMgr.minSeverity == sev ? LxColor.amber.opacity(0.12) : Color.clear))
                                    .overlay(RoundedRectangle(cornerRadius: 6).stroke(notifyMgr.minSeverity == sev ? LxColor.amber.opacity(0.3) : theme.borderSubtle, lineWidth: 0.5))
                                    .foregroundColor(notifyMgr.minSeverity == sev ? LxColor.amber : theme.textTertiary)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
            }
        }
    }
    
    private var aboutPanel: some View {
        GlassPanel(neon: LxColor.coolSteel) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("menu.about"), icon: "info.circle", color: LxColor.coolSteel)
                HStack(spacing: 16) {
                    VStack(alignment: .leading, spacing: 4) {
                        Text(loc.t("app.name")).font(LxFont.mono(16, weight: .bold)).foregroundColor(theme.textPrimary)
                        Text(loc.t("app.version") + " • " + loc.t("app.about.build")).font(LxFont.mono(11)).foregroundColor(theme.textSecondary)
                        Text(loc.t("app.copyright")).font(LxFont.micro).foregroundColor(theme.textTertiary)
                    }
                    Spacer()
                    VStack(alignment: .trailing, spacing: 4) {
                        Text("macOS \(ProcessInfo.processInfo.operatingSystemVersionString)").font(LxFont.micro).foregroundColor(theme.textTertiary)
                        Text(loc.t("settings.techStack")).font(LxFont.micro).foregroundColor(theme.textTertiary)
                    }
                }
                HStack {
                    NeonButton(loc.t("onboarding.reopenSetup"), icon: "arrow.counterclockwise", color: LxColor.coolSteel) {
                        OnboardingManager.shared.resetOnboarding()
                    }
                }
            }
        }
    }
    
    // MARK: - Config Validation Panel
    
    private var configValidationPanel: some View {
        let result = previewValidation()
        return Group {
            if !result.issues.isEmpty {
                GlassPanel(neon: result.hasErrors ? LxColor.bloodRed : LxColor.amber) {
                    VStack(alignment: .leading, spacing: 8) {
                        GlassSectionHeader(
                            loc.t("validation.title"),
                            icon: result.hasErrors ? "xmark.octagon" : "exclamationmark.triangle",
                            color: result.hasErrors ? LxColor.bloodRed : LxColor.amber
                        )
                        ForEach(result.issues) { issue in
                            HStack(spacing: 6) {
                                Image(systemName: issue.severity.icon)
                                    .font(.system(size: 10))
                                    .foregroundColor(issue.severity.color)
                                Text(loc.t(issue.messageKey))
                                    .font(LxFont.mono(10))
                                    .foregroundColor(issue.severity.color)
                                Spacer()
                                Text(issue.field)
                                    .font(LxFont.micro)
                                    .foregroundColor(theme.textTertiary)
                            }
                        }
                    }
                }
            }
        }
    }
    
    private func previewValidation() -> ConfigValidationResult {
        var c = engine.config
        c.symbol = symbol
        c.orderQty = Double(orderQty) ?? c.orderQty
        c.signalThreshold = Double(signalThreshold) ?? c.signalThreshold
        c.maxPositionSize = Double(maxPositionSize) ?? c.maxPositionSize
        c.maxLeverage = Double(maxLeverage) ?? c.maxLeverage
        c.maxDailyLoss = Double(maxDailyLoss) ?? c.maxDailyLoss
        c.maxDrawdown = Double(maxDrawdown) ?? c.maxDrawdown
        c.maxOrdersPerSec = Int(maxOrdersPerSec) ?? c.maxOrdersPerSec
        c.cbEnabled = cbEnabled
        return ConfigValidator.validate(c, mode: engine.tradingMode, hasCredentials: KeychainManager.shared.hasCredentials)
    }
    
    private func loadCurrentConfig() {
        let c = engine.config
        symbol = c.symbol
        orderQty = String(c.orderQty)
        signalThreshold = String(c.signalThreshold)
        entryOffsetBps = String(c.entryOffsetBps)
        maxPositionSize = String(c.maxPositionSize)
        maxLeverage = String(c.maxLeverage)
        maxDailyLoss = String(c.maxDailyLoss)
        maxDrawdown = String(c.maxDrawdown)
        maxOrdersPerSec = String(c.maxOrdersPerSec)
        obLevels = String(c.obLevels)
        ioThreads = String(c.ioThreads)
        mlModelEnabled = c.mlModelEnabled
        adaptiveThresholdEnabled = c.adaptiveThresholdEnabled
        regimeDetectionEnabled = c.regimeDetectionEnabled
        requoteEnabled = c.requoteEnabled
        adaptiveSizingEnabled = c.adaptiveSizingEnabled
        cbEnabled = c.cbEnabled
        cbLossThreshold = String(c.cbLossThreshold)
        cbDrawdownThreshold = String(c.cbDrawdownThreshold)
        cbConsecutiveLosses = String(c.cbConsecutiveLosses)
        cbCooldownSec = String(c.cbCooldownSec)
        featureTickMs = String(c.featureTickMs)
        onnxEnabled = c.onnxEnabled
        onnxModelPath = c.onnxModelPath
        onnxIntraThreads = String(c.onnxIntraThreads)
        if KeychainManager.shared.hasCredentials {
            apiKey = KeychainManager.shared.loadAPIKey() ?? ""
            apiSecret = KeychainManager.shared.loadAPISecret() ?? ""
        }
    }
    
    private func saveCredentials() {
        do {
            try KeychainManager.shared.saveAPIKey(apiKey)
            try KeychainManager.shared.saveAPISecret(apiSecret)
            keySaved = true
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { keySaved = false }
        } catch {
            engine.addLog(.error, "Failed to save credentials: \(error.localizedDescription)")
        }
    }
    
    private func applySettings() {
        var c = engine.config
        c.symbol = symbol
        c.orderQty = Double(orderQty) ?? c.orderQty
        c.signalThreshold = Double(signalThreshold) ?? c.signalThreshold
        c.entryOffsetBps = Double(entryOffsetBps) ?? c.entryOffsetBps
        c.maxPositionSize = Double(maxPositionSize) ?? c.maxPositionSize
        c.maxLeverage = Double(maxLeverage) ?? c.maxLeverage
        c.maxDailyLoss = Double(maxDailyLoss) ?? c.maxDailyLoss
        c.maxDrawdown = Double(maxDrawdown) ?? c.maxDrawdown
        c.maxOrdersPerSec = Int(maxOrdersPerSec) ?? c.maxOrdersPerSec
        c.obLevels = Int(obLevels) ?? c.obLevels
        c.ioThreads = Int(ioThreads) ?? c.ioThreads
        c.mlModelEnabled = mlModelEnabled
        c.adaptiveThresholdEnabled = adaptiveThresholdEnabled
        c.regimeDetectionEnabled = regimeDetectionEnabled
        c.requoteEnabled = requoteEnabled
        c.adaptiveSizingEnabled = adaptiveSizingEnabled
        c.cbEnabled = cbEnabled
        c.cbLossThreshold = Double(cbLossThreshold) ?? c.cbLossThreshold
        c.cbDrawdownThreshold = Double(cbDrawdownThreshold) ?? c.cbDrawdownThreshold
        c.cbConsecutiveLosses = Int(cbConsecutiveLosses) ?? c.cbConsecutiveLosses
        c.cbCooldownSec = Int(cbCooldownSec) ?? c.cbCooldownSec
        c.featureTickMs = Int(featureTickMs) ?? c.featureTickMs
        c.onnxEnabled = onnxEnabled
        c.onnxModelPath = onnxModelPath
        c.onnxIntraThreads = Int(onnxIntraThreads) ?? c.onnxIntraThreads
        engine.config = c
        
        let validation = engine.validateConfig()
        if validation.hasErrors {
            engine.addLog(.warn, "Settings applied with \(validation.errors.count) error(s)")
        } else if validation.hasWarnings {
            engine.addLog(.warn, "Settings applied with \(validation.warnings.count) warning(s)")
        } else {
            engine.addLog(.info, "Settings applied (v2.5)")
        }
        IncidentStore.shared.record(
            severity: .info,
            category: .configChange,
            titleKey: "incident.configApplied",
            detail: "Symbol: \(c.symbol), Qty: \(c.orderQty), Mode: \(engine.tradingMode.rawValue)"
        )
    }
    
    // MARK: - Save Profile Sheet
    
    private var saveProfileSheet: some View {
        VStack(spacing: 16) {
            Text(loc.t("profiles.saveTitle"))
                .font(LxFont.mono(16, weight: .bold))
                .foregroundColor(theme.textPrimary)
            TextField(loc.t("profiles.nameField"), text: $profileName)
                .textFieldStyle(.roundedBorder)
                .font(LxFont.mono(12))
                .frame(width: 250)
            HStack(spacing: 12) {
                Button(loc.t("common.cancel")) { showSaveProfile = false }
                    .keyboardShortcut(.cancelAction)
                NeonButton(loc.t("common.save"), icon: "checkmark", color: LxColor.gold) {
                    profileMgr.saveProfile(name: profileName, config: engine.config)
                    profileName = ""
                    showSaveProfile = false
                }
                .disabled(profileName.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
        .padding(24)
        .frame(width: 320)
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - Account Section
    
    private var accountSection: some View {
        GlassPanel(neon: LxColor.electricCyan.opacity(1.0)) {
            VStack(alignment: .leading, spacing: 12) {
                HStack {
                    Image(systemName: "person.circle.fill")
                        .foregroundColor(LxColor.electricCyan)
                        .shadow(color: LxColor.electricCyan.opacity(0.4), radius: 3)
                    Text(loc.t("account.title"))
                        .font(LxFont.mono(14, weight: .bold))
                        .foregroundColor(theme.textPrimary)
                    Spacer()
                    if let profile = auth.currentProfile {
                        StatusBadge(
                            loc.t(profile.plan.locKey),
                            color: LxColor.neonLime
                        )
                    }
                }
                
                if let profile = auth.currentProfile {
                    HStack(spacing: 14) {
                        // Avatar / Initials / Photo
                        ZStack {
                            Circle()
                                .fill(LxColor.electricCyan.opacity(0.1))
                                .frame(width: 44, height: 44)
                            Circle()
                                .stroke(LxColor.electricCyan.opacity(0.2), lineWidth: 0.5)
                                .frame(width: 44, height: 44)
                            
                            if let photoPath = profile.profilePhotoPath,
                               let img = NSImage(contentsOfFile: photoPath) {
                                Image(nsImage: img)
                                    .resizable()
                                    .scaledToFill()
                                    .frame(width: 44, height: 44)
                                    .clipShape(Circle())
                            } else {
                                Text(profile.initials)
                                    .font(LxFont.mono(14, weight: .bold))
                                    .foregroundColor(LxColor.electricCyan)
                            }
                        }
                        
                        VStack(alignment: .leading, spacing: 3) {
                            Text(profile.displayName)
                                .font(LxFont.mono(13, weight: .semibold))
                                .foregroundColor(theme.textPrimary)
                            Text(profile.provider == .email ? (profile.username ?? profile.email) : profile.email)
                                .font(LxFont.mono(11))
                                .foregroundColor(theme.textSecondary)
                            HStack(spacing: 8) {
                                Label(profile.provider.displayName, systemImage: profile.provider.icon)
                                    .font(LxFont.mono(9))
                                    .foregroundColor(theme.textTertiary)
                                Text("•")
                                    .foregroundColor(theme.textTertiary)
                                Text(loc.t(profile.subscriptionState.locKey))
                                    .font(LxFont.mono(9))
                                    .foregroundColor(profile.subscriptionState.isValid ? LxColor.neonLime : theme.textTertiary)
                            }
                        }
                        
                        Spacer()
                        
                        Button(action: { showSignOutConfirm = true }) {
                            HStack(spacing: 4) {
                                Image(systemName: "rectangle.portrait.and.arrow.right")
                                    .font(.system(size: 10))
                                Text(loc.t("account.signOut"))
                                    .font(LxFont.mono(10, weight: .medium))
                            }
                            .foregroundColor(LxColor.bloodRed.opacity(0.8))
                            .padding(.horizontal, 10)
                            .padding(.vertical, 6)
                            .background(
                                RoundedRectangle(cornerRadius: 6)
                                    .fill(LxColor.bloodRed.opacity(0.08))
                            )
                        }
                        .buttonStyle(.plain)
                        .confirmationDialog(loc.t("account.signOutConfirm"), isPresented: $showSignOutConfirm, titleVisibility: .visible) {
                            Button(loc.t("account.signOut"), role: .destructive) {
                                auth.signOut()
                            }
                        }
                    }
                    
                    HStack(spacing: 16) {
                        accountInfoItem(loc.t("account.plan"), loc.t(profile.plan.locKey),
                                       color: profile.isPremium ? LxColor.gold : theme.textSecondary)
                        accountInfoItem(loc.t("account.status"), loc.t(profile.subscriptionState.locKey),
                                       color: profile.subscriptionState.isValid ? LxColor.neonLime : LxColor.amber)
                        accountInfoItem(loc.t("account.provider"), profile.provider.displayName,
                                       color: theme.textSecondary)
                    }
                    .padding(.top, 4)
                } else {
                    HStack {
                        Text(loc.t("account.notSignedIn"))
                            .font(LxFont.mono(12))
                            .foregroundColor(theme.textTertiary)
                        Spacer()
                    }
                }
            }
        }
    }
    
    private func accountInfoItem(_ label: String, _ value: String, color: Color) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label)
                .font(LxFont.micro)
                .foregroundColor(theme.textTertiary)
            Text(value)
                .font(LxFont.mono(11, weight: .semibold))
                .foregroundColor(color)
        }
    }
}

struct SettingsField: View {
    let label: String
    @Binding var value: String
    @Environment(\.theme) var theme
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label).font(LxFont.micro).foregroundColor(theme.textTertiary)
            TextField(label, text: $value)
                .textFieldStyle(.roundedBorder)
                .font(LxFont.mono(12))
        }
    }
}
