// RegistrationView.swift — Premium registration for Lynrix v2.5
// Firebase Auth email/password + Firestore profile

import SwiftUI
import UniformTypeIdentifiers

struct RegistrationView: View {
    @EnvironmentObject var loc: LocalizationManager
    @EnvironmentObject var themeManager: ThemeManager
    @ObservedObject var auth: AuthManager
    @Environment(\.theme) var theme
    
    let onBack: () -> Void
    
    @State private var firstName: String = ""
    @State private var lastName: String = ""
    @State private var gender: Gender = .male
    @State private var dateOfBirth: Date = Calendar.current.date(byAdding: .year, value: -25, to: Date()) ?? Date()
    @State private var username: String = ""
    @State private var email: String = ""
    @State private var password: String = ""
    @State private var confirmPassword: String = ""
    @State private var photoData: Data?
    @State private var photoImage: NSImage?
    
    @State private var showError: Bool = false
    @State private var errorMessage: String = ""
    @State private var isSubmitting: Bool = false
    
    private var isFormValid: Bool {
        !firstName.trimmingCharacters(in: .whitespaces).isEmpty &&
        !lastName.trimmingCharacters(in: .whitespaces).isEmpty &&
        !email.trimmingCharacters(in: .whitespaces).isEmpty &&
        email.contains("@") &&
        password.count >= 6 &&
        password == confirmPassword
    }
    
    var body: some View {
        ScrollView {
            VStack(spacing: 0) {
                // Header
                headerSection
                
                Spacer().frame(height: 32)
                
                // Form
                VStack(spacing: 20) {
                    photoSection
                    nameSection
                    personalSection
                    credentialsSection
                }
                .frame(maxWidth: 400)
                
                Spacer().frame(height: 28)
                
                // Actions
                actionsSection
                    .frame(maxWidth: 400)
                
                Spacer().frame(height: 20)
                
                // Error
                if showError {
                    errorBanner
                        .frame(maxWidth: 400)
                }
            }
            .padding(40)
            .frame(maxWidth: .infinity)
        }
    }
    
    // MARK: - Header
    
    private var headerSection: some View {
        VStack(spacing: 8) {
            HStack {
                Button(action: onBack) {
                    HStack(spacing: 4) {
                        Image(systemName: "chevron.left")
                            .font(.system(size: 11, weight: .semibold))
                        Text(loc.t("auth.back"))
                            .font(LxFont.mono(11, weight: .medium))
                    }
                    .foregroundColor(theme.textTertiary)
                }
                .buttonStyle(.plain)
                Spacer()
            }
            
            Text(loc.t("auth.createAccount"))
                .font(LxFont.mono(22, weight: .bold))
                .foregroundColor(theme.textPrimary)
            
            Text(loc.t("auth.registerSubtitle"))
                .font(LxFont.mono(12))
                .foregroundColor(theme.textTertiary)
        }
    }
    
    // MARK: - Photo
    
    private var photoSection: some View {
        VStack(spacing: 8) {
            Button(action: selectPhoto) {
                ZStack {
                    Circle()
                        .fill(theme.backgroundSecondary)
                        .frame(width: 80, height: 80)
                    Circle()
                        .stroke(LxColor.electricCyan.opacity(0.2), lineWidth: 1)
                        .frame(width: 80, height: 80)
                    
                    if let img = photoImage {
                        Image(nsImage: img)
                            .resizable()
                            .scaledToFill()
                            .frame(width: 80, height: 80)
                            .clipShape(Circle())
                    } else {
                        VStack(spacing: 4) {
                            Image(systemName: "camera.fill")
                                .font(.system(size: 20))
                                .foregroundColor(LxColor.electricCyan.opacity(0.5))
                            Text(loc.t("auth.addPhoto"))
                                .font(LxFont.mono(8))
                                .foregroundColor(theme.textTertiary)
                        }
                    }
                }
            }
            .buttonStyle(.plain)
            
            Text(loc.t("auth.photoOptional"))
                .font(LxFont.mono(9))
                .foregroundColor(theme.textTertiary.opacity(0.6))
        }
    }
    
    // MARK: - Name Fields
    
    private var nameSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            sectionLabel(loc.t("auth.personalInfo"))
            
