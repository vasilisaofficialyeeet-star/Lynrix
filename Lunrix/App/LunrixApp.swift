// LynrixApp.swift — Lynrix v2.5.0 macOS app entry point — Glassmorphism 2026

import SwiftUI
import UniformTypeIdentifiers
import os.log
import FirebaseCore
import FirebaseAuth
import GoogleSignIn

private let appLogger = Logger(subsystem: "com.lynrix.trader", category: "App")
private let isoFormatter: ISO8601DateFormatter = {
    let f = ISO8601DateFormatter()
    f.formatOptions = [.withInternetDateTime]
    return f
}()

@main
struct LynrixApp: App {
    @StateObject private var engine = LynrixEngine()
    @StateObject private var loc = LocalizationManager.shared
    @StateObject private var themeManager = ThemeManager.shared
    @StateObject private var onboarding = OnboardingManager.shared
    @StateObject private var auth = AuthManager.shared
    @State private var showLiveConfirmation = false
    
    init() {
        // Firebase MUST be configured before AuthManager accesses Auth.auth()
        FirebaseApp.configure()
        
        // Fix keychain access for ad-hoc / "Sign to Run Locally" builds:
        // By default Firebase Auth uses a shared keychain access group which
        // requires a real development team certificate. Passing nil switches
        // to the app's own default keychain, which works without a team ID.
        do {
            try Auth.auth().useUserAccessGroup(nil)
        } catch {
            appLogger.error("Failed to set keychain access group: \(error.localizedDescription)")
        }
        
        AuthManager.shared.configure()
        appLogger.info("Lynrix v2.5.0 launching — Firebase configured")
        NSSetUncaughtExceptionHandler { exception in
            appLogger.fault("UNCAUGHT EXCEPTION: \(exception.name.rawValue) — \(exception.reason ?? "unknown")")
            appLogger.fault("Stack: \(exception.callStackSymbols.joined(separator: "\n"))")
        }
        signal(SIGABRT) { _ in appLogger.fault("SIGABRT received") }
        signal(SIGSEGV) { _ in appLogger.fault("SIGSEGV received") }
        appLogger.info("Lynrix ready")
    }
    
    var body: some Scene {
        WindowGroup {
            Group {
                if auth.isSignedIn {
                    ContentView()
                        .environmentObject(engine)
                        .environmentObject(loc)
                        .environmentObject(themeManager)
                        .withLxTheme()
                        .frame(minWidth: 1280, minHeight: 860)
                        .onAppear {
                            configureMainWindow()
                            restorePersistedConfig()
                        }
                        .sheet(isPresented: $onboarding.showOnboarding) {
                            OnboardingView()
                                .environmentObject(loc)
                                .environmentObject(engine)
                                .environmentObject(themeManager)
                                .withLxTheme()
                                .preferredColorScheme(themeManager.effectiveColorScheme)
                        }
                        .confirmationDialog(loc.t("settings.switchToLive"), isPresented: $showLiveConfirmation, titleVisibility: .visible) {
                            Button(loc.t("settings.toLive"), role: .destructive) {
                                engine.togglePaperMode()
                            }
                        } message: {
                            Text(loc.t("settings.liveWarning"))
                        }
                } else {
                    AuthGateView(auth: auth)
                        .environmentObject(loc)
                        .environmentObject(themeManager)
                        .withLxTheme()
                }
            }
            .preferredColorScheme(themeManager.effectiveColorScheme)
            .onOpenURL { url in
                GIDSignIn.sharedInstance.handle(url)
            }
        }
        .defaultSize(width: 1560, height: 980)
        .windowStyle(.hiddenTitleBar)
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandMenu(loc.t("menu.trading")) {
                Button(engine.status.isActive ? loc.t("menu.stopEngine") : loc.t("menu.startEngine")) {
                    if engine.status.isActive { engine.stop() }
                    else { startEngine() }
                }
                .keyboardShortcut("r", modifiers: [.command])
                
                Button(loc.t("menu.toggleMode")) {
                    if engine.paperMode {
                        showLiveConfirmation = true
                    } else {
                        engine.togglePaperMode()
                    }
                }
                .keyboardShortcut("t", modifiers: [.command, .shift])
                
                Divider()
                
                Button(loc.t("menu.emergencyStop")) {
                    engine.triggerPanic(loc.t("engine.manualStop"))
                    engine.emergencyStop()
                }
                .keyboardShortcut(".", modifiers: [.command, .shift])
                
                Divider()
                
                Button(loc.t("menu.globalKill")) {
                    engine.activateGlobalKill(reason: loc.t("risk.manualGlobalKill"))
                }
                .keyboardShortcut("k", modifiers: [.command, .shift])
                .disabled(engine.killSwitch.globalHalt)
                
                if engine.killSwitch.globalHalt {
                    Button(loc.t("menu.resetKill")) {
                        engine.resetGlobalKill()
                    }
                }
            }
            CommandMenu(loc.t("menu.observability")) {
                Button(loc.t("menu.exportFlamegraph")) { exportFlamegraph() }
                Button(loc.t("menu.exportHistograms")) { exportHistograms() }
                Divider()
                Button(loc.t("menu.enableChaos")) { engine.enableChaosNightly() }
                Button(loc.t("menu.disableChaos")) { engine.disableChaos() }
                Divider()
                Button(loc.t("menu.loadReplay")) { loadReplayFile() }
            }
            CommandMenu(loc.t("menu.export")) {
                Button(loc.t("menu.exportLogs")) { exportLogs() }
                    .keyboardShortcut("e", modifiers: [.command, .shift])
                Button(loc.t("menu.exportTradesCSV")) { exportCSV(engine.exportTradesCSV(), name: "lynrix_trades") }
                Button(loc.t("menu.exportSignalsCSV")) { exportCSV(engine.exportSignalsCSV(), name: "lynrix_signals") }
                Button(loc.t("menu.exportJournalCSV")) { exportCSV(TradeJournalStore.shared.exportCSV(), name: "lynrix_journal") }
                Button(loc.t("menu.exportIncidentsCSV")) { exportIncidents() }
                Divider()
                Button(loc.t("menu.clearLogs")) { engine.clearLogs() }
                    .keyboardShortcut("k", modifiers: [.command])
            }
            CommandGroup(replacing: .help) {
                Button(loc.t("menu.about")) { showAboutPanel() }
            }
            CommandGroup(replacing: .sidebar) {
                Button(loc.t("menu.toggleSidebar")) {
                    NSApp.keyWindow?.firstResponder?.tryToPerform(
                        #selector(NSSplitViewController.toggleSidebar(_:)), with: nil)
                }
                .keyboardShortcut("s", modifiers: [.command, .control])
            }
        }
    }
    
