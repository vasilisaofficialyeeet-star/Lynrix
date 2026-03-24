// KeychainManager.swift — Secure API key storage via macOS Keychain (Lynrix v2.5)

import Foundation
import Security

final class KeychainManager {
    
    static let shared = KeychainManager()
    
    private let service = "com.lynrix.trader"
    private let apiKeyAccount = "api_key"
    private let apiSecretAccount = "api_secret"
    
    private init() {}
    
    // MARK: - Public API
    
    func saveAPIKey(_ key: String) throws {
        try save(account: apiKeyAccount, value: key)
    }
    
    func saveAPISecret(_ secret: String) throws {
        try save(account: apiSecretAccount, value: secret)
    }
    
    func loadAPIKey() -> String? {
        return load(account: apiKeyAccount)
    }
    
    func loadAPISecret() -> String? {
        return load(account: apiSecretAccount)
    }
    
    func deleteCredentials() {
        delete(account: apiKeyAccount)
        delete(account: apiSecretAccount)
    }
    
    var hasCredentials: Bool {
        loadAPIKey() != nil && loadAPISecret() != nil
    }
    
    // MARK: - Private Keychain Operations
    
    private func save(account: String, value: String) throws {
        guard let data = value.data(using: .utf8) else {
            throw KeychainError.encodingFailed
        }
        
        delete(account: account)
        
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecValueData as String: data,
            kSecAttrAccessible as String: kSecAttrAccessibleWhenUnlockedThisDeviceOnly
        ]
        
        let status = SecItemAdd(query as CFDictionary, nil)
        guard status == errSecSuccess else {
            throw KeychainError.saveFailed(status)
        }
    }
    
    private func load(account: String) -> String? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]
        
        var result: AnyObject?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        
        guard status == errSecSuccess, let data = result as? Data else {
            return nil
        }
        
        return String(data: data, encoding: .utf8)
    }
    
    private func delete(account: String) {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]
        SecItemDelete(query as CFDictionary)
    }
    
    // MARK: - Errors
    
    enum KeychainError: LocalizedError {
        case encodingFailed
        case saveFailed(OSStatus)
        
        var errorDescription: String? {
            switch self {
            case .encodingFailed:
                return "Failed to encode value"
            case .saveFailed(let status):
                let msg = SecCopyErrorMessageString(status, nil) as String? ?? "Unknown error"
                return "Keychain save failed: \(msg) (\(status))"
            }
        }
    }
}
