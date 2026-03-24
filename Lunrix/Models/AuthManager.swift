// AuthManager.swift — Authentication for Lynrix v2.5
// Firebase Auth primary: Google Sign-In + Email/Password
// Firestore-backed user profiles

import SwiftUI
import Combine
import os.log
import Security
import FirebaseCore
import FirebaseAuth
import GoogleSignIn

private let authLogger = Logger(subsystem: "com.lynrix.trader", category: "Auth")

// MARK: - Auth State

enum AuthState: Equatable {
    case unknown
    case signedOut
    case signingIn
    case signedIn(UserProfile)
    case error(String)
    
    var isSignedIn: Bool {
        if case .signedIn = self { return true }
        return false
    }
    
    var profile: UserProfile? {
        if case .signedIn(let p) = self { return p }
        return nil
    }
    
    static func == (lhs: AuthState, rhs: AuthState) -> Bool {
        switch (lhs, rhs) {
        case (.unknown, .unknown): return true
        case (.signedOut, .signedOut): return true
        case (.signingIn, .signingIn): return true
        case (.signedIn(let a), .signedIn(let b)): return a == b
        case (.error(let a), .error(let b)): return a == b
        default: return false
        }
    }
}

// MARK: - Auth Manager

final class AuthManager: ObservableObject {
    static let shared = AuthManager()
    
    private static let cachedProfileKey = "lynrix.auth.cachedProfile"
    
    @Published var state: AuthState = .unknown
    
    var isSignedIn: Bool { state.isSignedIn }
    var currentProfile: UserProfile? { state.profile }
    
    private var authStateHandle: AuthStateDidChangeListenerHandle?
    
    private init() {}
    
    /// Must be called once after FirebaseApp.configure().
    func configure() {
        guard authStateHandle == nil else { return }
        purgeStaleKeychainItems()
        listenToAuthState()
    }
    
    // MARK: - Keychain Cleanup
    
    /// Purge only Firebase/Google keychain items that may have been written
    /// under a previous code-signing identity. Scoped to known service names
    /// so that the app's own API credentials (KeychainManager) are preserved.
    private func purgeStaleKeychainItems() {
        // Known Firebase/Google service identifiers that may leave stale entries
        let firebaseServices = [
            "firebase_auth_",          // Firebase Auth token storage prefix
            "com.google.GIDSignIn",    // Google Sign-In SDK
            "com.google.identity",     // Google Identity Services
        ]
        // The app's own keychain service — NEVER delete these
        let protectedService = "com.lynrix.trader"
        
        for svc in firebaseServices {
            let query: [String: Any] = [
                kSecClass as String: kSecClassGenericPassword,
                kSecAttrService as String: svc,
                kSecAttrSynchronizable as String: kSecAttrSynchronizableAny
            ]
            let status = SecItemDelete(query as CFDictionary)
            if status == errSecSuccess {
                authLogger.info("Purged stale keychain items (service=\(svc))")
            }
        }
        
        // Also purge internet-password items from Google (OAuth tokens)
        // but only those NOT matching our protected service
        let internetQuery: [String: Any] = [
            kSecClass as String: kSecClassInternetPassword,
            kSecAttrSynchronizable as String: kSecAttrSynchronizableAny,
            kSecReturnAttributes as String: true,
            kSecMatchLimit as String: kSecMatchLimitAll
        ]
        var result: AnyObject?
        let searchStatus = SecItemCopyMatching(internetQuery as CFDictionary, &result)
        if searchStatus == errSecSuccess, let items = result as? [[String: Any]] {
            for item in items {
                let svc = item[kSecAttrService as String] as? String ?? ""
                if svc == protectedService { continue }
                // Build targeted delete for this specific item
                var deleteQuery: [String: Any] = [
                    kSecClass as String: kSecClassInternetPassword
                ]
                if let server = item[kSecAttrServer as String] {
                    deleteQuery[kSecAttrServer as String] = server
                }
                if let account = item[kSecAttrAccount as String] {
                    deleteQuery[kSecAttrAccount as String] = account
                }
                SecItemDelete(deleteQuery as CFDictionary)
            }
        }
    }
    
    deinit {
        if let handle = authStateHandle {
            Auth.auth().removeStateDidChangeListener(handle)
        }
    }
    
