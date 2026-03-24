// FirestoreService.swift — Firestore CRUD for Lynrix user profiles
// Collection: users/{uid}

import Foundation
import FirebaseFirestore
import os.log

private let fsLogger = Logger(subsystem: "com.lynrix.trader", category: "Firestore")

final class FirestoreService {
    static let shared = FirestoreService()
    
    private let db = Firestore.firestore()
    private let usersCollection = "users"
    
    private init() {}
    
    // MARK: - Create or Update Profile
    
    /// Creates or fully updates a user profile document in Firestore.
    /// Uses merge to avoid overwriting fields set by other services (e.g. billing).
    func saveProfile(_ profile: UserProfile, completion: ((Error?) -> Void)? = nil) {
        let ref = db.collection(usersCollection).document(profile.userId)
        let data = profile.toFirestoreData()
        
        ref.setData(data, merge: true) { error in
            if let error = error {
                fsLogger.error("Failed to save profile \(profile.userId): \(error.localizedDescription)")
            } else {
                fsLogger.info("Profile saved for \(profile.userId)")
            }
            completion?(error)
        }
    }
    
    // MARK: - Fetch Profile
    
    /// Fetches a user profile from Firestore by UID.
    func fetchProfile(uid: String, completion: @escaping (UserProfile?) -> Void) {
        let ref = db.collection(usersCollection).document(uid)
        
        ref.getDocument { snapshot, error in
            if let error = error {
                fsLogger.error("Failed to fetch profile \(uid): \(error.localizedDescription)")
                completion(nil)
                return
            }
            
            guard let data = snapshot?.data() else {
                fsLogger.info("No profile document found for \(uid)")
                completion(nil)
                return
            }
            
            let profile = UserProfile.fromFirestoreData(data, uid: uid)
            if profile == nil {
                fsLogger.error("Failed to parse profile document for \(uid)")
            }
            completion(profile)
        }
    }
    
    // MARK: - Update Last Login
    
    func updateLastLogin(uid: String) {
        let ref = db.collection(usersCollection).document(uid)
        ref.updateData([
            "lastLoginAt": Date().timeIntervalSince1970
        ]) { error in
            if let error = error {
                fsLogger.error("Failed to update lastLogin for \(uid): \(error.localizedDescription)")
            }
        }
    }
    
    // MARK: - Update Specific Fields
    
    func updateFields(uid: String, fields: [String: Any], completion: ((Error?) -> Void)? = nil) {
        let ref = db.collection(usersCollection).document(uid)
        ref.updateData(fields) { error in
            if let error = error {
                fsLogger.error("Failed to update fields for \(uid): \(error.localizedDescription)")
            }
            completion?(error)
        }
    }
    
    // MARK: - Check Username Availability
    
    func isUsernameTaken(_ username: String, completion: @escaping (Bool) -> Void) {
        let normalized = username.lowercased().trimmingCharacters(in: .whitespaces)
        guard !normalized.isEmpty else {
            completion(false)
            return
        }
        
        db.collection(usersCollection)
            .whereField("username", isEqualTo: normalized)
            .limit(to: 1)
            .getDocuments { snapshot, error in
                if let error = error {
                    fsLogger.error("Username check failed: \(error.localizedDescription)")
                    completion(false)
                    return
                }
                completion(!(snapshot?.documents.isEmpty ?? true))
            }
    }
    
    // MARK: - Delete Profile
    
    func deleteProfile(uid: String, completion: ((Error?) -> Void)? = nil) {
        let ref = db.collection(usersCollection).document(uid)
        ref.delete { error in
            if let error = error {
                fsLogger.error("Failed to delete profile \(uid): \(error.localizedDescription)")
            }
            completion?(error)
        }
    }
}
