// AIDashboardView.swift — AI Edition metrics: regime, prediction, threshold, circuit breaker

import SwiftUI

struct AIDashboardView: View {
    @EnvironmentObject var engine: TradingEngine
    
    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                // Row 1: Regime + Model Prediction
                HStack(spacing: 16) {
                    regimeCard
                    predictionCard
                }
                
                // Row 2: Adaptive Threshold + Circuit Breaker
                HStack(spacing: 16) {
                    thresholdCard
                    circuitBreakerCard
                }
                
                // Row 3: Multi-horizon predictions
                multiHorizonCard
                
                // Row 4: Model latency + accuracy
                HStack(spacing: 16) {
                    MetricCard(title: "ML Латенси (p50)",
                               value: String(format: "%.1f мкс", engine.metrics.modelLatencyP50Us),
                               color: .teal)
                    MetricCard(title: "ML Латенси (p99)",
                               value: String(format: "%.1f мкс", engine.metrics.modelLatencyP99Us),
                               color: .teal)
                    MetricCard(title: "Точность модели",
                               value: String(format: "%.1f%%", engine.accuracy.rollingAccuracy * 100),
                               subtitle: "\(engine.accuracy.correctPredictions)/\(engine.accuracy.totalPredictions)",
                               color: accuracyColor)
                    MetricCard(title: "Drawdown",
                               value: String(format: "%.2f%%", engine.circuitBreaker.drawdownPct),
                               color: engine.circuitBreaker.drawdownPct > 3 ? .red : .secondary)
                }
                
                // Row 5: Accuracy details + per-class metrics
                accuracyDetailsCard
                
                // Row 6: Per-horizon accuracy
                horizonAccuracyCard
                
                // Row 7: Charts
                HStack(spacing: 16) {
                    pnlChartCard
                    drawdownChartCard
                }
                
                // Row 8: Accuracy chart
                accuracyChartCard
                
