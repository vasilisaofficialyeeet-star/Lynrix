// GlassPanel.swift — Reusable glassmorphism card with blur, neon border, inner glow
// Theme-aware: adapts glass/shadow/glow intensity for Light and Dark modes

import SwiftUI

struct GlassPanel<Content: View>: View {
    let neonColor: Color
    let cornerRadius: CGFloat
    let padding: CGFloat
    @ViewBuilder let content: () -> Content
    
    @State private var isHovered = false
    @Environment(\.theme) var theme
    
    init(
        neon: Color = LxColor.electricCyan,
        cornerRadius: CGFloat = 12,
        padding: CGFloat = 16,
        @ViewBuilder content: @escaping () -> Content
    ) {
        self.neonColor = neon
        self.cornerRadius = cornerRadius
        self.padding = padding
        self.content = content
    }
    
    var body: some View {
        content()
            .padding(padding)
            .background(
                ZStack {
                    // PERF P1: Replaced .ultraThinMaterial with solid semi-transparent fill.
                    // Eliminates real-time Gaussian blur compositing on GPU.
                    if theme.isDark {
                        RoundedRectangle(cornerRadius: cornerRadius)
                            .fill(Color(white: 0.11, opacity: 0.92))
                        RoundedRectangle(cornerRadius: cornerRadius)
                            .fill(theme.glassHighlight)
                        RoundedRectangle(cornerRadius: cornerRadius)
                            .fill(
                                LinearGradient(
                                    colors: [Color.white.opacity(0.06), Color.clear],
                                    startPoint: .top,
                                    endPoint: .center
                                )
                            )
                    } else {
                        RoundedRectangle(cornerRadius: cornerRadius)
                            .fill(theme.panelBackground)
                        RoundedRectangle(cornerRadius: cornerRadius)
                            .fill(
                                LinearGradient(
                                    colors: [Color.white.opacity(0.8), Color.white.opacity(0.4)],
                                    startPoint: .top,
                                    endPoint: .bottom
                                )
                            )
                            .opacity(0.3)
                    }
                }
            )
            .overlay(
                RoundedRectangle(cornerRadius: cornerRadius)
                    .stroke(
                        LinearGradient(
                            colors: theme.isDark
                                ? [neonColor.opacity(isHovered ? 0.4 : 0.15),
                                   neonColor.opacity(isHovered ? 0.2 : 0.05),
                                   Color.clear]
                                : [neonColor.opacity(isHovered ? 0.35 : 0.18),
                                   neonColor.opacity(isHovered ? 0.15 : 0.06),
                                   theme.borderSubtle],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        ),
                        lineWidth: 0.5
                    )
            )
            .shadow(
                color: theme.isDark
                    ? neonColor.opacity(isHovered ? 0.12 : 0.04)
                    : theme.shadowColor.opacity(isHovered ? 0.10 : 0.05),
                radius: isHovered ? 8 * theme.glowIntensity : 4 * theme.glowIntensity,
                y: theme.isDark ? 2 : 3
            )
            .clipShape(RoundedRectangle(cornerRadius: cornerRadius))
            // PERF P1: Removed hover scaleEffect — eliminates layout recalc on hover
            .onHover { hovering in
                isHovered = hovering
            }
    }
}

// MARK: - Compact Glass Panel (no hover, smaller padding)

struct GlassCard<Content: View>: View {
    let neonColor: Color
    @ViewBuilder let content: () -> Content
    @Environment(\.theme) var theme
    
    init(neon: Color = LxColor.electricCyan, @ViewBuilder content: @escaping () -> Content) {
        self.neonColor = neon
        self.content = content
    }
    
    var body: some View {
        content()
            .padding(12)
            .background(
                ZStack {
                    if theme.isDark {
                        // PERF P1: Solid fill instead of .ultraThinMaterial
                        RoundedRectangle(cornerRadius: 10)
                            .fill(Color(white: 0.11, opacity: 0.92))
                        RoundedRectangle(cornerRadius: 10)
                            .fill(theme.glassHighlight)
                    } else {
                        RoundedRectangle(cornerRadius: 10)
                            .fill(theme.panelBackground)
                    }
                }
            )
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(neonColor.opacity(theme.accentBorderOpacity * 0.7), lineWidth: 0.5)
            )
            .shadow(color: theme.shadowColor.opacity(0.04), radius: 2, y: 1)
            .clipShape(RoundedRectangle(cornerRadius: 10))
    }
}

// MARK: - Section Header

struct GlassSectionHeader: View {
    let icon: String
    let title: String
    let color: Color
    @Environment(\.theme) var theme
    
    init(_ title: String, icon: String, color: Color = LxColor.electricCyan) {
        self.title = title
        self.icon = icon
        self.color = color
    }
    
    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: icon)
                .font(.system(size: 13, weight: .semibold))
                .foregroundColor(color)
                .shadow(color: color.opacity(0.5 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
            Text(title.uppercased())
                .font(LxFont.sectionTitle)
                .foregroundColor(theme.textPrimary)
                .shadow(color: color.opacity(0.2 * theme.glowOpacity), radius: 2 * theme.glowIntensity)
        }
    }
}

// MARK: - Metric Row

struct GlassMetric: View {
    let label: String
    let value: String
    let color: Color
    @Environment(\.theme) var theme
    
    init(_ label: String, value: String, color: Color = .clear) {
        self.label = label
        self.value = value
        self.color = color
    }
    
    private var resolvedColor: Color {
        color == .clear ? theme.textPrimary : color
    }
    
    var body: some View {
        HStack {
            Text(label)
                .font(LxFont.label)
                .foregroundColor(theme.textSecondary)
            Spacer()
            Text(value)
                .font(LxFont.label)
                .fontWeight(.medium)
                .foregroundColor(resolvedColor)
                .shadow(color: resolvedColor == theme.textPrimary ? .clear : resolvedColor.opacity(0.3 * theme.glowOpacity), radius: 3 * theme.glowIntensity)
        }
    }
}

// MARK: - Status Dot

struct StatusDot: View {
    let color: Color
    let size: CGFloat
    let pulse: Bool
    @Environment(\.theme) var theme
    
    @State private var isPulsing = false
    
    init(_ color: Color, size: CGFloat = 8, pulse: Bool = false) {
        self.color = color
        self.size = size
        self.pulse = pulse
    }
    
    var body: some View {
        ZStack {
            if pulse {
                Circle()
                    .fill(color.opacity(0.3))
                    .frame(width: size * 2, height: size * 2)
                    .scaleEffect(isPulsing ? 1.3 : 0.8)
                    .opacity(isPulsing ? 0 : 0.5)
                    .animation(LxAnimation.pulseLoop, value: isPulsing)
            }
            Circle()
                .fill(color)
                .frame(width: size, height: size)
                .shadow(color: color.opacity(0.6 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
        }
        .onAppear { isPulsing = true }
    }
}
