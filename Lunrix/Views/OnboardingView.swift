// OnboardingView.swift — First-run setup flow for Lynrix v2.5

import SwiftUI

struct OnboardingView: View {
    @EnvironmentObject var loc: LocalizationManager
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var themeManager: ThemeManager
    @ObservedObject var onboarding = OnboardingManager.shared
    @Environment(\.theme) var theme
    
    // D2: State for new API key and trading mode steps
    @State private var apiKeyInput: String = ""
    @State private var apiSecretInput: String = ""
    @State private var selectedTradingMode: TradingMode = .paper
    @State private var apiKeyVisible: Bool = false
    
    var body: some View {
        VStack(spacing: 0) {
            // Progress bar
            progressBar
            
            // Step content
            Group {
                switch onboarding.currentStep {
                case .welcome:      welcomeStep
                case .preferences:  preferencesStep
                case .apiKeys:      apiKeysStep
                case .tradingMode:  tradingModeStep
                case .safety:       safetyStep
                case .ready:        readyStep
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .transition(.asymmetric(
                insertion: .move(edge: .trailing).combined(with: .opacity),
                removal: .move(edge: .leading).combined(with: .opacity)
            ))
            .animation(.easeInOut(duration: 0.25), value: onboarding.currentStep)
            
            // Navigation
            navigationBar
        }
        .frame(width: 620, height: 560)
        .background(
            ZStack {
                theme.backgroundPrimary
                VisualEffectBackground(material: theme.hudMaterial, blendingMode: .behindWindow, state: .active)
                    .opacity(0.5)
            }
        )
        .clipShape(RoundedRectangle(cornerRadius: 16))
        .overlay(
            RoundedRectangle(cornerRadius: 16)
                .stroke(LxColor.electricCyan.opacity(0.15), lineWidth: 0.5)
        )
        .shadow(color: .black.opacity(0.4), radius: 30, y: 10)
    }
    
    // MARK: - Progress Bar
    
    private var progressBar: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                Rectangle()
                    .fill(theme.borderSubtle)
                Rectangle()
                    .fill(LxColor.electricCyan)
                    .frame(width: geo.size.width * onboarding.currentStep.progress)
                    .animation(.easeInOut(duration: 0.3), value: onboarding.currentStep)
            }
        }
        .frame(height: 2)
    }
    
    // MARK: - Step 1: Welcome
    
    private var welcomeStep: some View {
        VStack(spacing: 24) {
            Spacer()
            
            ZStack {
                Circle()
                    .fill(LxColor.electricCyan.opacity(0.08))
                    .frame(width: 80, height: 80)
                Image(systemName: "diamond.fill")
                    .font(.system(size: 32, weight: .bold))
                    .foregroundColor(LxColor.electricCyan)
                    .shadow(color: LxColor.electricCyan.opacity(0.5), radius: 8)
            }
            
            VStack(spacing: 8) {
                Text(loc.t("onboarding.welcomeTitle"))
                    .font(LxFont.mono(24, weight: .bold))
                    .foregroundColor(theme.textPrimary)
                Text(loc.t("onboarding.welcomeSubtitle"))
                    .font(LxFont.mono(13))
                    .foregroundColor(theme.textSecondary)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 400)
            }
            
            VStack(alignment: .leading, spacing: 10) {
                onboardingFeature("brain", loc.t("onboarding.featureAI"), LxColor.electricCyan)
                onboardingFeature("shield.checkerboard", loc.t("onboarding.featureRisk"), LxColor.amber)
                onboardingFeature("chart.bar.doc.horizontal", loc.t("onboarding.featureExec"), LxColor.gold)
                onboardingFeature("magnifyingglass.circle", loc.t("onboarding.featureResearch"), LxColor.neonLime)
            }
            .padding(.horizontal, 60)
            
            Spacer()
        }
        .padding(32)
    }
    
    private func onboardingFeature(_ icon: String, _ text: String, _ color: Color) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .font(.system(size: 14, weight: .semibold))
                .foregroundColor(color)
                .frame(width: 24)
            Text(text)
                .font(LxFont.mono(12))
                .foregroundColor(theme.textSecondary)
            Spacer()
        }
    }
    
    // MARK: - Step 2: Preferences
    
    private var preferencesStep: some View {
        VStack(spacing: 24) {
            Spacer()
            
            Text(loc.t("onboarding.prefsTitle"))
                .font(LxFont.mono(20, weight: .bold))
                .foregroundColor(theme.textPrimary)
            
            // Language
            VStack(alignment: .leading, spacing: 8) {
                Label(loc.t("onboarding.language"), systemImage: "globe")
                    .font(LxFont.mono(12, weight: .bold))
                    .foregroundColor(theme.textSecondary)
                HStack(spacing: 8) {
                    ForEach(LocalizationManager.Language.allCases) { lang in
                        Button(action: { loc.currentLanguage = lang }) {
                            HStack(spacing: 4) {
                                Text(lang.flag)
                                Text(lang.displayName)
                                    .font(LxFont.mono(11, weight: loc.currentLanguage == lang ? .bold : .regular))
                            }
                            .padding(.horizontal, 12)
                            .padding(.vertical, 8)
                            .background(
                                RoundedRectangle(cornerRadius: 8)
                                    .fill(loc.currentLanguage == lang ? LxColor.electricCyan.opacity(0.12) : Color.clear)
                            )
                            .overlay(
                                RoundedRectangle(cornerRadius: 8)
                                    .stroke(loc.currentLanguage == lang ? LxColor.electricCyan.opacity(0.3) : theme.borderSubtle, lineWidth: 0.5)
                            )
                            .foregroundColor(loc.currentLanguage == lang ? LxColor.electricCyan : theme.textSecondary)
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .padding(.horizontal, 60)
            
            // Appearance
            VStack(alignment: .leading, spacing: 8) {
                Label(loc.t("onboarding.appearance"), systemImage: "sun.max.fill")
                    .font(LxFont.mono(12, weight: .bold))
                    .foregroundColor(theme.textSecondary)
                HStack(spacing: 8) {
                    ForEach(AppearanceMode.allCases) { mode in
                        Button(action: { themeManager.mode = mode }) {
                            HStack(spacing: 4) {
                                Image(systemName: mode.icon)
                                    .font(.system(size: 11))
                                Text(loc.t(mode.locKey))
                                    .font(LxFont.mono(11, weight: themeManager.mode == mode ? .bold : .regular))
                            }
                            .padding(.horizontal, 12)
                            .padding(.vertical, 8)
                            .background(
                                RoundedRectangle(cornerRadius: 8)
                                    .fill(themeManager.mode == mode ? LxColor.electricCyan.opacity(0.12) : Color.clear)
                            )
                            .overlay(
                                RoundedRectangle(cornerRadius: 8)
                                    .stroke(themeManager.mode == mode ? LxColor.electricCyan.opacity(0.3) : theme.borderSubtle, lineWidth: 0.5)
                            )
                            .foregroundColor(themeManager.mode == mode ? LxColor.electricCyan : theme.textSecondary)
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .padding(.horizontal, 60)
            
            Spacer()
        }
        .padding(32)
    }
    
    // MARK: - Step 3: API Keys (D2)
    
    private var apiKeysStep: some View {
        VStack(spacing: 24) {
            Spacer()
            
            Text(loc.t("onboarding.apiKeysTitle"))
                .font(LxFont.mono(20, weight: .bold))
                .foregroundColor(theme.textPrimary)
            
            Text(loc.t("onboarding.apiKeysSubtitle"))
                .font(LxFont.mono(12))
                .foregroundColor(theme.textSecondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 420)
            
            VStack(alignment: .leading, spacing: 14) {
                // API Key
                VStack(alignment: .leading, spacing: 4) {
                    Label(loc.t("onboarding.apiKey"), systemImage: "key.fill")
                        .font(LxFont.mono(11, weight: .bold))
                        .foregroundColor(theme.textSecondary)
                    TextField(loc.t("onboarding.apiKeyPlaceholder"), text: $apiKeyInput)
                        .textFieldStyle(.plain)
                        .font(LxFont.mono(12))
                        .padding(10)
                        .background(
                            RoundedRectangle(cornerRadius: 8)
                                .fill(theme.backgroundSecondary)
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(theme.borderSubtle, lineWidth: 0.5)
                        )
                }
                
                // API Secret
                VStack(alignment: .leading, spacing: 4) {
                    Label(loc.t("onboarding.apiSecret"), systemImage: "lock.fill")
                        .font(LxFont.mono(11, weight: .bold))
                        .foregroundColor(theme.textSecondary)
                    HStack(spacing: 6) {
                        Group {
                            if apiKeyVisible {
                                TextField(loc.t("onboarding.apiSecretPlaceholder"), text: $apiSecretInput)
                            } else {
                                SecureField(loc.t("onboarding.apiSecretPlaceholder"), text: $apiSecretInput)
                            }
                        }
                        .textFieldStyle(.plain)
                        .font(LxFont.mono(12))
                        
                        Button(action: { apiKeyVisible.toggle() }) {
                            Image(systemName: apiKeyVisible ? "eye.slash" : "eye")
                                .font(.system(size: 11))
                                .foregroundColor(theme.textTertiary)
                        }
                        .buttonStyle(.plain)
                    }
                    .padding(10)
                    .background(
                        RoundedRectangle(cornerRadius: 8)
                            .fill(theme.backgroundSecondary)
                    )
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .stroke(theme.borderSubtle, lineWidth: 0.5)
                    )
                }
                
                // Keychain notice
                HStack(spacing: 6) {
                    Image(systemName: "lock.shield")
                        .font(.system(size: 10))
                        .foregroundColor(LxColor.neonLime)
                    Text(loc.t("onboarding.keychainNote"))
                        .font(LxFont.mono(10))
                        .foregroundColor(theme.textTertiary)
                }
            }
            .padding(.horizontal, 60)
            
            Spacer()
        }
        .padding(32)
    }
    
    // MARK: - Step 4: Trading Mode (D2)
    
    private var tradingModeStep: some View {
        VStack(spacing: 24) {
            Spacer()
            
            Text(loc.t("onboarding.tradingModeTitle"))
                .font(LxFont.mono(20, weight: .bold))
                .foregroundColor(theme.textPrimary)
            
            Text(loc.t("onboarding.tradingModeSubtitle"))
                .font(LxFont.mono(12))
                .foregroundColor(theme.textSecondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 420)
            
            VStack(spacing: 12) {
                tradingModeOption(
                    mode: .paper,
                    icon: "doc.text",
                    titleKey: "onboarding.modePaper",
                    descKey: "onboarding.modePaperDesc",
                    color: LxColor.electricCyan,
                    recommended: true
                )
                tradingModeOption(
                    mode: .testnet,
                    icon: "testtube.2",
                    titleKey: "onboarding.modeTestnet",
                    descKey: "onboarding.modeTestnetDesc",
                    color: LxColor.amber,
                    recommended: false
                )
                tradingModeOption(
                    mode: .live,
                    icon: "bolt.fill",
                    titleKey: "onboarding.modeLive",
                    descKey: "onboarding.modeLiveDesc",
                    color: LxColor.bloodRed,
                    recommended: false
                )
            }
            .padding(.horizontal, 50)
            
            Spacer()
        }
        .padding(32)
    }
    
    private func tradingModeOption(mode: TradingMode, icon: String, titleKey: String, descKey: String, color: Color, recommended: Bool) -> some View {
        Button(action: { selectedTradingMode = mode }) {
            HStack(spacing: 12) {
                Image(systemName: icon)
                    .font(.system(size: 16, weight: .semibold))
                    .foregroundColor(selectedTradingMode == mode ? color : theme.textTertiary)
                    .frame(width: 28)
                VStack(alignment: .leading, spacing: 2) {
                    HStack(spacing: 6) {
                        Text(loc.t(titleKey))
                            .font(LxFont.mono(13, weight: .bold))
                            .foregroundColor(selectedTradingMode == mode ? theme.textPrimary : theme.textSecondary)
                        if recommended {
                            Text(loc.t("onboarding.recommended"))
                                .font(LxFont.mono(8, weight: .bold))
                                .foregroundColor(LxColor.neonLime)
                                .padding(.horizontal, 6)
                                .padding(.vertical, 2)
                                .background(Capsule().fill(LxColor.neonLime.opacity(0.12)))
                        }
                    }
                    Text(loc.t(descKey))
                        .font(LxFont.mono(10))
                        .foregroundColor(theme.textTertiary)
                }
                Spacer()
                Image(systemName: selectedTradingMode == mode ? "checkmark.circle.fill" : "circle")
                    .font(.system(size: 16))
                    .foregroundColor(selectedTradingMode == mode ? color : theme.borderSubtle)
            }
            .padding(14)
            .background(
                RoundedRectangle(cornerRadius: 10)
                    .fill(selectedTradingMode == mode ? color.opacity(0.06) : Color.clear)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(selectedTradingMode == mode ? color.opacity(0.3) : theme.borderSubtle, lineWidth: 0.5)
            )
        }
        .buttonStyle(.plain)
    }
    
    // MARK: - Step 5: Safety
    
    private var safetyStep: some View {
        VStack(spacing: 24) {
            Spacer()
            
            Text(loc.t("onboarding.safetyTitle"))
                .font(LxFont.mono(20, weight: .bold))
                .foregroundColor(theme.textPrimary)
            
            Text(loc.t("onboarding.safetySubtitle"))
                .font(LxFont.mono(12))
                .foregroundColor(theme.textSecondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 420)
            
            VStack(alignment: .leading, spacing: 14) {
                safetyItem("checkmark.shield", loc.t("onboarding.safetyPaper"), loc.t("onboarding.safetyPaperDesc"), LxColor.electricCyan)
                safetyItem("exclamationmark.triangle", loc.t("onboarding.safetyCircuit"), loc.t("onboarding.safetyCircuitDesc"), LxColor.amber)
                safetyItem("xmark.octagon", loc.t("onboarding.safetyKill"), loc.t("onboarding.safetyKillDesc"), LxColor.bloodRed)
                safetyItem("key.fill", loc.t("onboarding.safetyKeys"), loc.t("onboarding.safetyKeysDesc"), LxColor.gold)
            }
            .padding(.horizontal, 50)
            
            Spacer()
        }
        .padding(32)
    }
    
    private func safetyItem(_ icon: String, _ title: String, _ desc: String, _ color: Color) -> some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: icon)
                .font(.system(size: 14, weight: .semibold))
                .foregroundColor(color)
                .frame(width: 24)
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                    .font(LxFont.mono(12, weight: .bold))
                    .foregroundColor(theme.textPrimary)
                Text(desc)
                    .font(LxFont.mono(10))
                    .foregroundColor(theme.textTertiary)
            }
            Spacer()
        }
    }
    
    // MARK: - Step 4: Ready
    
    private var readyStep: some View {
        VStack(spacing: 24) {
            Spacer()
            
            ZStack {
                Circle()
                    .fill(LxColor.neonLime.opacity(0.08))
                    .frame(width: 80, height: 80)
                Image(systemName: "checkmark.circle.fill")
                    .font(.system(size: 36, weight: .bold))
                    .foregroundColor(LxColor.neonLime)
                    .shadow(color: LxColor.neonLime.opacity(0.5), radius: 8)
            }
            
            Text(loc.t("onboarding.readyTitle"))
                .font(LxFont.mono(20, weight: .bold))
                .foregroundColor(theme.textPrimary)
            
            Text(loc.t("onboarding.readySubtitle"))
                .font(LxFont.mono(12))
                .foregroundColor(theme.textSecondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 420)
            
            VStack(alignment: .leading, spacing: 10) {
                readyTip("1", loc.t("onboarding.tip1"))
                readyTip("2", loc.t("onboarding.tip2"))
                readyTip("3", loc.t("onboarding.tip3"))
            }
            .padding(.horizontal, 60)
            
            Spacer()
        }
        .padding(32)
    }
    
    private func readyTip(_ number: String, _ text: String) -> some View {
        HStack(spacing: 12) {
            Text(number)
                .font(LxFont.mono(11, weight: .bold))
                .foregroundColor(LxColor.electricCyan)
                .frame(width: 22, height: 22)
                .background(Circle().fill(LxColor.electricCyan.opacity(0.12)))
            Text(text)
                .font(LxFont.mono(11))
                .foregroundColor(theme.textSecondary)
            Spacer()
        }
    }
    
    // MARK: - Navigation
    
    private var navigationBar: some View {
        HStack {
            if let prev = onboarding.currentStep.previous {
                Button(action: {
                    withAnimation { onboarding.currentStep = prev }
                }) {
                    HStack(spacing: 4) {
                        Image(systemName: "chevron.left")
                            .font(.system(size: 10, weight: .bold))
                        Text(loc.t("common.back"))
                            .font(LxFont.mono(12, weight: .medium))
                    }
                    .foregroundColor(theme.textSecondary)
                }
                .buttonStyle(.plain)
            }
            
            Spacer()
            
            // Step indicator
            HStack(spacing: 6) {
                ForEach(OnboardingManager.OnboardingStep.allCases, id: \.rawValue) { step in
                    Circle()
                        .fill(step == onboarding.currentStep ? LxColor.electricCyan : theme.borderSubtle)
                        .frame(width: 6, height: 6)
                }
            }
            
            Spacer()
            
            if onboarding.currentStep == .ready {
                NeonButton(loc.t("onboarding.getStarted"), icon: "arrow.right", color: LxColor.neonLime) {
                    // D2: Apply API keys and trading mode if provided
                    if !apiKeyInput.isEmpty && !apiSecretInput.isEmpty {
                        try? KeychainManager.shared.saveAPIKey(apiKeyInput)
                        try? KeychainManager.shared.saveAPISecret(apiSecretInput)
                    }
                    engine.setTradingMode(selectedTradingMode)
                    onboarding.completeOnboarding()
                }
            } else if let next = onboarding.currentStep.next {
                HStack(spacing: 8) {
                    // D2: Skip button for skippable steps
                    if onboarding.currentStep.isSkippable {
                        Button(action: { withAnimation { onboarding.currentStep = next } }) {
                            Text(loc.t("common.skip"))
                                .font(LxFont.mono(11))
                                .foregroundColor(theme.textTertiary)
                        }
                        .buttonStyle(.plain)
                    }
                    NeonButton(loc.t("common.next"), icon: "arrow.right", color: LxColor.electricCyan) {
                        withAnimation { onboarding.currentStep = next }
                    }
                }
            }
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 16)
        .background(theme.backgroundSecondary.opacity(0.5))
    }
}
