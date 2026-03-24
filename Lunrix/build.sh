#!/bin/bash
# build.sh — Build Lynrix v2.5 macOS app
# Usage: ./build.sh [debug|release]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
CONFIG="${1:-release}"

echo "═══════════════════════════════════════════════"
echo "  Lynrix v2.5 Build Script"
echo "  Config: $CONFIG"
echo "═══════════════════════════════════════════════"

# Step 1: Build C++ core library
echo ""
echo "▶ Building C++ trading core (v2.5)..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ "$CONFIG" = "debug" ]; then
    cmake "$PROJECT_ROOT" -DCMAKE_BUILD_TYPE=Debug
else
    cmake "$PROJECT_ROOT" -DCMAKE_BUILD_TYPE=Release
fi

cmake --build . --target trading_core -j"$(sysctl -n hw.ncpu)"
echo "✓ libtrading_core.a built"

# Step 2: Check for xcodegen
if ! command -v xcodegen &>/dev/null; then
    echo ""
    echo "▶ xcodegen not found. Installing via Homebrew..."
    brew install xcodegen
fi

# Step 3: Generate Xcode project
echo ""
echo "▶ Generating Xcode project..."
cd "$SCRIPT_DIR"
xcodegen generate
echo "✓ Lynrix.xcodeproj generated"

# Step 4: Build with xcodebuild
echo ""
echo "▶ Building macOS app..."
XCODE_CONFIG="Release"
if [ "$CONFIG" = "debug" ]; then
    XCODE_CONFIG="Debug"
fi

xcodebuild -project Lynrix.xcodeproj \
    -scheme Lynrix \
    -configuration "$XCODE_CONFIG" \
    -arch arm64 -arch x86_64 \
    ONLY_ACTIVE_ARCH=NO \
    build 2>&1 | tail -20

echo ""
echo "═══════════════════════════════════════════════"
echo "  ✓ Build complete!"
echo "  App: $(find ~/Library/Developer/Xcode/DerivedData -name 'Lynrix.app' -type d 2>/dev/null | head -1 || echo 'Check DerivedData')"
echo "═══════════════════════════════════════════════"
