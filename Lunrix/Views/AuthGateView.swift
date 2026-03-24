// AuthGateView.swift — Premium authentication gate for Lynrix v2.5
// Firebase Auth: Google Sign-In + Email/Password

import SwiftUI

// MARK: - Auth Screen Mode

enum AuthScreenMode {
    case welcome
    case emailLogin
    case register
}

struct AuthGateView: View {
    @EnvironmentObject var loc: LocalizationManager
    @EnvironmentObject var themeManager: ThemeManager
    @ObservedObject var auth: AuthManager
    @Environment(\.theme) var theme
    
    @State private var screenMode: AuthScreenMode = .welcome
    @State private var showError: Bool = false
    @State private var errorMessage: String = ""
    @State private var logoScale: CGFloat = 0.8
    @State private var logoOpacity: Double = 0.0
    @State private var contentOpacity: Double = 0.0
    
    // Email login fields
    @State private var emailField: String = ""
    @State private var passwordField: String = ""
    
    var body: some View {
        ZStack {
            backgroundLayer
            
            Group {
                switch screenMode {
                case .welcome:
                    welcomeContent
                case .emailLogin:
                    emailLoginContent
                case .register:
                    RegistrationView(auth: auth) {
                        withAnimation(.easeInOut(duration: 0.25)) {
                            screenMode = .welcome
                        }
                    }
                    .environmentObject(loc)
                    .environmentObject(themeManager)
                }
            }
            .transition(.opacity.combined(with: .move(edge: .trailing)))
        }
        .frame(minWidth: 1280, minHeight: 860)
        .onAppear {
            withAnimation(.easeOut(duration: 0.6).delay(0.1)) {
                logoScale = 1.0
                logoOpacity = 1.0
            }
            withAnimation(.easeOut(duration: 0.5).delay(0.4)) {
                contentOpacity = 1.0
            }
        }
        .onChange(of: auth.state) { newState in
            if case .error(let msg) = newState {
                errorMessage = msg
                showError = true
            }
        }
    }
    
    // MARK: - Background
    
