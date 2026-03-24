// PnlSparkline.swift — D-A3: Compact inline PnL sparkline for sidebar/status bar
// Shows last N PnL values as a mini line chart with color-coded profit/loss.

import SwiftUI

struct PnlSparkline: View {
    let data: [Double]
    var width: CGFloat = 60
    var height: CGFloat = 20
    var lineWidth: CGFloat = 1.2
    
    @Environment(\.theme) var theme
    
    private var normalizedData: [CGFloat] {
        guard data.count >= 2 else { return [] }
        let mn = data.min() ?? 0
        let mx = data.max() ?? 1
        let range = mx - mn
        if range < 1e-12 { return data.map { _ in CGFloat(0.5) } }
        return data.map { CGFloat(($0 - mn) / range) }
    }
    
    private var lineColor: Color {
        guard let last = data.last else { return theme.textTertiary }
        if last > 0.01 { return LxColor.neonLime }
        if last < -0.01 { return LxColor.bloodRed }
        return theme.textTertiary
    }
    
    private var pnlText: String {
        guard let last = data.last else { return "--" }
        let sign = last >= 0 ? "+" : ""
        if abs(last) >= 1000 {
            return String(format: "%@%.1fK", sign, last / 1000)
        }
        return String(format: "%@%.2f", sign, last)
    }
    
    var body: some View {
        HStack(spacing: 4) {
            // Sparkline chart
            if normalizedData.count >= 2 {
                SparklinePath(data: normalizedData, lineWidth: lineWidth)
                    .stroke(lineColor, style: StrokeStyle(lineWidth: lineWidth, lineCap: .round, lineJoin: .round))
                    .frame(width: width, height: height)
            } else {
                Rectangle()
                    .fill(theme.textTertiary.opacity(0.2))
                    .frame(width: width, height: height)
                    .overlay(
                        Text("--")
                            .font(LxFont.mono(8))
                            .foregroundColor(theme.textTertiary)
                    )
            }
            
            // Current PnL value
            Text(pnlText)
                .font(LxFont.mono(10, weight: .bold))
                .foregroundColor(lineColor)
                .lineLimit(1)
        }
    }
}

// MARK: - Sparkline Path Shape

private struct SparklinePath: Shape {
    let data: [CGFloat]
    let lineWidth: CGFloat
    
    func path(in rect: CGRect) -> Path {
        guard data.count >= 2 else { return Path() }
        
        let inset = lineWidth
        let drawRect = rect.insetBy(dx: 0, dy: inset)
        let stepX = drawRect.width / CGFloat(data.count - 1)
        
        var path = Path()
        for (i, val) in data.enumerated() {
            let x = drawRect.minX + stepX * CGFloat(i)
            let y = drawRect.maxY - val * drawRect.height
            if i == 0 {
                path.move(to: CGPoint(x: x, y: y))
            } else {
                path.addLine(to: CGPoint(x: x, y: y))
            }
        }
        return path
    }
}
