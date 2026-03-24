// ChaosControlView.swift — Glassmorphism 2026 Chaos Engine panel (Lynrix v2.5)

import SwiftUI

struct ChaosControlView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    @State private var flashActive = false
    
    private var chaosColor: Color { engine.chaosState.enabled ? LxColor.bloodRed : LxColor.coolSteel }
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                // Master header with toggle
                chaosHeader
                
                // Profile buttons
                profileButtons
                
                // 6 fault type cards (2 rows of 3)
                HStack(spacing: 12) {
                    ChaosFaultCard(loc.t("chaos.latencySpikes"), count: engine.chaosState.latencySpikes,
                                   icon: "clock.badge.exclamationmark", color: LxColor.amber)
                    ChaosFaultCard(loc.t("chaos.packetsDropped"), count: engine.chaosState.packetsDropped,
                                   icon: "wifi.slash", color: LxColor.bloodRed)
                    ChaosFaultCard(loc.t("chaos.fakeDeltas"), count: engine.chaosState.fakeDeltasInjected,
                                   icon: "doc.badge.gearshape", color: LxColor.magentaPink)
                }
                
                HStack(spacing: 12) {
                    ChaosFaultCard(loc.t("chaos.oomSimulation"), count: engine.chaosState.oomSimulations,
                                   icon: "memorychip", color: LxColor.amber)
                    ChaosFaultCard(loc.t("chaos.corruptedJson"), count: engine.chaosState.corruptedJsons,
                                   icon: "curlybraces", color: LxColor.magentaPink)
                    ChaosFaultCard(loc.t("chaos.clockSkews"), count: engine.chaosState.clockSkews,
                                   icon: "clock.arrow.2.circlepath", color: LxColor.electricCyan)
                }
                
                // Injection summary
                summaryPanel
            }
            .padding(16)
        }
        .background(
            ZStack {
                theme.backgroundPrimary
                if flashActive {
                    LxColor.bloodRed.opacity(0.08)
                        .transition(.opacity)
                }
            }
        )
        .onChange(of: engine.chaosState.totalInjections) { _ in
            withAnimation(LxAnimation.chaosFlash) { flashActive = true }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                withAnimation(LxAnimation.chaosFlash) { flashActive = false }
            }
        }
    }
    
    // MARK: - Header
    
    private var chaosHeader: some View {
        GlassPanel(neon: chaosColor) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 8) {
                        Image(systemName: "tornado")
                            .font(.system(size: 18, weight: .bold))
                            .foregroundColor(chaosColor)
                            .shadow(color: chaosColor.opacity(0.5), radius: 6)
                        Text(loc.t("chaos.title"))
                            .font(LxFont.mono(16, weight: .bold))
                            .foregroundColor(theme.textPrimary)
                    }
                    Text(loc.t("chaos.subtitle"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                }
                Spacer()
                
                VStack(alignment: .trailing, spacing: 4) {
                    StatusBadge(
                        engine.chaosState.enabled ? loc.t("common.active") : loc.t("chaos.disabled"),
                        color: chaosColor,
                        pulse: engine.chaosState.enabled
                    )
                    if engine.chaosState.enabled {
                        Text("\(engine.chaosState.totalInjections) \(loc.t("chaos.injections"))")
                            .font(LxFont.micro)
                            .foregroundColor(LxColor.bloodRed.opacity(0.7))
                    }
                }
            }
        }
    }
    
    // MARK: - Profile Buttons
    
    private var profileButtons: some View {
        HStack(spacing: 10) {
            NeonButton(loc.t("chaos.nightly"), icon: "moon.stars", color: LxColor.electricCyan) {
                engine.enableChaosNightly()
            }
            NeonButton(loc.t("chaos.flashCrash"), icon: "bolt.trianglebadge.exclamationmark", color: LxColor.bloodRed) {
                engine.enableChaosFlashCrash()
            }
            NeonButton(loc.t("chaos.disable"), icon: "checkmark.shield", color: LxColor.neonLime) {
                engine.disableChaos()
            }
            NeonButton(loc.t("chaos.reset"), icon: "arrow.counterclockwise", color: LxColor.coolSteel) {
                engine.resetChaosStats()
            }
        }
    }
    
    // MARK: - Summary
    
    private var summaryPanel: some View {
        GlassPanel(neon: chaosColor) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("chaos.summary"), icon: "chart.bar.doc.horizontal", color: chaosColor)
                
                GlassMetric(loc.t("chaos.totalInjections"), value: "\(engine.chaosState.totalInjections)",
                            color: engine.chaosState.totalInjections > 0 ? LxColor.bloodRed : theme.textPrimary)
                GlassMetric(loc.t("chaos.totalLatency"),
                            value: String(format: "%.2f ms", Double(engine.chaosState.totalInjectedLatencyNs) / 1_000_000),
                            color: LxColor.amber)
                GlassMetric(loc.t("chaos.maxLatency"),
                            value: String(format: "%.2f ms", Double(engine.chaosState.maxInjectedLatencyNs) / 1_000_000),
                            color: engine.chaosState.maxInjectedLatencyNs > 10_000_000 ? LxColor.bloodRed : theme.textPrimary)
                GlassMetric(loc.t("chaos.engineStatus"), value: loc.t(engine.status.locKey),
                            color: engine.status == .error ? LxColor.bloodRed : LxColor.neonLime)
            }
        }
    }
}

