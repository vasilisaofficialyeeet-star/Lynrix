// AnimatedEquityCurve.swift — Canvas-drawn PnL curve with gradient fill and glow
// Theme-aware: adapts glow intensity and empty-state colors

import SwiftUI

struct AnimatedEquityCurve: View {
    let data: [Double]
    let positiveColor: Color
    let negativeColor: Color
    let height: CGFloat
    let showBaseline: Bool
    @EnvironmentObject var loc: LocalizationManager
    @Environment(\.theme) var theme
    
    @State private var animationProgress: CGFloat = 0
    
    init(
        data: [Double],
        positive: Color = LxColor.neonLime,
        negative: Color = LxColor.magentaPink,
        height: CGFloat = 100,
        showBaseline: Bool = true
    ) {
        self.data = data
        self.positiveColor = positive
        self.negativeColor = negative
        self.height = height
        self.showBaseline = showBaseline
    }
    
    private var lastValue: Double { data.last ?? 0 }
    private var lineColor: Color { lastValue >= 0 ? positiveColor : negativeColor }
    
    // D9: Throttle counter — only re-animate every Nth data change (~1 FPS at 100ms poll)
    @State private var updateCounter: Int = 0
    private static let updateThrottle = 10
    
    var body: some View {
        if data.count < 2 {
            emptyState
        } else {
            Canvas { context, size in
                drawCurve(context: context, size: size)
            }
            .frame(height: height)
            // D9: drawingGroup() composites into a single GPU texture — eliminates per-frame recomposition
            .drawingGroup()
            .onAppear {
                withAnimation(.easeOut(duration: 0.8)) {
                    animationProgress = 1.0
                }
            }
            .onChange(of: data.count) { _ in
                // D9: Throttle animation re-trigger to ~1 FPS
                updateCounter += 1
                guard updateCounter >= Self.updateThrottle else { return }
                updateCounter = 0
                animationProgress = 0
                withAnimation(.easeOut(duration: 0.4)) {
                    animationProgress = 1.0
                }
            }
        }
    }
    
    private var emptyState: some View {
        RoundedRectangle(cornerRadius: 8)
            .fill(theme.glassHighlight)
            .frame(height: height)
            .overlay(
                Text(loc.t("ai.noData").uppercased())
                    .font(LxFont.micro)
                    .foregroundColor(theme.textTertiary)
            )
    }
    