    private var backgroundLayer: some View {
        ZStack {
            theme.backgroundPrimary
                .ignoresSafeArea()
            
            LinearGradient(
                colors: [
                    LxColor.electricCyan.opacity(0.03),
                    Color.clear,
                    LxColor.magentaPink.opacity(0.02)
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()
            
            GeometryReader { geo in
                Canvas { context, size in
                    let spacing: CGFloat = 40
                    let lineColor = theme.borderSubtle.opacity(0.3)
                    for x in stride(from: 0, through: size.width, by: spacing) {
                        var path = Path()
                        path.move(to: CGPoint(x: x, y: 0))
                        path.addLine(to: CGPoint(x: x, y: size.height))
                        context.stroke(path, with: .color(lineColor), lineWidth: 0.3)
                    }
                    for y in stride(from: 0, through: size.height, by: spacing) {
                        var path = Path()
                        path.move(to: CGPoint(x: 0, y: y))
                        path.addLine(to: CGPoint(x: size.width, y: y))
                        context.stroke(path, with: .color(lineColor), lineWidth: 0.3)
                    }
                }
            }
            .ignoresSafeArea()
        }
    }
    
    // MARK: - Logo
    
    private var logoSection: some View {
        VStack(spacing: 16) {
            ZStack {
                Circle()
                    .fill(LxColor.electricCyan.opacity(0.06))
                    .frame(width: 88, height: 88)
                Circle()
                    .stroke(LxColor.electricCyan.opacity(0.15), lineWidth: 1)
                    .frame(width: 88, height: 88)
                Image(systemName: "diamond.fill")
                    .font(.system(size: 32, weight: .bold))
                    .foregroundColor(LxColor.electricCyan)
                    .shadow(color: LxColor.electricCyan.opacity(0.6), radius: 12)
            }
            
            Text("LYNRIX")
                .font(LxFont.mono(28, weight: .bold))
                .foregroundColor(theme.textPrimary)
                .tracking(6)
            
            Text(loc.t("auth.tagline"))
                .font(LxFont.mono(11))
                .foregroundColor(theme.textTertiary)
                .tracking(1)
        }
    }
    
    // MARK: - Welcome Content (main auth screen)
    
    private var welcomeContent: some View {
        VStack(spacing: 0) {
            Spacer()
            
            logoSection
                .scaleEffect(logoScale)
                .opacity(logoOpacity)
            
            Spacer().frame(height: 48)
            
            // Auth options
            VStack(spacing: 14) {
                // Google Sign-In
                Button(action: { auth.signInWithGoogle() }) {
                    HStack(spacing: 12) {
                        Image(systemName: "globe")
                            .font(.system(size: 15, weight: .semibold))
                        Text(loc.t("auth.googleSignIn"))
                            .font(LxFont.mono(13, weight: .semibold))
                    }
                    .frame(maxWidth: .infinity)
                    .frame(height: 46)
                    .background(
                        RoundedRectangle(cornerRadius: 10)
                            .fill(theme.backgroundSecondary)
                            .overlay(
                                RoundedRectangle(cornerRadius: 10)
                                    .stroke(LxColor.electricCyan.opacity(0.3), lineWidth: 1)
                            )
                    )
                    .foregroundColor(theme.textPrimary)
                }
                .buttonStyle(.plain)
                .disabled(auth.state == .signingIn)
                
                // Divider
                HStack {
                    Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                    Text(loc.t("auth.or"))
                        .font(LxFont.mono(9))
                        .foregroundColor(theme.textTertiary)
                        .padding(.horizontal, 12)
                    Rectangle().fill(theme.borderSubtle).frame(height: 0.5)
                }
                .padding(.vertical, 4)
                
                // Email Sign In
                Button(action: {
                    withAnimation(.easeInOut(duration: 0.25)) { screenMode = .emailLogin }
                }) {
                    HStack(spacing: 12) {
                        Image(systemName: "envelope.fill")
                            .font(.system(size: 15, weight: .semibold))
                        Text(loc.t("auth.emailSignIn"))
                            .font(LxFont.mono(13, weight: .semibold))
                    }
                    .frame(maxWidth: .infinity)
                    .frame(height: 46)
                    .background(
                        RoundedRectangle(cornerRadius: 10)
                            .fill(theme.backgroundSecondary)
                            .overlay(
                                RoundedRectangle(cornerRadius: 10)
                                    .stroke(theme.borderSubtle.opacity(0.3), lineWidth: 0.5)
                            )
                    )
                    .foregroundColor(theme.textPrimary)
                }
                .buttonStyle(.plain)
                
                // Register
                Button(action: {
                    withAnimation(.easeInOut(duration: 0.25)) { screenMode = .register }
                }) {
                    HStack(spacing: 6) {
                        Image(systemName: "person.badge.plus")
                            .font(.system(size: 12, weight: .medium))
                        Text(loc.t("auth.createAccountLink"))
                            .font(LxFont.mono(11, weight: .medium))
                    }
                    .foregroundColor(LxColor.electricCyan.opacity(0.8))
                }
                .buttonStyle(.plain)
                .padding(.top, 4)
                
                // Loading
                if auth.state == .signingIn {
                    HStack(spacing: 8) {
                        ProgressView()
                            .scaleEffect(0.7)
                            .progressViewStyle(.circular)
                        Text(loc.t("auth.signingIn"))
                            .font(LxFont.mono(11))
                            .foregroundColor(theme.textTertiary)
                    }
                    .padding(.top, 4)
                }
                
                // Error
                if showError {
                    errorBanner
                }
            }
            .frame(maxWidth: 360)
            .opacity(contentOpacity)
            
            Spacer()
            
            footerSection
                .opacity(contentOpacity)
        }
        .padding(40)
    }
    
    // MARK: - Email Login Content
    
    private var emailLoginContent: some View {
        VStack(spacing: 0) {
            Spacer()
            
            VStack(spacing: 24) {
                // Back + Title
                VStack(spacing: 8) {
                    HStack {
                        Button(action: {
                            withAnimation(.easeInOut(duration: 0.25)) { screenMode = .welcome }
                        }) {
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
                    
                    ZStack {
                        Circle()
                            .fill(LxColor.electricCyan.opacity(0.06))
                            .frame(width: 64, height: 64)
                        Circle()
                            .stroke(LxColor.electricCyan.opacity(0.15), lineWidth: 1)
                            .frame(width: 64, height: 64)
                        Image(systemName: "envelope.fill")
                            .font(.system(size: 26, weight: .medium))
                            .foregroundColor(LxColor.electricCyan)
                    }
                    
                    Text(loc.t("auth.welcomeBack"))
                        .font(LxFont.mono(20, weight: .bold))
                        .foregroundColor(theme.textPrimary)
                    
                    Text(loc.t("auth.signInSubtitle"))
                        .font(LxFont.mono(11))
                        .foregroundColor(theme.textTertiary)
                }
                
                // Fields
                VStack(spacing: 12) {
                    // Email
                    HStack(spacing: 8) {
                        Image(systemName: "envelope")
                            .font(.system(size: 12))
                            .foregroundColor(theme.textTertiary)
                        TextField(loc.t("auth.emailPlaceholder"), text: $emailField)
                            .textFieldStyle(.plain)
                            .font(LxFont.mono(13))
                            .foregroundColor(theme.textPrimary)
                            .onSubmit { attemptEmailLogin() }
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 10)
                    .background(
                        RoundedRectangle(cornerRadius: 8)
                            .fill(theme.backgroundSecondary)
                            .overlay(
                                RoundedRectangle(cornerRadius: 8)
                                    .stroke(theme.borderSubtle.opacity(0.3), lineWidth: 0.5)
                            )
                    )
                    
                    // Password
                    HStack(spacing: 8) {
                        Image(systemName: "lock")
                            .font(.system(size: 12))
                            .foregroundColor(theme.textTertiary)
                        SecureField(loc.t("auth.passwordPlaceholder"), text: $passwordField)
                            .textFieldStyle(.plain)
                            .font(LxFont.mono(13))
                            .foregroundColor(theme.textPrimary)
                            .onSubmit { attemptEmailLogin() }
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 10)
                    .background(
                        RoundedRectangle(cornerRadius: 8)
                            .fill(theme.backgroundSecondary)
                            .overlay(
                                RoundedRectangle(cornerRadius: 8)
                                    .stroke(theme.borderSubtle.opacity(0.3), lineWidth: 0.5)
                            )
                    )
                }
                
                // Sign In Button
                Button(action: attemptEmailLogin) {
                    HStack(spacing: 8) {
                        if auth.state == .signingIn {
                            ProgressView()
                                .scaleEffect(0.6)
                                .progressViewStyle(.circular)
                        }
                        Text(loc.t("auth.signIn"))
                            .font(LxFont.mono(13, weight: .semibold))
                    }
                    .frame(maxWidth: .infinity)
                    .frame(height: 44)
                    .background(
                        RoundedRectangle(cornerRadius: 10)
                            .fill(!emailField.isEmpty && !passwordField.isEmpty
                                  ? LxColor.electricCyan.opacity(0.15)
                                  : theme.backgroundSecondary)
                            .overlay(
                                RoundedRectangle(cornerRadius: 10)
                                    .stroke(!emailField.isEmpty && !passwordField.isEmpty
                                            ? LxColor.electricCyan.opacity(0.4)
                                            : theme.borderSubtle.opacity(0.2), lineWidth: 1)
                            )
                    )
                    .foregroundColor(!emailField.isEmpty && !passwordField.isEmpty
                                     ? LxColor.electricCyan : theme.textTertiary)
                }
                .buttonStyle(.plain)
                .disabled(emailField.isEmpty || passwordField.isEmpty || auth.state == .signingIn)
                
                // Register link
                HStack(spacing: 4) {
                    Text(loc.t("auth.noAccount"))
                        .font(LxFont.mono(10))
                        .foregroundColor(theme.textTertiary)
                    Button(action: {
                        withAnimation(.easeInOut(duration: 0.25)) { screenMode = .register }
                    }) {
                        Text(loc.t("auth.createAccountLink"))
                            .font(LxFont.mono(10, weight: .semibold))
                            .foregroundColor(LxColor.electricCyan)
                    }
                    .buttonStyle(.plain)
                }
                
                // Error
                if showError {
                    errorBanner
                }
            }
            .frame(maxWidth: 340)
            
            Spacer()
            
            footerSection
        }
        .padding(40)
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
        .onTapGesture {
            showError = false
        }
    }
    
    // MARK: - Footer
    
    private var footerSection: some View {
        VStack(spacing: 6) {
            Text(loc.t("auth.version"))
                .font(LxFont.mono(10))
                .foregroundColor(theme.textTertiary.opacity(0.4))
            Text(loc.t("auth.copyright"))
                .font(LxFont.mono(9))
                .foregroundColor(theme.textTertiary.opacity(0.3))
        }
        .padding(.bottom, 20)
    }
    
    // MARK: - Actions
    
    private func attemptEmailLogin() {
        showError = false
        auth.signInWithEmail(email: emailField.trimmingCharacters(in: .whitespaces), password: passwordField) { error in
            if error != nil {
                // AuthManager already sets state = .error(sanitizedMessage)
                // The onChange(of: auth.state) handler above will display it
                passwordField = ""
            }
        }
    }
}