                Spacer()
            }
            .padding(20)
        }
        .background(Color(.windowBackgroundColor))
    }
    
    // MARK: - Regime Card
    
    private var regimeCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("РЕЖИМ РЫНКА")
                    .font(.system(.caption2, design: .monospaced))
                    .foregroundColor(.secondary)
                Spacer()
                Circle()
                    .fill(regimeColor)
                    .frame(width: 8, height: 8)
            }
            
            Text(engine.regime.regimeName)
                .font(.system(.title2, design: .monospaced))
                .fontWeight(.bold)
                .foregroundColor(regimeColor)
            
            HStack(spacing: 12) {
                VStack(alignment: .leading) {
                    Text("Увер.")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    Text(String(format: "%.0f%%", engine.regime.confidence * 100))
                        .font(.system(.caption, design: .monospaced))
                }
                VStack(alignment: .leading) {
                    Text("Волат.")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    Text(String(format: "%.5f", engine.regime.volatility))
                        .font(.system(.caption, design: .monospaced))
                }
                VStack(alignment: .leading) {
                    Text("Тренд")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    Text(String(format: "%.2f", engine.regime.trendScore))
                        .font(.system(.caption, design: .monospaced))
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(Color(.controlBackgroundColor).opacity(0.6))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
    
    // MARK: - Prediction Card
    
    private var predictionCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("ПРЕДСКАЗАНИЕ ML")
                    .font(.system(.caption2, design: .monospaced))
                    .foregroundColor(.secondary)
                Spacer()
                Text(engine.prediction.direction)
                    .font(.title2)
            }
            
            HStack(spacing: 16) {
                VStack(alignment: .leading) {
                    Text("↑ Рост")
                        .font(.caption2)
                        .foregroundColor(.green)
                    Text(String(format: "%.1f%%", engine.prediction.probUp * 100))
                        .font(.system(.title3, design: .monospaced))
                        .foregroundColor(.green)
                }
                VStack(alignment: .leading) {
                    Text("↓ Падение")
                        .font(.caption2)
                        .foregroundColor(.red)
                    Text(String(format: "%.1f%%", engine.prediction.probDown * 100))
                        .font(.system(.title3, design: .monospaced))
                        .foregroundColor(.red)
                }
                VStack(alignment: .leading) {
                    Text("Увер.")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                    Text(String(format: "%.1f%%", engine.prediction.modelConfidence * 100))
                        .font(.system(.title3, design: .monospaced))
                        .foregroundColor(.white)
                }
            }
            
            // Probability bar
            GeometryReader { geo in
                HStack(spacing: 0) {
                    Rectangle()
                        .fill(Color.green.opacity(0.6))
                        .frame(width: geo.size.width * CGFloat(engine.prediction.probUp))
                    Rectangle()
                        .fill(Color.gray.opacity(0.3))
                        .frame(width: geo.size.width * CGFloat(1.0 - engine.prediction.probUp - engine.prediction.probDown))
                    Rectangle()
                        .fill(Color.red.opacity(0.6))
                        .frame(width: geo.size.width * CGFloat(engine.prediction.probDown))
                }
                .clipShape(RoundedRectangle(cornerRadius: 3))
            }
            .frame(height: 6)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(Color(.controlBackgroundColor).opacity(0.6))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
    
    // MARK: - Threshold Card
    
    private var thresholdCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("АДАПТИВНЫЙ ПОРОГ")
                .font(.system(.caption2, design: .monospaced))
                .foregroundColor(.secondary)
            
            Text(String(format: "%.3f", engine.threshold.currentThreshold))
                .font(.system(.title2, design: .monospaced))
                .fontWeight(.bold)
                .foregroundColor(.yellow)
            
            HStack(spacing: 8) {
                adjBadge("Vol", engine.threshold.volatilityAdj)
                adjBadge("Acc", engine.threshold.accuracyAdj)
                adjBadge("Liq", engine.threshold.liquidityAdj)
                adjBadge("Spr", engine.threshold.spreadAdj)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(Color(.controlBackgroundColor).opacity(0.6))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
    
    private func adjBadge(_ label: String, _ value: Double) -> some View {
        VStack(spacing: 2) {
            Text(label)
                .font(.system(size: 9, design: .monospaced))
                .foregroundColor(.secondary)
            Text(String(format: "%+.3f", value))
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(value > 0 ? .red : (value < 0 ? .green : .secondary))
        }
        .padding(.horizontal, 6)
        .padding(.vertical, 3)
        .background(Color(.controlBackgroundColor).opacity(0.8))
        .clipShape(RoundedRectangle(cornerRadius: 4))
    }
    
    // MARK: - Circuit Breaker Card
    
    private var circuitBreakerCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("CIRCUIT BREAKER")
                    .font(.system(.caption2, design: .monospaced))
                    .foregroundColor(.secondary)
                Spacer()
                if engine.circuitBreaker.tripped {
                    Text("СТОП")
                        .font(.system(.caption, design: .monospaced))
                        .fontWeight(.bold)
                        .foregroundColor(.white)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 2)
                        .background(Color.red)
                        .clipShape(RoundedRectangle(cornerRadius: 4))
                } else {
                    Text("OK")
                        .font(.system(.caption, design: .monospaced))
                        .fontWeight(.bold)
                        .foregroundColor(.green)
                }
            }
            
            Text(engine.circuitBreaker.tripped ? "Торговля остановлена" : "Система активна")
                .font(.system(.body, design: .monospaced))
                .foregroundColor(engine.circuitBreaker.tripped ? .red : .green)
            
            HStack {
                Text("Просадка:")
                    .font(.caption2)
                    .foregroundColor(.secondary)
                Text(String(format: "%.2f%%", engine.circuitBreaker.drawdownPct))
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(engine.circuitBreaker.drawdownPct > 3 ? .orange : .secondary)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(
            engine.circuitBreaker.tripped
                ? Color.red.opacity(0.1)
                : Color(.controlBackgroundColor).opacity(0.6)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .overlay(
            engine.circuitBreaker.tripped
                ? RoundedRectangle(cornerRadius: 8).stroke(Color.red, lineWidth: 1)
                : nil
        )
    }
    
    // MARK: - Multi-Horizon Card
    
    private var multiHorizonCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("МУЛЬТИ-ГОРИЗОНТ ПРЕДСКАЗАНИЙ")
                .font(.system(.caption2, design: .monospaced))
                .foregroundColor(.secondary)
            
            HStack(spacing: 12) {
                horizonColumn("100мс",
                              up: engine.prediction.h100ms.up,
                              down: engine.prediction.h100ms.down)
                horizonColumn("500мс",
                              up: engine.prediction.h500ms.up,
                              down: engine.prediction.h500ms.down)
                horizonColumn("1с",
                              up: engine.prediction.h1s.up,
                              down: engine.prediction.h1s.down)
                horizonColumn("3с",
                              up: engine.prediction.h3s.up,
                              down: engine.prediction.h3s.down)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(Color(.controlBackgroundColor).opacity(0.6))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
    
    private func horizonColumn(_ label: String, up: Double, down: Double) -> some View {
        VStack(spacing: 4) {
            Text(label)
                .font(.system(.caption2, design: .monospaced))
                .foregroundColor(.secondary)
            
            // Vertical bar
            VStack(spacing: 1) {
                Rectangle()
                    .fill(Color.green.opacity(0.6))
                    .frame(height: CGFloat(up) * 40)
                Rectangle()
                    .fill(Color.gray.opacity(0.2))
                    .frame(height: CGFloat(1.0 - up - down) * 40)
                Rectangle()
                    .fill(Color.red.opacity(0.6))
                    .frame(height: CGFloat(down) * 40)
            }
            .frame(width: 20)
            .clipShape(RoundedRectangle(cornerRadius: 3))
            
            Text(String(format: "%.0f/%.0f", up * 100, down * 100))
                .font(.system(size: 9, design: .monospaced))
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity)
    }
    
    // MARK: - PnL Chart
    
    private var pnlChartCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("PnL ГРАФИК")
                    .font(.system(.caption2, design: .monospaced))
                    .foregroundColor(.secondary)
                Spacer()
                Text(String(format: "%+.4f", engine.position.netPnl))
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(engine.position.netPnl >= 0 ? .green : .red)
            }
            
            if engine.pnlHistory.count > 1 {
                PnLChartShape(data: engine.pnlHistory)
                    .stroke(engine.position.netPnl >= 0 ? Color.green : Color.red, lineWidth: 1.5)
                    .frame(height: 80)
                    .background(
                        PnLChartShape(data: engine.pnlHistory)
                            .fill(
                                LinearGradient(
                                    colors: [
                                        (engine.position.netPnl >= 0 ? Color.green : Color.red).opacity(0.2),
                                        Color.clear
                                    ],
                                    startPoint: .top,
                                    endPoint: .bottom
                                )
                            )
                    )
            } else {
                Rectangle()
                    .fill(Color(.controlBackgroundColor).opacity(0.3))
                    .frame(height: 80)
                    .overlay(
                        Text("Ожидание данных...")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    )
            }
        }
        .padding(12)
        .background(Color(.controlBackgroundColor).opacity(0.6))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
    
    // MARK: - Accuracy Details Card
    
    private var accuracyDetailsCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("ТОЧНОСТЬ МОДЕЛИ")
                    .font(.system(.caption2, design: .monospaced))
                    .foregroundColor(.secondary)
                Spacer()
                if engine.accuracy.usingOnnx {
                    Text("ONNX")
                        .font(.system(size: 9, design: .monospaced))
                        .fontWeight(.bold)
                        .foregroundColor(.white)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(Color.purple)
                        .clipShape(RoundedRectangle(cornerRadius: 3))
                } else {
                    Text("GRU")
                        .font(.system(size: 9, design: .monospaced))
                        .foregroundColor(.secondary)
                }
            }
            
            HStack(spacing: 20) {
                // Per-class metrics
                classMetricColumn("Рост", 
                                  precision: engine.accuracy.precisionUp,
                                  recall: engine.accuracy.recallUp,
                                  f1: engine.accuracy.f1Up,
                                  color: .green)
                classMetricColumn("Падение",
                                  precision: engine.accuracy.precisionDown,
                                  recall: engine.accuracy.recallDown,
                                  f1: engine.accuracy.f1Down,
                                  color: .red)
                classMetricColumn("Флэт",
                                  precision: engine.accuracy.precisionFlat,
                                  recall: engine.accuracy.recallFlat,
                                  f1: engine.accuracy.f1Flat,
                                  color: .gray)
                
                Divider()
                
                VStack(alignment: .leading, spacing: 4) {
                    Text("Калибровка")
                        .font(.system(size: 9, design: .monospaced))
                        .foregroundColor(.secondary)
                    Text(String(format: "%.3f", engine.accuracy.calibrationError))
                        .font(.system(.caption, design: .monospaced))
                        .foregroundColor(engine.accuracy.calibrationError < 0.05 ? .green : .orange)
                    
                    Text("Общая точн.")
                        .font(.system(size: 9, design: .monospaced))
                        .foregroundColor(.secondary)
                    Text(String(format: "%.1f%%", engine.accuracy.accuracy * 100))
                        .font(.system(.caption, design: .monospaced))
                        .fontWeight(.bold)
                        .foregroundColor(accuracyColor)
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(Color(.controlBackgroundColor).opacity(0.6))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
    
    private func classMetricColumn(_ label: String, precision: Double, recall: Double, f1: Double, color: Color) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label)
                .font(.system(size: 10, design: .monospaced))
                .fontWeight(.bold)
                .foregroundColor(color)
            HStack(spacing: 4) {
                Text("P")
                    .font(.system(size: 8, design: .monospaced))
                    .foregroundColor(.secondary)
                Text(String(format: "%.0f%%", precision * 100))
                    .font(.system(size: 10, design: .monospaced))
            }
            HStack(spacing: 4) {
                Text("R")
                    .font(.system(size: 8, design: .monospaced))
                    .foregroundColor(.secondary)
                Text(String(format: "%.0f%%", recall * 100))
                    .font(.system(size: 10, design: .monospaced))
            }
            HStack(spacing: 4) {
                Text("F1")
                    .font(.system(size: 8, design: .monospaced))
                    .foregroundColor(.secondary)
                Text(String(format: "%.2f", f1))
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundColor(f1 > 0.5 ? .green : (f1 > 0.3 ? .yellow : .red))
            }
        }
    }
    
    // MARK: - Horizon Accuracy Card
    
    private var horizonAccuracyCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("ТОЧНОСТЬ ПО ГОРИЗОНТАМ")
                .font(.system(.caption2, design: .monospaced))
                .foregroundColor(.secondary)
            
            HStack(spacing: 16) {
                horizonAccBar("100мс", value: engine.accuracy.horizonAccuracy100ms)
                horizonAccBar("500мс", value: engine.accuracy.horizonAccuracy500ms)
                horizonAccBar("1с", value: engine.accuracy.horizonAccuracy1s)
                horizonAccBar("3с", value: engine.accuracy.horizonAccuracy3s)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(Color(.controlBackgroundColor).opacity(0.6))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
    
    private func horizonAccBar(_ label: String, value: Double) -> some View {
        VStack(spacing: 4) {
            Text(label)
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(.secondary)
            
            ZStack(alignment: .bottom) {
                Rectangle()
                    .fill(Color(.controlBackgroundColor).opacity(0.5))
                    .frame(width: 30, height: 40)
                Rectangle()
                    .fill(value > 0.6 ? Color.green.opacity(0.7) : (value > 0.4 ? Color.yellow.opacity(0.7) : Color.red.opacity(0.7)))
                    .frame(width: 30, height: CGFloat(max(value, 0)) * 40)
            }
            .clipShape(RoundedRectangle(cornerRadius: 3))
            
            Text(String(format: "%.0f%%", value * 100))
                .font(.system(size: 9, design: .monospaced))
                .foregroundColor(.secondary)
        }
        .frame(maxWidth: .infinity)
    }
    
    // MARK: - Drawdown Chart Card
    
    private var drawdownChartCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("ПРОСАДКА")
                    .font(.system(.caption2, design: .monospaced))
                    .foregroundColor(.secondary)
                Spacer()
                Text(String(format: "%.2f%%", engine.circuitBreaker.drawdownPct))
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(engine.circuitBreaker.drawdownPct > 3 ? .red : .orange)
            }
            
            if engine.drawdownHistory.count > 1 {
                PnLChartShape(data: engine.drawdownHistory)
                    .stroke(Color.orange, lineWidth: 1.5)
                    .frame(height: 80)
                    .background(
                        PnLChartShape(data: engine.drawdownHistory)
                            .fill(
                                LinearGradient(
                                    colors: [Color.orange.opacity(0.2), Color.clear],
                                    startPoint: .top,
                                    endPoint: .bottom
                                )
                            )
                    )
            } else {
                Rectangle()
                    .fill(Color(.controlBackgroundColor).opacity(0.3))
                    .frame(height: 80)
                    .overlay(
                        Text("Ожидание данных...")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    )
            }
        }
        .padding(12)
        .background(Color(.controlBackgroundColor).opacity(0.6))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
    
    // MARK: - Accuracy Chart Card
    
    private var accuracyChartCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("ТОЧНОСТЬ (ROLLING)")
                    .font(.system(.caption2, design: .monospaced))
                    .foregroundColor(.secondary)
                Spacer()
                Text(String(format: "%.1f%%", engine.accuracy.rollingAccuracy * 100))
                    .font(.system(.caption, design: .monospaced))
                    .foregroundColor(accuracyColor)
            }
            
            if engine.accuracyHistory.count > 1 {
                ZStack {
                    // 50% baseline
                    GeometryReader { geo in
                        let minVal = engine.accuracyHistory.min() ?? 0
                        let maxVal = engine.accuracyHistory.max() ?? 1
                        let range = max(maxVal - minVal, 0.0001)
                        let y = geo.size.height * (1.0 - CGFloat((0.5 - minVal) / range))
                        Path { path in
                            path.move(to: CGPoint(x: 0, y: y))
                            path.addLine(to: CGPoint(x: geo.size.width, y: y))
                        }
                        .stroke(Color.gray.opacity(0.3), style: StrokeStyle(lineWidth: 1, dash: [4, 4]))
                    }
                    
                    PnLChartShape(data: engine.accuracyHistory)
                        .stroke(Color.cyan, lineWidth: 1.5)
                        .background(
                            PnLChartShape(data: engine.accuracyHistory)
                                .fill(
                                    LinearGradient(
                                        colors: [Color.cyan.opacity(0.2), Color.clear],
                                        startPoint: .top,
                                        endPoint: .bottom
                                    )
                                )
                        )
                }
                .frame(height: 80)
            } else {
                Rectangle()
                    .fill(Color(.controlBackgroundColor).opacity(0.3))
                    .frame(height: 80)
                    .overlay(
                        Text("Ожидание данных...")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    )
            }
        }
        .padding(12)
        .background(Color(.controlBackgroundColor).opacity(0.6))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
    
    // MARK: - Colors
    
    private var regimeColor: Color {
        switch engine.regime.current {
        case 0: return .green
        case 1: return .red
        case 2: return .blue
        case 3: return .purple
        case 4: return .orange
        default: return .gray
        }
    }
    
    private var accuracyColor: Color {
        let acc = engine.accuracy.rollingAccuracy
        if acc > 0.6 { return .green }
        if acc > 0.45 { return .yellow }
        return .red
    }
}

