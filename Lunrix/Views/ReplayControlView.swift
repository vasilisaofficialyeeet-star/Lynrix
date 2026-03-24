// ReplayControlView.swift — Glassmorphism 2026 Deterministic Replay (Lynrix v2.5)

import SwiftUI

struct ReplayControlView: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    @State private var speedValue: Double = 1.0
    
    private var accentColor: Color { engine.replayState.playing ? LxColor.neonLime : LxColor.gold }
    
    var body: some View {
        ScrollView {
            VStack(spacing: 14) {
                replayHeader
                fileLoaderPanel
                if engine.replayState.loaded {
                    transportPanel
                    verificationPanel
                }
            }
            .padding(16)
        }
        .background(theme.backgroundPrimary)
    }
    
    // MARK: - Header
    
    private var replayHeader: some View {
        GlassPanel(neon: accentColor) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 8) {
                        Image(systemName: "play.rectangle")
                            .font(.system(size: 18, weight: .bold))
                            .foregroundColor(accentColor)
                            .shadow(color: accentColor.opacity(0.5), radius: 6)
                        Text(loc.t("replay.title"))
                            .font(LxFont.mono(16, weight: .bold))
                            .foregroundColor(theme.textPrimary)
                    }
                    Text(loc.t("replay.subtitle"))
                        .font(LxFont.label)
                        .foregroundColor(theme.textTertiary)
                }
                Spacer()
                
                if engine.replayState.loaded {
                    let crcOk = engine.replayState.checksumValid
                    HStack(spacing: 5) {
                        Image(systemName: crcOk ? "checkmark.seal.fill" : "xmark.seal.fill")
                            .shadow(color: (crcOk ? LxColor.neonLime : LxColor.bloodRed).opacity(0.5), radius: 4)
                        Text(loc.t("replay.crc32"))
                    }
                    .font(LxFont.mono(11, weight: .bold))
                    .foregroundColor(crcOk ? LxColor.neonLime : LxColor.bloodRed)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 5)
                    .background(
                        Capsule().fill((crcOk ? LxColor.neonLime : LxColor.bloodRed).opacity(0.1))
                    )
                    .overlay(
                        Capsule().stroke((crcOk ? LxColor.neonLime : LxColor.bloodRed).opacity(0.2), lineWidth: 0.5)
                    )
                }
            }
        }
    }
    
    // MARK: - File Loader
    
    private var fileLoaderPanel: some View {
        GlassPanel(neon: LxColor.electricCyan) {
            VStack(alignment: .leading, spacing: 10) {
                GlassSectionHeader(loc.t("replay.loadedFile"), icon: "doc.fill", color: LxColor.electricCyan)
                
                HStack {
                    Text(engine.replayState.loaded ? engine.replayState.loadedFile : loc.t("replay.noFile"))
                        .font(LxFont.mono(11))
                        .foregroundColor(engine.replayState.loaded ? theme.textPrimary : theme.textTertiary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    Spacer()
                    
                    NeonButton(loc.t("replay.load"), icon: "folder", color: LxColor.electricCyan) {
                        let panel = NSOpenPanel()
                        panel.allowedContentTypes = [.data]
                        panel.allowsMultipleSelection = false
                        panel.message = loc.t("replay.selectBin")
                        panel.begin { result in
                            if result == .OK, let url = panel.url {
                                engine.loadReplayFile(url: url)
                            }
                        }
                    }
                }
                
                if engine.replayState.loaded {
                    HStack(spacing: 12) {
                        NeonMetricCard(loc.t("replay.events"), value: "\(engine.replayState.eventCount)", color: LxColor.electricCyan)
                        NeonMetricCard(loc.t("replay.filtered"), value: "\(engine.replayState.eventsFiltered)", color: LxColor.amber)
                        NeonMetricCard(loc.t("replay.sequence"),
                                       value: engine.replayState.sequenceMonotonic ? loc.t("replay.mono") : loc.t("replay.nonMono"),
                                       color: engine.replayState.sequenceMonotonic ? LxColor.neonLime : LxColor.amber)
                    }
                }
            }
        }
    }
    
    // MARK: - Transport Controls
    
    private var transportPanel: some View {
        GlassPanel(neon: accentColor) {
            VStack(spacing: 12) {
                // Progress
                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        GlassSectionHeader(loc.t("replay.progress"), icon: "timer", color: accentColor)
                        Spacer()
                        Text("\(engine.replayState.eventsReplayed) / \(engine.replayState.eventCount)")
                            .font(LxFont.mono(10))
                            .foregroundColor(theme.textTertiary)
                    }
                    
                    NeonProgressBar(value: engine.replayState.progress, color: accentColor, height: 6)
                    
                    HStack {
                        GlowText(
                            String(format: "%.1f%%", engine.replayState.progress * 100),
                            font: LxFont.mono(11, weight: .bold),
                            color: accentColor,
                            glow: 3
                        )
                        Spacer()
                        Text(formatDuration(engine.replayState.replayDurationNs))
                            .font(LxFont.micro)
                            .foregroundColor(theme.textTertiary)
                    }
                }
                
                // Speed
                VStack(alignment: .leading, spacing: 6) {
                    HStack {
                        Text(loc.t("replay.speedLabel"))
                            .font(LxFont.mono(9, weight: .bold))
                            .foregroundColor(theme.textTertiary)
                        Spacer()
                        GlowText(speedLabel, font: LxFont.mono(12, weight: .bold), color: LxColor.electricCyan, glow: 4)
                    }
                    
                    Slider(value: $speedValue, in: 0...10, step: 0.5) {
                        Text(loc.t("replay.speedName"))
                    } onEditingChanged: { editing in
                        if !editing { engine.setReplaySpeed(speedValue) }
                    }
                    .tint(LxColor.electricCyan)
                    
                    HStack {
                        Text(loc.t("replay.speedMax"))
                            .font(LxFont.mono(8))
                            .foregroundColor(theme.textTertiary)
                        Spacer()
                        Text(loc.t("replay.speedRealtime"))
                            .font(LxFont.mono(8))
                            .foregroundColor(theme.textTertiary)
                    }
                }
                
                // Transport buttons
                HStack(spacing: 10) {
                    NeonButton(loc.t("replay.play"), icon: "play.fill", color: LxColor.neonLime) {
                        engine.startReplay()
                    }
                    NeonButton(loc.t("replay.pause"), icon: "pause.fill", color: LxColor.amber) {
                        engine.stopReplay()
                    }
                    NeonButton(loc.t("replay.step"), icon: "forward.frame.fill", color: LxColor.electricCyan) {
                        engine.stepReplay()
                    }
                }
            }
        }
    }
    
    // MARK: - Verification
    
    private var verificationPanel: some View {
        GlassPanel(neon: LxColor.neonLime) {
            VStack(alignment: .leading, spacing: 8) {
                GlassSectionHeader(loc.t("replay.integrity"), icon: "checkmark.shield", color: LxColor.neonLime)
                
                GlassVerifyRow(loc.t("replay.crc32Checksum"), passed: engine.replayState.checksumValid)
                GlassVerifyRow(loc.t("replay.seqMonotonic"), passed: engine.replayState.sequenceMonotonic)
                GlassVerifyRow(loc.t("replay.fileLoaded"), passed: engine.replayState.loaded)
                GlassVerifyRow(loc.t("replay.eventsGt0"), passed: engine.replayState.eventCount > 0)
            }
        }
    }
    
    // MARK: - Helpers
    
    private var speedLabel: String {
        if speedValue == 0 { return loc.t("replay.maxSpeed") }
        return String(format: "%.1fx", speedValue)
    }
    
    private func formatDuration(_ ns: UInt64) -> String {
        let ms = Double(ns) / 1_000_000
        if ms < 1000 { return String(format: "%.1f ms", ms) }
        let sec = ms / 1000
        if sec < 60 { return String(format: "%.1f s", sec) }
        return String(format: "%.1f min", sec / 60)
    }
}

// MARK: - Verify Row

struct GlassVerifyRow: View {
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    let label: String
    let passed: Bool
    
    init(_ label: String, passed: Bool) {
        self.label = label
        self.passed = passed
    }
    
    var color: Color { passed ? LxColor.neonLime : LxColor.bloodRed }
    
    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: passed ? "checkmark.circle.fill" : "xmark.circle.fill")
                .font(.system(size: 12))
                .foregroundColor(color)
                .shadow(color: color.opacity(0.4), radius: 3)
            Text(label)
                .font(LxFont.mono(11))
                .foregroundColor(theme.textSecondary)
            Spacer()
            Text(passed ? loc.t("replay.pass") : loc.t("replay.fail"))
                .font(LxFont.mono(9, weight: .bold))
                .foregroundColor(color)
                .shadow(color: color.opacity(0.3), radius: 2)
        }
    }
}
