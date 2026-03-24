// GlowText.swift — Text with neon glow shadow effect
// Theme-aware: reduces glow in Light mode for readability

import SwiftUI

struct GlowText: View {
    let text: String
    let font: Font
    let color: Color
    let glowRadius: CGFloat
    @Environment(\.theme) var theme
    
    init(_ text: String, font: Font = LxFont.metric, color: Color = LxColor.electricCyan, glow: CGFloat = 6) {
        self.text = text
        self.font = font
        self.color = color
        self.glowRadius = glow
    }
    
    var body: some View {
        Text(text)
            .font(font)
            .foregroundColor(color)
            .shadow(color: color.opacity(0.5 * theme.glowOpacity), radius: glowRadius * theme.glowIntensity)
            .shadow(color: color.opacity(0.2 * theme.glowOpacity), radius: glowRadius * 2 * theme.glowIntensity)
    }
}

// MARK: - Hero Number (large metric with glow)

struct HeroNumber: View {
    let value: String
    let subtitle: String
    let color: Color
    @Environment(\.theme) var theme
    
    init(_ value: String, subtitle: String = "", color: Color = LxColor.electricCyan) {
        self.value = value
        self.subtitle = subtitle
        self.color = color
    }
    
    var body: some View {
        VStack(spacing: 2) {
            Text(value)
                .font(LxFont.heroNumber)
                .foregroundColor(color)
                .shadow(color: color.opacity(0.4 * theme.glowOpacity), radius: 8 * theme.glowIntensity)
                .shadow(color: color.opacity(0.15 * theme.glowOpacity), radius: 16 * theme.glowIntensity)
            if !subtitle.isEmpty {
                Text(subtitle)
                    .font(LxFont.micro)
                    .foregroundColor(theme.textSecondary)
            }
        }
    }
}

// MARK: - Signed Number (positive = lime, negative = magenta)

struct SignedNumber: View {
    let value: Double
    let format: String
    let font: Font
    let prefix: String
    @Environment(\.theme) var theme
    
    init(_ value: Double, format: String = "%.2f", font: Font = LxFont.metric, prefix: String = "") {
        self.value = value
        self.format = format
        self.font = font
        self.prefix = prefix
    }
    
    var color: Color {
        value > 0 ? theme.positive : (value < 0 ? theme.negative : theme.textSecondary)
    }
    
    var body: some View {
        Text(prefix + String(format: format, value))
            .font(font)
            .foregroundColor(color)
            .shadow(color: color.opacity(0.3 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
    }
}

// MARK: - Neon Label

struct NeonLabel: View {
    let icon: String
    let text: String
    let color: Color
    @Environment(\.theme) var theme
    
    init(_ text: String, icon: String, color: Color = LxColor.electricCyan) {
        self.text = text
        self.icon = icon
        self.color = color
    }
    
    var body: some View {
        HStack(spacing: 6) {
            Image(systemName: icon)
                .font(.system(size: 11, weight: .semibold))
                .foregroundColor(color)
                .shadow(color: color.opacity(0.5 * theme.glowOpacity), radius: 3 * theme.glowIntensity)
            Text(text)
                .font(LxFont.mono(11, weight: .medium))
                .foregroundColor(theme.textPrimary)
        }
    }
}
