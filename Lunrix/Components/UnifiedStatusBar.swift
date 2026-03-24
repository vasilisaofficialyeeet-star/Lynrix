// UnifiedStatusBar.swift — Shared top status strip for all views (Lynrix v2.5)

import SwiftUI

struct UnifiedStatusBar: View {
    @EnvironmentObject var engine: LynrixEngine
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    var body: some View {
        HStack(spacing: 0) {
            // Engine status
            EngineStatusBadge(status: engine.status)
            
            separator
            
            // Mode badge
            TradingModeBadge(mode: engine.tradingMode)
            
            separator
            
            // Symbol
            HStack(spacing: 4) {
                Image(systemName: "chart.line.uptrend.xyaxis")
                    .font(.system(size: 9, weight: .semibold))
                    .foregroundColor(LxColor.electricCyan)
                Text(engine.config.symbol)
                    .font(LxFont.mono(10, weight: .bold))
                    .foregroundColor(theme.textPrimary)
            }
            
            separator
            
            // Mid price
            if engine.orderBook.midPrice > 0 {
                HStack(spacing: 4) {
                    Text(loc.t("dashboard.midPrice"))
                        .font(LxFont.mono(9))
                        .foregroundColor(theme.textTertiary)
                    Text(String(format: "%.2f", engine.orderBook.midPrice))
                        .font(LxFont.mono(10, weight: .bold))
                        .foregroundColor(LxColor.electricCyan)
                }
                
                separator
            }
            
            // Spread
            if engine.orderBook.spread > 0 {
                HStack(spacing: 4) {
                    Text(loc.t("dashboard.spread"))
                        .font(LxFont.mono(9))
                        .foregroundColor(theme.textTertiary)
                    Text(String(format: "%.4f", engine.orderBook.spread))
                        .font(LxFont.mono(10, weight: .bold))
                        .foregroundColor(LxColor.neonLime)
                }
                
                separator
            }
            
            // Latency
            HStack(spacing: 4) {
                Text(loc.t("dashboard.latency"))
                    .font(LxFont.mono(9))
                    .foregroundColor(theme.textTertiary)
                Text(String(format: "%.0f µs", engine.metrics.e2eLatencyP50Us))
                    .font(LxFont.mono(10, weight: .bold))
                    .foregroundColor(engine.metrics.e2eLatencyP50Us > 100 ? LxColor.amber : LxColor.neonLime)
            }
            
            // D8: Persistent connection status indicator
            if engine.status.isActive {
                separator
                connectionStatusIndicator
            }
            
            Spacer()
            
            // Kill switch halt
            if engine.killSwitch.isAnyHaltActive {
                HStack(spacing: 3) {
                    Image(systemName: "exclamationmark.octagon.fill")
                        .font(.system(size: 9))
                    Text(loc.t("risk.halt"))
                        .font(LxFont.mono(9, weight: .bold))
                }
                .foregroundColor(LxColor.bloodRed)
                .padding(.horizontal, 6)
                .padding(.vertical, 2)
                .background(Capsule().fill(LxColor.bloodRed.opacity(0.12)))
                .overlay(Capsule().stroke(LxColor.bloodRed.opacity(0.25), lineWidth: 0.5))
                
                separator
            }
            
            // Chaos indicator
            if engine.chaosState.enabled {
                HStack(spacing: 3) {
                    Image(systemName: "tornado")
                        .font(.system(size: 9))
                    Text(loc.t("engine.chaos"))
                        .font(LxFont.mono(9, weight: .bold))
                }
                .foregroundColor(LxColor.bloodRed)
                .padding(.horizontal, 6)
                .padding(.vertical, 2)
                .background(Capsule().fill(LxColor.bloodRed.opacity(0.1)))
                .overlay(Capsule().stroke(LxColor.bloodRed.opacity(0.2), lineWidth: 0.5))
                
                separator
            }
            
            // Reconnecting indicator
            if engine.isReconnecting {
                HStack(spacing: 3) {
                    Image(systemName: "arrow.triangle.2.circlepath")
                        .font(.system(size: 9))
                    Text(loc.t("engine.reconnecting"))
                        .font(LxFont.mono(9, weight: .bold))
                }
                .foregroundColor(LxColor.amber)
                .padding(.horizontal, 6)
                .padding(.vertical, 2)
                .background(Capsule().fill(LxColor.amber.opacity(0.1)))
                .overlay(Capsule().stroke(LxColor.amber.opacity(0.2), lineWidth: 0.5))
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 5)
        .background(
            // PERF P1: Solid background replaces VisualEffectBackground blur
            theme.backgroundSecondary.opacity(0.85)
        )
        .overlay(
            Rectangle()
                .fill(theme.divider)
                .frame(height: 0.5),
            alignment: .bottom
        )
    }
    
    // D8: Connection status indicator — derives health from OB validity, reconnect state, latency
    private var connectionStatusIndicator: some View {
        let health = connectionHealth
        return HStack(spacing: 4) {
            Circle()
                .fill(health.color)
                .frame(width: 6, height: 6)
                .shadow(color: health.color.opacity(0.5 * theme.glowOpacity), radius: 3 * theme.glowIntensity)
            Text(loc.t(health.locKey))
                .font(LxFont.mono(9, weight: .medium))
                .foregroundColor(health.color)
            if engine.systemMonitor.wsLatencyP50Us > 0 {
                Text(String(format: "%.0fms", engine.systemMonitor.wsLatencyP50Us / 1000.0))
                    .font(LxFont.mono(8))
                    .foregroundColor(theme.textTertiary)
            }
        }
    }
    
    private var connectionHealth: ConnectionHealth {
        if engine.isReconnecting {
            return .disconnected
        }
        if !engine.orderBook.valid && engine.status.isActive {
            return .degraded
        }
        if engine.systemMonitor.wsLatencyP50Us > 50000 { // > 50ms WS latency
            return .degraded
        }
        return .connected
    }
    
    enum ConnectionHealth {
        case connected, degraded, disconnected
        
        var color: Color {
            switch self {
            case .connected:    return LxColor.neonLime
            case .degraded:     return LxColor.amber
            case .disconnected: return LxColor.bloodRed
            }
        }
        
        var locKey: String {
            switch self {
            case .connected:    return "conn.connected"
            case .degraded:     return "conn.degraded"
            case .disconnected: return "conn.disconnected"
            }
        }
    }
    
    private var separator: some View {
        Rectangle()
            .fill(theme.divider)
            .frame(width: 0.5, height: 14)
            .padding(.horizontal, 8)
    }
}
