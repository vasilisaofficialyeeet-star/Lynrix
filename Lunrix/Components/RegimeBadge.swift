// RegimeBadge.swift — Regime indicator pill with colored glow and subtle pulse
// Theme-aware: adapts glow intensity for Light and Dark modes

import SwiftUI

struct RegimeBadge: View {
    let regime: Int
    let confidence: Double
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    @State private var isPulsing = false
    
    var color: Color { LxColor.regime(regime) }
    
    var body: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(color)
                .frame(width: 7, height: 7)
                .shadow(color: color.opacity(0.7 * theme.glowOpacity), radius: isPulsing ? 6 * theme.glowIntensity : 3 * theme.glowIntensity)
                .scaleEffect(isPulsing ? 1.2 : 1.0)
            Text(loc.t(LxColor.regimeLocKey(regime)))
                .font(LxFont.mono(11, weight: .semibold))
                .foregroundColor(color)
            if confidence > 0 {
                Text(String(format: "%.0f%%", confidence * 100))
                    .font(LxFont.micro)
                    .foregroundColor(color.opacity(0.7))
            }
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
        .background(
            Capsule()
                .fill(color.opacity(theme.accentTintOpacity))
        )
        .overlay(
            Capsule()
                .stroke(color.opacity(theme.accentBorderOpacity), lineWidth: 0.5)
        )
        .shadow(color: color.opacity(0.15 * theme.glowOpacity), radius: 6 * theme.glowIntensity)
        .onAppear {
            withAnimation(LxAnimation.breathe) {
                isPulsing = true
            }
        }
    }
}

// MARK: - Status Badge (generic)

struct StatusBadge: View {
    let text: String
    let color: Color
    let pulse: Bool
    @Environment(\.theme) var theme
    
    @State private var isPulsing = false
    
    init(_ text: String, color: Color, pulse: Bool = false) {
        self.text = text
        self.color = color
        self.pulse = pulse
    }
    
    var body: some View {
        HStack(spacing: 5) {
            if pulse {
                Circle()
                    .fill(color)
                    .frame(width: 5, height: 5)
                    .shadow(color: color.opacity(0.6 * theme.glowOpacity), radius: isPulsing ? 4 * theme.glowIntensity : 2 * theme.glowIntensity)
                    .scaleEffect(isPulsing ? 1.3 : 1.0)
            }
            Text(text)
                .font(LxFont.mono(10, weight: .bold))
                .foregroundColor(color)
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 3)
        .background(
            Capsule()
                .fill(color.opacity(0.1))
        )
        .overlay(
            Capsule()
                .stroke(color.opacity(0.25), lineWidth: 0.5)
        )
        .shadow(color: color.opacity(0.1 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
        .onAppear {
            if pulse {
                withAnimation(LxAnimation.pulseLoop) {
                    isPulsing = true
                }
            }
        }
    }
}

// MARK: - Engine Status Badge

struct EngineStatusBadge: View {
    let status: EngineStatus
    @EnvironmentObject var loc: LocalizationManager
    
    var color: Color {
        switch status {
        case .idle:       return LxColor.coolSteel
        case .connecting: return LxColor.amber
        case .connected:  return LxColor.electricCyan
        case .trading:    return LxColor.neonLime
        case .error:      return LxColor.bloodRed
        case .stopping:   return LxColor.amber
        }
    }
    
    var body: some View {
        StatusBadge(
            loc.t(status.locKey).uppercased(),
            color: color,
            pulse: status == .trading || status == .connecting
        )
    }
}
