// ModelGovernance.swift — Model governance foundations (Lynrix v2.5 Sprint 5)
// Model identity, performance tracking, health/drift indicators, deployment status.
// All fields marked as measured (from C++ engine) or derived (computed in Swift).

import Foundation
import SwiftUI

// MARK: - Model Deployment Status

enum ModelDeploymentStatus: String, Equatable {
    case active      = "active"       // currently running inference
    case candidate   = "candidate"    // loaded but not primary
    case retired     = "retired"      // no longer in use
    case degraded    = "degraded"     // active but performance below threshold
    case rollback    = "rollback"     // rolled back from candidate

    var locKey: String {
        switch self {
        case .active:    return "model.status.active"
        case .candidate: return "model.status.candidate"
        case .retired:   return "model.status.retired"
        case .degraded:  return "model.status.degraded"
        case .rollback:  return "model.status.rollback"
        }
    }

    var color: Color {
        switch self {
        case .active:    return LxColor.neonLime
        case .candidate: return LxColor.electricCyan
        case .retired:   return LxColor.coolSteel
        case .degraded:  return LxColor.amber
        case .rollback:  return LxColor.bloodRed
        }
    }

    var icon: String {
        switch self {
        case .active:    return "checkmark.seal.fill"
        case .candidate: return "flask.fill"
        case .retired:   return "archivebox"
        case .degraded:  return "exclamationmark.triangle.fill"
        case .rollback:  return "arrow.uturn.backward.circle.fill"
        }
    }
}

// MARK: - Model Drift Level

enum ModelDriftLevel: Int, Equatable, Comparable {
    case none     = 0
    case low      = 1
    case moderate = 2
    case high     = 3
    case critical = 4

    var locKey: String {
        switch self {
        case .none:     return "model.drift.none"
        case .low:      return "model.drift.low"
        case .moderate: return "model.drift.moderate"
        case .high:     return "model.drift.high"
        case .critical: return "model.drift.critical"
        }
    }

    var color: Color {
        switch self {
        case .none:     return LxColor.neonLime
        case .low:      return LxColor.electricCyan
        case .moderate: return LxColor.gold
        case .high:     return LxColor.amber
        case .critical: return LxColor.bloodRed
        }
    }

    static func < (lhs: ModelDriftLevel, rhs: ModelDriftLevel) -> Bool {
        lhs.rawValue < rhs.rawValue
    }
}

// MARK: - Model Identity

struct ModelIdentity: Equatable {
    var name: String = "GRU Ensemble"         // derived: from config (ONNX vs native)
    var version: String = "v2.5.0"            // derived: from app version
    var backend: String = "CPU"               // measured: from systemMonitor.inferenceBackend
    var isOnnx: Bool = false                  // measured: from accuracy.usingOnnx
    var activeFeatures: Int = 0               // measured: from featureImportance.activeFeatures
    var horizons: Int = 4                     // derived: fixed architecture (100ms/500ms/1s/3s)
    var stateVectorDim: Int = 32              // derived: fixed architecture
    var deploymentStatus: ModelDeploymentStatus = .active  // derived: computed from health
}

// MARK: - Model Performance Summary

struct ModelPerformanceSummary: Equatable {
    // Measured (from AccuracySnapshot)
    var overallAccuracy: Double = 0
    var rollingAccuracy: Double = 0
    var calibrationError: Double = 0
    var horizonAccuracy100ms: Double = 0
    var horizonAccuracy500ms: Double = 0
    var horizonAccuracy1s: Double = 0
    var horizonAccuracy3s: Double = 0
    var totalPredictions: Int = 0
    var f1Up: Double = 0
    var f1Down: Double = 0
    var f1Flat: Double = 0

    // Measured (from PredictionSnapshot)
    var currentConfidence: Double = 0
    var inferenceLatencyUs: Double = 0

    // Measured (from SystemMonitorModel)
    var latencyP50Us: Double = 0
    var latencyP99Us: Double = 0

    // Derived
    var bestHorizon: String = "—"
    var bestHorizonAccuracy: Double = 0
    var avgF1: Double = 0
    var confidenceTrend: Double = 0           // derived: direction of recent confidence
}

// MARK: - Model Health Assessment

struct ModelHealthAssessment: Equatable {
    var overallScore: Double = 0              // derived: 0-100 composite
    var driftLevel: ModelDriftLevel = .none    // derived: from calibration + accuracy trends
    var accuracyStable: Bool = true           // derived: !accuracyDeclining
    var latencyHealthy: Bool = true           // derived: p99 < threshold
    var calibrationHealthy: Bool = true       // derived: calibrationError < threshold
    var confidenceHealthy: Bool = true        // derived: modelConfidence > threshold
    var rlStable: Bool = true                 // derived: rollbackCount < threshold

    // Component scores (0-100)
    var accuracyScore: Double = 0
    var calibrationScore: Double = 0
    var latencyScore: Double = 0
    var confidenceScore: Double = 0
    var stabilityScore: Double = 0
}

// MARK: - Model Governance Snapshot

struct ModelGovernanceSnapshot: Equatable {
    var identity: ModelIdentity = .init()
    var performance: ModelPerformanceSummary = .init()
    var health: ModelHealthAssessment = .init()
    var lastEvaluationTime: Date = .distantPast
    var uptimeHours: Double = 0
}

