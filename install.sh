#!/bin/bash
#
# MTT S30 GPU Driver Install Script
# Fedora Linux compatible
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.
# Adapted for Fedora by: Fedora Driver Project
#

set -e

# ── Root check — must happen before any system files are touched ──────────────
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: must run as root (sudo)"
    exit 1
fi

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Configuration
DRIVER_VERSION="1.1.1"
KERNEL_VERSION=$(uname -r)
DKMS_ENABLED=1   # default: attempt DKMS registration when dkms is present

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

# Function to check for GPU
check_gpu() {
    print_info "Checking for Moore Threads GPU..."

    local gpu_info
    gpu_info=$(lspci | grep -i moore || true)

    if [ -n "$gpu_info" ]; then
        print_status "GPU detected:"
        echo "$gpu_info"
    else
        print_warning "No Moore Threads GPU detected"
        print_info "The driver may still install, but GPU functionality requires hardware"
    fi

    local device_ids
    device_ids=$(lspci -nn | grep -i "1ed5:" | grep -v "Audio" || true)
    if [ -n "$device_ids" ]; then
        print_status "Supported devices found:"
        echo "$device_ids"
    fi

    echo ""
}

# Function to check dependencies
check_dependencies() {
    print_info "Checking dependencies..."

    local missing=()

    command -v gcc &>/dev/null || missing+=("gcc")
    command -v make &>/dev/null || missing+=("make")
    [ -d "/lib/modules/${KERNEL_VERSION}/build" ] || missing+=("kernel-devel-${KERNEL_VERSION}")

    if [ ${#missing[@]} -gt 0 ]; then
        print_warning "Missing dependencies: ${missing[*]}"
        print_info "Install with: sudo dnf install -y ${missing[*]}"
    else
        print_status "All dependencies satisfied"
    fi

    echo ""
}

# Function to build kernel module
build_module() {
    print_info "Building kernel module..."

    local src_dir="${SCRIPT_DIR}/kernel/src"

    if [ ! -d "$src_dir" ]; then
        print_error "Kernel source directory not found: $src_dir"
        exit 1
    fi

    make -C /lib/modules/${KERNEL_VERSION}/build M="${src_dir}" modules 2>&1
    if [ $? -eq 0 ]; then
        print_status "Kernel module built successfully"
    else
        print_error "Failed to build kernel module"
        exit 1
    fi

    echo ""
}

# Function to install kernel module
install_module() {
    print_info "Installing kernel module..."

    local src_dir="${SCRIPT_DIR}/kernel/src"
    local dest_dir="/lib/modules/${KERNEL_VERSION}/kernel/drivers/gpu/drm/mtt"

    mkdir -p "$dest_dir"
    cp "${src_dir}/sgpu_km.ko" "${dest_dir}/"
    cp "${src_dir}/mtgpu_stub.ko" "${dest_dir}/"
    depmod -a

    print_status "Kernel modules installed to ${dest_dir}"
    echo ""
}

# Function to install firmware
install_firmware() {
    print_info "Installing firmware files..."

    local fw_src="${SCRIPT_DIR}/firmware/mthreads"
    local fw_dest="/lib/firmware/mthreads"

    if [ -d "$fw_src" ]; then
        mkdir -p "$fw_dest"
        cp -r "${fw_src}/"* "${fw_dest}/"
        chmod 644 "${fw_dest}/"*
        print_status "Firmware installed to ${fw_dest}"

        # Apply SELinux file contexts to the firmware directory
        apply_selinux_contexts

        # Update initramfs
        if command -v dracut &>/dev/null; then
            dracut --force 2>/dev/null || print_warning "Failed to update initramfs"
        fi
    else
        print_warning "Firmware directory not found: $fw_src"
    fi

    echo ""
}

# Function to install udev rules
install_udev_rules() {
    print_info "Installing udev rules..."

    cat > /etc/udev/rules.d/99-mtt-s30.rules << 'EOF'
# Moore Threads MTT S30 GPU udev rules
SUBSYSTEM=="drm", KERNEL=="card*", ATTRS{vendor}=="0x1ed5", GROUP="video", MODE="0660"
SUBSYSTEM=="drm", KERNEL=="renderD*", ATTRS{vendor}=="0x1ed5", GROUP="render", MODE="0660"
SUBSYSTEM=="mtgpu", GROUP="video", MODE="0660"
KERNEL=="sgpu-km", GROUP="video", MODE="0660"
KERNEL=="sgpu-km-ctl", GROUP="video", MODE="0660"
KERNEL=="mtgpu.*", GROUP="video", MODE="0660"
EOF

    udevadm control --reload-rules
    udevadm trigger
    print_status "Udev rules installed"
    echo ""
}

# Function to install modprobe config
install_modprobe_config() {
    print_info "Installing modprobe configuration..."

    cat > /etc/modprobe.d/mtt-s30.conf << 'EOF'
# Moore Threads MTT S30 GPU Driver Configuration
# Load mtgpu_stub before sgpu_km
softdep sgpu_km pre: mtgpu_stub
# Default module parameters
options sgpu_km total_gpu_num=1
EOF

    print_status "Modprobe configuration installed"
    echo ""
}

# Function to install systemd service
install_systemd_service() {
    print_info "Installing systemd service..."

    cat > /etc/systemd/system/mtt-s30-driver.service << 'EOF'
[Unit]
Description=Moore Threads MTT S30 GPU Driver
After=systemd-modules-load.service
Before=display-manager.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/sbin/modprobe mtgpu_stub
ExecStart=/sbin/modprobe sgpu_km
ExecStop=/sbin/modprobe -r sgpu_km
ExecStop=/sbin/modprobe -r mtgpu_stub

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable mtt-s30-driver.service
    print_status "Systemd service installed and enabled"
    echo ""
}

# Function to load modules
load_modules() {
    print_info "Loading kernel modules..."

    # Load mtgpu_stub first (creates /dev/mtgpu.* devices)
    if ! lsmod | grep -q mtgpu_stub; then
        modprobe mtgpu_stub && print_status "mtgpu_stub loaded" || print_warning "Failed to load mtgpu_stub"
        sleep 1
    else
        print_info "mtgpu_stub already loaded"
    fi

    # Load sgpu_km
    if ! lsmod | grep -q sgpu_km; then
        modprobe sgpu_km && print_status "sgpu_km loaded" || print_warning "Failed to load sgpu_km"
    else
        print_info "sgpu_km already loaded"
    fi

    echo ""
}

# Function to verify installation
verify_installation() {
    print_info "Verifying installation..."

    # Check modules loaded
    lsmod | grep -q mtgpu_stub && print_status "mtgpu_stub module loaded" || print_warning "mtgpu_stub not loaded"
    lsmod | grep -q sgpu_km && print_status "sgpu_km module loaded" || print_warning "sgpu_km not loaded"

    # Check proc interface (at /proc/sgpu_km/)
    [ -d "/proc/sgpu_km" ] && print_status "Proc interface available at /proc/sgpu_km/" || print_warning "Proc interface not found"

    # Check device nodes
    [ -e "/dev/mtgpu.0" ] && print_status "/dev/mtgpu.0 device node exists" || print_warning "/dev/mtgpu.0 not found"
    [ -e "/dev/sgpu-km" ] && print_status "/dev/sgpu-km device node exists" || print_warning "/dev/sgpu-km not found"

    # Check firmware
    [ -d "/lib/firmware/mthreads" ] && print_status "Firmware installed" || print_warning "Firmware not found"

    echo ""
}

# Function to register kernel modules with DKMS
register_dkms() {
    if [ "${DKMS_ENABLED}" -eq 0 ]; then
        print_info "DKMS registration skipped (--no-dkms)"
        return
    fi

    if ! command -v dkms &>/dev/null; then
        print_warning "dkms not found, skipping DKMS registration"
        print_info "Install with: sudo dnf install -y dkms"
        echo ""
        return
    fi

    print_info "Registering kernel modules with DKMS..."

    local dkms_src="/usr/src/mtt-s30-${DRIVER_VERSION}"

    # Copy kernel source tree into the DKMS source directory
    rm -rf "${dkms_src}"
    cp -r "${SCRIPT_DIR}/kernel" "${dkms_src}"

    # Ensure dkms.conf is present (should already be in kernel/src/)
    if [ ! -f "${dkms_src}/dkms.conf" ] && [ -f "${SCRIPT_DIR}/kernel/src/dkms.conf" ]; then
        cp "${SCRIPT_DIR}/kernel/src/dkms.conf" "${dkms_src}/"
    fi

    # dkms add
    if dkms status "mtt-s30/${DRIVER_VERSION}" 2>/dev/null | grep -q "mtt-s30"; then
        print_info "DKMS module mtt-s30/${DRIVER_VERSION} already added, skipping add step"
    else
        dkms add -m mtt-s30 -v "${DRIVER_VERSION}" && \
            print_status "DKMS: module added" || \
            { print_warning "DKMS add failed, skipping DKMS registration"; echo ""; return; }
    fi

    # dkms build
    dkms build -m mtt-s30 -v "${DRIVER_VERSION}" -k "${KERNEL_VERSION}" && \
        print_status "DKMS: module built for kernel ${KERNEL_VERSION}" || \
        { print_warning "DKMS build failed, skipping DKMS install"; echo ""; return; }

    # dkms install
    dkms install -m mtt-s30 -v "${DRIVER_VERSION}" -k "${KERNEL_VERSION}" && \
        print_status "DKMS: module installed for kernel ${KERNEL_VERSION}" || \
        print_warning "DKMS install failed"

    echo ""
}

# Function to detect Secure Boot and warn if modules are unsigned
check_secure_boot() {
    print_info "Checking Secure Boot status..."

    if ! command -v mokutil &>/dev/null; then
        print_info "mokutil not found, skipping Secure Boot check"
        echo ""
        return
    fi

    local sb_state
    sb_state=$(mokutil --sb-state 2>/dev/null || true)

    if echo "${sb_state}" | grep -qi "SecureBoot enabled"; then
        # Check whether the modules carry a signature
        local unsigned=()
        local src_dir="${SCRIPT_DIR}/kernel/src"
        for ko in sgpu_km.ko mtgpu_stub.ko; do
            local ko_path="${src_dir}/${ko}"
            if [ -f "${ko_path}" ]; then
                local sig_algo
                sig_algo=$(modinfo --field=sig_hashalgo "${ko_path}" 2>/dev/null || true)
                if [ -z "${sig_algo}" ]; then
                    unsigned+=("${ko}")
                fi
            fi
        done

        if [ ${#unsigned[@]} -gt 0 ]; then
            print_warning "Secure Boot is ENABLED and the following modules are unsigned: ${unsigned[*]}"
            echo ""
            echo "  To sign the modules for Secure Boot, follow these steps:"
            echo ""
            echo "  1. Generate a Machine Owner Key (MOK) pair:"
            echo "       openssl req -new -x509 -newkey rsa:2048 -keyout /root/mok.key \\"
            echo "         -out /root/mok.crt -days 3650 -subj '/CN=MTT S30 MOK/' -nodes"
            echo ""
            echo "  2. Enroll the public key with MOK manager:"
            echo "       mokutil --import /root/mok.crt"
            echo "     (Reboot and confirm enrollment in the MOK manager UI)"
            echo ""
            echo "  3. Sign each module:"
            local sign_script="/usr/src/linux-headers-${KERNEL_VERSION}/scripts/sign-file"
            [ -f "${sign_script}" ] || sign_script="/lib/modules/${KERNEL_VERSION}/build/scripts/sign-file"
            for ko in "${unsigned[@]}"; do
                echo "       ${sign_script} sha256 /root/mok.key /root/mok.crt ${SCRIPT_DIR}/kernel/src/${ko}"
            done
            echo ""
            echo "  4. Re-run this installer after signing."
            echo ""
        else
            print_status "Secure Boot is enabled and all modules are signed"
        fi
    else
        print_status "Secure Boot is not enabled (or status unknown)"
    fi

    echo ""
}

# Function to apply SELinux file contexts to firmware and detect AVC denials
apply_selinux_contexts() {
    print_info "Applying SELinux file contexts to firmware..."

    if ! command -v restorecon &>/dev/null; then
        print_info "restorecon not found, skipping SELinux context restoration"
        echo ""
        return
    fi

    restorecon -r /lib/firmware/mthreads/ 2>/dev/null && \
        print_status "SELinux contexts restored on /lib/firmware/mthreads/" || \
        print_warning "restorecon returned non-zero for /lib/firmware/mthreads/"

    # Check for recent AVC denials related to mthreads/sgpu/mtgpu
    if command -v ausearch &>/dev/null; then
        local avcs
        avcs=$(ausearch -m avc -ts recent 2>/dev/null | grep -E "mthreads|sgpu|mtgpu" || true)
        if [ -n "${avcs}" ]; then
            print_warning "SELinux AVC denials detected:"
            echo "${avcs}"
            echo ""
            echo "  To generate a local policy module allowing these operations, run:"
            echo "    ausearch -m avc -ts recent | audit2allow -M mtt-s30-local"
            echo "    semodule -i mtt-s30-local.pp"
            echo ""
        fi
    fi

    echo ""
}

# Main function
main() {
    echo "=========================================="
    echo "  MTT S30 GPU Driver for Fedora Linux"
    echo "  Install Script (Run as root)"
    echo "=========================================="
    echo ""
    echo "Detected:"
    echo "  Kernel Version: $KERNEL_VERSION"
    echo ""

    check_root
    check_secure_boot
    check_gpu
    check_dependencies
    build_module
    install_module
    register_dkms
    install_firmware
    install_udev_rules
    install_modprobe_config
    install_systemd_service
    load_modules
    # Install mtt-monitor tool
    if [ -f "${SCRIPT_DIR}/tools/mtt-monitor" ]; then
        cp "${SCRIPT_DIR}/tools/mtt-monitor" /usr/local/bin/mtt-monitor
        chmod +x /usr/local/bin/mtt-monitor
        print_status "mtt-monitor installed to /usr/local/bin/"
    fi

    verify_installation

    echo "=========================================="
    echo "  Installation Complete!"
    echo "=========================================="
    echo ""
    echo "The MTT S30 GPU driver has been installed."
    echo ""
    echo "Useful commands:"
    echo "  Check modules:    lsmod | grep -E 'sgpu|mtgpu'"
    echo "  Check GPU:        lspci | grep -i moore"
    echo "  Check proc:       ls /proc/sgpu_km/"
    echo "  Check devices:    ls /dev/mtgpu* /dev/sgpu*"
    echo "  View logs:        journalctl -k | grep -i sgpu"
    echo ""
    echo "Run tests: sudo ./tests/test.sh"
    echo "Monitor GPU: mtt-monitor"
    echo ""
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            echo "Usage: sudo $0 [--dkms] [--no-dkms]"
            exit 0
            ;;
        --dkms)
            DKMS_ENABLED=1
            ;;
        --no-dkms)
            DKMS_ENABLED=0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
    shift
done

main
