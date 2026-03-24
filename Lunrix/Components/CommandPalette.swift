// CommandPalette.swift — D-A1: VS Code-style command palette (Cmd+K)
// Fuzzy search through tabs, actions, and engine commands.

import SwiftUI

// MARK: - Command Item

struct CommandItem: Identifiable {
    let id = UUID()
    let title: String
    let subtitle: String
    let icon: String
    let color: Color
    let shortcut: String?
    let action: () -> Void
    
    /// Simple fuzzy match: all query chars appear in order in the title (case-insensitive)
    func matches(_ query: String) -> Bool {
        if query.isEmpty { return true }
        let lower = title.lowercased() + " " + subtitle.lowercased()
        var idx = lower.startIndex
        for ch in query.lowercased() {
            guard let found = lower[idx...].firstIndex(of: ch) else { return false }
            idx = lower.index(after: found)
        }
        return true
    }
}

// MARK: - Command Palette View

struct CommandPalette: View {
    @Binding var isPresented: Bool
    let commands: [CommandItem]
    
    @State private var query: String = ""
    @State private var selectedIndex: Int = 0
    @FocusState private var isFocused: Bool
    @Environment(\.theme) var theme
    @EnvironmentObject var loc: LocalizationManager
    
    private var filtered: [CommandItem] {
        commands.filter { $0.matches(query) }
    }
    
    var body: some View {
        VStack(spacing: 0) {
            // Search field
            HStack(spacing: 8) {
                Image(systemName: "magnifyingglass")
                    .font(.system(size: 14, weight: .medium))
                    .foregroundColor(LxColor.electricCyan.opacity(0.6))
                
                TextField(loc.t("palette.placeholder"), text: $query)
                    .textFieldStyle(.plain)
                    .font(LxFont.mono(14))
                    .foregroundColor(theme.textPrimary)
                    .focused($isFocused)
                    .onSubmit { executeSelected() }
                    .onChange(of: query) { _ in selectedIndex = 0 }
                
                if !query.isEmpty {
                    Button(action: { query = "" }) {
                        Image(systemName: "xmark.circle.fill")
                            .font(.system(size: 12))
                            .foregroundColor(theme.textTertiary)
                    }
                    .buttonStyle(.plain)
                }
                
                // Dismiss hint
                Text("esc")
                    .font(LxFont.mono(9, weight: .medium))
                    .foregroundColor(theme.textTertiary)
                    .padding(.horizontal, 5)
                    .padding(.vertical, 2)
                    .background(
                        RoundedRectangle(cornerRadius: 3)
                            .fill(theme.inputBackground)
                    )
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .background(theme.backgroundSecondary.opacity(0.8))
            
            Divider().background(LxColor.electricCyan.opacity(0.2))
            
            // Results
            ScrollViewReader { proxy in
                ScrollView(.vertical, showsIndicators: false) {
                    LazyVStack(spacing: 1) {
                        ForEach(Array(filtered.enumerated()), id: \.element.id) { index, item in
                            CommandRow(
                                item: item,
                                isSelected: index == selectedIndex
                            ) {
                                item.action()
                                dismiss()
                            }
                            .id(index)
                        }
                        
                        if filtered.isEmpty {
                            HStack {
                                Spacer()
                                Text(loc.t("palette.noResults"))
                                    .font(LxFont.mono(12))
                                    .foregroundColor(theme.textTertiary)
                                Spacer()
                            }
                            .padding(.vertical, 20)
                        }
                    }
                    .padding(.vertical, 4)
                }
                .frame(maxHeight: 340)
                .onChange(of: selectedIndex) { idx in
                    withAnimation(.easeOut(duration: 0.1)) {
                        proxy.scrollTo(idx, anchor: .center)
                    }
                }
            }
        }
        .frame(width: 480)
        .background(
            RoundedRectangle(cornerRadius: 12)
                .fill(theme.backgroundPrimary.opacity(0.95))
                .overlay(
                    RoundedRectangle(cornerRadius: 12)
                        .stroke(LxColor.electricCyan.opacity(0.15), lineWidth: 0.5)
                )
                .shadow(color: Color.black.opacity(0.4), radius: 20, y: 8)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12))
        .onAppear {
            query = ""
            selectedIndex = 0
            isFocused = true
        }
        .modifier(PaletteKeyHandler(
            onUp: { moveSelection(-1) },
            onDown: { moveSelection(1) },
            onEscape: { dismiss() },
            onReturn: { executeSelected() }
        ))
    }
    
    private func moveSelection(_ delta: Int) {
        let count = filtered.count
        guard count > 0 else { return }
        selectedIndex = (selectedIndex + delta + count) % count
    }
    
    private func executeSelected() {
        guard !filtered.isEmpty, selectedIndex < filtered.count else { return }
        filtered[selectedIndex].action()
        dismiss()
    }
    
    private func dismiss() {
        query = ""
        isPresented = false
    }
}

