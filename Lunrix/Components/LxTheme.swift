// LxTheme.swift — Unified Design System for Lynrix v2.5
// Contains: Semantic tokens (LxTheme), raw palette (LxColor),
// typography (LxFont), animations (LxAnimation), and theme-aware modifiers.
// This is the SINGLE source of truth for all visual design tokens.

import SwiftUI

// MARK: - Semantic Theme Tokens

struct LxTheme {
    let scheme: ColorScheme
    
    /// Convenience: true when resolved scheme is dark
    var isDark: Bool { scheme == .dark }
    
    // ─── Surfaces ───────────────────────────────────────────
    
    /// Main app/window background
    var backgroundPrimary: Color {
        scheme == .dark ? Color(hex: 0x050505) : Color(hex: 0xF2F3F5)
    }
    
    /// Slightly elevated surface (sidebar, secondary panels)
    var backgroundSecondary: Color {
        scheme == .dark ? Color(hex: 0x0A0A0C) : Color(hex: 0xEBECEF)
    }
    
    /// Card / panel surface
    var panelBackground: Color {
        scheme == .dark ? Color(hex: 0x0F1014) : Color(hex: 0xFFFFFF)
    }
    
    /// Sidebar background
    var sidebarBackground: Color {
        scheme == .dark ? Color(hex: 0x050505).opacity(0.5) : Color(hex: 0xE8E9ED).opacity(0.6)
    }
    
    /// Input field background
    var inputBackground: Color {
        scheme == .dark ? Color(hex: 0x12131A) : Color(hex: 0xF5F6F8)
    }
    
    /// Glass highlight overlay
    var glassHighlight: Color {
        scheme == .dark ? Color.white.opacity(0.04) : Color.black.opacity(0.02)
    }
    
    /// Glass inner gradient top highlight
    var glassInnerHighlight: Color {
        scheme == .dark ? Color.white.opacity(0.06) : Color.white.opacity(0.8)
    }
    
    // ─── Text ───────────────────────────────────────────────
    
    /// Primary text (headings, values)
    var textPrimary: Color {
        scheme == .dark ? Color(hex: 0xF5F5F7) : Color(hex: 0x1A1C21)
    }
    
    /// Secondary text (labels, descriptions)
    var textSecondary: Color {
        scheme == .dark ? Color(hex: 0x8E9EB0) : Color(hex: 0x5A6270)
    }
    
    /// Tertiary text (placeholders, captions)
    var textTertiary: Color {
        scheme == .dark ? Color(hex: 0x4A5568) : Color(hex: 0x9CA3AF)
    }
    
    // ─── Borders & Dividers ─────────────────────────────────
    
    /// Subtle border (panels, cards)
    var borderSubtle: Color {
        scheme == .dark ? Color.white.opacity(0.08) : Color.black.opacity(0.08)
    }
    
    /// Strong border (focused, active)
    var borderStrong: Color {
        scheme == .dark ? Color.white.opacity(0.15) : Color.black.opacity(0.15)
    }
    
    /// Divider line
    var divider: Color {
        scheme == .dark ? Color.white.opacity(0.06) : Color.black.opacity(0.08)
    }
    
    // ─── Interactive States ─────────────────────────────────
    
    /// Hover background
    var hoverBackground: Color {
        scheme == .dark ? Color.white.opacity(0.03) : Color.black.opacity(0.04)
    }
    
    /// Selected item background
    var selectedBackground: Color {
        scheme == .dark ? Color.white.opacity(0.08) : Color.black.opacity(0.06)
    }
    
    /// Pressed state
    var pressedBackground: Color {
        scheme == .dark ? Color.white.opacity(0.12) : Color.black.opacity(0.10)
    }
    
    // ─── Shadows ────────────────────────────────────────────
    
    var shadowColor: Color {
        scheme == .dark ? Color.black.opacity(0.5) : Color.black.opacity(0.08)
    }
    
    var shadowRadius: CGFloat {
        scheme == .dark ? 6 : 4
    }
    
    // ─── Neon / Glow Intensity ──────────────────────────────
    // In light mode, neon glows are toned down to avoid looking garish
    
    /// Multiplier for glow/shadow radius
    var glowIntensity: CGFloat {
        scheme == .dark ? 1.0 : 0.3
    }
    
    /// Multiplier for neon shadow opacity
    var glowOpacity: Double {
        scheme == .dark ? 1.0 : 0.4
    }
    
    /// Background tint opacity for accent-colored panels
    var accentTintOpacity: Double {
        scheme == .dark ? 0.08 : 0.06
    }
    
    /// Border tint opacity for accent-colored panels
    var accentBorderOpacity: Double {
        scheme == .dark ? 0.15 : 0.20
    }
    
    // ─── Accent Colors (brand-consistent, with minor tweaks) ─
    
    /// Bid/positive color
    var positive: Color {
        scheme == .dark ? Color(hex: 0xA3FF00) : Color(hex: 0x22A744)
    }
    
