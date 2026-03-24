// TradingMode.swift — Unified operating mode, kill switches, config validation, incident log (Lynrix v2.5)

import Foundation
import Combine
import SwiftUI

// MARK: - Trading Mode

enum TradingMode: String, CaseIterable, Identifiable, Codable {
    case replay   = "replay"
    case backtest = "backtest"
    case paper    = "paper"
    case testnet  = "testnet"
    case live     = "live"
    
    var id: String { rawValue }
    
    var locKey: String {
        switch self {
        case .replay:   return "mode.replay"
        case .backtest: return "mode.backtest"
        case .paper:    return "mode.paper"
        case .testnet:  return "mode.testnet"
        case .live:     return "mode.live"
        }
    }
    
    var icon: String {
        switch self {
        case .replay:   return "play.rectangle"
        case .backtest: return "clock.arrow.circlepath"
        case .paper:    return "doc.text"
        case .testnet:  return "testtube.2"
        case .live:     return "bolt.fill"
        }
    }
    
    var color: Color {
        switch self {
        case .replay:   return LxColor.gold
        case .backtest: return LxColor.electricCyan
        case .paper:    return LxColor.electricCyan
        case .testnet:  return LxColor.amber
        case .live:     return LxColor.bloodRed
        }
    }
    
    var isSimulated: Bool {
        switch self {
        case .replay, .backtest, .paper: return true
        case .testnet, .live: return false
        }
    }
    
    var allowsRealOrders: Bool {
        self == .live || self == .testnet
    }
    
    var requiresCredentials: Bool {
        self == .live || self == .testnet
    }
    
    var riskLevel: Int {
        switch self {
        case .replay:   return 0
        case .backtest: return 0
        case .paper:    return 1
        case .testnet:  return 2
        case .live:     return 3
        }
    }
}

// MARK: - Kill Switch State

struct KillSwitchState: Equatable {
    var globalHalt: Bool = false
    var globalHaltReason: String = ""
    var globalHaltTimestamp: Date? = nil
    var strategyHalt: Bool = false
    var strategyHaltReason: String = ""
    var circuitBreakerHalt: Bool = false
    
    var isAnyHaltActive: Bool {
        globalHalt || strategyHalt || circuitBreakerHalt
    }
    
    var primaryReason: String {
        if globalHalt { return globalHaltReason }
        if circuitBreakerHalt { return "Circuit breaker tripped" }
        if strategyHalt { return strategyHaltReason }
        return ""
    }
    
    mutating func activateGlobal(reason: String) {
        globalHalt = true
        globalHaltReason = reason
        globalHaltTimestamp = Date()
    }
    
    mutating func resetGlobal() {
        globalHalt = false
        globalHaltReason = ""
        globalHaltTimestamp = nil
    }
    
    mutating func activateStrategy(reason: String) {
        strategyHalt = true
        strategyHaltReason = reason
    }
    
    mutating func resetStrategy() {
        strategyHalt = false
        strategyHaltReason = ""
    }
}

// MARK: - Config Validation

enum ConfigSeverity: Int, Comparable {
    case info = 0
    case warning = 1
    case error = 2
    
    static func < (lhs: ConfigSeverity, rhs: ConfigSeverity) -> Bool {
        lhs.rawValue < rhs.rawValue
    }
    
    var color: Color {
        switch self {
        case .info:    return LxColor.electricCyan
        case .warning: return LxColor.amber
        case .error:   return LxColor.bloodRed
        }
    }
    
    var icon: String {
        switch self {
        case .info:    return "info.circle"
        case .warning: return "exclamationmark.triangle"
        case .error:   return "xmark.octagon"
        }
    }
}

struct ConfigIssue: Identifiable, Equatable {
    let id = UUID()
    let severity: ConfigSeverity
    let field: String
    let messageKey: String
    
    static func == (lhs: ConfigIssue, rhs: ConfigIssue) -> Bool {
        lhs.field == rhs.field && lhs.messageKey == rhs.messageKey && lhs.severity == rhs.severity
    }
}

