#!/bin/bash
#
# MTT S30 GPU Driver Build Script
# Fedora Linux compatible
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.
# Adapted for Fedora by: Fedora Driver Project
#

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Configuration
DRIVER_NAME="mtt-s30-driver"
DRIVER_VERSION="1.1.1"
FEDORA_VERSION=$(rpm -E %fedora)
ARCH=$(uname -m)
KERNELReleased=$(uname -r)

# Function to print status messages
print_status() {
    echo -e "${GREEN}[OK]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

# Function to check for required tools
check_tools() {
    print_info "Checking for required tools..."

    local missing_tools=()

    # Check for rpm-build
    if ! command -v rpm &>/dev/null; then
        missing_tools+=("rpm")
    fi

    # Check for make
    if ! command -v make &>/dev/null; then
        missing_tools+=("make")
    fi

    # Check for gcc
    if ! command -v gcc &>/dev/null; then
        missing_tools+=("gcc")
    fi

    # Check for kernel headers
    if [ ! -d "/lib/modules/${KERNELReleased}/build" ]; then
        echo "ERROR: kernel headers not found. Install: sudo dnf install kernel-devel-$(uname -r)"
        exit 1
    fi

    # Check for dkms
    if ! command -v dkms &>/dev/null; then
        missing_tools+=("dkms")
    fi

    # Check for rpmbuild
    if ! command -v rpmbuild &>/dev/null; then
        missing_tools+=("rpm-build")
    fi

    if [ ${#missing_tools[@]} -ne 0 ]; then
        print_error "Missing required tools: ${missing_tools[*]}"
        print_info "Install with: sudo dnf install -y ${missing_tools[*]}"
        exit 1
    fi

    print_status "All required tools are installed"
}

# Function to build kernel module
build_kernel_module() {
    print_info "Building kernel module..."

    cd "$PROJECT_ROOT/Driver/kernel"

    # Clean previous build
    make clean 2>/dev/null || true

    # Build the module
    if make DEBUG=0 -C /lib/modules/${KERNELReleased}/build M=$(pwd) modules; then
        print_status "Kernel module built successfully"
        ls -lh src/sgpu_km.ko
    else
        print_error "Failed to build kernel module"
        exit 1
    fi
}

# Function to build RPM packages
build_rpm_packages() {
    print_info "Building RPM packages..."

    # Create build directory
    mkdir -p "$PROJECT_ROOT/build/rpm"

    # Create source tarbal
    cd "$PROJECT_ROOT"
    print_info "Creating source tarball..."

    # Exclude object files and kernel modules from source
    tar --exclude='*.o' --exclude='*.ko' --exclude='*.mod' --exclude='*.mod.c' \
        --exclude='.*.cmd' --exclude='Module.symvers' --exclude='modules.order' \
        -czf "$PROJECT_ROOT/build/rpm/${DRIVER_NAME}-${DRIVER_VERSION}.tar.gz" \
        -C Driver .

    # Build each RPM package
    cd "$PROJECT_ROOT/build/rpm"

    # Build firmware RPM
    print_info "Building firmware RPM..."
    rpmbuild -bb --target noarch \
        "$PROJECT_ROOT/Driver/packaging/rpm/mtt-s30-firmware.spec" \
        --define "_topdir $PROJECT_ROOT/build/rpm" \
        --define "_sourcedir $PROJECT_ROOT/build/rpm" \
        --define "_specdir $PROJECT_ROOT/Driver/packaging/rpm" \
        --define "_builddir $PROJECT_ROOT/build/rpm/BUILD" \
        || print_warning "Firmware RPM build failed"

    # Build driver RPM
    print_info "Building driver RPM..."
    rpmbuild -bb --target x86_64 \
        "$PROJECT_ROOT/Driver/packaging/rpm/mtt-s30-driver.spec" \
        --define "_topdir $PROJECT_ROOT/build/rpm" \
        --define "_sourcedir $PROJECT_ROOT/build/rpm" \
        --define "_specdir $PROJECT_ROOT/Driver/packaging/rpm" \
        --define "_builddir $PROJECT_ROOT/build/rpm/BUILD" \
        --define "_kernel_ver ${KERNELReleased}" \
        || print_warning "Driver RPM build failed"

    # Build DKMS RPM
    print_info "Building DKMS RPM..."
    rpmbuild -bb --target noarch \
        "$PROJECT_ROOT/Driver/packaging/rpm/mtt-s30-dkms.spec" \
        --define "_topdir $PROJECT_ROOT/build/rpm" \
        --define "_sourcedir $PROJECT_ROOT/build/rpm" \
        --define "_specdir $PROJECT_ROOT/Driver/packaging/rpm" \
        --define "_builddir $PROJECT_ROOT/build/rpm/BUILD" \
        || print_warning "DKMS RPM build failed"

    # List built packages
    print_info "Build complete!"
    echo ""
    print_status "Built packages:"
    ls -lh "$PROJECT_ROOT/build/rpm"/*/RPMS/*/*.rpm 2>/dev/null || true
    ls -lh "$PROJECT_ROOT/build/rpm/RPMS/"* 2>/dev/null || true

    # Copy RPMs to project root for convenience
    mkdir -p "$PROJECT_ROOT/RPMS"
    cp "$PROJECT_ROOT/build/rpm"/*/RPMS/*/*.rpm "$PROJECT_ROOT/RPMS/" 2>/dev/null || true
    cp "$PROJECT_ROOT/build/rpm/RPMS/"* "$PROJECT_ROOT/RPMS/" 2>/dev/null || true

    echo ""
    print_status "RPM packages available in: $PROJECT_ROOT/RPMS/"
}

# Function to create tar.gz of the entire project
create_source_tarball() {
    print_info "Creating source tarball for distribution..."

    cd "$PROJECT_ROOT"
    tar -czf "$PROJECT_ROOT/mtt-s30-driver-${DRIVER_VERSION}-fedoracore.tar.gz" \
        --exclude="build" --exclude="RPMS" --exclude="*.tar.gz" Driver

    print_status "Source tarball created: $PROJECT_ROOT/mtt-s30-driver-${DRIVER_VERSION}-fedoracore.tar.gz"
}

# Main function
main() {
    echo "=========================================="
    echo "  MTT S30 GPU Driver for Fedora Linux"
    echo "  Build Script"
    echo "=========================================="
    echo ""
    echo "Detected:"
    echo "  Fedora Version: $FEDORA_VERSION"
    echo "  Kernel Version: $KERNELReleased"
    echo "  Architecture: $ARCH"
    echo ""

    # Check tools
    check_tools
    echo ""

    # Build kernel module
    if [ "$BUILD_MODULE" = true ]; then
        build_kernel_module
        echo ""
    fi

    # Build RPM packages
    if [ "$BUILD_RPM" = true ]; then
        build_rpm_packages
        echo ""

        # Create source tarball
        create_source_tarball
        echo ""
    fi

    print_status "Build completed successfully!"
    echo ""
    echo "Next steps:"
    echo "  1. Install packages: sudo ./install.sh"
    echo "  2. Or manually install RPMs from: $PROJECT_ROOT/RPMS/"
    echo ""
}

# Print usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help      Show this help message"
    echo "  -k, --kernel     Only build kernel module"
    echo "  -r, --rpm        Only build RPM packages"
    echo "  -c, --clean      Clean build directory"
    echo ""
    echo "Examples:"
    echo "  $0                    Build everything"
    echo "  $0 -k                 Build only kernel module"
    echo "  $0 -r                 Build only RPM packages"
    echo "  $0 -c                 Clean build directory"
    echo ""
}

# Parse arguments
BUILD_MODULE=true
BUILD_RPM=true
CLEAN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -k|--kernel)
            BUILD_RPM=false
            ;;
        -r|--rpm)
            BUILD_MODULE=false
            ;;
        -c|--clean)
            CLEAN=true
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
    shift
done

# Clean if requested
if [ "$CLEAN" = true ]; then
    print_info "Cleaning build artefacts..."
    rm -rf "$PROJECT_ROOT/build"
    rm -rf "$PROJECT_ROOT/RPMS"
    find "$PROJECT_ROOT/Driver/kernel/src" -maxdepth 1 -name "*.ko" -delete 2>/dev/null || true
    rm -f "$PROJECT_ROOT"/*.tar.gz
    print_status "Cleaned successfully"
fi

# Run main
main
