// SettingsView.swift — Configuration panel with Keychain-backed API keys

import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var engine: TradingEngine
    
    @State private var apiKey: String = ""
    @State private var apiSecret: String = ""
    @State private var showSecret: Bool = false
    @State private var keySaved: Bool = false
    @State private var showDeleteConfirm: Bool = false
    @State private var showLiveConfirm: Bool = false
    
    // Local editable copies
    @State private var symbol: String = "BTCUSDT"
    @State private var orderQty: String = "0.001"
    @State private var signalThreshold: String = "0.6"
    @State private var entryOffsetBps: String = "1.0"
    @State private var maxPositionSize: String = "0.1"
    @State private var maxLeverage: String = "10.0"
    @State private var maxDailyLoss: String = "500.0"
    @State private var maxDrawdown: String = "0.1"
    @State private var maxOrdersPerSec: String = "5"
    @State private var obLevels: String = "500"
    @State private var ioThreads: String = "2"
    
    // AI Edition
    @State private var mlModelEnabled: Bool = true
    @State private var adaptiveThresholdEnabled: Bool = true
    @State private var regimeDetectionEnabled: Bool = true
    @State private var requoteEnabled: Bool = true
    @State private var adaptiveSizingEnabled: Bool = true
    @State private var cbEnabled: Bool = true
    @State private var cbLossThreshold: String = "200.0"
    @State private var cbDrawdownThreshold: String = "0.05"
    @State private var cbConsecutiveLosses: String = "10"
    @State private var cbCooldownSec: String = "300"
    @State private var featureTickMs: String = "10"
    @State private var onnxEnabled: Bool = false
    @State private var onnxModelPath: String = ""
    @State private var onnxIntraThreads: String = "4"
    
    var body: some View {
        ScrollView {
            VStack(spacing: 20) {
                // API Credentials
                GroupBox {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Image(systemName: "key.fill")
                                .foregroundColor(.yellow)
                            Text("API КЛЮЧИ")
                                .font(.system(.headline, design: .monospaced))
                            Spacer()
                            if KeychainManager.shared.hasCredentials {
                                Label("Сохранено в Keychain", systemImage: "lock.shield.fill")
                                    .font(.caption)
                                    .foregroundColor(.green)
                            }
                        }
                        
                        Text("API ключи хранятся безопасно в macOS Keychain. Никогда не сохраняются в файлах.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        
                        HStack(spacing: 12) {
                            VStack(alignment: .leading, spacing: 4) {
                                Text("API Ключ")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                                SecureField("Введите API ключ", text: $apiKey)
                                    .textFieldStyle(.roundedBorder)
                                    .font(.system(.body, design: .monospaced))
                            }
                            VStack(alignment: .leading, spacing: 4) {
                                Text("API Секрет")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                                if showSecret {
                                    TextField("Введите API секрет", text: $apiSecret)
                                        .textFieldStyle(.roundedBorder)
                                        .font(.system(.body, design: .monospaced))
                                } else {
                                    SecureField("Введите API секрет", text: $apiSecret)
                                        .textFieldStyle(.roundedBorder)
                                        .font(.system(.body, design: .monospaced))
                                }
                            }
                        }
                        
                        HStack {
                            Button(action: saveCredentials) {
                                Label("Сохранить в Keychain", systemImage: "lock.fill")
                            }
                            .buttonStyle(.borderedProminent)
                            .disabled(apiKey.isEmpty || apiSecret.isEmpty)
                            
                            Toggle("Показать секрет", isOn: $showSecret)
                                .toggleStyle(.switch)
                                .controlSize(.small)
                            
                            Spacer()
                            
                            if keySaved {
                                Label("Сохранено", systemImage: "checkmark.circle.fill")
                                    .foregroundColor(.green)
                                    .font(.caption)
                            }
                            
                            Button(role: .destructive, action: { showDeleteConfirm = true }) {
                                Label("Удалить ключи", systemImage: "trash")
                            }
                            .confirmationDialog("Удалить API ключи?", isPresented: $showDeleteConfirm) {
                                Button("Удалить", role: .destructive) {
                                    KeychainManager.shared.deleteCredentials()
                                    apiKey = ""
                                    apiSecret = ""
                                    keySaved = false
                                }
                            }
                        }
                    }
                }
                
                // Trading Mode
                GroupBox {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Image(systemName: "arrow.triangle.swap")
                                .foregroundColor(.blue)
                            Text("РЕЖИМ ТОРГОВЛИ")
                                .font(.system(.headline, design: .monospaced))
                        }
                        
                        HStack(spacing: 16) {
                            VStack(alignment: .leading) {
                                Text(engine.paperMode ? "Бумажная торговля" : "Реальная торговля")
                                    .font(.system(.title3, design: .monospaced))
                                    .fontWeight(.bold)
                                    .foregroundColor(engine.paperMode ? .blue : .red)
                                Text(engine.paperMode
                                     ? "Ордера симулируются. Реальные средства не задействованы."
                                     : "РЕАЛЬНЫЕ ордера будут размещены. Средства под риском!")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }
                            
                            Spacer()
                            
                            Button(action: {
                                if engine.paperMode {
                                    showLiveConfirm = true
                                } else {
                                    engine.togglePaperMode()
                                }
                            }) {
                                Text(engine.paperMode ? "Переключить на РЕАЛЬНУЮ" : "Переключить на БУМАЖНУЮ")
                                    .fontWeight(.medium)
                            }
                            .buttonStyle(.borderedProminent)
                            .tint(engine.paperMode ? .red : .blue)
                            .confirmationDialog("Переключить на реальную торговлю?", isPresented: $showLiveConfirm,
                                              titleVisibility: .visible) {
                                Button("Включить реальную торговлю", role: .destructive) {
                                    engine.togglePaperMode()
                                }
                            } message: {
                                Text("Реальные ордера будут размещены на Bybit. Убедитесь, что риск-лимиты настроены корректно.")
                            }
                        }
                    }
                }
                
                // Trading Parameters
                GroupBox {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Image(systemName: "slider.horizontal.3")
                                .foregroundColor(.purple)
                            Text("ПАРАМЕТРЫ ТОРГОВЛИ")
                                .font(.system(.headline, design: .monospaced))
                        }
                        
                        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                            SettingsField(label: "Символ", value: $symbol)
                            SettingsField(label: "Объём ордера", value: $orderQty)
                            SettingsField(label: "Порог сигнала", value: $signalThreshold)
                            SettingsField(label: "Смещение входа (bps)", value: $entryOffsetBps)
                            SettingsField(label: "Уровни стакана", value: $obLevels)
                            SettingsField(label: "IO потоки", value: $ioThreads)
                        }
                    }
                }
                
                // Risk Parameters
                GroupBox {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Image(systemName: "shield.lefthalf.filled")
                                .foregroundColor(.orange)
                            Text("РИСК-ЛИМИТЫ")
                                .font(.system(.headline, design: .monospaced))
                        }
                        
                        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                            SettingsField(label: "Макс. позиция", value: $maxPositionSize)
                            SettingsField(label: "Макс. плечо", value: $maxLeverage)
                            SettingsField(label: "Макс. дневной убыток ($)", value: $maxDailyLoss)
                            SettingsField(label: "Макс. просадка (%)", value: $maxDrawdown)
                            SettingsField(label: "Макс. ордеров/сек", value: $maxOrdersPerSec)
                        }
                    }
                }
                
                // AI Edition Settings
                GroupBox {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Image(systemName: "brain")
                                .foregroundColor(.cyan)
                            Text("AI EDITION")
                                .font(.system(.headline, design: .monospaced))
                        }
                        
                        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                            Toggle("ML модель", isOn: $mlModelEnabled)
                                .toggleStyle(.switch)
                            Toggle("Адаптивный порог", isOn: $adaptiveThresholdEnabled)
                                .toggleStyle(.switch)
                            Toggle("Детекция режима", isOn: $regimeDetectionEnabled)
                                .toggleStyle(.switch)
                            Toggle("Ре-котирование", isOn: $requoteEnabled)
                                .toggleStyle(.switch)
                            Toggle("Адаптивный размер", isOn: $adaptiveSizingEnabled)
                                .toggleStyle(.switch)
                            SettingsField(label: "Feature tick (мс)", value: $featureTickMs)
                        }
                        
                        Divider()
                        
                        HStack {
                            Image(systemName: "cpu")
                                .foregroundColor(.purple)
                            Text("ONNX Runtime")
                                .font(.system(.subheadline, design: .monospaced))
                                .fontWeight(.semibold)
                        }
                        
                        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                            Toggle("ONNX модель", isOn: $onnxEnabled)
                                .toggleStyle(.switch)
                            SettingsField(label: "Путь к .onnx", value: $onnxModelPath)
                            SettingsField(label: "ONNX потоки", value: $onnxIntraThreads)
                        }
                    }
                }
                
                // Circuit Breaker Settings
                GroupBox {
                    VStack(alignment: .leading, spacing: 12) {
                        HStack {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundColor(.red)
                            Text("CIRCUIT BREAKER")
                                .font(.system(.headline, design: .monospaced))
                            Spacer()
                            Toggle("Включён", isOn: $cbEnabled)
                                .toggleStyle(.switch)
                                .labelsHidden()
                        }
                        
                        if cbEnabled {
                            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
                                SettingsField(label: "Порог убытка ($)", value: $cbLossThreshold)
                                SettingsField(label: "Порог просадки", value: $cbDrawdownThreshold)
                                SettingsField(label: "Подряд убытков", value: $cbConsecutiveLosses)
                                SettingsField(label: "Кулдаун (сек)", value: $cbCooldownSec)
                            }
                        }
                    }
                }
                
                // Apply button
                HStack {
                    Spacer()
                    Button(action: applySettings) {
                        Label("Применить настройки", systemImage: "checkmark.circle")
                            .font(.body)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(engine.status.isActive)
                    
                    if engine.status.isActive {
                        Text("Остановите движок для изменения настроек")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }
                
                Spacer()
            }
            .padding(20)
        }
        .background(Color(.windowBackgroundColor))
        .onAppear { loadCurrentConfig() }
    }
    
    private func loadCurrentConfig() {
        let c = engine.config
        symbol = c.symbol
        orderQty = String(c.orderQty)
        signalThreshold = String(c.signalThreshold)
        entryOffsetBps = String(c.entryOffsetBps)
        maxPositionSize = String(c.maxPositionSize)
        maxLeverage = String(c.maxLeverage)
        maxDailyLoss = String(c.maxDailyLoss)
        maxDrawdown = String(c.maxDrawdown)
        maxOrdersPerSec = String(c.maxOrdersPerSec)
        obLevels = String(c.obLevels)
        ioThreads = String(c.ioThreads)
        
        // AI Edition
        mlModelEnabled = c.mlModelEnabled
        adaptiveThresholdEnabled = c.adaptiveThresholdEnabled
        regimeDetectionEnabled = c.regimeDetectionEnabled
        requoteEnabled = c.requoteEnabled
        adaptiveSizingEnabled = c.adaptiveSizingEnabled
        cbEnabled = c.cbEnabled
        cbLossThreshold = String(c.cbLossThreshold)
        cbDrawdownThreshold = String(c.cbDrawdownThreshold)
        cbConsecutiveLosses = String(c.cbConsecutiveLosses)
        cbCooldownSec = String(c.cbCooldownSec)
        featureTickMs = String(c.featureTickMs)
        onnxEnabled = c.onnxEnabled
        onnxModelPath = c.onnxModelPath
        onnxIntraThreads = String(c.onnxIntraThreads)
        
        // Load saved keys (masked)
        if KeychainManager.shared.hasCredentials {
            apiKey = KeychainManager.shared.loadAPIKey() ?? ""
            apiSecret = KeychainManager.shared.loadAPISecret() ?? ""
        }
    }
    
    private func saveCredentials() {
        do {
            try KeychainManager.shared.saveAPIKey(apiKey)
            try KeychainManager.shared.saveAPISecret(apiSecret)
            keySaved = true
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { keySaved = false }
        } catch {
            engine.addLog(.error, "Failed to save credentials: \(error.localizedDescription)")
        }
    }
    
    private func applySettings() {
        var c = engine.config
        c.symbol = symbol
        c.orderQty = Double(orderQty) ?? c.orderQty
        c.signalThreshold = Double(signalThreshold) ?? c.signalThreshold
        c.entryOffsetBps = Double(entryOffsetBps) ?? c.entryOffsetBps
        c.maxPositionSize = Double(maxPositionSize) ?? c.maxPositionSize
        c.maxLeverage = Double(maxLeverage) ?? c.maxLeverage
        c.maxDailyLoss = Double(maxDailyLoss) ?? c.maxDailyLoss
        c.maxDrawdown = Double(maxDrawdown) ?? c.maxDrawdown
        c.maxOrdersPerSec = Int(maxOrdersPerSec) ?? c.maxOrdersPerSec
        c.obLevels = Int(obLevels) ?? c.obLevels
        c.ioThreads = Int(ioThreads) ?? c.ioThreads
        
        // AI Edition
        c.mlModelEnabled = mlModelEnabled
        c.adaptiveThresholdEnabled = adaptiveThresholdEnabled
        c.regimeDetectionEnabled = regimeDetectionEnabled
        c.requoteEnabled = requoteEnabled
        c.adaptiveSizingEnabled = adaptiveSizingEnabled
        c.cbEnabled = cbEnabled
        c.cbLossThreshold = Double(cbLossThreshold) ?? c.cbLossThreshold
        c.cbDrawdownThreshold = Double(cbDrawdownThreshold) ?? c.cbDrawdownThreshold
        c.cbConsecutiveLosses = Int(cbConsecutiveLosses) ?? c.cbConsecutiveLosses
        c.cbCooldownSec = Int(cbCooldownSec) ?? c.cbCooldownSec
        c.featureTickMs = Int(featureTickMs) ?? c.featureTickMs
        c.onnxEnabled = onnxEnabled
        c.onnxModelPath = onnxModelPath
        c.onnxIntraThreads = Int(onnxIntraThreads) ?? c.onnxIntraThreads
        
        engine.config = c
        engine.addLog(.info, "Settings applied (AI Edition)")
    }
}

struct SettingsField: View {
    let label: String
    @Binding var value: String
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(label)
                .font(.caption)
                .foregroundColor(.secondary)
            TextField(label, text: $value)
                .textFieldStyle(.roundedBorder)
                .font(.system(.body, design: .monospaced))
        }
    }
}
