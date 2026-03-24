// UserProfile.swift — Unified user model for Lynrix v2.5
// Firebase Auth + Firestore backed

import Foundation

// MARK: - Auth Provider

enum AuthProvider: String, Codable, Equatable {
    case google = "google"
    case email  = "email"
    
    var displayName: String {
        switch self {
        case .google: return "Google"
        case .email:  return "Email"
        }
    }
    
    var icon: String {
        switch self {
        case .google: return "globe"
        case .email:  return "envelope.fill"
        }
    }
}

// MARK: - Gender

enum Gender: String, Codable, Equatable, CaseIterable {
    case male    = "male"
    case female  = "female"
    case other   = "other"
    
    var locKey: String {
        switch self {
        case .male:   return "gender.male"
        case .female: return "gender.female"
        case .other:  return "gender.other"
        }
    }
}

// MARK: - Subscription Plan

enum SubscriptionPlan: String, Codable, Equatable, CaseIterable {
    case free       = "free"
    case starter    = "starter"
    case pro        = "pro"
    case research   = "research"
    
    var locKey: String {
        switch self {
        case .free:     return "plan.free"
        case .starter:  return "plan.starter"
        case .pro:      return "plan.pro"
        case .research: return "plan.research"
        }
    }
    
    var isPremium: Bool {
        self != .free
    }
}

// MARK: - Subscription State

enum SubscriptionState: String, Codable, Equatable {
    case active    = "active"
    case expired   = "expired"
    case trial     = "trial"
    case cancelled = "cancelled"
    case none      = "none"
    
    var locKey: String {
        switch self {
        case .active:    return "sub.active"
        case .expired:   return "sub.expired"
        case .trial:     return "sub.trial"
        case .cancelled: return "sub.cancelled"
        case .none:      return "sub.none"
        }
    }
    
    var isValid: Bool {
        self == .active || self == .trial
    }
}

// MARK: - Account Status

enum AccountStatus: String, Codable, Equatable {
    case active     = "active"
    case suspended  = "suspended"
    case deactivated = "deactivated"
}

// MARK: - User Profile

struct UserProfile: Codable, Equatable {
    var userId: String
    var provider: AuthProvider
    
    // Identity
    var firstName: String
    var lastName: String
    var gender: Gender?
    var dateOfBirth: Date?
    var username: String?
    var email: String
    
    // Display
    var profilePhotoURL: String?
    var profilePhotoPath: String?
    
    // Timestamps
    var createdAt: Date
    var lastLoginAt: Date
    
    // Subscription
    var plan: SubscriptionPlan
    var subscriptionState: SubscriptionState
    
    // Account
    var accountStatus: AccountStatus
    
    // MARK: - Computed
    
    var displayName: String {
        let full = "\(firstName) \(lastName)".trimmingCharacters(in: .whitespaces)
        return full.isEmpty ? (username ?? email) : full
    }
    
    var isPremium: Bool {
        plan.isPremium && subscriptionState.isValid
    }
    
    var initials: String {
        let f = firstName.prefix(1)
        let l = lastName.prefix(1)
        if !f.isEmpty && !l.isEmpty {
            return "\(f)\(l)".uppercased()
        }
        return String(displayName.prefix(2)).uppercased()
    }
    
    // Backward compatibility
    var login: String? { username }
    var avatarURL: String? { profilePhotoURL }
    
    // MARK: - Factory: Google User
    
    static func fromGoogle(
        uid: String,
        email: String,
        fullName: String?,
        photoURL: String?,
        creationDate: Date?
    ) -> UserProfile {
        let parts = (fullName ?? "").split(separator: " ", maxSplits: 1)
        return UserProfile(
            userId: uid,
            provider: .google,
            firstName: parts.first.map(String.init) ?? "",
            lastName: parts.count > 1 ? String(parts[1]) : "",
            gender: nil,
            dateOfBirth: nil,
            username: nil,
            email: email,
            profilePhotoURL: photoURL,
            profilePhotoPath: nil,
            createdAt: creationDate ?? Date(),
            lastLoginAt: Date(),
            plan: .free,
            subscriptionState: .active,
            accountStatus: .active
        )
    }
    
    // MARK: - Factory: Email/Password User
    
    static func fromEmailRegistration(
        uid: String,
        email: String,
        firstName: String,
        lastName: String,
        gender: Gender,
        dateOfBirth: Date,
        username: String?
    ) -> UserProfile {
        UserProfile(
            userId: uid,
            provider: .email,
            firstName: firstName,
            lastName: lastName,
            gender: gender,
            dateOfBirth: dateOfBirth,
            username: username,
            email: email,
            profilePhotoURL: nil,
            profilePhotoPath: nil,
            createdAt: Date(),
            lastLoginAt: Date(),
            plan: .free,
            subscriptionState: .active,
            accountStatus: .active
        )
    }
    
    // MARK: - Firestore Serialization
    
    func toFirestoreData() -> [String: Any] {
        var data: [String: Any] = [
            "uid": userId,
            "provider": provider.rawValue,
            "firstName": firstName,
            "lastName": lastName,
            "email": email,
            "plan": plan.rawValue,
            "subscriptionState": subscriptionState.rawValue,
            "accountStatus": accountStatus.rawValue,
            "createdAt": createdAt.timeIntervalSince1970,
            "lastLoginAt": Date().timeIntervalSince1970
        ]
        if let g = gender { data["gender"] = g.rawValue }
        if let d = dateOfBirth { data["dateOfBirth"] = d.timeIntervalSince1970 }
        if let u = username { data["username"] = u }
        if let p = profilePhotoURL { data["profilePhotoURL"] = p }
        return data
    }
    
    static func fromFirestoreData(_ data: [String: Any], uid: String) -> UserProfile? {
        guard let providerRaw = data["provider"] as? String,
              let provider = AuthProvider(rawValue: providerRaw),
              let email = data["email"] as? String else {
            return nil
        }
        
        let planRaw = data["plan"] as? String ?? "free"
        let subRaw = data["subscriptionState"] as? String ?? "active"
        let statusRaw = data["accountStatus"] as? String ?? "active"
        
        var genderVal: Gender?
        if let gRaw = data["gender"] as? String { genderVal = Gender(rawValue: gRaw) }
        
        var dobVal: Date?
        if let dobTs = data["dateOfBirth"] as? TimeInterval { dobVal = Date(timeIntervalSince1970: dobTs) }
        
        let createdTs = data["createdAt"] as? TimeInterval ?? Date().timeIntervalSince1970
        let lastLoginTs = data["lastLoginAt"] as? TimeInterval ?? Date().timeIntervalSince1970
        
        return UserProfile(
            userId: uid,
            provider: provider,
            firstName: data["firstName"] as? String ?? "",
            lastName: data["lastName"] as? String ?? "",
            gender: genderVal,
            dateOfBirth: dobVal,
            username: data["username"] as? String,
            email: email,
            profilePhotoURL: data["profilePhotoURL"] as? String,
            profilePhotoPath: nil,
            createdAt: Date(timeIntervalSince1970: createdTs),
            lastLoginAt: Date(timeIntervalSince1970: lastLoginTs),
            plan: SubscriptionPlan(rawValue: planRaw) ?? .free,
            subscriptionState: SubscriptionState(rawValue: subRaw) ?? .active,
            accountStatus: AccountStatus(rawValue: statusRaw) ?? .active
        )
    }
}
