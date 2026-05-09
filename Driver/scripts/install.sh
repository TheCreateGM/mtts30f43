#!/bin/bash
#
# MTT S30 GPU Driver Install Script
# Fedora Linux compatible
#
# Usage: sudo ./scripts/install.sh [--no-dkms]
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
DRIVER_ROOT="$(dirname "$SCRIPT_DIR")"

DRIVER_VERSION="1.1.1"
KERNEL_VERSION=$(uname -r)
DKMS_ENABLED=1

print_status() { echo -e "${GREEN}[OK]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }
print_info() { echo -e "${YELLOW}[INFO]${NC} $1"; }

check_gpu() {
    print_info "Checking for Moore Threads GPU..."
    local gpu
    gpu=$(lspci | grep -i moore || true)
    if [ -n "$gpu" ]; then
        print_status "GPU: $gpu"
    else
        print_warning "No Moore Threads GPU detected"
    fi
}

check_deps() {
    print_info "Checking dependencies..."
    local missing=()
    command -v gcc &>/dev/null || missing+=("gcc")
    command -v make &>/dev/null || missing+=("make")
    [ -d "/lib/modules/${KERNEL_VERSION}/build" ] || missing+=("kernel-devel-${KERNEL_VERSION}")
    [ ${#missing[@]} -eq 0 ] && print_status "All deps satisfied" || \
        print_warning "Missing: ${missing[*]}"
}

build_module() {
    print_info "Building kernel module..."
    make -C /lib/modules/${KERNEL_VERSION}/build \
        M="${DRIVER_ROOT}/kernel/src" modules
    print_status "Module built"
}

install_module() {
    print_info "Installing kernel module..."
    local dest="/lib/modules/${KERNEL_VERSION}/kernel/drivers/gpu/drm/mtt"
    mkdir -p "$dest"
    cp "${DRIVER_ROOT}/kernel/src/sgpu_km.ko" "${dest}/"
    cp "${DRIVER_ROOT}/kernel/src/mtgpu_stub.ko" "${dest}/"
    depmod -a
    print_status "Modules installed to $dest"
}

install_firmware() {
    print_info "Installing firmware..."
    local src="${DRIVER_ROOT}/firmware/mthreads"
    local dst="/lib/firmware/mthreads"
    if [ -d "$src" ]; then
        mkdir -p "$dst"
        cp -r "${src}/"* "${dst}/"
        chmod 644 "${dst}/"*
        restorecon -r "$dst" 2>/dev/null || true
        dracut --force 2>/dev/null || true
        print_status "Firmware installed"
    else
        print_warning "Firmware dir not found: $src"
    fi
}

install_udev() {
    print_info "Installing udev rules..."
    cp "${DRIVER_ROOT}/udev/99-mtt-s30.rules" /etc/udev/rules.d/
    udevadm control --reload-rules
    udevadm trigger
    print_status "Udev rules installed"
}

install_modprobe() {
    print_info "Installing modprobe config..."
    cat > /etc/modprobe.d/mtt-s30.conf << 'EOF'
softdep sgpu_km pre: mtgpu_stub
options sgpu_km total_gpu_num=1
EOF
    print_status "Modprobe config installed"
}

install_systemd() {
    print_info "Installing systemd service..."
    cp "${DRIVER_ROOT}/systemd/mtt-s30-driver.service" /etc/systemd/system/
    systemctl daemon-reload
    systemctl enable mtt-s30-driver.service
    print_status "Systemd service installed"
}

load_modules() {
    print_info "Loading modules..."
    modprobe mtgpu_stub 2>/dev/null && print_status "mtgpu_stub loaded" || print_warning "mtgpu_stub failed"
    sleep 1
    modprobe sgpu_km 2>/dev/null && print_status "sgpu_km loaded" || print_warning "sgpu_km failed"
}

register_dkms() {
    [ "${DKMS_ENABLED}" -eq 0 ] && { print_info "DKMS skipped"; return; }
    command -v dkms &>/dev/null || { print_warning "dkms not found"; return; }

    local dkms_src="/usr/src/mtt-s30-${DRIVER_VERSION}"
    rm -rf "${dkms_src}"
    cp -r "${DRIVER_ROOT}/kernel" "${dkms_src}"

    dkms add -m mtt-s30 -v "${DRIVER_VERSION}" 2>/dev/null || true
    dkms build -m mtt-s30 -v "${DRIVER_VERSION}" -k "${KERNEL_VERSION}" && \
        dkms install -m mtt-s30 -v "${DRIVER_VERSION}" -k "${KERNEL_VERSION}" && \
        print_status "DKMS registered" || \
        print_warning "DKMS build/install failed"
}

verify() {
    print_info "Verifying..."
    lsmod | grep -q mtgpu_stub && print_status "mtgpu_stub loaded" || print_warning "mtgpu_stub not loaded"
    lsmod | grep -q sgpu_km && print_status "sgpu_km loaded" || print_warning "sgpu_km not loaded"
    [ -d "/proc/sgpu_km" ] && print_status "Proc interface up" || print_warning "No proc interface"
    [ -e "/dev/mtgpu.0" ] && print_status "/dev/mtgpu.0 exists" || print_warning "/dev/mtgpu.0 not found"
}

main() {
    echo "=========================================="
    echo "  MTT S30 GPU Driver Installer"
    echo "=========================================="
    echo ""

    check_gpu
    check_deps
    build_module
    install_module
    install_firmware
    install_udev
    install_modprobe
    install_systemd
    register_dkms
    load_modules

    # Install mtt-monitor
    if [ -f "${DRIVER_ROOT}/userspace/tools/mtt-monitor" ]; then
        cp "${DRIVER_ROOT}/userspace/tools/mtt-monitor" /usr/local/bin/mtt-monitor
        chmod +x /usr/local/bin/mtt-monitor
        print_status "mtt-monitor installed"
    fi

    verify

    echo ""
    echo "Installation complete."
    echo "  mtt-monitor   - monitor GPU"
    echo "  mtt-tool      - management CLI"
    echo "  sudo ./scripts/test.sh - run tests"
    echo ""
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --no-dkms) DKMS_ENABLED=0 ;;
        -h|--help) echo "Usage: sudo $0 [--no-dkms]"; exit 0 ;;
        *) ;;
    esac
    shift
done

main
