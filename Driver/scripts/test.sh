#!/bin/bash
#
# MTT S30 GPU Driver Test Script
# Fedora Linux compatible
#
# Usage: sudo ./scripts/test.sh [options]
#
# Options:
#   -v, --verbose     Verbose output
#   -q, --quick       Quick test (skip module rebuild)
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVER_ROOT="$(dirname "$SCRIPT_DIR")"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS=0
FAIL=0
SKIP=0

pass()  { PASS=$((PASS+1)); echo -e "${GREEN}[PASS]${NC} $1"; }
fail()  { FAIL=$((FAIL+1)); echo -e "${RED}[FAIL]${NC} $1"; }
skip()  { SKIP=$((SKIP+1)); echo -e "${YELLOW}[SKIP]${NC} $1"; }

require_root() {
    if [ "$(id -u)" -ne 0 ]; then
        fail "Must run as root (sudo)"
        exit 1
    fi
}

test_module_loaded() {
    if lsmod | grep -q sgpu_km; then
        pass "sgpu_km module is loaded"
    else
        fail "sgpu_km module is not loaded"
    fi
    if lsmod | grep -q mtgpu_stub; then
        pass "mtgpu_stub module is loaded"
    else
        fail "mtgpu_stub module is not loaded"
    fi
}

test_proc_interface() {
    local proc="/proc/sgpu_km"
    if [ -d "$proc" ]; then
        pass "Proc interface exists at $proc"
        local entries
        entries=$(ls "$proc" 2>/dev/null | wc -l)
        [ "$entries" -gt 0 ] && pass "Proc has $entries entries" || \
            fail "Proc has no entries"
    else
        fail "Proc interface not found at $proc"
    fi
}

test_device_nodes() {
    [ -e "/dev/mtgpu.0" ] && pass "/dev/mtgpu.0 exists" || \
        fail "/dev/mtgpu.0 not found"
    [ -e "/dev/sgpu-km" ] && pass "/dev/sgpu-km exists" || \
        fail "/dev/sgpu-km not found"
}

test_firmware() {
    local fw="/lib/firmware/mthreads"
    if [ -d "$fw" ]; then
        pass "Firmware directory exists"
        local count
        count=$(ls "$fw" 2>/dev/null | wc -l)
        [ "$count" -ge 3 ] && pass "Firmware has $count files" || \
            fail "Firmware has only $count files"
    else
        fail "Firmware directory not found"
    fi
}

test_pci_detection() {
    local pci
    pci=$(lspci -nn 2>/dev/null | grep "1ed5:" || true)
    if [ -n "$pci" ]; then
        pass "MTT GPU detected: $(echo "$pci" | head -1)"
    else
        skip "No MTT GPU found via lspci (expected if no hardware)"
    fi
}

test_udev_rules() {
    local rules="/etc/udev/rules.d/99-mtt-s30.rules"
    [ -f "$rules" ] && pass "Udev rules installed" || \
        fail "Udev rules not found"
}

test_systemd_service() {
    local srv="mtt-s30-driver.service"
    if systemctl is-enabled "$srv" &>/dev/null; then
        pass "Systemd service $srv is enabled"
    else
        fail "Systemd service $srv not enabled"
    fi
}

test_modprobe_config() {
    [ -f "/etc/modprobe.d/mtt-s30.conf" ] && pass "Modprobe config exists" || \
        fail "Modprobe config not found"
}

test_kernel_source() {
    local src="$DRIVER_ROOT/kernel/src"
    [ -f "$src/sgpu.c" ]  && pass "sgpu.c exists"  || fail "sgpu.c missing"
    [ -f "$src/mtgpu_stub.c" ] && pass "mtgpu_stub.c exists" || fail "mtgpu_stub.c missing"
    [ -f "$src/Makefile" ] && pass "Makefile exists" || fail "Makefile missing"
}

main() {
    echo "=========================================="
    echo "  MTT S30 GPU Driver Test Suite"
    echo "=========================================="
    echo ""

    if [ "$QUICK" != true ]; then
        require_root
    fi

    echo "--- Kernel Module ---"
    test_module_loaded

    echo ""
    echo "--- Proc Interface ---"
    test_proc_interface

    echo ""
    echo "--- Device Nodes ---"
    test_device_nodes

    echo ""
    echo "--- Firmware ---"
    test_firmware

    echo ""
    echo "--- Hardware Detection ---"
    test_pci_detection

    echo ""
    echo "--- System Configuration ---"
    test_udev_rules
    test_systemd_service
    test_modprobe_config

    echo ""
    echo "--- Source Integrity ---"
    test_kernel_source

    echo ""
    echo "=========================================="
    echo "  Results: $PASS passed, $FAIL failed, $SKIP skipped"
    echo "=========================================="

    [ "$FAIL" -eq 0 ]
}

QUICK=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose) VERBOSE=true ;;
        -q|--quick)   QUICK=true ;;
        -h|--help)
            sed -n '3,9p' "$0"
            exit 0 ;;
        *) ;;
    esac
    shift
done

main
