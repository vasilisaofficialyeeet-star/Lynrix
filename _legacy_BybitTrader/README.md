# BybitTrader — macOS Desktop Application

Native macOS desktop trading application for Bybit V5 API. Built with **SwiftUI** frontend and **C++20** low-latency trading engine backend.

## Architecture

```
┌─────────────────────────────────────────────────┐
│              SwiftUI (main thread)               │
│  Dashboard │ OrderBook │ Portfolio │ Settings     │
├─────────────────────────────────────────────────┤
│           TradingEngine.swift (ObservableObject)  │
│           30 FPS polling, GCD dispatch            │
├─────────────────────────────────────────────────┤
│      TradingCoreBridge.mm (Objective-C++)         │
│      Callbacks → DispatchQueue.main               │
├─────────────────────────────────────────────────┤
│         trading_core_api.h (Pure C API)           │
│         Opaque handle + function pointers         │
├─────────────────────────────────────────────────┤
│       libtrading_core.a (C++ static library)      │
│  OrderBook │ FeatureEngine │ Model │ Risk │ WS    │
│       Background threads, Boost.Asio              │
└─────────────────────────────────────────────────┘
```

## Prerequisites

```bash
# Xcode Command Line Tools
xcode-select --install

# Homebrew dependencies
brew install boost openssl cmake

# XcodeGen (generates .xcodeproj from project.yml)
brew install xcodegen
```

## Build

### Quick Build (script)

```bash
cd BybitTrader
./build.sh release    # or: ./build.sh debug
```

### Manual Build

```bash
# 1. Build C++ trading core
cd /path/to/bybit
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target trading_core -j$(sysctl -n hw.ncpu)

# 2. Generate Xcode project
cd ../BybitTrader
xcodegen generate

# 3. Open in Xcode
open BybitTrader.xcodeproj

# 4. Build & Run (⌘R) or:
xcodebuild -project BybitTrader.xcodeproj \
    -scheme BybitTrader \
    -configuration Release \
    build
```

## Project Structure

```
BybitTrader/
├── App/
│   ├── BybitTraderApp.swift        # @main entry point
│   ├── ContentView.swift           # NavigationSplitView layout
│   ├── Info.plist                  # App bundle metadata
│   └── BybitTrader.entitlements    # Hardened runtime entitlements
├── Views/
│   ├── DashboardView.swift         # Metrics cards, mini OB
│   ├── OrderBookView.swift         # Full depth with color bars
│   ├── PortfolioView.swift         # Position, PnL, latency
│   ├── SignalsView.swift           # Signal history table
│   ├── SettingsView.swift          # Config + Keychain API keys
│   └── LogPanelView.swift          # Filterable log viewer
├── Models/
│   ├── TradingEngine.swift         # ObservableObject, bridge manager
│   └── KeychainManager.swift       # macOS Keychain wrapper
├── Bridge/
│   ├── TradingCoreBridge.h         # Obj-C interface
│   ├── TradingCoreBridge.mm        # Obj-C++ ↔ C API
│   └── BybitTrader-Bridging-Header.h
├── Assets.xcassets/
│   └── AppIcon.appiconset/
├── project.yml                     # XcodeGen spec
├── build.sh                        # One-click build script
└── README.md
```

## Security

### API Key Storage

API keys are stored in **macOS Keychain** via `Security.framework`:
- Encrypted at rest by macOS
- Accessible only when device is unlocked (`kSecAttrAccessibleWhenUnlockedThisDeviceOnly`)
- Never written to config files or disk
- Per-app access via Keychain access groups

### Hardened Runtime

The app is built with:
- **Hardened Runtime** enabled
- **Code signing** ready (set your Team ID in Xcode)
- **Notarization** compatible
- Network client entitlement for WebSocket/REST

## Code Signing & Notarization

### Development

```bash
# Sign with development identity
codesign --force --sign "Apple Development: Your Name" \
    --entitlements BybitTrader/App/BybitTrader.entitlements \
    build/Release/BybitTrader.app
```

### Distribution

```bash
# 1. Archive
xcodebuild -project BybitTrader.xcodeproj \
    -scheme BybitTrader \
    -configuration Release \
    -archivePath build/BybitTrader.xcarchive \
    archive

# 2. Export
xcodebuild -exportArchive \
    -archivePath build/BybitTrader.xcarchive \
    -exportPath build/export \
    -exportOptionsPlist ExportOptions.plist

# 3. Notarize
xcrun notarytool submit build/export/BybitTrader.app.zip \
    --apple-id "your@email.com" \
    --team-id "TEAMID" \
    --password "@keychain:AC_PASSWORD" \
    --wait

# 4. Staple
xcrun stapler staple build/export/BybitTrader.app
```

## UI Overview

| View | Description |
|------|-------------|
| **Dashboard** | Live mid price, spread, PnL, latency cards, mini orderbook |
| **Order Book** | Full L2 depth with qty bars, spread indicator, 10/20/50 levels |
| **Portfolio** | Position details, PnL breakdown, execution stats, latency |
| **Signals** | Signal history with side, price, qty, confidence bar |
| **Settings** | API keys (Keychain), paper/live toggle, risk limits, params |
| **Logs** | Filterable system log with level colors, search, export |

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| ⌘R | Start/Stop engine |
| ⇧⌘T | Toggle Paper/Live mode |
| ⌘K | Clear logs |
| ⇧⌘E | Export logs |

## Performance

- UI polling at **30 FPS** (throttled, no blocking)
- C++ engine runs on background threads via Boost.Asio
- Data transfer via lock-free polling (no mutex on hot path)
- SwiftUI only redraws on state change (`Equatable` diffing)
- Universal binary: **Apple Silicon + Intel**

## Troubleshooting

### Build fails: "library not found"
Ensure C++ core is built first:
```bash
cd build && cmake --build . --target trading_core
```

### Keychain access denied
Run the app outside sandbox for development, or add proper keychain access group.

### No market data
- Check WebSocket URLs in Settings
- Verify network connectivity
- Check Logs panel for connection errors