// MARK: - D-A2: Palette Key Handler (macOS 13+ compatible)

struct PaletteKeyHandler: ViewModifier {
    let onUp: () -> Void
    let onDown: () -> Void
    let onEscape: () -> Void
    let onReturn: () -> Void
    
    func body(content: Content) -> some View {
        if #available(macOS 14.0, *) {
            content
                .onKeyPress(.upArrow) { onUp(); return .handled }
                .onKeyPress(.downArrow) { onDown(); return .handled }
                .onKeyPress(.escape) { onEscape(); return .handled }
                .onKeyPress(.return) { onReturn(); return .handled }
        } else {
            content
                .background(PaletteKeyMonitor(onUp: onUp, onDown: onDown, onEscape: onEscape, onReturn: onReturn))
        }
    }
}

/// NSEvent-based fallback for macOS 13
private struct PaletteKeyMonitor: NSViewRepresentable {
    let onUp: () -> Void
    let onDown: () -> Void
    let onEscape: () -> Void
    let onReturn: () -> Void
    
    func makeNSView(context: Context) -> NSView {
        let view = PaletteKeyView()
        view.onUp = onUp
        view.onDown = onDown
        view.onEscape = onEscape
        view.onReturn = onReturn
        return view
    }
    
    func updateNSView(_ nsView: NSView, context: Context) {}
}

private class PaletteKeyView: NSView {
    var onUp: (() -> Void)?
    var onDown: (() -> Void)?
    var onEscape: (() -> Void)?
    var onReturn: (() -> Void)?
    
    override var acceptsFirstResponder: Bool { true }
    
    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 126: onUp?()      // up arrow
        case 125: onDown?()    // down arrow
        case 53:  onEscape?()  // escape
        case 36:  onReturn?()  // return
        default: super.keyDown(with: event)
        }
    }
}

// MARK: - D-A2: Keyboard Shortcut Modifier (for ContentView)

struct KeyboardShortcutModifier: ViewModifier {
    let onCommand: (String) -> Bool
    @Binding var showPalette: Bool
    
    func body(content: Content) -> some View {
        if #available(macOS 14.0, *) {
            content
                .onKeyPress(phases: .down) { press in
                    guard press.modifiers.contains(.command) else { return .ignored }
                    return onCommand(String(press.characters)) ? .handled : .ignored
                }
        } else {
            content
                .background(GlobalKeyMonitor(onCommand: onCommand))
        }
    }
}

/// NSEvent local monitor fallback for macOS 13
private struct GlobalKeyMonitor: NSViewRepresentable {
    let onCommand: (String) -> Bool
    
    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        context.coordinator.monitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            if event.modifierFlags.contains(.command),
               let chars = event.characters,
               self.onCommand(chars) {
                return nil // consumed
            }
            return event
        }
        return view
    }
    
    func updateNSView(_ nsView: NSView, context: Context) {}
    
    func makeCoordinator() -> Coordinator { Coordinator() }
    
    static func dismantleNSView(_ nsView: NSView, coordinator: Coordinator) {
        if let monitor = coordinator.monitor {
            NSEvent.removeMonitor(monitor)
        }
    }
    
    class Coordinator {
        var monitor: Any?
    }
}

// MARK: - Command Row

struct CommandRow: View {
    let item: CommandItem
    let isSelected: Bool
    let action: () -> Void
    
    @State private var isHovered = false
    @Environment(\.theme) var theme
    
    var body: some View {
        Button(action: action) {
            HStack(spacing: 10) {
                Image(systemName: item.icon)
                    .font(.system(size: 12, weight: .medium))
                    .foregroundColor(isSelected ? item.color : theme.textSecondary)
                    .frame(width: 20)
                
                VStack(alignment: .leading, spacing: 1) {
                    Text(item.title)
                        .font(LxFont.mono(12, weight: isSelected ? .bold : .medium))
                        .foregroundColor(isSelected ? theme.textPrimary : theme.textSecondary)
                    if !item.subtitle.isEmpty {
                        Text(item.subtitle)
                            .font(LxFont.mono(10))
                            .foregroundColor(theme.textTertiary)
                    }
                }
                
                Spacer()
                
                if let shortcut = item.shortcut {
                    Text(shortcut)
                        .font(LxFont.mono(10, weight: .medium))
                        .foregroundColor(theme.textTertiary)
                        .padding(.horizontal, 5)
                        .padding(.vertical, 2)
                        .background(
                            RoundedRectangle(cornerRadius: 3)
                                .fill(theme.inputBackground)
                        )
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(
                RoundedRectangle(cornerRadius: 6)
                    .fill(isSelected ? item.color.opacity(0.08) : (isHovered ? theme.hoverBackground : Color.clear))
            )
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { hovering in
            withAnimation(.easeOut(duration: 0.1)) { isHovered = hovering }
        }
    }
}
