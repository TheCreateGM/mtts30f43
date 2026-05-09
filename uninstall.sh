#!/bin/bash
#
# MTT S30 GPU Driver Uninstall Script
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
DRIVER_VERSION="1.1.1"
KERNEL_VERSION=$(uname -r)

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

# Function to check if running as root
check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        print_error "This script must be run as root"
        print_info "Try: sudo $0"
        exit 1
    fi
}

# Function to unload kernel module
unload_module() {
    print_info "Unloading kernel module..."

    # Check if module is loaded
    if lsmod | grep -q sgpu_km; then
        # Unload the module
        if modprobe -r sgpu_km; then
            print_status "Kernel module unloaded successfully"
        else
            print_error "Failed to unload kernel module"
            print_info "Try manual unload: sudo rmmod -f sgpu_km"
        fi
    else
        print_info "Kernel module is not loaded"
    fi

    echo ""
}

# Function to remove DKMS module
remove_dkms() {
    print_info "Removing DKMS module..."

    # Check if dkms binary is available
    if ! command -v dkms &>/dev/null; then
        print_info "dkms not installed, skipping DKMS deregistration"
        echo ""
        return
    fi

    # Check if DKMS module is registered
    if dkms status | grep -q "mtt-s30/${DRIVER_VERSION}"; then
        # Uninstall DKMS module
        if dkms remove "mtt-s30/${DRIVER_VERSION}" --all; then
            print_status "DKMS module unregistered"
        else
            print_warning "Failed to unregister DKMS module"
        fi
    else
        print_info "DKMS module is not registered"
    fi

    # Remove source directory
    if [ -d "/usr/src/mtt-s30-${DRIVER_VERSION}" ]; then
        rm -rf "/usr/src/mtt-s30-${DRIVER_VERSION}"
        print_status "DKMS source directory removed"
    fi

    echo ""
}

# Function to remove MUSA SDK
remove_sdk() {
    local musa_home="${MUSA_HOME:-/usr/local/musa}"

    print_info "Removing MUSA SDK..."

    if [ -d "$musa_home" ]; then
        rm -rf "$musa_home"
        print_status "Removed MUSA_HOME: $musa_home"
    else
        print_info "MUSA_HOME not found: $musa_home"
    fi

    if [ -f "/etc/profile.d/musa.sh" ]; then
        rm -f "/etc/profile.d/musa.sh"
        print_status "Removed /etc/profile.d/musa.sh"
    else
        print_info "No /etc/profile.d/musa.sh found"
    fi

    if [ -f "/etc/ld.so.conf.d/musa.conf" ]; then
        rm -f "/etc/ld.so.conf.d/musa.conf"
        print_status "Removed /etc/ld.so.conf.d/musa.conf"
    else
        print_info "No /etc/ld.so.conf.d/musa.conf found"
    fi

    print_info "Running ldconfig..."
    ldconfig
    print_status "ldconfig completed"

    echo ""
}

# Function to remove RPM packages
remove_rpm_packages() {
    print_info "Removing RPM packages..."

    local removed=false

    # Check and remove each package
    for pkg in mtt-s30-driver mtt-s30-dkms mtt-s30-firmware; do
        if rpm -q "$pkg" &>/dev/null; then
            if dnf remove -y "$pkg"; then
                print_status "Removed package: $pkg"
                removed=true
            else
                print_warning "Failed to remove package: $pkg"
            fi
        fi
    done

    if [ "$removed" = true ]; then
        print_status "RPM packages removed"
    else
        print_info "No RPM packages found to remove"
    fi

    echo ""
}

# Function to remove firmware files
remove_firmware() {
    print_info "Removing firmware files..."

    local firmware_dir="/lib/firmware/mthreads"

    if [ -d "$firmware_dir" ]; then
        # Check if there are other files in the directory
        local file_count=$(ls -1 "$firmware_dir" 2>/dev/null | wc -l)

        if [ "$file_count" -gt 0 ]; then
            # Remove only MTT firmware files
            local removed_any=false
            for f in musa.fw.* musa.sh.*; do
                if [ -f "${firmware_dir}/${f}" ]; then
                    rm -f "${firmware_dir}/${f}"
                    print_status "Removed: ${firmware_dir}/${f}"
                    removed_any=true
                fi
            done

            # Remove directory if empty
            if [ "$(ls -1 "$firmware_dir" 2>/dev/null | wc -l)" -eq 0 ]; then
                rmdir "$firmware_dir"
                print_status "Firmware directory removed"
            fi

            if [ "$removed_any" = true ]; then
                # Update initramfs
                if command -v dracut &>/dev/null; then
                    dracut --force || print_warning "Failed to update initramfs"
                fi
            fi
        else
            rmdir "$firmware_dir"
            print_status "Firmware directory removed (was empty)"
        fi
    else
        print_info "No firmware directory found"
    fi

    echo ""
}

# Function to remove udev rules
remove_udev_rules() {
    print_info "Removing udev rules..."

    local udev_file="/etc/udev/rules.d/99-mtt-s30.rules"

    if [ -f "$udev_file" ]; then
        rm -f "$udev_file"
        print_status "Udev rules removed"

        # Reload udev rules
        udevadm control --reload-rules
        udevadm trigger
    else
        print_info "No udev rules found to remove"
    fi

    echo ""
}