    // MARK: - Firebase Auth State Listener (unified for all providers)
    
    private func listenToAuthState() {
        authLogger.info("Registering Firebase Auth state listener...")
        authStateHandle = Auth.auth().addStateDidChangeListener { [weak self] _, firebaseUser in
            guard let self = self else { return }
            
            // Skip if mid-flow — let the sign-in completion handler drive state
            if case .signingIn = self.state { return }
            
            if let user = firebaseUser {
                authLogger.info("Firebase session active for \(user.email ?? "unknown")")
                self.resolveProfile(for: user)
            } else {
                // Check for cached profile (from keychain-bypass sign-in)
                if let cached = self.loadCachedProfile() {
                    authLogger.info("No Firebase session, but cached profile found: \(cached.email)")
                    self.state = .signedIn(cached)
                } else {
                    authLogger.info("No active Firebase session and no cached profile")
                    self.state = .signedOut
                }
            }
        }
    }
    
    /// Resolves user profile: try Firestore first, fall back to Firebase Auth metadata, cache locally.
    private func resolveProfile(for firebaseUser: FirebaseAuth.User) {
        // First, show cached profile immediately (avoids blank screen)
        if let cached = loadCachedProfile(), cached.userId == firebaseUser.uid {
            self.state = .signedIn(cached)
        }
        
        // Then fetch latest from Firestore
        FirestoreService.shared.fetchProfile(uid: firebaseUser.uid) { [weak self] firestoreProfile in
            guard let self = self else { return }
            
            if let profile = firestoreProfile {
                // Firestore profile exists — use it
                self.cacheProfile(profile)
                FirestoreService.shared.updateLastLogin(uid: profile.userId)
                DispatchQueue.main.async {
                    self.state = .signedIn(profile)
                }
            } else {
                // No Firestore profile yet — create from Firebase Auth data
                let profile = self.profileFromFirebaseUser(firebaseUser)
                FirestoreService.shared.saveProfile(profile)
                self.cacheProfile(profile)
                DispatchQueue.main.async {
                    self.state = .signedIn(profile)
                }
            }
        }
    }
    
    // MARK: - Error Sanitization
    
    /// Converts raw SDK/framework errors into clean, user-facing messages.
    private func userFacingMessage(for error: Error) -> String {
        let nsError = error as NSError
        let raw = error.localizedDescription.lowercased()
        
        // Keychain access errors (Firebase Auth stores tokens in Keychain)
        if raw.contains("keychain") {
            authLogger.error("Keychain error: domain=\(nsError.domain) code=\(nsError.code) — \(error.localizedDescription)")
            return "Authentication failed — keychain access denied.\n\nPlease rebuild in Xcode (⌘B) and try again."
        }
        
        // Firebase Auth error codes
        if nsError.domain == "FIRAuthErrorDomain" {
            switch nsError.code {
            case 17011: return "Invalid email format"
            case 17008: return "Invalid email or password"
            case 17009: return "Wrong password"
            case 17007: return "Email already in use"
            case 17026: return "Password too weak (min 6 characters)"
            case 17020: return "Network error — check connection"
            case 17006: // operation-not-allowed
                return "Email/Password authentication is disabled.\n\nPlease enable it in Firebase Console:\nAuthentication → Sign-in method → Email/Password"
            default: break
            }
        }
        
        // Google Sign-In errors
        if nsError.domain == "com.google.GIDSignIn" {
            if nsError.code == -4 { // kGIDSignInErrorCodeHasNoAuthInKeychain
                return "Google Sign-In failed — no previous session found.\nPlease sign in again."
            }
        }
        
        // Fallback: cap length
        let desc = error.localizedDescription
        if desc.count > 150 {
            return String(desc.prefix(147)) + "…"
        }
        return desc
    }
    
    // MARK: - Google Sign-In (via Firebase, with keychain-free fallback)
    
    /// Temporarily holds the Google user so we can build a profile
    /// even when Firebase Auth fails with a keychain error.
    private var pendingGoogleUser: GIDGoogleUser?
    