    // MARK: - Engine
    
    private func startEngine() {
        guard !engine.killSwitch.globalHalt else {
            engine.addLog(.error, "Cannot start — global kill switch active")
            return
        }
        let validation = engine.validateConfig()
        if validation.hasErrors {
            engine.addLog(.error, "Cannot start — config has errors")
            for issue in validation.errors {
                engine.addLog(.error, "Config: \(loc.t(issue.messageKey))")
            }
            return
        }
        let key = KeychainManager.shared.loadAPIKey()
        let secret = KeychainManager.shared.loadAPISecret()
        engine.start(apiKey: key, apiSecret: secret)
    }
    
    // MARK: - Window Configuration
    
    private func configureMainWindow() {
        DispatchQueue.main.async {
            guard let window = NSApp.windows.first(where: { $0.isVisible }) else { return }
            window.titlebarAppearsTransparent = true
            window.titleVisibility = .hidden
            window.isMovableByWindowBackground = true
            window.styleMask.insert([.resizable, .miniaturizable, .closable, .titled, .fullSizeContentView])
            window.appearance = themeManager.mode.nsAppearance
            window.backgroundColor = LxTheme.resolvedWindowBackground(for: themeManager.resolvedScheme)
            window.hasShadow = true
            window.invalidateShadow()
            window.setFrameAutosaveName("LynrixMain")
            window.center()
        }
    }
    
    // MARK: - Export Helpers
    
    private func exportLogs() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.plainText]
        panel.nameFieldStringValue = "lynrix_logs_\(isoFormatter.string(from: Date())).txt"
        panel.begin { result in
            if result == .OK, let url = panel.url {
                try? engine.exportLogs().write(to: url, atomically: true, encoding: .utf8)
            }
        }
    }
    
    private func exportFlamegraph() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.plainText]
        panel.nameFieldStringValue = "lynrix_flamegraph_\(isoFormatter.string(from: Date())).txt"
        panel.begin { result in
            if result == .OK, let url = panel.url {
                engine.exportFlamegraph(to: url)
            }
        }
    }
    
    private func exportHistograms() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.commaSeparatedText]
        panel.nameFieldStringValue = "lynrix_histograms_\(isoFormatter.string(from: Date())).csv"
        panel.begin { result in
            if result == .OK, let url = panel.url {
                engine.exportHistograms(to: url)
            }
        }
    }
    
    private func loadReplayFile() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.data]
        panel.allowsMultipleSelection = false
        panel.message = loc.t("menu.replayMessage")
        panel.begin { result in
            if result == .OK, let url = panel.url {
                engine.loadReplayFile(url: url)
            }
        }
    }
    
    private func showAboutPanel() {
        let options: [NSApplication.AboutPanelOptionKey: Any] = [
            .applicationName: loc.t("app.nameTitleCase"),
            .applicationVersion: "2.5.0",
            .version: loc.t("app.about.build"),
            .credits: NSAttributedString(
                string: loc.t("app.about.credits"),
                attributes: [
                    .foregroundColor: NSColor.secondaryLabelColor,
                    .font: NSFont.monospacedSystemFont(ofSize: 10, weight: .regular)
                ]
            )
        ]
        NSApp.orderFrontStandardAboutPanel(options: options)
    }
    
    private func restorePersistedConfig() {
        if let persisted = ProfileManager.shared.loadPersistedConfig() {
            engine.config = persisted
            engine.addLog(.info, "Config restored from last session")
        }
    }
    
    private func exportIncidents() {
        let loc = self.loc
        var csv = "Timestamp,Severity,Category,Title,Detail,Resolved\n"
        let fmt = isoFormatter
        for incident in IncidentStore.shared.incidents {
            csv += "\(fmt.string(from: incident.timestamp)),"
            csv += "\(incident.severity.rawValue),"
            csv += "\(incident.category.rawValue),"
            csv += "\"\(loc.t(incident.titleKey).replacingOccurrences(of: "\"", with: "\"\""))\","
            csv += "\"\(incident.detail.replacingOccurrences(of: "\"", with: "\"\""))\","
            csv += "\(incident.resolved)\n"
        }
        exportCSV(csv, name: "lynrix_incidents")
    }
    
    private func exportCSV(_ content: String, name: String) {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.commaSeparatedText]
        panel.nameFieldStringValue = "\(name)_\(isoFormatter.string(from: Date())).csv"
        panel.begin { result in
            if result == .OK, let url = panel.url {
                let header = "# Lynrix Export — \(isoFormatter.string(from: Date()))\n"
                try? (header + content).write(to: url, atomically: true, encoding: .utf8)
            }
        }
    }
}