struct ConfigValidationResult {
    let issues: [ConfigIssue]
    
    var hasErrors: Bool { issues.contains { $0.severity == .error } }
    var hasWarnings: Bool { issues.contains { $0.severity >= .warning } }
    var isValid: Bool { !hasErrors }
    var errors: [ConfigIssue] { issues.filter { $0.severity == .error } }
    var warnings: [ConfigIssue] { issues.filter { $0.severity == .warning } }
    
    static let valid = ConfigValidationResult(issues: [])
}

struct ConfigValidator {
    
    static func validate(_ config: LynrixEngine.TradingConfig, mode: TradingMode, hasCredentials: Bool) -> ConfigValidationResult {
        var issues: [ConfigIssue] = []
        
        // Symbol
        if config.symbol.trimmingCharacters(in: .whitespaces).isEmpty {
            issues.append(ConfigIssue(severity: .error, field: "symbol", messageKey: "validation.emptySymbol"))
        }
        
        // Order quantity
        if config.orderQty <= 0 {
            issues.append(ConfigIssue(severity: .error, field: "orderQty", messageKey: "validation.zeroQty"))
        } else if config.orderQty > config.maxPositionSize {
            issues.append(ConfigIssue(severity: .warning, field: "orderQty", messageKey: "validation.qtyExceedsMax"))
        }
        
        // Max position size
        if config.maxPositionSize <= 0 {
            issues.append(ConfigIssue(severity: .error, field: "maxPositionSize", messageKey: "validation.zeroMaxPosition"))
        }
        
        // Leverage
        if config.maxLeverage <= 0 {
            issues.append(ConfigIssue(severity: .error, field: "maxLeverage", messageKey: "validation.zeroLeverage"))
        } else if config.maxLeverage > 50 {
            issues.append(ConfigIssue(severity: .warning, field: "maxLeverage", messageKey: "validation.highLeverage"))
        } else if config.maxLeverage > 100 {
            issues.append(ConfigIssue(severity: .error, field: "maxLeverage", messageKey: "validation.extremeLeverage"))
        }
        
        // Max daily loss
        if config.maxDailyLoss <= 0 {
            issues.append(ConfigIssue(severity: .error, field: "maxDailyLoss", messageKey: "validation.zeroMaxLoss"))
        }
        
        // Max drawdown
        if config.maxDrawdown <= 0 || config.maxDrawdown > 1.0 {
            issues.append(ConfigIssue(severity: .error, field: "maxDrawdown", messageKey: "validation.invalidDrawdown"))
        } else if config.maxDrawdown > 0.5 {
            issues.append(ConfigIssue(severity: .warning, field: "maxDrawdown", messageKey: "validation.highDrawdown"))
        }
        
        // Signal threshold
        if config.signalThreshold < 0.1 || config.signalThreshold > 0.99 {
            issues.append(ConfigIssue(severity: .warning, field: "signalThreshold", messageKey: "validation.thresholdRange"))
        }
        
        // Circuit breaker
        if !config.cbEnabled && mode == .live {
            issues.append(ConfigIssue(severity: .warning, field: "cbEnabled", messageKey: "validation.cbDisabledLive"))
        }
        
        // Live mode checks
        if mode == .live {
            if !hasCredentials {
                issues.append(ConfigIssue(severity: .error, field: "credentials", messageKey: "validation.noCredsLive"))
            }
            if config.maxLeverage > 20 {
                issues.append(ConfigIssue(severity: .warning, field: "maxLeverage", messageKey: "validation.highLeverageLive"))
            }
        }
        
        // Testnet checks
        if mode == .testnet && !hasCredentials {
            issues.append(ConfigIssue(severity: .warning, field: "credentials", messageKey: "validation.noCredsTestnet"))
        }
        
        // Max orders per sec
        if config.maxOrdersPerSec <= 0 {
            issues.append(ConfigIssue(severity: .error, field: "maxOrdersPerSec", messageKey: "validation.zeroOrderRate"))
        } else if config.maxOrdersPerSec > 50 {
            issues.append(ConfigIssue(severity: .warning, field: "maxOrdersPerSec", messageKey: "validation.highOrderRate"))
        }
        
        return ConfigValidationResult(issues: issues)
    }
}