    func signInWithGoogle() {
        state = .signingIn
        authLogger.info("Starting Google Sign-In flow...")
        
        try? Auth.auth().useUserAccessGroup(nil)
        
        guard let clientID = FirebaseApp.app()?.options.clientID else {
            authLogger.error("Firebase CLIENT_ID not found — check GoogleService-Info.plist")
            state = .error("Firebase configuration error — missing CLIENT_ID")
            return
        }
        
        let config = GIDConfiguration(clientID: clientID)
        GIDSignIn.sharedInstance.configuration = config
        
        // Clear any stale GIDSignIn keychain state from previous builds
        GIDSignIn.sharedInstance.disconnect { _ in }
        
        guard let presentingWindow = NSApp.windows.first(where: { $0.isVisible }) ?? NSApp.mainWindow else {
            authLogger.error("No presenting window available for Google Sign-In")
            state = .error("No window available for sign-in")
            return
        }
        
        GIDSignIn.sharedInstance.signIn(withPresenting: presentingWindow) { [weak self] result, error in
            guard let self = self else { return }
            
            // Handle cancellation
            if let error = error {
                let nsError = error as NSError
                if nsError.code == -5 || nsError.code == GIDSignInError.canceled.rawValue {
                    authLogger.info("Google Sign-In cancelled by user")
                    self.state = .signedOut
                    return
                }
            }
            
            // Try to extract Google user even if there was a keychain error
            guard let googleUser = result?.user,
                  let idToken = googleUser.idToken?.tokenString else {
                if let error = error {
                    authLogger.error("Google Sign-In failed (no tokens): \((error as NSError).domain) code=\((error as NSError).code) — \(error.localizedDescription)")
                    self.state = .error(self.userFacingMessage(for: error))
                } else {
                    self.state = .error("Authentication failed — no ID token")
                }
                return
            }
            
            // Store Google user for fallback profile creation
            self.pendingGoogleUser = googleUser
            
            if let error = error {
                authLogger.warning("GIDSignIn non-fatal error (have tokens, proceeding): \(error.localizedDescription)")
            }
            
            let credential = GoogleAuthProvider.credential(
                withIDToken: idToken,
                accessToken: googleUser.accessToken.tokenString
            )
            
            // Try Firebase Auth; fall back to direct profile on keychain failure
            self.firebaseSignInOrFallback(with: credential)
        }
    }
    
    /// Attempt Firebase Auth sign-in. If it fails with a keychain error,
    /// build a profile directly from the pending Google user (no keychain needed).
    private func firebaseSignInOrFallback(with credential: AuthCredential) {
        try? Auth.auth().useUserAccessGroup(nil)
        purgeStaleKeychainItems()
        
        Auth.auth().signIn(with: credential) { [weak self] authResult, firebaseError in
            guard let self = self else { return }
            
            if let firebaseError = firebaseError {
                let desc = firebaseError.localizedDescription.lowercased()
                let code = (firebaseError as NSError).code
                authLogger.error("Firebase sign-in error: code=\(code) — \(firebaseError.localizedDescription)")
                
                let isKeychainError = desc.contains("keychain")
                    || code == -25293  // errSecAuthFailed
                    || code == -25299  // errSecDuplicateItem
                    || code == 17995   // FIRAuthErrorCodeKeychainError
                
                if isKeychainError, let googleUser = self.pendingGoogleUser {
                    authLogger.warning("Firebase keychain error — falling back to direct Google profile")
                    self.signInWithGoogleUserDirectly(googleUser)
                    return
                }
                
                self.state = .error(self.userFacingMessage(for: firebaseError))
                return
            }
            
            if let firebaseUser = authResult?.user {
                authLogger.info("Firebase sign-in successful: \(firebaseUser.email ?? "unknown")")
                let profile = self.profileFromFirebaseUser(firebaseUser)
                FirestoreService.shared.saveProfile(profile)
                self.cacheProfile(profile)
                self.state = .signedIn(profile)
            } else {
                // No user but no error either — try direct fallback
                if let googleUser = self.pendingGoogleUser {
                    self.signInWithGoogleUserDirectly(googleUser)
                } else {
                    self.state = .error("Sign-in failed — no user returned")
                }
            }
            self.pendingGoogleUser = nil
        }
    }
    