// MARK: - Model Governance Computer

enum ModelGovernanceComputer {

    static func compute(
        accuracy: AccuracySnapshot,
        prediction: PredictionSnapshot,
        strategyHealth: StrategyHealthModel,
        systemMonitor: SystemMonitorModel,
        featureImportance: FeatureImportanceModel,
        rlv2: RLv2StateModel,
        config: LynrixEngine.TradingConfig
    ) -> ModelGovernanceSnapshot {

        var snapshot = ModelGovernanceSnapshot()

        // Identity
        snapshot.identity.isOnnx = accuracy.usingOnnx
        snapshot.identity.name = accuracy.usingOnnx ? "ONNX GRU" : "Native GRU Ensemble"
        snapshot.identity.backend = systemMonitor.inferenceBackend
        snapshot.identity.activeFeatures = featureImportance.activeFeatures

        // Performance
        snapshot.performance.overallAccuracy = accuracy.accuracy
        snapshot.performance.rollingAccuracy = accuracy.rollingAccuracy
        snapshot.performance.calibrationError = accuracy.calibrationError
        snapshot.performance.horizonAccuracy100ms = accuracy.horizonAccuracy100ms
        snapshot.performance.horizonAccuracy500ms = accuracy.horizonAccuracy500ms
        snapshot.performance.horizonAccuracy1s = accuracy.horizonAccuracy1s
        snapshot.performance.horizonAccuracy3s = accuracy.horizonAccuracy3s
        snapshot.performance.totalPredictions = accuracy.totalPredictions
        snapshot.performance.f1Up = accuracy.f1Up
        snapshot.performance.f1Down = accuracy.f1Down
        snapshot.performance.f1Flat = accuracy.f1Flat
        snapshot.performance.currentConfidence = prediction.modelConfidence
        snapshot.performance.inferenceLatencyUs = prediction.inferenceLatencyUs
        snapshot.performance.latencyP50Us = systemMonitor.modelLatencyP50Us
        snapshot.performance.latencyP99Us = systemMonitor.modelLatencyP99Us

        // Best horizon
        let horizons: [(String, Double)] = [
            ("100ms", accuracy.horizonAccuracy100ms),
            ("500ms", accuracy.horizonAccuracy500ms),
            ("1s", accuracy.horizonAccuracy1s),
            ("3s", accuracy.horizonAccuracy3s)
        ]
        if let best = horizons.max(by: { $0.1 < $1.1 }) {
            snapshot.performance.bestHorizon = best.0
            snapshot.performance.bestHorizonAccuracy = best.1
        }

        snapshot.performance.avgF1 = (accuracy.f1Up + accuracy.f1Down + accuracy.f1Flat) / 3.0

        // Health assessment
        let accScore = min(100, max(0, accuracy.rollingAccuracy * 200))  // 0.5 → 100
        let calScore = max(0, 100 - accuracy.calibrationError * 1000)    // 0.1 → 0
        let latScore = max(0, 100 - systemMonitor.modelLatencyP99Us / 10) // 1000µs → 0
        let confScore = min(100, prediction.modelConfidence * 150)        // 0.67 → 100
        let stabScore: Double = rlv2.rollbackCount < 2 ? 100 : max(0, 100 - Double(rlv2.rollbackCount) * 20)

        snapshot.health.accuracyScore = accScore
        snapshot.health.calibrationScore = calScore
        snapshot.health.latencyScore = latScore
        snapshot.health.confidenceScore = confScore
        snapshot.health.stabilityScore = stabScore

        snapshot.health.overallScore = accScore * 0.30 + calScore * 0.20 + latScore * 0.15 +
                                       confScore * 0.20 + stabScore * 0.15

        snapshot.health.accuracyStable = !strategyHealth.accuracyDeclining
        snapshot.health.latencyHealthy = systemMonitor.modelLatencyP99Us < 500
        snapshot.health.calibrationHealthy = accuracy.calibrationError < 0.15
        snapshot.health.confidenceHealthy = prediction.modelConfidence > 0.3
        snapshot.health.rlStable = rlv2.rollbackCount < 3

        // Drift detection
        if accuracy.calibrationError > 0.25 || (strategyHealth.accuracyDeclining && accuracy.rollingAccuracy < 0.4) {
            snapshot.health.driftLevel = .critical
        } else if accuracy.calibrationError > 0.15 || strategyHealth.accuracyDeclining {
            snapshot.health.driftLevel = .high
        } else if accuracy.calibrationError > 0.08 {
            snapshot.health.driftLevel = .moderate
        } else if accuracy.calibrationError > 0.03 {
            snapshot.health.driftLevel = .low
        } else {
            snapshot.health.driftLevel = .none
        }

        // Deployment status
        if snapshot.health.overallScore < 30 {
            snapshot.identity.deploymentStatus = .degraded
        } else if rlv2.rollbackCount >= 3 {
            snapshot.identity.deploymentStatus = .rollback
        } else {
            snapshot.identity.deploymentStatus = .active
        }

        snapshot.uptimeHours = systemMonitor.uptimeHours
        snapshot.lastEvaluationTime = Date()

        return snapshot
    }
}