    /// Ask/negative color
    var negative: Color {
        scheme == .dark ? Color(hex: 0xFF00CC) : Color(hex: 0xD93B6B)
    }
    
    /// Primary accent
    var accent: Color {
        scheme == .dark ? Color(hex: 0x00F5FF) : Color(hex: 0x0891B2)
    }
    
    /// Warning accent
    var warning: Color {
        scheme == .dark ? Color(hex: 0xFF9500) : Color(hex: 0xD97706)
    }
    
    /// Danger accent
    var danger: Color {
        scheme == .dark ? Color(hex: 0xFF2D55) : Color(hex: 0xDC2626)
    }
    
    /// Gold accent
    var gold: Color {
        scheme == .dark ? Color(hex: 0xFFD700) : Color(hex: 0xB8860B)
    }
    
    /// Muted steel
    var coolSteel: Color {
        scheme == .dark ? Color(hex: 0x8E9EB0) : Color(hex: 0x6B7280)
    }
    
    // ─── Button Contrast ────────────────────────────────────
    // The text color used when a button is filled with accent color
    
    var buttonContrast: Color {
        scheme == .dark ? Color(hex: 0x050505) : Color.white
    }
    
    // ─── Material ───────────────────────────────────────────
    
    var sidebarMaterial: NSVisualEffectView.Material {
        scheme == .dark ? .sidebar : .sidebar
    }
    
    var hudMaterial: NSVisualEffectView.Material {
        scheme == .dark ? .hudWindow : .headerView
    }
    
    // ─── Static Helpers ─────────────────────────────────────
    
    /// Resolved NSColor for window background (used in AppKit bridging)
    static func resolvedWindowBackground(for scheme: ColorScheme) -> NSColor {
        scheme == .dark
            ? NSColor(red: 0.02, green: 0.02, blue: 0.02, alpha: 1.0)
            : NSColor(red: 0.95, green: 0.95, blue: 0.96, alpha: 1.0)
    }
}

// MARK: - Environment Key

private struct ThemeKey: EnvironmentKey {
    static let defaultValue = LxTheme(scheme: .dark)
}

extension EnvironmentValues {
    var theme: LxTheme {
        get { self[ThemeKey.self] }
        set { self[ThemeKey.self] = newValue }
    }
}

// MARK: - View Extension for injecting theme

extension View {
    /// Injects the resolved theme into the environment based on current colorScheme
    func withLxTheme() -> some View {
        modifier(LxThemeModifier())
    }
}

private struct LxThemeModifier: ViewModifier {
    @Environment(\.colorScheme) var colorScheme
    
    func body(content: Content) -> some View {
        content
            .environment(\.theme, LxTheme(scheme: colorScheme))
    }
}

// MARK: - Raw Color Palette (Dark-mode constants, used as brand reference)

enum LxColor {
    // Neon Accents — brand colors, constant across themes
    static let electricCyan    = Color(hex: 0x00F5FF)
    static let neonLime        = Color(hex: 0xA3FF00)
    static let magentaPink     = Color(hex: 0xFF00CC)
    static let amber           = Color(hex: 0xFF9500)
    static let gold            = Color(hex: 0xFFD700)
    static let coolSteel       = Color(hex: 0x8E9EB0)
    static let bloodRed        = Color(hex: 0xFF2D55)
    
    // Regime colors — constant across themes
    static func regime(_ regime: Int) -> Color {
        switch regime {
        case 0: return coolSteel      // LowVol
        case 1: return amber          // HighVol
        case 2: return gold           // Trending
        case 3: return electricCyan   // MeanRev
        case 4: return bloodRed       // LiqVacuum
        default: return coolSteel
        }
    }
    
    static func regimeLocKey(_ regime: Int) -> String {
        switch regime {
        case 0: return "regime.quiet"
        case 1: return "regime.volatile"
        case 2: return "regime.trending"
        case 3: return "regime.meanReverting"
        case 4: return "regime.unknown"
        default: return "regime.unknown"
        }
    }
}

// MARK: - Color Extension

extension Color {
    init(hex: UInt, alpha: Double = 1.0) {
        self.init(
            .sRGB,
            red: Double((hex >> 16) & 0xFF) / 255.0,
            green: Double((hex >> 8) & 0xFF) / 255.0,
            blue: Double(hex & 0xFF) / 255.0,
            opacity: alpha
        )
    }
    
    func neonGlow(radius: CGFloat = 8) -> some View {
        self.opacity(0).shadow(color: self.opacity(0.6), radius: radius)
    }
}

// MARK: - Animations

enum LxAnimation {
    static let spring = Animation.spring(response: 0.35, dampingFraction: 0.82)
    static let snappy = Animation.spring(response: 0.25, dampingFraction: 0.9)
    static let gentle = Animation.spring(response: 0.5, dampingFraction: 0.85)
    static let micro  = Animation.spring(response: 0.15, dampingFraction: 0.9)
    
