// ProfileManager.swift — Config profiles & persistence for Lynrix v2.5

import SwiftUI
import Combine

final class ProfileManager: ObservableObject {
    static let shared = ProfileManager()
    
    private static let activeConfigKey = "lynrix.activeConfig"
    private static let profilesKey = "lynrix.savedProfiles"
    private static let activeProfileKey = "lynrix.activeProfileName"
    
    @Published var savedProfiles: [ConfigProfile] = []
    @Published var activeProfileName: String = ""
    
    struct ConfigProfile: Codable, Identifiable, Equatable {
        var id: String { name }
        var name: String
        var config: SavedConfig
        var isBuiltIn: Bool = false
        var createdAt: Date = Date()
    }
    
    struct SavedConfig: Codable, Equatable {
        var symbol: String
        var orderQty: Double
        var signalThreshold: Double
        var entryOffsetBps: Double
        var maxPositionSize: Double
        var maxLeverage: Double
        var maxDailyLoss: Double
        var maxDrawdown: Double
        var maxOrdersPerSec: Int
        var obLevels: Int
        var ioThreads: Int
        var mlModelEnabled: Bool
        var adaptiveThresholdEnabled: Bool
        var regimeDetectionEnabled: Bool
        var requoteEnabled: Bool
        var adaptiveSizingEnabled: Bool
        var cbEnabled: Bool
        var cbLossThreshold: Double
        var cbDrawdownThreshold: Double
        var cbConsecutiveLosses: Int
        var cbCooldownSec: Int
        var featureTickMs: Int
        var onnxEnabled: Bool
        var onnxModelPath: String
        var onnxIntraThreads: Int
    }
    
    // MARK: - Built-in Presets
    
    static let builtInPresets: [ConfigProfile] = [
        ConfigProfile(
            name: "Conservative",
            config: SavedConfig(
                symbol: "BTCUSDT", orderQty: 0.0005, signalThreshold: 0.8,
                entryOffsetBps: 0.5, maxPositionSize: 0.05, maxLeverage: 3.0,
                maxDailyLoss: 200.0, maxDrawdown: 0.05, maxOrdersPerSec: 3,
                obLevels: 500, ioThreads: 2,
                mlModelEnabled: true, adaptiveThresholdEnabled: true,
                regimeDetectionEnabled: true, requoteEnabled: true,
                adaptiveSizingEnabled: true, cbEnabled: true,
                cbLossThreshold: 100.0, cbDrawdownThreshold: 0.03,
                cbConsecutiveLosses: 5, cbCooldownSec: 600,
                featureTickMs: 10, onnxEnabled: false, onnxModelPath: "", onnxIntraThreads: 4
            ),
            isBuiltIn: true
        ),
        ConfigProfile(
            name: "Balanced",
            config: SavedConfig(
                symbol: "BTCUSDT", orderQty: 0.001, signalThreshold: 0.6,
                entryOffsetBps: 1.0, maxPositionSize: 0.1, maxLeverage: 10.0,
                maxDailyLoss: 500.0, maxDrawdown: 0.1, maxOrdersPerSec: 5,
                obLevels: 500, ioThreads: 2,
                mlModelEnabled: true, adaptiveThresholdEnabled: true,
                regimeDetectionEnabled: true, requoteEnabled: true,
                adaptiveSizingEnabled: true, cbEnabled: true,
                cbLossThreshold: 200.0, cbDrawdownThreshold: 0.05,
                cbConsecutiveLosses: 10, cbCooldownSec: 300,
                featureTickMs: 10, onnxEnabled: false, onnxModelPath: "", onnxIntraThreads: 4
            ),
            isBuiltIn: true
        ),
        ConfigProfile(
            name: "Aggressive",
            config: SavedConfig(
                symbol: "BTCUSDT", orderQty: 0.005, signalThreshold: 0.4,
                entryOffsetBps: 2.0, maxPositionSize: 0.5, maxLeverage: 20.0,
                maxDailyLoss: 2000.0, maxDrawdown: 0.2, maxOrdersPerSec: 10,
                obLevels: 500, ioThreads: 4,
                mlModelEnabled: true, adaptiveThresholdEnabled: true,
                regimeDetectionEnabled: true, requoteEnabled: true,
                adaptiveSizingEnabled: true, cbEnabled: true,
                cbLossThreshold: 500.0, cbDrawdownThreshold: 0.1,
                cbConsecutiveLosses: 15, cbCooldownSec: 120,
                featureTickMs: 5, onnxEnabled: false, onnxModelPath: "", onnxIntraThreads: 4
            ),
            isBuiltIn: true
        ),
        ConfigProfile(
            name: "Research",
            config: SavedConfig(
                symbol: "BTCUSDT", orderQty: 0.001, signalThreshold: 0.3,
                entryOffsetBps: 1.0, maxPositionSize: 0.01, maxLeverage: 1.0,
                maxDailyLoss: 50.0, maxDrawdown: 0.02, maxOrdersPerSec: 2,
                obLevels: 1000, ioThreads: 2,
                mlModelEnabled: true, adaptiveThresholdEnabled: true,
                regimeDetectionEnabled: true, requoteEnabled: false,
                adaptiveSizingEnabled: false, cbEnabled: true,
                cbLossThreshold: 50.0, cbDrawdownThreshold: 0.02,
                cbConsecutiveLosses: 3, cbCooldownSec: 60,
                featureTickMs: 10, onnxEnabled: false, onnxModelPath: "", onnxIntraThreads: 4
            ),
            isBuiltIn: true
        ),
    ]
    
