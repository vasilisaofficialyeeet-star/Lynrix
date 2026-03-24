// NeonButton.swift — Glowing button with hover scale, neon border, and glow effect
// Theme-aware: adapts glow/contrast for Light and Dark modes

import SwiftUI

struct NeonButton: View {
    let title: String
    let icon: String?
    let color: Color
    let action: () -> Void
    
    @State private var isHovered = false
    @State private var isPressed = false
    @Environment(\.theme) var theme
    
    init(_ title: String, icon: String? = nil, color: Color = LxColor.electricCyan, action: @escaping () -> Void) {
        self.title = title
        self.icon = icon
        self.color = color
        self.action = action
    }
    
    var body: some View {
        Button(action: {
            withAnimation(LxAnimation.snappy) {
                isPressed = true
            }
            action()
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) {
                withAnimation(LxAnimation.snappy) {
                    isPressed = false
                }
            }
        }) {
            HStack(spacing: 6) {
                if let icon = icon {
                    Image(systemName: icon)
                        .font(.system(size: 12, weight: .semibold))
                }
                Text(title)
                    .font(LxFont.mono(12, weight: .semibold))
            }
            .foregroundColor(isHovered ? theme.buttonContrast : color)
            .padding(.horizontal, 14)
            .padding(.vertical, 7)
            .background(
                ZStack {
                    RoundedRectangle(cornerRadius: 8)
                        .fill(isHovered ? color : color.opacity(theme.accentTintOpacity))
                    if isHovered && theme.isDark {
                        RoundedRectangle(cornerRadius: 8)
                            .fill(color)
                            .shadow(color: color.opacity(0.5), radius: 12)
                    }
                }
            )
            .overlay(
                RoundedRectangle(cornerRadius: 8)
                    .stroke(color.opacity(isHovered ? 0.8 : 0.3), lineWidth: 0.5)
            )
            .scaleEffect(isPressed ? 0.95 : (isHovered ? 1.03 : 1.0))
            .shadow(color: color.opacity((isHovered ? 0.4 : 0.1) * theme.glowOpacity), radius: isHovered ? 8 * theme.glowIntensity : 2)
            .animation(LxAnimation.micro, value: isHovered)
            .animation(LxAnimation.micro, value: isPressed)
        }
        .buttonStyle(.plain)
        .onHover { hovering in
            isHovered = hovering
        }
    }
}

// MARK: - Icon-only Neon Button

struct NeonIconButton: View {
    let icon: String
    let color: Color
    let size: CGFloat
    let action: () -> Void
    
    @State private var isHovered = false
    @Environment(\.theme) var theme
    
    init(_ icon: String, color: Color = LxColor.electricCyan, size: CGFloat = 28, action: @escaping () -> Void) {
        self.icon = icon
        self.color = color
        self.size = size
        self.action = action
    }
    
    var body: some View {
        Button(action: action) {
            Image(systemName: icon)
                .font(.system(size: size * 0.45, weight: .semibold))
                .foregroundColor(isHovered ? theme.buttonContrast : color)
                .frame(width: size, height: size)
                .background(
                    Circle()
                        .fill(isHovered ? color : color.opacity(theme.accentTintOpacity))
                )
                .overlay(
                    Circle()
                        .stroke(color.opacity(isHovered ? 0.6 : 0.2), lineWidth: 0.5)
                )
                .shadow(color: color.opacity((isHovered ? 0.4 : 0) * theme.glowOpacity), radius: 8 * theme.glowIntensity)
                .scaleEffect(isHovered ? 1.08 : 1.0)
                .animation(LxAnimation.micro, value: isHovered)
        }
        .buttonStyle(.plain)
        .onHover { hovering in isHovered = hovering }
    }
}

// MARK: - Toggle-style Neon Button

struct NeonToggle: View {
    let title: String
    @Binding var isOn: Bool
    let onColor: Color
    let offColor: Color
    
    @State private var isHovered = false
    @Environment(\.theme) var theme
    
    init(_ title: String, isOn: Binding<Bool>, onColor: Color = LxColor.neonLime, offColor: Color = LxColor.coolSteel) {
        self.title = title
        self._isOn = isOn
        self.onColor = onColor
        self.offColor = offColor
    }
    
    var activeColor: Color { isOn ? onColor : offColor }
    
    var body: some View {
        Button {
            withAnimation(LxAnimation.spring) {
                isOn.toggle()
            }
        } label: {
            HStack(spacing: 6) {
                Circle()
                    .fill(activeColor)
                    .frame(width: 6, height: 6)
                    .shadow(color: activeColor.opacity(0.6 * theme.glowOpacity), radius: isOn ? 4 * theme.glowIntensity : 0)
                Text(title)
                    .font(LxFont.mono(11, weight: .medium))
                    .foregroundColor(isOn ? activeColor : theme.textSecondary)
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 5)
            .background(
                RoundedRectangle(cornerRadius: 6)
                    .fill(activeColor.opacity(isOn ? 0.1 : 0.03))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(activeColor.opacity(isOn ? 0.3 : 0.1), lineWidth: 0.5)
            )
            .scaleEffect(isHovered ? 1.03 : 1.0)
            .animation(LxAnimation.micro, value: isHovered)
        }
        .buttonStyle(.plain)
        .onHover { hovering in isHovered = hovering }
    }
}
