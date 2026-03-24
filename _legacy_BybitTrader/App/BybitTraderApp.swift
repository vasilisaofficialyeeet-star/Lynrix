// BybitTraderApp.swift — macOS application entry point

import SwiftUI
import UniformTypeIdentifiers
import os.log

private let appLogger = Logger(subsystem: "com.bybittrader.app", category: "App")

@main
struct BybitTraderApp: App {
    @StateObject private var engine = TradingEngine()
    
    init() {
        appLogger.info("BybitTrader launching...")
        NSSetUncaughtExceptionHandler { exception in
            appLogger.fault("UNCAUGHT EXCEPTION: \(exception.name.rawValue) — \(exception.reason ?? "unknown")")
            appLogger.fault("Stack: \(exception.callStackSymbols.joined(separator: "\n"))")
        }
        signal(SIGABRT) { _ in appLogger.fault("SIGABRT received") }
        signal(SIGSEGV) { _ in appLogger.fault("SIGSEGV received") }
        appLogger.info("BybitTrader ready")
    }
    
    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(engine)
                .frame(minWidth: 1200, minHeight: 800)
                .preferredColorScheme(.dark)
                .onAppear {
                    configureMainWindow()
                }
        }
        .defaultSize(width: 1400, height: 900)
        .windowStyle(.titleBar)
        .windowToolbarStyle(.unified(showsTitle: true))
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandMenu("Торговля") {
                Button(engine.status.isActive ? "Остановить" : "Запустить") {
                    if engine.status.isActive {
                        engine.stop()
                    } else {
                        startEngine()
                    }
                }
                .keyboardShortcut("r", modifiers: [.command])
                
                Button("Переключить Бумажная/Реальная") {
                    engine.togglePaperMode()
                }
                .keyboardShortcut("t", modifiers: [.command, .shift])
                
                Divider()
                
                Button("Аварийная остановка") {
                    engine.triggerPanic("Ручная аварийная остановка")
                    engine.stop()
                }
                .keyboardShortcut(".", modifiers: [.command, .shift])
            }
            CommandMenu("Экспорт") {
                Button("Экспорт логов...") { exportLogs() }
                    .keyboardShortcut("e", modifiers: [.command, .shift])
                Button("Экспорт сделок CSV...") { exportCSV(engine.exportTradesCSV(), name: "trades") }
                Button("Экспорт сигналов CSV...") { exportCSV(engine.exportSignalsCSV(), name: "signals") }
                Divider()
                Button("Очистить логи") { engine.clearLogs() }
                    .keyboardShortcut("k", modifiers: [.command])
            }
            CommandGroup(replacing: .help) {
                Button("О BybitTrader...") {
                    showAboutPanel()
                }
            }
            CommandGroup(replacing: .sidebar) {
                Button("Показать/Скрыть боковую панель") {
                    NSApp.keyWindow?.firstResponder?.tryToPerform(
                        #selector(NSSplitViewController.toggleSidebar(_:)), with: nil)
                }
                .keyboardShortcut("s", modifiers: [.command, .control])
            }
        }
    }
    
    private func startEngine() {
        let key = KeychainManager.shared.loadAPIKey()
        let secret = KeychainManager.shared.loadAPISecret()
        engine.start(apiKey: key, apiSecret: secret)
    }
    
    private func exportLogs() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.plainText]
        panel.nameFieldStringValue = "bybit_logs_\(ISO8601DateFormatter().string(from: Date())).txt"
        panel.begin { result in
            if result == .OK, let url = panel.url {
                try? engine.exportLogs().write(to: url, atomically: true, encoding: .utf8)
            }
        }
    }
    
    private func configureMainWindow() {
        DispatchQueue.main.async {
            guard let window = NSApp.windows.first(where: { $0.isVisible }) else { return }
            window.title = "BybitTrader AI Edition"
            window.titlebarAppearsTransparent = false
            window.isMovableByWindowBackground = false
            window.styleMask.insert([.resizable, .miniaturizable, .closable, .titled, .fullSizeContentView])
            window.setFrameAutosaveName("BybitTraderMain")
            window.center()
        }
    }
    
    private func showAboutPanel() {
        let options: [NSApplication.AboutPanelOptionKey: Any] = [
            .applicationName: "BybitTrader AI Edition",
            .applicationVersion: "2.0.0",
            .version: "Build 1",
            .credits: NSAttributedString(
                string: "HFT Trading Platform for Bybit\nC++20 Core • SwiftUI • ONNX Runtime",
                attributes: [
                    .foregroundColor: NSColor.secondaryLabelColor,
                    .font: NSFont.systemFont(ofSize: 11)
                ]
            )
        ]
        NSApp.orderFrontStandardAboutPanel(options: options)
    }
    
    private func exportCSV(_ content: String, name: String) {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.commaSeparatedText]
        panel.nameFieldStringValue = "\(name)_\(ISO8601DateFormatter().string(from: Date())).csv"
        panel.begin { result in
            if result == .OK, let url = panel.url {
                try? content.write(to: url, atomically: true, encoding: .utf8)
            }
        }
    }
}