# Function to remove modprobe configuration
remove_modprobe() {
    print_info "Removing modprobe configuration..."

    local modprobe_file="/etc/modprobe.d/mtt-s30.conf"

    if [ -f "$modprobe_file" ]; then
        rm -f "$modprobe_file"
        print_status "Modprobe configuration removed"
    else
        print_info "No modprobe configuration found to remove"
    fi

    echo ""
}

# Function to remove systemd service
remove_systemd_service() {
    print_info "Removing systemd service..."

    local service_file="/etc/systemd/system/mtt-s30-driver.service"

    if [ -f "$service_file" ]; then
        # Stop and disable service
        systemctl stop mtt-s30-driver.service 2>/dev/null || true
        systemctl disable mtt-s30-driver.service 2>/dev/null || true

        rm -f "$service_file"
        print_status "Systemd service removed"

        # Reload systemd
        systemctl daemon-reload
    else
        print_info "No systemd service found to remove"
    fi

    echo ""
}

# Function to remove kernel module files
remove_kernel_module() {
    print_info "Removing kernel module files..."

    local module_dir="/lib/modules/${KERNEL_VERSION}/kernel/drivers/gpu/drm/mtt"
    local module_file="${module_dir}/sgpu_km.ko"

    if [ -f "$module_file" ]; then
        rm -f "$module_file"
        print_status "Kernel module file removed"

        # Clean up directory if empty
        if [ "$(ls -1 "$module_dir" 2>/dev/null | wc -l)" -eq 0 ]; then
            rmdir "$module_dir"
            print_status "Module directory removed"
        fi

        # Update module dependencies
        depmod -a
    else
        print_info "No kernel module file found to remove"
    fi

    echo ""
}

# Function to verify uninstallation
verify_uninstallation() {
    print_info "Verifying uninstallation..."

    local status=true

    # Check module is not loaded
    if ! lsmod | grep -q sgpu_km; then
        print_status "Kernel module is not loaded"
    else
        print_warning "Kernel module is still loaded"
        status=false
    fi

    # Check DKMS module is not registered (only if dkms is available)
    if command -v dkms &>/dev/null; then
        if ! dkms status | grep -q "mtt-s30/${DRIVER_VERSION}"; then
            print_status "DKMS module is not registered"
        else
            print_warning "DKMS module may still be registered"
            status=false
        fi
    fi

    # Check RPM packages are not installed
    local packages_found=false
    for pkg in mtt-s30-driver mtt-s30-dkms mtt-s30-firmware; do
        if rpm -q "$pkg" &>/dev/null; then
            print_warning "RPM package still installed: $pkg"
            packages_found=true
            status=false
        fi
    done

    if [ "$packages_found" = false ]; then
        print_status "No RPM packages are installed"
    fi

    echo ""

    if [ "$status" = true ]; then
        print_status "Uninstallation verified successfully!"
    else
        print_error "Uninstallation verification found issues"
        print_info "Please check the warnings above"
    fi

    echo ""
}

# Function to print summary
print_summary() {
    echo "============================================"
    echo "  MTT S30 GPU Driver Uninstallation Summary"
    echo "============================================"
    echo ""
    echo "Uninstallation completed with the following actions:"
    echo "  - Kernel module unloaded"
    echo "  - DKMS module unregistered"
    echo "  - RPM packages removed"
    echo "  - Firmware files removed"
    echo "  - Udev rules removed"
    echo "  - Modprobe configuration removed"
    echo "  - Systemd service removed"
    echo ""
    echo "Note: Some files may still exist if they are shared with other packages."
    echo "Manual cleanup may be required in:"
    echo "  - /lib/firmware/mthreads/ (if other MTT packages use it)"
    echo "  - /usr/src/mtt-s30-* (DKMS source directories)"
    echo ""
}

# Main function
main() {
    echo "=========================================="
    echo "  MTT S30 GPU Driver for Fedora Linux"
    echo "  Uninstall Script (Run as root)"
    echo "=========================================="
    echo ""
    echo "Detected:"
    echo "  Kernel Version: $KERNEL_VERSION"
    echo ""

    # Check root
    check_root

    # Unload module first
    unload_module

    # Remove DKMS
    remove_dkms

    # Remove RPM packages
    remove_rpm_packages

    # Remove kernel module files
    remove_kernel_module

    # Remove firmware
    remove_firmware

    # Remove udev rules
    remove_udev_rules

    # Remove modprobe config
    remove_modprobe

    # Remove systemd service
    remove_systemd_service

    # Verify uninstallation
    verify_uninstallation

    # Remove SDK if requested
    if [ "$REMOVE_SDK" = true ]; then
        remove_sdk
    fi

    # Print summary
    print_summary
}

# Print usage
usage() {
    echo "Usage: sudo $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help      Show this help message"
    echo "  -f, --force     Force uninstallation (remove all files)"
    echo "  -p, --purge     Purge all configuration and data files"
    echo "      --sdk       Also remove the MUSA SDK (MUSA_HOME, profile.d, ld.so.conf.d)"
    echo ""
    echo "Examples:"
    echo "  sudo $0              Standard uninstallation"
    echo "  sudo $0 -f          Force uninstallation"
    echo "  sudo $0 -p          Purge all files"
    echo "  sudo $0 --sdk       Uninstall driver and MUSA SDK"
    echo ""
}

# Parse arguments
FORCE=false
PURGE=false
REMOVE_SDK=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -f|--force)
            FORCE=true
            ;;
        -p|--purge)
            PURGE=true
            ;;
        --sdk)
            REMOVE_SDK=true
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
    shift
done

# Run main
main