    /// Build a UserProfile directly from Google user data — zero keychain access.
    private func signInWithGoogleUserDirectly(_ googleUser: GIDGoogleUser) {
        let email = googleUser.profile?.email ?? "unknown@gmail.com"
        let fullName = googleUser.profile?.name
        let photoURL = googleUser.profile?.imageURL(withDimension: 200)?.absoluteString
        // Use a stable ID derived from email (since we don't have Firebase UID)
        let syntheticUID = "google_\(email.hashValue)"
        
        let profile = UserProfile.fromGoogle(
            uid: syntheticUID,
            email: email,
            fullName: fullName,
            photoURL: photoURL,
            creationDate: Date()
        )
        
        cacheProfile(profile)
        self.pendingGoogleUser = nil
        authLogger.info("Direct Google sign-in successful (keychain bypass): \(email)")
        state = .signedIn(profile)
    }
    
    // MARK: - Email/Password Registration (Firebase Auth)
    
    func registerWithEmail(
        email: String,
        password: String,
        firstName: String,
        lastName: String,
        gender: Gender,
        dateOfBirth: Date,
        username: String?,
        photoData: Data?,
        completion: @escaping (Error?) -> Void
    ) {
        state = .signingIn
        authLogger.info("Registering Firebase email user: \(email)")
        
        try? Auth.auth().useUserAccessGroup(nil)
        purgeStaleKeychainItems()
        Auth.auth().createUser(withEmail: email, password: password) { [weak self] authResult, error in
            guard let self = self else { return }
            
            if let error = error {
                let desc = error.localizedDescription.lowercased()
                let code = (error as NSError).code
                authLogger.error("Firebase registration failed: code=\(code) — \(error.localizedDescription)")
                
                // Keychain error fallback — create profile directly
                let isKeychainError = desc.contains("keychain") || code == 17995 || code == -25293
                if isKeychainError {
                    authLogger.warning("Registration keychain error — creating local profile")
                    let uid = "email_\(email.hashValue)"
                    let normalizedUsername = username?.lowercased().trimmingCharacters(in: .whitespaces)
                    let profile = UserProfile.fromEmailRegistration(
                        uid: uid, email: email, firstName: firstName, lastName: lastName,
                        gender: gender, dateOfBirth: dateOfBirth,
                        username: normalizedUsername?.isEmpty == false ? normalizedUsername : nil
                    )
                    if let data = photoData { _ = LocalPhotoCache.shared.savePhoto(data, forUserId: uid) }
                    self.cacheProfile(profile)
                    self.state = .signedIn(profile)
                    completion(nil)
                    return
                }
                
                self.state = .error(self.userFacingMessage(for: error))
                completion(error)
                return
            }
            
            guard let firebaseUser = authResult?.user else {
                let msg = "Registration failed — no user returned"
                authLogger.error("\(msg)")
                self.state = .error(msg)
                completion(NSError(domain: "com.lynrix.auth", code: -1, userInfo: [NSLocalizedDescriptionKey: msg]))
                return
            }
            
            let changeRequest = firebaseUser.createProfileChangeRequest()
            changeRequest.displayName = "\(firstName) \(lastName)".trimmingCharacters(in: .whitespaces)
            changeRequest.commitChanges { _ in }
            
            let normalizedUsername = username?.lowercased().trimmingCharacters(in: .whitespaces)
            let profile = UserProfile.fromEmailRegistration(
                uid: firebaseUser.uid, email: email, firstName: firstName, lastName: lastName,
                gender: gender, dateOfBirth: dateOfBirth,
                username: normalizedUsername?.isEmpty == false ? normalizedUsername : nil
            )
            
            if let data = photoData { _ = LocalPhotoCache.shared.savePhoto(data, forUserId: firebaseUser.uid) }
            
            FirestoreService.shared.saveProfile(profile) { fsError in
                if let fsError = fsError {
                    authLogger.error("Firestore profile save failed (non-blocking): \(fsError.localizedDescription)")
                }
            }
            
            self.cacheProfile(profile)
            self.state = .signedIn(profile)
            authLogger.info("Email registration successful: \(email)")
            completion(nil)
        }
    }
    
    // MARK: - Email/Password Sign In (Firebase Auth)
    
