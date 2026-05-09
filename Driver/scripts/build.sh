#!/bin/bash
#
# MTT S30 GPU Driver Build Script
# Fedora Linux compatible
#
# Usage: ./scripts/build.sh [OPTIONS]
#
# Options:
#   -h, --help      Show help
#   -k, --kernel    Build kernel module only
#   -r, --rpm       Build RPM packages only
#   -c, --clean     Clean build artifacts
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVER_ROOT="$(dirname "$SCRIPT_DIR")"

DRIVER_NAME="mtt-s30-driver"
DRIVER_VERSION="1.1.1"
KERNEL_VERSION=$(uname -r)
ARCH=$(uname -m)

print_status() { echo -e "${GREEN}[OK]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }
print_info() { echo -e "${YELLOW}[INFO]${NC} $1"; }

check_tools() {
    print_info "Checking for required tools..."

    local missing=()
    command -v rpm &>/dev/null || missing+=("rpm")
    command -v make &>/dev/null || missing+=("make")
    command -v gcc &>/dev/null || missing+=("gcc")
    [ -d "/lib/modules/${KERNEL_VERSION}/build" ] || {
        print_error "kernel-devel not found. Install: sudo dnf install kernel-devel-${KERNEL_VERSION}"
        exit 1
    }
    command -v dkms &>/dev/null || missing+=("dkms")
    command -v rpmbuild &>/dev/null || missing+=("rpm-build")

    if [ ${#missing[@]} -ne 0 ]; then
        print_error "Missing: ${missing[*]}"
        print_info "Install: sudo dnf install -y ${missing[*]}"
        exit 1
    fi

    print_status "All required tools present"
}

build_kernel_module() {
    print_info "Building kernel module..."

    cd "$DRIVER_ROOT/kernel/src"
    make clean 2>/dev/null || true
    if make DEBUG=0 -C /lib/modules/${KERNEL_VERSION}/build M="$(pwd)" modules; then
        print_status "Kernel module built"
        ls -lh sgpu_km.ko mtgpu_stub.ko 2>/dev/null
    else
        print_error "Kernel module build failed"
        exit 1
    fi
}

build_rpm_packages() {
    print_info "Building RPM packages..."

    mkdir -p "$DRIVER_ROOT/build/rpm"
    cd "$DRIVER_ROOT"

    tar --exclude='*.o' --exclude='*.ko' --exclude='*.mod' --exclude='*.mod.c' \
        --exclude='.*.cmd' --exclude='Module.symvers' --exclude='modules.order' \
        -czf "$DRIVER_ROOT/build/rpm/${DRIVER_NAME}-${DRIVER_VERSION}.tar.gz" \
        -C "$DRIVER_ROOT" \
        --transform="s|^|${DRIVER_NAME}-${DRIVER_VERSION}/|" \
        kernel/ firmware/ packaging/ docs/ udev/ selinux/ systemd/ userspace/ scripts/ build/ \
        LICENSE README.md

    for spec in packaging/rpm/mtt-s30-firmware.spec \
                packaging/rpm/mtt-s30-driver.spec \
                packaging/rpm/mtt-s30-dkms.spec; do
        local pkg_name
        pkg_name=$(basename "$spec" .spec)
        print_info "Building ${pkg_name}..." && \
        rpmbuild -bb "$DRIVER_ROOT/$spec" \
            --define "_topdir $DRIVER_ROOT/build/rpm" \
            --define "_sourcedir $DRIVER_ROOT/build/rpm" \
            --define "_specdir $DRIVER_ROOT/packaging/rpm" \
            --define "_builddir $DRIVER_ROOT/build/rpm/BUILD" \
            --define "_kernel_ver ${KERNEL_VERSION}" \
            --define "_driver_root $DRIVER_ROOT" \
            2>&1 || print_warning "${pkg_name} build had issues"
    done

    print_info "RPMs:"
    find "$DRIVER_ROOT/build/rpm" -name "*.rpm" 2>/dev/null | head -10
}

main() {
    echo "=========================================="
    echo "  MTT S30 GPU Driver for Fedora Linux"
    echo "  Build Script"
    echo "=========================================="
    echo ""
    echo "  Fedora: $(rpm -E %fedora 2>/dev/null || echo unknown)"
    echo "  Kernel: $KERNEL_VERSION"
    echo "  Arch:   $ARCH"
    echo ""

    check_tools
    echo ""

    if [ "$BUILD_MODULE" = true ]; then
        build_kernel_module
        echo ""
    fi

    if [ "$BUILD_RPM" = true ]; then
        build_rpm_packages
        echo ""
    fi

    print_status "Done!"
    echo "Next: sudo ./scripts/install.sh"
    echo ""
}

BUILD_MODULE=true
BUILD_RPM=true

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            sed -n '3,12p' "$0"
            exit 0 ;;
        -k|--kernel) BUILD_RPM=false ;;
        -r|--rpm)    BUILD_MODULE=false ;;
        -c|--clean)
            rm -rf "$DRIVER_ROOT/build"
            find "$DRIVER_ROOT/kernel/src" -maxdepth 1 \( -name "*.ko" -o -name "*.o" -o -name ".*.cmd" -o -name "Module.symvers" -o -name "modules.order" \) -delete 2>/dev/null || true
            print_status "Cleaned"
            exit 0 ;;
        *) echo "Unknown: $1"; exit 1 ;;
    esac
    shift
done

main
