#!/bin/bash
#
# MTT S30 GPU Driver Uninstall Script
# Fedora Linux compatible
#
# Usage: sudo ./scripts/uninstall.sh [OPTIONS]
#
# Options:
#   --sdk       Also remove MUSA SDK
#   -f, --force Force unload
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.

set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: must run as root (sudo)"
    exit 1
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVER_VERSION="1.1.1"
KERNEL_VERSION=$(uname -r)

print_status() { echo -e "${GREEN}[OK]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }
print_info() { echo -e "${YELLOW}[INFO]${NC} $1"; }

unload_module() {
    print_info "Unloading modules..."
    modprobe -r sgpu_km 2>/dev/null && print_status "sgpu_km unloaded" || print_info "sgpu_km not loaded"
    modprobe -r mtgpu_stub 2>/dev/null && print_status "mtgpu_stub unloaded" || print_info "mtgpu_stub not loaded"
}

remove_dkms() {
    command -v dkms &>/dev/null || return
    if dkms status | grep -q "mtt-s30/${DRIVER_VERSION}"; then
        dkms remove "mtt-s30/${DRIVER_VERSION}" --all 2>/dev/null && print_status "DKMS removed" || true
    fi
    rm -rf "/usr/src/mtt-s30-${DRIVER_VERSION}"
}

remove_kernel_module() {
    local dir="/lib/modules/${KERNEL_VERSION}/kernel/drivers/gpu/drm/mtt"
    rm -f "${dir}/sgpu_km.ko" "${dir}/mtgpu_stub.ko"
    rmdir "$dir" 2>/dev/null || true
    depmod -a
    print_status "Module files removed"
}

remove_firmware() {
    local dir="/lib/firmware/mthreads"
    if [ -d "$dir" ]; then
        rm -f "${dir}/musa.fw."* "${dir}/musa.sh."*
        rmdir "$dir" 2>/dev/null || true
        dracut --force 2>/dev/null || true
        print_status "Firmware removed"
    fi
}

remove_udev() {
    rm -f /etc/udev/rules.d/99-mtt-s30.rules
    udevadm control --reload-rules 2>/dev/null || true
    print_status "Udev rules removed"
}

remove_modprobe() {
    rm -f /etc/modprobe.d/mtt-s30.conf
    print_status "Modprobe config removed"
}

remove_systemd() {
    systemctl stop mtt-s30-driver.service 2>/dev/null || true
    systemctl disable mtt-s30-driver.service 2>/dev/null || true
    rm -f /etc/systemd/system/mtt-s30-driver.service
    systemctl daemon-reload 2>/dev/null || true
    print_status "Systemd service removed"
}

remove_tools() {
    rm -f /usr/local/bin/mtt-monitor
    rm -f /usr/local/bin/mtt-tool
    print_status "Tools removed"
}

remove_sdk() {
    local musa_home="${MUSA_HOME:-/usr/local/musa}"
    rm -rf "$musa_home" 2>/dev/null || true
    rm -f /etc/profile.d/musa.sh 2>/dev/null || true
    rm -f /etc/ld.so.conf.d/musa.conf 2>/dev/null || true
    ldconfig 2>/dev/null || true
    print_status "MUSA SDK removed"
}

main() {
    echo "=========================================="
    echo "  MTT S30 GPU Driver Uninstaller"
    echo "=========================================="
    echo ""

    unload_module
    remove_dkms
    remove_kernel_module
    remove_firmware
    remove_udev
    remove_modprobe
    remove_systemd
    remove_tools

    if [ "$REMOVE_SDK" = true ]; then
        remove_sdk
    fi

    echo ""
    print_status "Uninstall complete"
    echo ""
}

REMOVE_SDK=false
while [[ $# -gt 0 ]]; do
    case $1 in
        --sdk) REMOVE_SDK=true ;;
        -f|--force) ;;
        -h|--help)
            sed -n '3,8p' "$0"
            exit 0 ;;
        *) ;;
    esac
    shift
done

main
