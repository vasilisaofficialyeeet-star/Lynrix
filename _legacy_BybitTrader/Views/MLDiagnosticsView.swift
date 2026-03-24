// MLDiagnosticsView.swift — ML diagnostics tab: accuracy, feature importance, predictions

import SwiftUI
import Charts

struct MLDiagnosticsView: View {
    @EnvironmentObject var engine: TradingEngine
    
    private let featureNames = [
        "imbalance_1", "imbalance_5", "imbalance_20", "ob_slope",
        "depth_conc", "cancel_spike", "liq_wall",
        "aggr_ratio", "avg_trade_sz", "trade_vel", "trade_accel", "vol_accel",
        "microprice", "spread_bps", "spread_chg", "mid_momentum", "volatility",
        "mp_dev", "st_pressure", "bid_depth", "ask_depth",
        "d_imb_dt", "d2_imb_dt2", "d_vol_dt", "d_mom_dt"
    ]
    
    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                HStack(spacing: 16) {
                    accuracyOverviewCard
                    perClassCard
                }
                
                HStack(spacing: 16) {
                    horizonAccuracyCard
                    accuracyTrendCard
                }
                
                featureImportanceCard
            }
            .padding()
        }
        .background(Color(.windowBackgroundColor))
    }
    
    // MARK: - Accuracy Overview
    
    private var accuracyOverviewCard: some View {
        let acc = engine.accuracy
        return VStack(alignment: .leading, spacing: 8) {
            Label("Точность модели", systemImage: "target")
                .font(.headline)
            
            Divider()
            
            metricRow("Overall Accuracy", value: String(format: "%.1f%%", acc.accuracy * 100),
                       color: acc.accuracy > 0.4 ? .green : (acc.accuracy < 0.33 ? .red : .primary))
            metricRow("Rolling Accuracy", value: String(format: "%.1f%%", acc.rollingAccuracy * 100),
                       color: acc.rollingAccuracy > 0.4 ? .green : .primary)
            metricRow("Total Predictions", value: "\(acc.totalPredictions)")
            metricRow("Correct", value: "\(acc.correctPredictions)", color: .green)
            metricRow("Calibration Error", value: String(format: "%.4f", acc.calibrationError),
                       color: acc.calibrationError > 0.1 ? .orange : .primary)
            metricRow("Rolling Window", value: "\(acc.rollingWindow)")
            
            Divider()
            
            metricRow("Backend", value: acc.usingOnnx ? "ONNX Runtime" : "Native GRU")
            metricRow("Inference", value: String(format: "%.0f μs", engine.prediction.inferenceLatencyUs))
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Per-Class Metrics
    
    private var perClassCard: some View {
        let acc = engine.accuracy
        return VStack(alignment: .leading, spacing: 8) {
            Label("Per-Class Metrics", systemImage: "chart.bar.doc.horizontal")
                .font(.headline)
            
            Divider()
            
            HStack {
                Text("")
                    .frame(width: 60)
                Text("Precision")
                    .font(.caption2)
                    .frame(width: 60)
                Text("Recall")
                    .font(.caption2)
                    .frame(width: 60)
                Text("F1")
                    .font(.caption2)
                    .frame(width: 60)
            }
            .foregroundColor(.secondary)
            
            classRow("UP ↑", precision: acc.precisionUp, recall: acc.recallUp, f1: acc.f1Up, color: .green)
            classRow("DOWN ↓", precision: acc.precisionDown, recall: acc.recallDown, f1: acc.f1Down, color: .red)
            classRow("FLAT →", precision: acc.precisionFlat, recall: acc.recallFlat, f1: acc.f1Flat, color: .blue)
            
            Divider()
            
            // Prediction distribution
            let pred = engine.prediction
            HStack(spacing: 4) {
                Text("Current:")
                    .font(.caption)
                    .foregroundColor(.secondary)
                predBar("↑", value: pred.probUp, color: .green)
                predBar("→", value: 1 - pred.probUp - pred.probDown, color: .blue)
                predBar("↓", value: pred.probDown, color: .red)
            }
            
            metricRow("Confidence", value: String(format: "%.1f%%", pred.modelConfidence * 100),
                       color: pred.modelConfidence > 0.7 ? .green : .secondary)
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Horizon Accuracy
    
    private var horizonAccuracyCard: some View {
        let acc = engine.accuracy
        return VStack(alignment: .leading, spacing: 8) {
            Label("Точность по горизонтам", systemImage: "clock.arrow.2.circlepath")
                .font(.headline)
            
            Divider()
            
            horizonBar("100ms", accuracy: acc.horizonAccuracy100ms)
            horizonBar("500ms", accuracy: acc.horizonAccuracy500ms)
            horizonBar("1s", accuracy: acc.horizonAccuracy1s)
            horizonBar("3s", accuracy: acc.horizonAccuracy3s)
            
            Divider()
            
            // Multi-horizon predictions
            let pred = engine.prediction
            VStack(alignment: .leading, spacing: 4) {
                Text("Multi-Horizon Probabilities")
                    .font(.caption)
                    .foregroundColor(.secondary)
                horizonProbRow("100ms", up: pred.h100ms.up, down: pred.h100ms.down)
                horizonProbRow("500ms", up: pred.h500ms.up, down: pred.h500ms.down)
                horizonProbRow("1s", up: pred.h1s.up, down: pred.h1s.down)
                horizonProbRow("3s", up: pred.h3s.up, down: pred.h3s.down)
            }
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Accuracy Trend
    
    private var accuracyTrendCard: some View {
        VStack(alignment: .leading, spacing: 8) {
            Label("Тренд точности", systemImage: "chart.line.uptrend.xyaxis")
                .font(.headline)
            
            if #available(macOS 14.0, *), !engine.accuracyHistory.isEmpty {
                Chart {
                    ForEach(Array(engine.accuracyHistory.enumerated()), id: \.offset) { idx, val in
                        LineMark(
                            x: .value("T", idx),
                            y: .value("Acc", val * 100)
                        )
                        .foregroundStyle(Color.blue)
                    }
                    
                    RuleMark(y: .value("Random", 33.3))
                        .foregroundStyle(Color.red.opacity(0.5))
                        .lineStyle(StrokeStyle(dash: [4, 4]))
                }
                .frame(height: 140)
                .chartYAxis {
                    AxisMarks(position: .leading)
                }
                .chartYScale(domain: 0...100)
            } else {
                Text("Нет данных")
                    .foregroundColor(.secondary)
                    .frame(height: 100)
                    .frame(maxWidth: .infinity)
            }
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Feature Importance
    
    private var featureImportanceCard: some View {
        let fi = engine.featureImportance
        return VStack(alignment: .leading, spacing: 8) {
            HStack {
                Label("Feature Importance", systemImage: "chart.bar.fill")
                    .font(.headline)
                Spacer()
                Text("\(fi.activeFeatures) significant")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            
            Divider()
            
            // Top 10 features by ranking
            let topFeatures = Array(fi.ranking.prefix(10))
            
            if #available(macOS 14.0, *) {
                Chart {
                    ForEach(topFeatures.prefix(10), id: \.self) { idx in
                        let name = idx < featureNames.count ? featureNames[idx] : "f\(idx)"
                        let importance = idx < fi.mutualInformation.count ? fi.mutualInformation[idx] : 0
                        BarMark(
                            x: .value("MI", importance),
                            y: .value("Feature", name)
                        )
                        .foregroundStyle(Color.blue.gradient)
                    }
                }
                .frame(height: 200)
                .chartXAxisLabel("Mutual Information")
            } else {
                ForEach(topFeatures.prefix(10), id: \.self) { idx in
                    let name = idx < featureNames.count ? featureNames[idx] : "f\(idx)"
                    let corr = idx < fi.correlation.count ? fi.correlation[idx] : 0
                    let mi = idx < fi.mutualInformation.count ? fi.mutualInformation[idx] : 0
                    HStack {
                        Text(name)
                            .font(.caption.monospacedDigit())
                            .frame(width: 100, alignment: .leading)
                        Text(String(format: "MI=%.4f", mi))
                            .font(.caption2.monospacedDigit())
                        Text(String(format: "r=%.3f", corr))
                            .font(.caption2.monospacedDigit())
                            .foregroundColor(corr > 0 ? .green : .red)
                    }
                }
            }
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Helpers
    
    private func metricRow(_ label: String, value: String, color: Color = .primary) -> some View {
        HStack {
            Text(label)
                .font(.caption)
                .foregroundColor(.secondary)
            Spacer()
            Text(value)
                .font(.caption.monospacedDigit())
                .foregroundColor(color)
        }
    }
    
    private func classRow(_ label: String, precision: Double, recall: Double, f1: Double, color: Color) -> some View {
        HStack {
            Text(label)
                .font(.caption.bold())
                .foregroundColor(color)
                .frame(width: 60, alignment: .leading)
            Text(String(format: "%.1f%%", precision * 100))
                .font(.caption.monospacedDigit())
                .frame(width: 60)
            Text(String(format: "%.1f%%", recall * 100))
                .font(.caption.monospacedDigit())
                .frame(width: 60)
            Text(String(format: "%.3f", f1))
                .font(.caption.monospacedDigit())
                .frame(width: 60)
        }
    }
    
    private func predBar(_ label: String, value: Double, color: Color) -> some View {
        VStack(spacing: 2) {
            Text(label)
                .font(.caption2)
            Text(String(format: "%.0f%%", value * 100))
                .font(.caption2.monospacedDigit())
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 4)
        .background(color.opacity(max(value * 0.6, 0.1)))
        .cornerRadius(4)
    }
    
    private func horizonBar(_ label: String, accuracy: Double) -> some View {
        HStack(spacing: 6) {
            Text(label)
                .font(.caption.monospacedDigit())
                .frame(width: 50, alignment: .leading)
            
            GeometryReader { geo in
                ZStack(alignment: .leading) {
                    Rectangle()
                        .fill(Color.gray.opacity(0.2))
                        .frame(height: 10)
                        .cornerRadius(5)
                    Rectangle()
                        .fill(accuracy > 0.4 ? Color.green : (accuracy > 0.33 ? Color.yellow : Color.red))
                        .frame(width: max(geo.size.width * accuracy, 2), height: 10)
                        .cornerRadius(5)
                    
                    // Random baseline marker
                    Rectangle()
                        .fill(Color.red.opacity(0.7))
                        .frame(width: 1, height: 14)
                        .offset(x: geo.size.width * 0.333)
                }
            }
            .frame(height: 14)
            
            Text(String(format: "%.1f%%", accuracy * 100))
                .font(.caption2.monospacedDigit())
                .frame(width: 40, alignment: .trailing)
        }
    }
    
    private func horizonProbRow(_ label: String, up: Double, down: Double) -> some View {
        HStack(spacing: 4) {
            Text(label)
                .font(.caption2)
                .frame(width: 40, alignment: .leading)
            Text(String(format: "↑%.0f%%", up * 100))
                .font(.caption2.monospacedDigit())
                .foregroundColor(.green)
                .frame(width: 40)
            Text(String(format: "↓%.0f%%", down * 100))
                .font(.caption2.monospacedDigit())
                .foregroundColor(.red)
                .frame(width: 40)
            
            let dir = up > down + 0.05 ? "↑" : (down > up + 0.05 ? "↓" : "→")
            Text(dir)
                .font(.caption)
        }
    }
}
