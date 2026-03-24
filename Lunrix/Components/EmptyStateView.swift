// EmptyStateView.swift — D5: Reusable loading/empty state component for Lynrix v2.5
// Distinguishes: engine not started, connecting, connected-no-data, explicit empty

import SwiftUI

// MARK: - Data State

enum DataState {
    case engineStopped
    case connecting
    case loading
    case empty(icon: String, titleKey: String, detailKey: String)
    case loaded
    
    /// Convenience: derive state from engine status and data presence
    static func from(engineStatus: EngineStatus, hasData: Bool) -> DataState {
        switch engineStatus {
        case .idle:
            return .engineStopped
        case .connecting:
            return .connecting
        case .connected, .trading:
            return hasData ? .loaded : .loading
        case .stopping:
            return .loading
        case .error:
            return .engineStopped
        }
    }
}

// MARK: - Empty State View

struct EmptyStateView: View {
    let state: DataState
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    var body: some View {
        VStack(spacing: 16) {
            Spacer()
            
            switch state {
            case .engineStopped:
                stateContent(
                    icon: "power.circle",
                    iconColor: theme.textTertiary,
                    title: loc.t("empty.engineStopped"),
                    detail: loc.t("empty.engineStoppedDetail"),
                    pulse: false
                )
                
            case .connecting:
                stateContent(
                    icon: "antenna.radiowaves.left.and.right",
                    iconColor: LxColor.amber,
                    title: loc.t("empty.connecting"),
                    detail: loc.t("empty.connectingDetail"),
                    pulse: true
                )
                
            case .loading:
                stateContent(
                    icon: "circle.dotted",
                    iconColor: LxColor.electricCyan,
                    title: loc.t("empty.loading"),
                    detail: loc.t("empty.loadingDetail"),
                    pulse: true
                )
                
            case .empty(let icon, let titleKey, let detailKey):
                stateContent(
                    icon: icon,
                    iconColor: theme.textTertiary,
                    title: loc.t(titleKey),
                    detail: loc.t(detailKey),
                    pulse: false
                )
                
            case .loaded:
                EmptyView()
            }
            
            Spacer()
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
    
    @State private var isPulsing = false
    
    private func stateContent(icon: String, iconColor: Color, title: String, detail: String, pulse: Bool) -> some View {
        VStack(spacing: 12) {
            ZStack {
                Circle()
                    .fill(iconColor.opacity(0.06))
                    .frame(width: 64, height: 64)
                Image(systemName: icon)
                    .font(.system(size: 24, weight: .medium))
                    .foregroundColor(iconColor)
                    .scaleEffect(pulse && isPulsing ? 1.08 : 1.0)
                    .opacity(pulse && isPulsing ? 0.7 : 1.0)
            }
            .onAppear {
                if pulse {
                    withAnimation(LxAnimation.breathe) { isPulsing = true }
                }
            }
            
            Text(title)
                .font(LxFont.mono(14, weight: .bold))
                .foregroundColor(theme.textPrimary)
            
            Text(detail)
                .font(LxFont.mono(11))
                .foregroundColor(theme.textTertiary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 300)
        }
    }
}

// MARK: - View Extension for convenient empty-state wrapping

extension View {
    /// D5: Wraps content with empty-state overlay based on engine + data state
    func withEmptyState(
        engineStatus: EngineStatus,
        hasData: Bool,
        emptyIcon: String = "tray",
        emptyTitleKey: String = "empty.noData",
        emptyDetailKey: String = "empty.noDataDetail"
    ) -> some View {
        let state = DataState.from(engineStatus: engineStatus, hasData: hasData)
        return ZStack {
            self.opacity(state == .loaded ? 1.0 : 0.0)
            if state != .loaded {
                EmptyStateView(state: state == .loading
                    ? .empty(icon: emptyIcon, titleKey: emptyTitleKey, detailKey: emptyDetailKey)
                    : state
                )
            }
        }
    }
}

// MARK: - DataState Equatable (for animation)

extension DataState: Equatable {
    static func == (lhs: DataState, rhs: DataState) -> Bool {
        switch (lhs, rhs) {
        case (.engineStopped, .engineStopped),
             (.connecting, .connecting),
             (.loading, .loading),
             (.loaded, .loaded):
            return true
        case (.empty(let a, let b, let c), .empty(let d, let e, let f)):
            return a == d && b == e && c == f
        default:
            return false
        }
    }
}