// MARK: - Chaos Fault Card

struct ChaosFaultCard: View {
    @Environment(\.theme) var theme
    let title: String
    let count: UInt64
    let icon: String
    let color: Color
    
    @State private var isHovered = false
    @State private var flashScale: CGFloat = 1.0
    
    init(_ title: String, count: UInt64, icon: String, color: Color) {
        self.title = title
        self.count = count
        self.icon = icon
        self.color = color
    }
    
    private var activeColor: Color { count > 0 ? color : theme.textTertiary }
    
    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: icon)
                .font(.system(size: 20, weight: .medium))
                .foregroundColor(activeColor)
                .shadow(color: count > 0 ? activeColor.opacity(0.5) : .clear, radius: isHovered ? 8 : 4)
            
            Text("\(count)")
                .font(LxFont.bigMetric)
                .foregroundColor(activeColor)
                .shadow(color: count > 0 ? activeColor.opacity(0.3) : .clear, radius: 4)
            
            Text(title)
                .font(LxFont.mono(9, weight: .medium))
                .foregroundColor(theme.textSecondary)
                .multilineTextAlignment(.center)
                .lineLimit(2)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 14)
        .padding(.horizontal, 8)
        .background(
            ZStack {
                RoundedRectangle(cornerRadius: 10)
                    .fill(theme.isDark ? AnyShapeStyle(Color(white: 0.11, opacity: 0.92)) : AnyShapeStyle(Color.white.opacity(0.7)))
                RoundedRectangle(cornerRadius: 10)
                    .fill(count > 0 ? activeColor.opacity(isHovered ? 0.08 : 0.04) : theme.glassHighlight)
            }
        )
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(count > 0 ? activeColor.opacity(isHovered ? 0.3 : 0.12) : theme.borderSubtle, lineWidth: 0.5)
        )
        .shadow(color: count > 0 ? activeColor.opacity(isHovered ? 0.2 : 0.05) : .clear, radius: isHovered ? 8 : 3)
        .scaleEffect(isHovered ? 1.03 : flashScale)
        .animation(LxAnimation.micro, value: isHovered)
        .onHover { hovering in isHovered = hovering }
        .onChange(of: count) { _ in
            withAnimation(LxAnimation.chaosFlash) { flashScale = 1.08 }
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) {
                withAnimation(LxAnimation.chaosFlash) { flashScale = 1.0 }
            }
        }
    }
}