    private init() {
        loadProfiles()
    }
    
    // MARK: - Config ↔ SavedConfig
    
    static func fromEngine(_ c: LynrixEngine.TradingConfig) -> SavedConfig {
        SavedConfig(
            symbol: c.symbol, orderQty: c.orderQty, signalThreshold: c.signalThreshold,
            entryOffsetBps: c.entryOffsetBps, maxPositionSize: c.maxPositionSize,
            maxLeverage: c.maxLeverage, maxDailyLoss: c.maxDailyLoss, maxDrawdown: c.maxDrawdown,
            maxOrdersPerSec: c.maxOrdersPerSec, obLevels: c.obLevels, ioThreads: c.ioThreads,
            mlModelEnabled: c.mlModelEnabled, adaptiveThresholdEnabled: c.adaptiveThresholdEnabled,
            regimeDetectionEnabled: c.regimeDetectionEnabled, requoteEnabled: c.requoteEnabled,
            adaptiveSizingEnabled: c.adaptiveSizingEnabled, cbEnabled: c.cbEnabled,
            cbLossThreshold: c.cbLossThreshold, cbDrawdownThreshold: c.cbDrawdownThreshold,
            cbConsecutiveLosses: c.cbConsecutiveLosses, cbCooldownSec: c.cbCooldownSec,
            featureTickMs: c.featureTickMs, onnxEnabled: c.onnxEnabled,
            onnxModelPath: c.onnxModelPath, onnxIntraThreads: c.onnxIntraThreads
        )
    }
    
    static func toEngine(_ s: SavedConfig) -> LynrixEngine.TradingConfig {
        var c = LynrixEngine.TradingConfig()
        c.symbol = s.symbol; c.orderQty = s.orderQty; c.signalThreshold = s.signalThreshold
        c.entryOffsetBps = s.entryOffsetBps; c.maxPositionSize = s.maxPositionSize
        c.maxLeverage = s.maxLeverage; c.maxDailyLoss = s.maxDailyLoss; c.maxDrawdown = s.maxDrawdown
        c.maxOrdersPerSec = s.maxOrdersPerSec; c.obLevels = s.obLevels; c.ioThreads = s.ioThreads
        c.mlModelEnabled = s.mlModelEnabled; c.adaptiveThresholdEnabled = s.adaptiveThresholdEnabled
        c.regimeDetectionEnabled = s.regimeDetectionEnabled; c.requoteEnabled = s.requoteEnabled
        c.adaptiveSizingEnabled = s.adaptiveSizingEnabled; c.cbEnabled = s.cbEnabled
        c.cbLossThreshold = s.cbLossThreshold; c.cbDrawdownThreshold = s.cbDrawdownThreshold
        c.cbConsecutiveLosses = s.cbConsecutiveLosses; c.cbCooldownSec = s.cbCooldownSec
        c.featureTickMs = s.featureTickMs; c.onnxEnabled = s.onnxEnabled
        c.onnxModelPath = s.onnxModelPath; c.onnxIntraThreads = s.onnxIntraThreads
        return c
    }
    
    // MARK: - Persistence
    
    func saveCurrentConfig(_ engine: LynrixEngine) {
        let saved = Self.fromEngine(engine.config)
        if let data = try? JSONEncoder().encode(saved) {
            UserDefaults.standard.set(data, forKey: Self.activeConfigKey)
        }
    }
    
    func loadPersistedConfig() -> LynrixEngine.TradingConfig? {
        guard let data = UserDefaults.standard.data(forKey: Self.activeConfigKey),
              let saved = try? JSONDecoder().decode(SavedConfig.self, from: data) else { return nil }
        return Self.toEngine(saved)
    }
    
    // MARK: - Profile CRUD
    
    func saveProfile(name: String, config: LynrixEngine.TradingConfig) {
        let profile = ConfigProfile(name: name, config: Self.fromEngine(config))
        if let idx = savedProfiles.firstIndex(where: { $0.name == name && !$0.isBuiltIn }) {
            savedProfiles[idx] = profile
        } else {
            savedProfiles.append(profile)
        }
        activeProfileName = name
        persistProfiles()
    }
    
    func deleteProfile(name: String) {
        savedProfiles.removeAll { $0.name == name && !$0.isBuiltIn }
        if activeProfileName == name { activeProfileName = "" }
        persistProfiles()
    }
    
    func applyProfile(_ profile: ConfigProfile, to engine: LynrixEngine) {
        engine.config = Self.toEngine(profile.config)
        activeProfileName = profile.name
        UserDefaults.standard.set(activeProfileName, forKey: Self.activeProfileKey)
        saveCurrentConfig(engine)
    }
    
    var allProfiles: [ConfigProfile] {
        Self.builtInPresets + savedProfiles.filter { !$0.isBuiltIn }
    }
    
    // MARK: - Storage
    
    private func loadProfiles() {
        activeProfileName = UserDefaults.standard.string(forKey: Self.activeProfileKey) ?? ""
        guard let data = UserDefaults.standard.data(forKey: Self.profilesKey),
              let profiles = try? JSONDecoder().decode([ConfigProfile].self, from: data) else { return }
        savedProfiles = profiles
    }
    
    private func persistProfiles() {
        if let data = try? JSONEncoder().encode(savedProfiles.filter { !$0.isBuiltIn }) {
            UserDefaults.standard.set(data, forKey: Self.profilesKey)
        }
        UserDefaults.standard.set(activeProfileName, forKey: Self.activeProfileKey)
    }
}
