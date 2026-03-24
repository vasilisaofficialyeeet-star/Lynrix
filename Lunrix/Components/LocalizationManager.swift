// LocalizationManager.swift — Runtime language switching for Lynrix v2.5

import SwiftUI
import Combine

final class LocalizationManager: ObservableObject {
    static let shared = LocalizationManager()
    
    enum Language: String, CaseIterable, Identifiable {
        case system = "system"
        case en = "en"
        case ru = "ru"
        case zhHans = "zh-Hans"
        
        var id: String { rawValue }
        
        var displayName: String {
            switch self {
            case .system: return "System"
            case .en:     return "English"
            case .ru:     return "Русский"
            case .zhHans: return "中文(简体)"
            }
        }
        
        var flag: String {
            switch self {
            case .system: return "🌐"
            case .en:     return "🇬🇧"
            case .ru:     return "🇷🇺"
            case .zhHans: return "🇨🇳"
            }
        }
    }
    
    @Published private(set) var bundle: Bundle
    
    @Published var currentLanguage: Language {
        didSet {
            UserDefaults.standard.set(currentLanguage.rawValue, forKey: "app_language")
            loadBundle()
        }
    }
    
    private init() {
        let saved = UserDefaults.standard.string(forKey: "app_language") ?? "system"
        let lang = Language(rawValue: saved) ?? .system
        self.currentLanguage = lang
        self.bundle = .main
        loadBundle()
    }
    
    private func loadBundle() {
        let code = resolvedLanguageCode
        if let path = Bundle.main.path(forResource: code, ofType: "lproj"),
           let b = Bundle(path: path) {
            bundle = b
        } else if let path = Bundle.main.path(forResource: "en", ofType: "lproj"),
                  let b = Bundle(path: path) {
            bundle = b
        } else {
            bundle = .main
        }
    }
    
    var resolvedLanguageCode: String {
        if currentLanguage == .system {
            let preferred = Locale.preferredLanguages.first ?? "en"
            if preferred.hasPrefix("ru") { return "ru" }
            if preferred.hasPrefix("zh-Hans") || preferred.hasPrefix("zh-CN") { return "zh-Hans" }
            return "en"
        }
        return currentLanguage.rawValue
    }
    
    // MARK: - Translation
    
    func t(_ key: String) -> String {
        bundle.localizedString(forKey: key, value: key, table: nil)
    }
    
    func t(_ key: String, _ args: CVarArg...) -> String {
        let format = bundle.localizedString(forKey: key, value: key, table: nil)
        return String(format: format, arguments: args)
    }
}
