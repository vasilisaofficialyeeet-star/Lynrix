// NotificationManager.swift — macOS notification delivery for Lynrix v2.5

import Foundation
import UserNotifications
import Combine

final class NotificationManager: ObservableObject {
    static let shared = NotificationManager()
    
    private static let enabledKey = "lynrix.notificationsEnabled"
    private static let severityKey = "lynrix.notificationMinSeverity"
    
    @Published var isEnabled: Bool {
        didSet { UserDefaults.standard.set(isEnabled, forKey: Self.enabledKey) }
    }
    
    @Published var minSeverity: MinSeverity {
        didSet { UserDefaults.standard.set(minSeverity.rawValue, forKey: Self.severityKey) }
    }
    
    @Published var isAuthorized: Bool = false
    
    private var incidentCancellable: AnyCancellable?
    private let throttleInterval: TimeInterval = 5.0
    private var lastNotificationTime: Date = .distantPast
    
    enum MinSeverity: String, CaseIterable, Identifiable {
        case critical = "critical"
        case warning = "warning"
        case info = "info"
        
        var id: String { rawValue }
        
        var locKey: String {
            switch self {
            case .critical: return "notify.critical"
            case .warning:  return "notify.warning"
            case .info:     return "notify.info"
            }
        }
        
        func passes(_ severity: IncidentSeverity) -> Bool {
            switch self {
            case .critical: return severity == .critical
            case .warning:  return severity == .critical || severity == .warning
            case .info:     return true
            }
        }
    }
    
    private init() {
        self.isEnabled = UserDefaults.standard.object(forKey: Self.enabledKey) as? Bool ?? true
        let stored = UserDefaults.standard.string(forKey: Self.severityKey) ?? "warning"
        self.minSeverity = MinSeverity(rawValue: stored) ?? .warning
        
        checkAuthorization()
        observeIncidents()
    }
    
    // MARK: - Authorization
    
    func requestAuthorization() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound, .badge]) { [weak self] granted, _ in
            DispatchQueue.main.async {
                self?.isAuthorized = granted
            }
        }
    }
    
    private func checkAuthorization() {
        UNUserNotificationCenter.current().getNotificationSettings { [weak self] settings in
            DispatchQueue.main.async {
                self?.isAuthorized = settings.authorizationStatus == .authorized
                if settings.authorizationStatus == .notDetermined {
                    self?.requestAuthorization()
                }
            }
        }
    }
    
    // MARK: - Incident Observation
    
    private func observeIncidents() {
        incidentCancellable = IncidentStore.shared.$incidents
            .dropFirst()
            .compactMap { $0.first }
            .sink { [weak self] incident in
                self?.handleIncident(incident)
            }
    }
    
    private func handleIncident(_ incident: IncidentEntry) {
        guard isEnabled, isAuthorized else { return }
        guard minSeverity.passes(incident.severity) else { return }
        
        let now = Date()
        guard now.timeIntervalSince(lastNotificationTime) >= throttleInterval else { return }
        lastNotificationTime = now
        
        deliverNotification(
            title: notificationTitle(for: incident.severity),
            body: LocalizationManager.shared.t(incident.titleKey),
            category: incident.category.rawValue,
            severity: incident.severity
        )
    }
    
    // MARK: - Delivery
    
    private func deliverNotification(title: String, body: String, category: String, severity: IncidentSeverity) {
        let content = UNMutableNotificationContent()
        content.title = "Lynrix"
        content.subtitle = title
        content.body = body
        content.sound = severity == .critical ? .defaultCritical : .default
        content.categoryIdentifier = "lynrix.\(category)"
        
        let request = UNNotificationRequest(
            identifier: "lynrix.\(UUID().uuidString)",
            content: content,
            trigger: nil
        )
        
        UNUserNotificationCenter.current().add(request)
    }
    
    private func notificationTitle(for severity: IncidentSeverity) -> String {
        let loc = LocalizationManager.shared
        switch severity {
        case .critical: return loc.t("notify.titleCritical")
        case .warning:  return loc.t("notify.titleWarning")
        case .info:     return loc.t("notify.titleInfo")
        }
    }
    
    // MARK: - Manual Notify
    
    func notify(title: String, body: String, critical: Bool = false) {
        guard isEnabled, isAuthorized else { return }
        let content = UNMutableNotificationContent()
        content.title = "Lynrix"
        content.subtitle = title
        content.body = body
        content.sound = critical ? .defaultCritical : .default
        
        let request = UNNotificationRequest(
            identifier: "lynrix.\(UUID().uuidString)",
            content: content,
            trigger: nil
        )
        UNUserNotificationCenter.current().add(request)
    }
    
    // MARK: - D3: Trading Event Notifications
    
    private var lastLargeLossNotify: Date = .distantPast
    private var lastCBNotify: Date = .distantPast
    private var lastDisconnectNotify: Date = .distantPast
    private var lastFillNotify: Date = .distantPast
    private let eventThrottle: TimeInterval = 30.0
    
    /// D3: Notify on large unrealized loss exceeding threshold
    func notifyLargeLoss(pnl: Double, threshold: Double) {
        guard isEnabled, isAuthorized else { return }
        guard minSeverity.passes(.warning) else { return }
        let now = Date()
        guard now.timeIntervalSince(lastLargeLossNotify) >= eventThrottle else { return }
        guard pnl < -abs(threshold) else { return }
        lastLargeLossNotify = now
        deliverNotification(
            title: LocalizationManager.shared.t("notify.titleWarning"),
            body: String(format: "%@ $%.2f", LocalizationManager.shared.t("notify.largeLoss"), pnl),
            category: "trading.loss",
            severity: .warning
        )
    }
    
    /// D3: Notify when circuit breaker trips
    func notifyCircuitBreakerTripped() {
        guard isEnabled, isAuthorized else { return }
        guard minSeverity.passes(.critical) else { return }
        let now = Date()
        guard now.timeIntervalSince(lastCBNotify) >= eventThrottle else { return }
        lastCBNotify = now
        deliverNotification(
            title: LocalizationManager.shared.t("notify.titleCritical"),
            body: LocalizationManager.shared.t("notify.cbTripped"),
            category: "trading.circuitBreaker",
            severity: .critical
        )
    }
    
    /// D3: Notify on WS disconnect while holding a position
    func notifyDisconnectWithPosition(positionSize: Double) {
        guard isEnabled, isAuthorized else { return }
        guard minSeverity.passes(.critical) else { return }
        guard abs(positionSize) > 1e-12 else { return }
        let now = Date()
        guard now.timeIntervalSince(lastDisconnectNotify) >= eventThrottle else { return }
        lastDisconnectNotify = now
        deliverNotification(
            title: LocalizationManager.shared.t("notify.titleCritical"),
            body: String(format: "%@ (%.4f)", LocalizationManager.shared.t("notify.disconnectPosition"), positionSize),
            category: "trading.disconnect",
            severity: .critical
        )
    }
    
    /// D3: Notify on order fill
    func notifyFill(side: String, qty: Double, price: Double) {
        guard isEnabled, isAuthorized else { return }
        guard minSeverity.passes(.info) else { return }
        let now = Date()
        guard now.timeIntervalSince(lastFillNotify) >= eventThrottle else { return }
        lastFillNotify = now
        deliverNotification(
            title: LocalizationManager.shared.t("notify.titleInfo"),
            body: String(format: "%@ %@ %.4f @ %.2f", LocalizationManager.shared.t("notify.orderFilled"), side, qty, price),
            category: "trading.fill",
            severity: .info
        )
    }
}
