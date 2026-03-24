// ToastNotification.swift — D-A4: Non-intrusive toast notifications
// Slides in from top-right, auto-dismisses after configurable duration.

import SwiftUI

// MARK: - Toast Model

enum ToastLevel {
    case info, success, warning, error
    
    var icon: String {
        switch self {
        case .info:    return "info.circle.fill"
        case .success: return "checkmark.circle.fill"
        case .warning: return "exclamationmark.triangle.fill"
        case .error:   return "xmark.octagon.fill"
        }
    }
    
    var color: Color {
        switch self {
        case .info:    return LxColor.electricCyan
        case .success: return LxColor.neonLime
        case .warning: return LxColor.amber
        case .error:   return LxColor.bloodRed
        }
    }
}

struct ToastItem: Identifiable, Equatable {
    let id = UUID()
    let level: ToastLevel
    let title: String
    let message: String
    let duration: Double
    
    static func == (lhs: ToastItem, rhs: ToastItem) -> Bool {
        lhs.id == rhs.id
    }
    
    init(level: ToastLevel, title: String, message: String = "", duration: Double = 3.0) {
        self.level = level
        self.title = title
        self.message = message
        self.duration = duration
    }
}

// MARK: - Toast Manager

final class ToastManager: ObservableObject {
    static let shared = ToastManager()
    
    @Published var toasts: [ToastItem] = []
    
    func show(_ level: ToastLevel, title: String, message: String = "", duration: Double = 3.0) {
        let item = ToastItem(level: level, title: title, message: message, duration: duration)
        DispatchQueue.main.async {
            withAnimation(.spring(response: 0.3, dampingFraction: 0.8)) {
                self.toasts.append(item)
            }
            // Auto-dismiss
            DispatchQueue.main.asyncAfter(deadline: .now() + duration) {
                self.dismiss(item)
            }
        }
    }
    
    func dismiss(_ item: ToastItem) {
        withAnimation(.easeOut(duration: 0.2)) {
            self.toasts.removeAll { $0.id == item.id }
        }
    }
}

// MARK: - Toast Overlay

struct ToastOverlay: View {
    @ObservedObject var manager: ToastManager = .shared
    
    var body: some View {
        VStack(alignment: .trailing, spacing: 6) {
            ForEach(manager.toasts) { toast in
                ToastCard(item: toast) {
                    manager.dismiss(toast)
                }
                .transition(.asymmetric(
                    insertion: .move(edge: .trailing).combined(with: .opacity),
                    removal: .move(edge: .trailing).combined(with: .opacity)
                ))
            }
        }
        .padding(.top, 8)
        .padding(.trailing, 12)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
        .allowsHitTesting(!manager.toasts.isEmpty)
    }
}

// MARK: - Toast Card

struct ToastCard: View {
    let item: ToastItem
    let onDismiss: () -> Void
    
    @State private var isHovered = false
    @Environment(\.theme) var theme
    
    var body: some View {
        HStack(spacing: 8) {
            // Level icon
            Image(systemName: item.level.icon)
                .font(.system(size: 14, weight: .semibold))
                .foregroundColor(item.level.color)
                .shadow(color: item.level.color.opacity(0.4), radius: 3)
            
            // Content
            VStack(alignment: .leading, spacing: 1) {
                Text(item.title)
                    .font(LxFont.mono(11, weight: .bold))
                    .foregroundColor(theme.textPrimary)
                    .lineLimit(1)
                
                if !item.message.isEmpty {
                    Text(item.message)
                        .font(LxFont.mono(10))
                        .foregroundColor(theme.textSecondary)
                        .lineLimit(2)
                }
            }
            
            Spacer(minLength: 4)
            
            // Dismiss button
            Button(action: onDismiss) {
                Image(systemName: "xmark")
                    .font(.system(size: 8, weight: .bold))
                    .foregroundColor(theme.textTertiary)
                    .padding(4)
                    .background(Circle().fill(theme.hoverBackground))
            }
            .buttonStyle(.plain)
            .opacity(isHovered ? 1 : 0.5)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .frame(width: 280)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(theme.panelBackground.opacity(0.95))
                .overlay(
                    RoundedRectangle(cornerRadius: 10)
                        .stroke(item.level.color.opacity(0.2), lineWidth: 0.5)
                )
                .shadow(color: Color.black.opacity(0.3), radius: 10, y: 4)
        )
        .onHover { hovering in
            withAnimation(.easeOut(duration: 0.1)) { isHovered = hovering }
        }
    }
}