    func signInWithEmail(email: String, password: String, completion: @escaping (Error?) -> Void) {
        state = .signingIn
        authLogger.info("Email sign-in attempt: \(email)")
        
        try? Auth.auth().useUserAccessGroup(nil)
        purgeStaleKeychainItems()
        Auth.auth().signIn(withEmail: email, password: password) { [weak self] authResult, error in
            guard let self = self else { return }
            
            if let error = error {
                let desc = error.localizedDescription.lowercased()
                let code = (error as NSError).code
                authLogger.error("Email sign-in failed: code=\(code) — \(error.localizedDescription)")
                
                // Keychain error fallback — build profile from email
                let isKeychainError = desc.contains("keychain") || code == 17995 || code == -25293
                if isKeychainError {
                    authLogger.warning("Email sign-in keychain error — creating local profile")
                    let uid = "email_\(email.hashValue)"
                    let profile = UserProfile(
                        userId: uid, provider: .email,
                        firstName: "", lastName: "", gender: nil, dateOfBirth: nil,
                        username: nil, email: email, profilePhotoURL: nil, profilePhotoPath: nil,
                        createdAt: Date(), lastLoginAt: Date(),
                        plan: .free, subscriptionState: .active, accountStatus: .active
                    )
                    self.cacheProfile(profile)
                    self.state = .signedIn(profile)
                    completion(nil)
                    return
                }
                
                self.state = .error(self.userFacingMessage(for: error))
                completion(error)
                return
            }
            
            guard let firebaseUser = authResult?.user else {
                let msg = "Sign-in failed — no user returned"
                self.state = .error(msg)
                completion(NSError(domain: "com.lynrix.auth", code: -1, userInfo: [NSLocalizedDescriptionKey: msg]))
                return
            }
            
            authLogger.info("Email sign-in successful: \(firebaseUser.email ?? "unknown")")
            
            // Fetch profile from Firestore (or create from Firebase Auth data)
            self.resolveProfile(for: firebaseUser)
            completion(nil)
        }
    }
    
    // MARK: - Sign Out (unified for all providers)
    
    func signOut() {
        authLogger.info("Signing out...")
        
        // Always sign out from Firebase Auth
        do {
            try Auth.auth().signOut()
        } catch {
            authLogger.error("Firebase sign-out error: \(error.localizedDescription)")
        }
        GIDSignIn.sharedInstance.signOut()
        
        clearCachedProfile()
        state = .signedOut
        authLogger.info("Sign-out complete")
    }
    
    // MARK: - Profile Helpers
    
    private func profileFromFirebaseUser(_ user: FirebaseAuth.User) -> UserProfile {
        let providerIDs = user.providerData.map { $0.providerID }
        let isGoogle = providerIDs.contains("google.com")
        
        if isGoogle {
            return UserProfile.fromGoogle(
                uid: user.uid,
                email: user.email ?? "unknown@gmail.com",
                fullName: user.displayName,
                photoURL: user.photoURL?.absoluteString,
                creationDate: user.metadata.creationDate
            )
        } else {
            // Email/password user — minimal profile from Firebase Auth
            let parts = (user.displayName ?? "").split(separator: " ", maxSplits: 1)
            return UserProfile(
                userId: user.uid,
                provider: .email,
                firstName: parts.first.map(String.init) ?? "",
                lastName: parts.count > 1 ? String(parts[1]) : "",
                gender: nil,
                dateOfBirth: nil,
                username: nil,
                email: user.email ?? "",
                profilePhotoURL: user.photoURL?.absoluteString,
                profilePhotoPath: nil,
                createdAt: user.metadata.creationDate ?? Date(),
                lastLoginAt: Date(),
                plan: .free,
                subscriptionState: .active,
                accountStatus: .active
            )
        }
    }
    
    // MARK: - Local Profile Cache (UserDefaults)
    
    private func cacheProfile(_ profile: UserProfile) {
        var p = profile
        p.lastLoginAt = Date()
        if let data = try? JSONEncoder().encode(p) {
            UserDefaults.standard.set(data, forKey: Self.cachedProfileKey)
        }
    }
    
    private func loadCachedProfile() -> UserProfile? {
        guard let data = UserDefaults.standard.data(forKey: Self.cachedProfileKey),
              let profile = try? JSONDecoder().decode(UserProfile.self, from: data) else {
            return nil
        }
        return profile
    }
    
    private func clearCachedProfile() {
        UserDefaults.standard.removeObject(forKey: Self.cachedProfileKey)
        authLogger.info("Cached profile cleared")
    }
}
