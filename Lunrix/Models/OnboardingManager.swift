// OnboardingManager.swift — First-run onboarding state for Lynrix v2.5

import SwiftUI
import Combine

final class OnboardingManager: ObservableObject {
    static let shared = OnboardingManager()
    
    private static let completedKey = "lynrix.onboardingComplete"
    private static let versionKey = "lynrix.onboardingVersion"
    private static let currentOnboardingVersion = 2
    
    @Published var showOnboarding: Bool = false
    @Published var currentStep: OnboardingStep = .welcome
    
    var isComplete: Bool {
        UserDefaults.standard.bool(forKey: Self.completedKey) &&
        UserDefaults.standard.integer(forKey: Self.versionKey) >= Self.currentOnboardingVersion
    }
    
    private init() {
        if !isComplete {
            showOnboarding = true
        }
    }
    
    func completeOnboarding() {
        UserDefaults.standard.set(true, forKey: Self.completedKey)
        UserDefaults.standard.set(Self.currentOnboardingVersion, forKey: Self.versionKey)
        withAnimation(.easeInOut(duration: 0.3)) {
            showOnboarding = false
        }
    }
    
    func resetOnboarding() {
        UserDefaults.standard.removeObject(forKey: Self.completedKey)
        UserDefaults.standard.removeObject(forKey: Self.versionKey)
        currentStep = .welcome
        showOnboarding = true
    }
    
    // D2: Expanded from 4 → 6 steps for complete first-run guidance
    enum OnboardingStep: Int, CaseIterable {
        case welcome = 0
        case preferences = 1
        case apiKeys = 2
        case tradingMode = 3
        case safety = 4
        case ready = 5
        
        var next: OnboardingStep? {
            OnboardingStep(rawValue: rawValue + 1)
        }
        
        var previous: OnboardingStep? {
            OnboardingStep(rawValue: rawValue - 1)
        }
        
        var progress: Double {
            Double(rawValue + 1) / Double(OnboardingStep.allCases.count)
        }
        
        /// Whether this step can be skipped (non-essential for first run)
        var isSkippable: Bool {
            switch self {
            case .apiKeys, .tradingMode: return true
            default: return false
            }
        }
    }
}