// MARK: - Incident Log

enum IncidentSeverity: String, Codable, CaseIterable {
    case info     = "info"
    case warning  = "warning"
    case critical = "critical"
    
    var color: Color {
        switch self {
        case .info:     return LxColor.electricCyan
        case .warning:  return LxColor.amber
        case .critical: return LxColor.bloodRed
        }
    }
    
    var icon: String {
        switch self {
        case .info:     return "info.circle.fill"
        case .warning:  return "exclamationmark.triangle.fill"
        case .critical: return "xmark.octagon.fill"
        }
    }
    
    var locKey: String {
        switch self {
        case .info:     return "incident.info"
        case .warning:  return "incident.warning"
        case .critical: return "incident.critical"
        }
    }
}

enum IncidentCategory: String, Codable, CaseIterable {
    case modeChange     = "mode_change"
    case killSwitch     = "kill_switch"
    case circuitBreaker = "circuit_breaker"
    case riskHalt       = "risk_halt"
    case connection     = "connection"
    case configChange   = "config_change"
    case validation     = "validation"
    case execution      = "execution"
    case system         = "system"
    
    var locKey: String {
        switch self {
        case .modeChange:     return "incident.cat.modeChange"
        case .killSwitch:     return "incident.cat.killSwitch"
        case .circuitBreaker: return "incident.cat.circuitBreaker"
        case .riskHalt:       return "incident.cat.riskHalt"
        case .connection:     return "incident.cat.connection"
        case .configChange:   return "incident.cat.configChange"
        case .validation:     return "incident.cat.validation"
        case .execution:      return "incident.cat.execution"
        case .system:         return "incident.cat.system"
        }
    }
    
    var icon: String {
        switch self {
        case .modeChange:     return "arrow.triangle.swap"
        case .killSwitch:     return "power"
        case .circuitBreaker: return "bolt.trianglebadge.exclamationmark"
        case .riskHalt:       return "shield.slash"
        case .connection:     return "wifi.exclamationmark"
        case .configChange:   return "gearshape"
        case .validation:     return "checkmark.shield"
        case .execution:      return "arrow.left.arrow.right"
        case .system:         return "cpu"
        }
    }
}

struct IncidentEntry: Identifiable, Equatable {
    let id: UUID
    let timestamp: Date
    let severity: IncidentSeverity
    let category: IncidentCategory
    let titleKey: String
    let detail: String
    var resolved: Bool = false
    var resolvedTimestamp: Date? = nil
    
    static func == (lhs: IncidentEntry, rhs: IncidentEntry) -> Bool {
        lhs.id == rhs.id
    }
}

final class IncidentStore: ObservableObject {
    static let shared = IncidentStore()
    
    @Published var incidents: [IncidentEntry] = []
    private let maxIncidents = 200
    
    func record(severity: IncidentSeverity, category: IncidentCategory, titleKey: String, detail: String = "") {
        let entry = IncidentEntry(
            id: UUID(), timestamp: Date(),
            severity: severity, category: category,
            titleKey: titleKey, detail: detail
        )
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            self.incidents.insert(entry, at: 0)
            if self.incidents.count > self.maxIncidents {
                self.incidents.removeLast(self.incidents.count - self.maxIncidents)
            }
        }
    }
    
    func resolve(_ id: UUID) {
        if let idx = incidents.firstIndex(where: { $0.id == id }) {
            incidents[idx].resolved = true
            incidents[idx].resolvedTimestamp = Date()
        }
    }
    
    var unresolvedCount: Int {
        incidents.filter { !$0.resolved }.count
    }
    
    var criticalUnresolved: [IncidentEntry] {
        incidents.filter { $0.severity == .critical && !$0.resolved }
    }
    
    func clear() { incidents.removeAll() }
}
