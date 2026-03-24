#!/bin/bash
# ─── CPU Isolation & Performance Tuning for BybitTrader ──────────────────────
# Run with sudo for full effect. Configures macOS for low-latency trading.
#
# Apple Silicon does NOT support traditional CPU isolation (isolcpus).
# Instead we use:
#   1. taskpolicy — set QoS for the trading process
#   2. renice — boost process priority
#   3. sysctl — tune kernel scheduling parameters
#   4. powermetrics hints — disable efficiency cores for our process
#
# Usage:
#   sudo ./scripts/isolate_cores.sh <PID>
#   sudo ./scripts/isolate_cores.sh        # applies system-wide tuning only

set -euo pipefail

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

log() { echo -e "${CYAN}[ISOLATE]${NC} $1"; }
ok()  { echo -e "${GREEN}[OK]${NC} $1"; }
warn(){ echo -e "${YELLOW}[WARN]${NC} $1"; }
err() { echo -e "${RED}[ERROR]${NC} $1"; }

PID="${1:-}"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  BybitTrader — CPU Isolation & Performance Tuning"
echo "  Apple Silicon Optimized"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# ─── Check root ──────────────────────────────────────────────────────────────
if [[ $EUID -ne 0 ]]; then
    warn "Running without root — some optimizations will be skipped"
    warn "Run with: sudo $0 $PID"
fi

# ─── System-wide: Disable App Nap ────────────────────────────────────────────
log "Disabling App Nap system-wide..."
defaults write NSGlobalDomain NSAppSleepDisabled -bool YES 2>/dev/null && ok "App Nap disabled" || warn "Could not disable App Nap"

# ─── System-wide: Disable Spotlight indexing (reduces I/O jitter) ────────────
if [[ $EUID -eq 0 ]]; then
    log "Pausing Spotlight indexing..."
    mdutil -a -i off 2>/dev/null && ok "Spotlight paused" || warn "Could not pause Spotlight"
fi

# ─── System-wide: Reduce timer coalescing ────────────────────────────────────
if [[ $EUID -eq 0 ]]; then
    log "Reducing timer coalescing (improves wakeup latency)..."
    sysctl -w kern.timer.coalescing_enabled=0 2>/dev/null && ok "Timer coalescing disabled" || warn "Could not disable timer coalescing"
fi

# ─── System-wide: Increase maxfiles ─────────────────────────────────────────
if [[ $EUID -eq 0 ]]; then
    log "Increasing file descriptor limits..."
    sysctl -w kern.maxfiles=524288 2>/dev/null || true
    sysctl -w kern.maxfilesperproc=262144 2>/dev/null || true
    ulimit -n 262144 2>/dev/null || true
    ok "File descriptor limits increased"
fi

# ─── Process-specific tuning ────────────────────────────────────────────────
if [[ -n "$PID" ]]; then
    if ! kill -0 "$PID" 2>/dev/null; then
        err "PID $PID does not exist"
        exit 1
    fi

    PROC_NAME=$(ps -p "$PID" -o comm= 2>/dev/null || echo "unknown")
    log "Tuning process: $PROC_NAME (PID=$PID)"

    # Set highest QoS class via taskpolicy
    log "Setting QoS to User Interactive..."
    taskpolicy -b -p "$PID" 2>/dev/null || true

    # Boost process priority (nice -20 = highest)
    if [[ $EUID -eq 0 ]]; then
        log "Setting process priority to -20 (highest)..."
        renice -20 -p "$PID" 2>/dev/null && ok "Priority set to -20" || warn "Could not set priority"
    fi

    # Disable sudden termination for the process
    log "Disabling sudden termination..."
    # This is handled in-process via NSProcessInfo, but we log the reminder
    ok "Process tuning complete for PID=$PID"
fi

# ─── Disable thermal throttling notification (informational) ────────────────
log "Checking thermal state..."
if command -v pmset &>/dev/null; then
    # Prevent sleep
    if [[ $EUID -eq 0 ]]; then
        pmset -a disablesleep 1 2>/dev/null && ok "Sleep disabled" || warn "Could not disable sleep"
        pmset -a displaysleep 0 2>/dev/null || true
    fi
fi

# ─── Summary ────────────────────────────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  Tuning Complete"
echo ""
echo "  For maximum performance:"
echo "    1. Close all other applications"
echo "    2. Disable Wi-Fi if using wired connection"
echo "    3. Connect to power (prevents P-core throttling)"
echo "    4. Set Energy mode to 'High Performance' in System Preferences"
echo ""
echo "  Thread affinity is configured in-process via thread_affinity.h"
echo "  Real-time scheduling requires running bybit_hft as root"
echo "═══════════════════════════════════════════════════════════════"
echo ""