    private func drawCurve(context: GraphicsContext, size: CGSize) {
        guard data.count > 1 else { return }
        
        let minVal = data.min() ?? 0
        let maxVal = data.max() ?? 1
        let range = max(maxVal - minVal, 0.0001)
        let padding: CGFloat = 4
        let w = size.width - padding * 2
        let h = size.height - padding * 2
        let xStep = w / CGFloat(data.count - 1)
        let glowMul = theme.glowOpacity
        
        // Build path
        var linePath = Path()
        let visibleCount = Int(CGFloat(data.count) * animationProgress)
        
        for i in 0..<max(visibleCount, 2) {
            let idx = min(i, data.count - 1)
            let x = padding + CGFloat(idx) * xStep
            let y = padding + h - CGFloat((data[idx] - minVal) / range) * h
            if i == 0 {
                linePath.move(to: CGPoint(x: x, y: y))
            } else {
                linePath.addLine(to: CGPoint(x: x, y: y))
            }
        }
        
        // Baseline
        if showBaseline {
            let baseY: CGFloat
            if minVal < 0 && maxVal > 0 {
                baseY = padding + h - CGFloat((0 - minVal) / range) * h
            } else {
                baseY = padding + h
            }
            var basePath = Path()
            basePath.move(to: CGPoint(x: padding, y: baseY))
            basePath.addLine(to: CGPoint(x: padding + w, y: baseY))
            let baseColor = theme.isDark ? Color(hex: 0x4A5568).opacity(0.3) : Color.black.opacity(0.12)
            context.stroke(basePath, with: .color(baseColor), style: StrokeStyle(lineWidth: 0.5, dash: [4, 4]))
        }
        
        // Fill gradient
        var fillPath = linePath
        let lastX = padding + CGFloat(min(visibleCount - 1, data.count - 1)) * xStep
        fillPath.addLine(to: CGPoint(x: lastX, y: padding + h))
        fillPath.addLine(to: CGPoint(x: padding, y: padding + h))
        fillPath.closeSubpath()
        
        let fillOpacity = theme.isDark ? 0.2 : 0.12
        let gradient = Gradient(colors: [lineColor.opacity(fillOpacity), lineColor.opacity(0.02)])
        context.fill(fillPath, with: .linearGradient(gradient, startPoint: CGPoint(x: 0, y: 0), endPoint: CGPoint(x: 0, y: size.height)))
        
        // Glow line
        context.stroke(linePath, with: .color(lineColor.opacity(0.3 * glowMul)), style: StrokeStyle(lineWidth: 4))
        // Main line
        context.stroke(linePath, with: .color(lineColor), style: StrokeStyle(lineWidth: 1.5, lineCap: .round, lineJoin: .round))
        
        // End dot
        if visibleCount > 0 {
            let lastIdx = min(visibleCount - 1, data.count - 1)
            let dotX = padding + CGFloat(lastIdx) * xStep
            let dotY = padding + h - CGFloat((data[lastIdx] - minVal) / range) * h
            let dotRect = CGRect(x: dotX - 3, y: dotY - 3, width: 6, height: 6)
            context.fill(Path(ellipseIn: dotRect.insetBy(dx: -2, dy: -2)), with: .color(lineColor.opacity(0.3 * glowMul)))
            context.fill(Path(ellipseIn: dotRect), with: .color(lineColor))
        }
    }
}

// MARK: - Sparkline (mini inline chart)

struct Sparkline: View {
    let data: [Double]
    let color: Color
    let height: CGFloat
    @Environment(\.theme) var theme
    
    init(_ data: [Double], color: Color = LxColor.electricCyan, height: CGFloat = 24) {
        self.data = data
        self.color = color
        self.height = height
    }
    
    var body: some View {
        if data.count < 2 {
            Rectangle().fill(Color.clear).frame(height: height)
        } else {
            Canvas { context, size in
                let minV = data.min() ?? 0
                let maxV = data.max() ?? 1
                let range = max(maxV - minV, 0.0001)
                let xStep = size.width / CGFloat(data.count - 1)
                
                var path = Path()
                for (i, v) in data.enumerated() {
                    let x = CGFloat(i) * xStep
                    let y = size.height - CGFloat((v - minV) / range) * size.height
                    if i == 0 { path.move(to: CGPoint(x: x, y: y)) }
                    else { path.addLine(to: CGPoint(x: x, y: y)) }
                }
                let glowMul = theme.glowOpacity
                context.stroke(path, with: .color(color.opacity(0.4 * glowMul)), style: StrokeStyle(lineWidth: 3))
                context.stroke(path, with: .color(color), style: StrokeStyle(lineWidth: 1, lineCap: .round))
            }
            .frame(height: height)
        }
    }
}

// MARK: - Neon Progress Bar

struct NeonProgressBar: View {
    let value: Double
    let color: Color
    let height: CGFloat
    @Environment(\.theme) var theme
    
    init(value: Double, color: Color = LxColor.electricCyan, height: CGFloat = 4) {
        self.value = value
        self.color = color
        self.height = height
    }
    
    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: height / 2)
                    .fill(color.opacity(theme.isDark ? 0.1 : 0.08))
                RoundedRectangle(cornerRadius: height / 2)
                    .fill(color)
                    .frame(width: max(0, geo.size.width * CGFloat(min(max(value, 0), 1))))
                    .shadow(color: color.opacity(0.5 * theme.glowOpacity), radius: 4 * theme.glowIntensity)
            }
        }
        .frame(height: height)
    }
}