// MARK: - Metric Card

struct MetricCard: View {
    let title: String
    let value: String
    var subtitle: String? = nil
    let color: Color
    
    var body: some View {
        VStack(spacing: 6) {
            Text(title)
                .font(.system(.caption, design: .monospaced))
                .foregroundColor(.secondary)
            Text(value)
                .font(.system(.body, design: .monospaced))
                .fontWeight(.semibold)
                .foregroundColor(color)
            if let subtitle = subtitle {
                Text(subtitle)
                    .font(.system(.caption2, design: .monospaced))
                    .foregroundColor(.secondary)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(12)
        .background(Color(nsColor: .controlBackgroundColor))
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }
}

// MARK: - PnL Chart Shape

struct PnLChartShape: Shape {
    let data: [Double]
    
    func path(in rect: CGRect) -> Path {
        guard data.count > 1 else { return Path() }
        
        let minVal = data.min() ?? 0
        let maxVal = data.max() ?? 1
        let range = max(maxVal - minVal, 0.0001)
        
        var path = Path()
        for (i, val) in data.enumerated() {
            let x = rect.width * CGFloat(i) / CGFloat(data.count - 1)
            let y = rect.height * (1.0 - CGFloat((val - minVal) / range))
            if i == 0 {
                path.move(to: CGPoint(x: x, y: y))
            } else {
                path.addLine(to: CGPoint(x: x, y: y))
            }
        }
        return path
    }
}
