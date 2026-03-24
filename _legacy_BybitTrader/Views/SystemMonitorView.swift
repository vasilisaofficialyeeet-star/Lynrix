// SystemMonitorView.swift — System monitoring tab: CPU, memory, latency, throughput

import SwiftUI
import Charts

struct SystemMonitorView: View {
    @EnvironmentObject var engine: TradingEngine
    
    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                HStack(spacing: 16) {
                    resourcesCard
                    throughputCard
                }
                
                HStack(spacing: 16) {
                    latencyCard
                    networkCard
                }
                
                inferenceCard
            }
            .padding()
        }
        .background(Color(.windowBackgroundColor))
    }
    
    // MARK: - Resources
    
    private var resourcesCard: some View {
        let sys = engine.systemMonitor
        return VStack(alignment: .leading, spacing: 8) {
            Label("Ресурсы", systemImage: "cpu")
                .font(.headline)
            
            Divider()
            
            metricRow("CPU", value: String(format: "%.1f%%", sys.cpuUsagePct),
                       color: sys.cpuUsagePct > 80 ? .red : (sys.cpuUsagePct > 50 ? .orange : .green))
            metricRow("CPU Cores", value: "\(sys.cpuCores)")
            metricRow("Memory", value: String(format: "%.1f MB", sys.memoryUsedMb),
                       color: sys.memoryUsedMb > 500 ? .orange : .primary)
            metricRow("Peak Memory", value: String(format: "%.1f MB", Double(sys.memoryPeakBytes) / 1_048_576))
            metricRow("Uptime", value: formatUptime(sys.uptimeHours))
            
            if sys.gpuAvailable {
                Divider()
                metricRow("GPU", value: sys.gpuName)
                metricRow("GPU Usage", value: String(format: "%.1f%%", sys.gpuUsagePct))
            }
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Throughput
    
    private var throughputCard: some View {
        let sys = engine.systemMonitor
        return VStack(alignment: .leading, spacing: 8) {
            Label("Пропускная способность", systemImage: "speedometer")
                .font(.headline)
            
            Divider()
            
            metricRow("Ticks/sec", value: String(format: "%.0f", sys.ticksPerSec),
                       color: sys.ticksPerSec > 50 ? .green : .orange)
            metricRow("Signals/sec", value: String(format: "%.1f", sys.signalsPerSec))
            metricRow("Orders/sec", value: String(format: "%.1f", sys.ordersPerSec))
            
            Divider()
            
            metricRow("OB Updates", value: formatCount(engine.metrics.obUpdates))
            metricRow("Trades", value: formatCount(engine.metrics.tradesTotal))
            metricRow("Signals", value: formatCount(engine.metrics.signalsTotal))
            metricRow("Orders Sent", value: formatCount(engine.metrics.ordersSent))
            metricRow("Orders Filled", value: formatCount(engine.metrics.ordersFilled))
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Latency
    
    private var latencyCard: some View {
        let sys = engine.systemMonitor
        return VStack(alignment: .leading, spacing: 8) {
            Label("Латентность (μs)", systemImage: "clock.fill")
                .font(.headline)
            
            Divider()
            
            latencyRow("WebSocket", p50: sys.wsLatencyP50Us, p99: sys.wsLatencyP99Us)
            latencyRow("Features", p50: sys.featLatencyP50Us, p99: sys.featLatencyP99Us)
            latencyRow("Model", p50: sys.modelLatencyP50Us, p99: sys.modelLatencyP99Us)
            latencyRow("End-to-End", p50: sys.e2eLatencyP50Us, p99: sys.e2eLatencyP99Us)
            
            Divider()
            
            metricRow("Exchange RTT", value: String(format: "%.1f ms", sys.exchangeLatencyMs),
                       color: sys.exchangeLatencyMs > 100 ? .red : (sys.exchangeLatencyMs > 50 ? .orange : .green))
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Network
    
    private var networkCard: some View {
        let met = engine.metrics
        return VStack(alignment: .leading, spacing: 8) {
            Label("Сеть", systemImage: "network")
                .font(.headline)
            
            Divider()
            
            metricRow("WS Reconnects", value: "\(met.wsReconnects)",
                       color: met.wsReconnects > 0 ? .orange : .green)
            metricRow("Orders Cancelled", value: formatCount(met.ordersCancelled))
            
            Divider()
            
            // Drawdown chart
            if #available(macOS 14.0, *), !engine.drawdownHistory.isEmpty {
                Text("Drawdown History")
                    .font(.caption)
                    .foregroundColor(.secondary)
                Chart {
                    ForEach(Array(engine.drawdownHistory.enumerated()), id: \.offset) { idx, val in
                        AreaMark(
                            x: .value("T", idx),
                            y: .value("DD", val * 100)
                        )
                        .foregroundStyle(Color.red.opacity(0.3))
                    }
                }
                .frame(height: 60)
                .chartYAxis {
                    AxisMarks(position: .leading)
                }
            }
        }
        .padding()
        .background(Color(.controlBackgroundColor))
        .cornerRadius(8)
    }
    
    // MARK: - Inference
    
    private var inferenceCard: some View {
        let sys = engine.systemMonitor
        return VStack(alignment: .leading, spacing: 8) {
            HStack {
                Label("ML Inference", systemImage: "brain.head.profile")
                    .font(.headline)
                Spacer()
                Text(sys.inferenceBackend)
                    .font(.caption.bold())
                    .padding(.horizontal, 8)
                    .padding(.vertical, 2)
                    .background(sys.inferenceBackend == "CPU" ? Color.blue.opacity(0.2) : Color.green.opacity(0.2))
                    .cornerRadius(4)
                
                if engine.accuracy.usingOnnx {
                    Text("ONNX")
                        .font(.caption2)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(Color.purple.opacity(0.2))
                        .cornerRadius(4)
                }
            }
            
            Divider()
            
            HStack(spacing: 24) {
                VStack(alignment: .leading, spacing: 4) {
                    metricRow("Inference p50", value: String(format: "%.0f μs", sys.modelLatencyP50Us))
                    metricRow("Inference p99", value: String(format: "%.0f μs", sys.modelLatencyP99Us),
                               color: sys.modelLatencyP99Us > 1000 ? .orange : .primary)
                }
                VStack(alignment: .leading, spacing: 4) {
                    metricRow("GPU Available", value: sys.gpuAvailable ? "Yes" : "No",
                               color: sys.gpuAvailable ? .green : .secondary)
                    metricRow("GPU Name", value: sys.gpuName)
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
    
    private func latencyRow(_ label: String, p50: Double, p99: Double) -> some View {
        HStack {
            Text(label)
                .font(.caption)
                .foregroundColor(.secondary)
                .frame(width: 80, alignment: .leading)
            Spacer()
            Text("p50: \(String(format: "%.0f", p50))")
                .font(.caption.monospacedDigit())
            Text("p99: \(String(format: "%.0f", p99))")
                .font(.caption.monospacedDigit())
                .foregroundColor(p99 > 1000 ? .orange : .primary)
        }
    }
    
    private func formatUptime(_ hours: Double) -> String {
        if hours < 1 {
            return String(format: "%.0f min", hours * 60)
        } else if hours < 24 {
            return String(format: "%.1f h", hours)
        }
        return String(format: "%.1f d", hours / 24)
    }
    
    private func formatCount(_ count: UInt64) -> String {
        if count >= 1_000_000 {
            return String(format: "%.1fM", Double(count) / 1_000_000)
        } else if count >= 1_000 {
            return String(format: "%.1fK", Double(count) / 1_000)
        }
        return "\(count)"
    }
}