    static let chaosFlash = Animation.easeOut(duration: 0.3)
    static let pulseLoop  = Animation.easeInOut(duration: 1.5).repeatForever(autoreverses: true)
    static let breathe    = Animation.easeInOut(duration: 2.0).repeatForever(autoreverses: true)
}

// MARK: - Typography

enum LxFont {
    static func mono(_ size: CGFloat, weight: Font.Weight = .regular) -> Font {
        .system(size: size, design: .monospaced).weight(weight)
    }
    
    static let heroNumber    = Font.system(size: 36, design: .monospaced).weight(.bold)
    static let bigMetric     = Font.system(size: 24, design: .monospaced).weight(.semibold)
    static let metric        = Font.system(size: 16, design: .monospaced).weight(.medium)
    static let label         = Font.system(size: 12, design: .monospaced).weight(.regular)
    static let micro         = Font.system(size: 10, design: .monospaced).weight(.regular)
    static let sidebarItem   = Font.system(size: 13, design: .rounded).weight(.medium)
    static let sectionTitle  = Font.system(size: 14, design: .monospaced).weight(.bold)
}

// MARK: - Theme-Aware View Modifiers

struct NeonBorderModifier: ViewModifier {
    let color: Color
    let width: CGFloat
    let cornerRadius: CGFloat
    @Environment(\.theme) var theme
    
    func body(content: Content) -> some View {
        content
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .stroke(color.opacity(theme.isDark ? 1.0 : 0.6), lineWidth: width)
            )
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .stroke(color.opacity(0.3 * theme.glowOpacity), lineWidth: width + 1)
                    .blur(radius: 4 * theme.glowIntensity)
            )
    }
}

struct GlassBackgroundModifier: ViewModifier {
    let cornerRadius: CGFloat
    let neonColor: Color?
    @Environment(\.theme) var theme
    
    func body(content: Content) -> some View {
        content
            .background(
                ZStack {
                    if theme.isDark {
                        RoundedRectangle(cornerRadius: cornerRadius)
                            .fill(Color(white: 0.11, opacity: 0.92))
                        RoundedRectangle(cornerRadius: cornerRadius)
                            .fill(theme.glassHighlight)
                    } else {
                        RoundedRectangle(cornerRadius: cornerRadius)
                            .fill(theme.panelBackground)
                        RoundedRectangle(cornerRadius: cornerRadius)
                            .fill(.thinMaterial)
                            .opacity(0.3)
                    }
                }
            )
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .stroke(neonColor?.opacity(theme.accentBorderOpacity) ?? theme.borderSubtle, lineWidth: 0.5)
            )
            .clipShape(RoundedRectangle(cornerRadius: cornerRadius))
    }
}

struct InnerGlowModifier: ViewModifier {
    let color: Color
    let radius: CGFloat
    let cornerRadius: CGFloat
    @Environment(\.theme) var theme
    
    func body(content: Content) -> some View {
        content
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .stroke(color.opacity(0.15 * theme.glowOpacity), lineWidth: 1)
                    .blur(radius: radius * theme.glowIntensity)
                    .mask(RoundedRectangle(cornerRadius: cornerRadius))
            )
    }
}

extension View {
    func neonBorder(_ color: Color, width: CGFloat = 0.5, cornerRadius: CGFloat = 12) -> some View {
        modifier(NeonBorderModifier(color: color, width: width, cornerRadius: cornerRadius))
    }
    
    func glassBackground(cornerRadius: CGFloat = 12, neonColor: Color? = nil) -> some View {
        modifier(GlassBackgroundModifier(cornerRadius: cornerRadius, neonColor: neonColor))
    }
    
    func innerGlow(_ color: Color, radius: CGFloat = 4, cornerRadius: CGFloat = 12) -> some View {
        modifier(InnerGlowModifier(color: color, radius: radius, cornerRadius: cornerRadius))
    }
}

// MARK: - NSVisualEffectView Bridge

struct VisualEffectBackground: NSViewRepresentable {
    let material: NSVisualEffectView.Material
    let blendingMode: NSVisualEffectView.BlendingMode
    let state: NSVisualEffectView.State
    
    init(
        material: NSVisualEffectView.Material = .hudWindow,
        blendingMode: NSVisualEffectView.BlendingMode = .behindWindow,
        state: NSVisualEffectView.State = .active
    ) {
        self.material = material
        self.blendingMode = blendingMode
        self.state = state
    }
    
    func makeNSView(context: Context) -> NSVisualEffectView {
        let view = NSVisualEffectView()
        view.material = material
        view.blendingMode = blendingMode
        view.state = state
        view.wantsLayer = true
        return view
    }
    
    func updateNSView(_ nsView: NSVisualEffectView, context: Context) {
        nsView.material = material
        nsView.blendingMode = blendingMode
        nsView.state = state
    }
}