            HStack(spacing: 12) {
                fieldGroup(loc.t("auth.firstName"), text: $firstName, icon: "person")
                fieldGroup(loc.t("auth.lastName"), text: $lastName, icon: "person")
            }
        }
    }
    
    // MARK: - Personal Info
    
    private var personalSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 12) {
                // Gender
                VStack(alignment: .leading, spacing: 4) {
                    Text(loc.t("auth.gender"))
                        .font(LxFont.mono(9))
                        .foregroundColor(theme.textTertiary)
                    
                    Picker("", selection: $gender) {
                        ForEach(Gender.allCases, id: \.self) { g in
                            Text(loc.t(g.locKey)).tag(g)
                        }
                    }
                    .pickerStyle(.segmented)
                    .frame(height: 28)
                }
                
                // Date of Birth
                VStack(alignment: .leading, spacing: 4) {
                    Text(loc.t("auth.dateOfBirth"))
                        .font(LxFont.mono(9))
                        .foregroundColor(theme.textTertiary)
                    
                    DatePicker("", selection: $dateOfBirth, in: ...Date(), displayedComponents: .date)
                        .datePickerStyle(.field)
                        .labelsHidden()
                        .frame(height: 28)
                }
            }
        }
    }
    
    // MARK: - Credentials
    
    private var credentialsSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            sectionLabel(loc.t("auth.credentials"))
            
            // Email (required)
            VStack(alignment: .leading, spacing: 4) {
                Text(loc.t("auth.emailField"))
                    .font(LxFont.mono(9))
                    .foregroundColor(theme.textTertiary)
                
                HStack(spacing: 8) {
                    Image(systemName: "envelope")
                        .font(.system(size: 12))
                        .foregroundColor(theme.textTertiary)
                    TextField(loc.t("auth.emailPlaceholder"), text: $email)
                        .textFieldStyle(.plain)
                        .font(LxFont.mono(13))
                        .foregroundColor(theme.textPrimary)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 8)
                .background(
                    RoundedRectangle(cornerRadius: 8)
                        .fill(theme.backgroundSecondary)
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(theme.borderSubtle.opacity(0.3), lineWidth: 0.5)
                        )
                )
                
                if !email.isEmpty && !email.contains("@") {
                    Text(loc.t("auth.error.invalidEmail"))
                        .font(LxFont.mono(9))
                        .foregroundColor(LxColor.amber)
                }
            }
            
            // Username (optional)
            VStack(alignment: .leading, spacing: 4) {
                Text(loc.t("auth.usernameField"))
                    .font(LxFont.mono(9))
                    .foregroundColor(theme.textTertiary)
                
                HStack(spacing: 8) {
                    Image(systemName: "at")
                        .font(.system(size: 12))
                        .foregroundColor(theme.textTertiary)
                    TextField(loc.t("auth.usernamePlaceholder"), text: $username)
                        .textFieldStyle(.plain)
                        .font(LxFont.mono(13))
                        .foregroundColor(theme.textPrimary)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 8)
                .background(
                    RoundedRectangle(cornerRadius: 8)
                        .fill(theme.backgroundSecondary)
                        .overlay(
                            RoundedRectangle(cornerRadius: 8)
                                .stroke(theme.borderSubtle.opacity(0.3), lineWidth: 0.5)
                        )
                )
                
                Text(loc.t("auth.usernameOptional"))
                    .font(LxFont.mono(8))
                    .foregroundColor(theme.textTertiary.opacity(0.5))
            }
            
            // Password
            VStack(alignment: .leading, spacing: 4) {
                Text(loc.t("auth.password"))
                    .font(LxFont.mono(9))
                    .foregroundColor(theme.textTertiary)
                
                secureFieldStyled(loc.t("auth.passwordPlaceholder"), text: $password, icon: "lock")
                
                if !password.isEmpty && password.count < 6 {
                    Text(loc.t("auth.error.passwordShort"))
                        .font(LxFont.mono(9))
                        .foregroundColor(LxColor.amber)
                }
            }
            
            // Confirm Password
            VStack(alignment: .leading, spacing: 4) {
                Text(loc.t("auth.confirmPassword"))
                    .font(LxFont.mono(9))
                    .foregroundColor(theme.textTertiary)
                
                secureFieldStyled(loc.t("auth.confirmPasswordPlaceholder"), text: $confirmPassword, icon: "lock.rotation")
                
                if !confirmPassword.isEmpty && confirmPassword != password {
                    Text(loc.t("auth.error.passwordMismatch"))
                        .font(LxFont.mono(9))
                        .foregroundColor(LxColor.bloodRed)
                }
            }
        }
    }
    
    // MARK: - Actions
    
    private var actionsSection: some View {
        VStack(spacing: 12) {
            Button(action: submitRegistration) {
                HStack(spacing: 8) {
                    if isSubmitting {
                        ProgressView()
                            .scaleEffect(0.6)
                            .progressViewStyle(.circular)
                    } else {
                        Image(systemName: "person.badge.plus")
                            .font(.system(size: 14, weight: .semibold))
                    }
                    Text(loc.t("auth.register"))
                        .font(LxFont.mono(14, weight: .semibold))
                }
                .frame(maxWidth: .infinity)
                .frame(height: 44)
                .background(
                    RoundedRectangle(cornerRadius: 10)
                        .fill(isFormValid ? LxColor.electricCyan.opacity(0.15) : theme.backgroundSecondary)
                        .overlay(
                            RoundedRectangle(cornerRadius: 10)
                                .stroke(isFormValid ? LxColor.electricCyan.opacity(0.4) : theme.borderSubtle.opacity(0.2), lineWidth: 1)
                        )
                )
                .foregroundColor(isFormValid ? LxColor.electricCyan : theme.textTertiary)
            }
            .buttonStyle(.plain)
            .disabled(!isFormValid || isSubmitting)
        }
    }
    
    // MARK: - Error Banner
    
    private var errorBanner: some View {
        HStack(spacing: 6) {
            Image(systemName: "exclamationmark.triangle.fill")
                .font(.system(size: 11))
                .foregroundColor(LxColor.bloodRed)
            Text(errorMessage)
                .font(LxFont.mono(10))
                .foregroundColor(LxColor.bloodRed)
                .lineLimit(3)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(LxColor.bloodRed.opacity(0.06))
        )
        .onTapGesture { showError = false }
    }
    
    // MARK: - Helpers
    
    private func sectionLabel(_ text: String) -> some View {
        Text(text.uppercased())
            .font(LxFont.mono(9, weight: .bold))
            .foregroundColor(theme.textTertiary)
            .tracking(1)
    }
    
    private func fieldGroup(_ placeholder: String, text: Binding<String>, icon: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: icon)
                .font(.system(size: 12))
                .foregroundColor(theme.textTertiary)
            TextField(placeholder, text: text)
                .textFieldStyle(.plain)
                .font(LxFont.mono(13))
                .foregroundColor(theme.textPrimary)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(theme.backgroundSecondary)
                .overlay(
                    RoundedRectangle(cornerRadius: 8)
                        .stroke(theme.borderSubtle.opacity(0.3), lineWidth: 0.5)
                )
        )
    }
    
    private func secureFieldStyled(_ placeholder: String, text: Binding<String>, icon: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: icon)
                .font(.system(size: 12))
                .foregroundColor(theme.textTertiary)
            SecureField(placeholder, text: text)
                .textFieldStyle(.plain)
                .font(LxFont.mono(13))
                .foregroundColor(theme.textPrimary)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 8)
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(theme.backgroundSecondary)
                .overlay(
                    RoundedRectangle(cornerRadius: 8)
                        .stroke(theme.borderSubtle.opacity(0.3), lineWidth: 0.5)
                )
        )
    }
    
    // MARK: - Actions
    
    private func selectPhoto() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.image]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.message = loc.t("auth.selectPhoto")
        panel.begin { result in
            if result == .OK, let url = panel.url {
                if let img = NSImage(contentsOf: url) {
                    photoImage = img
                    photoData = img.tiffRepresentation.flatMap {
                        NSBitmapImageRep(data: $0)?.representation(using: .jpeg, properties: [.compressionFactor: 0.8])
                    }
                }
            }
        }
    }
    
    private func submitRegistration() {
        showError = false
        isSubmitting = true
        
        let trimmedEmail = email.trimmingCharacters(in: .whitespaces).lowercased()
        let trimmedUsername = username.trimmingCharacters(in: .whitespaces).lowercased()
        
        auth.registerWithEmail(
            email: trimmedEmail,
            password: password,
            firstName: firstName.trimmingCharacters(in: .whitespaces),
            lastName: lastName.trimmingCharacters(in: .whitespaces),
            gender: gender,
            dateOfBirth: dateOfBirth,
            username: trimmedUsername.isEmpty ? nil : trimmedUsername,
            photoData: photoData
        ) { error in
            isSubmitting = false
            if error != nil {
                // AuthManager sets state = .error(sanitizedMessage)
                // Extract the sanitized message from auth state
                if case .error(let msg) = auth.state {
                    errorMessage = msg
                } else {
                    errorMessage = error?.localizedDescription ?? "Registration failed"
                }
                showError = true
            }
        }
    }
}
