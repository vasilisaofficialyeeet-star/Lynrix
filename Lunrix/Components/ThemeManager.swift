// ThemeManager.swift — Appearance mode management for Lynrix v2.5
// Supports Light, Dark, and System appearance modes with persistence

import SwiftUI
import Combine

// MARK: - Appearance Mode

enum AppearanceMode: String, CaseIterable, Identifiable {
    case system = "system"
    case light  = "light"
    case dark   = "dark"
    
    var id: String { rawValue }
    
    var icon: String {
        switch self {
        case .system: return "circle.lefthalf.filled"
        case .light:  return "sun.max.fill"
        case .dark:   return "moon.fill"
        }
    }
    
    var locKey: String {
        switch self {
        case .system: return "settings.appearanceSystem"
        case .light:  return "settings.appearanceLight"
        case .dark:   return "settings.appearanceDark"
        }
    }
    
    var nsAppearance: NSAppearance? {
        switch self {
        case .system: return nil
        case .light:  return NSAppearance(named: .aqua)
        case .dark:   return NSAppearance(named: .darkAqua)
        }
    }
}

// MARK: - Theme Manager

final class ThemeManager: ObservableObject {
    static let shared = ThemeManager()
    
    private static let storageKey = "lynrix.appearanceMode"
    
    @Published var mode: AppearanceMode {
        didSet {
            UserDefaults.standard.set(mode.rawValue, forKey: Self.storageKey)
            applyAppearance()
        }
    }
    
    /// Resolved effective scheme for use in SwiftUI
    @Published var effectiveColorScheme: ColorScheme?
    
    private var systemAppearanceObserver: NSKeyValueObservation?
    
    private init() {
        let stored = UserDefaults.standard.string(forKey: Self.storageKey) ?? "dark"
        self.mode = AppearanceMode(rawValue: stored) ?? .dark
        
        // Observe system appearance changes
        systemAppearanceObserver = NSApp.observe(\.effectiveAppearance, options: [.new]) { [weak self] _, _ in
            DispatchQueue.main.async {
                self?.updateEffectiveScheme()
            }
        }
        
        applyAppearance()
    }
    
    func applyAppearance() {
        NSApp.appearance = mode.nsAppearance
        updateEffectiveScheme()
        
        // Update all visible windows
        DispatchQueue.main.async {
            for window in NSApp.windows where window.isVisible {
                window.appearance = self.mode.nsAppearance
                window.backgroundColor = LxTheme.resolvedWindowBackground(for: self.resolvedScheme)
                window.invalidateShadow()
            }
        }
    }
    
    private func updateEffectiveScheme() {
        switch mode {
        case .light:  effectiveColorScheme = .light
        case .dark:   effectiveColorScheme = .dark
        case .system: effectiveColorScheme = nil  // Let SwiftUI follow system
        }
        objectWillChange.send()
    }
    
    /// The currently active color scheme (resolved from mode + system)
    var resolvedScheme: ColorScheme {
        switch mode {
        case .light: return .light
        case .dark:  return .dark
        case .system:
            let appearance = NSApp.effectiveAppearance
            return appearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua ? .dark : .light
        }
    }
    
    var isDark: Bool { resolvedScheme == .dark }
}
