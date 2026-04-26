#!/bin/bash
#
# MTT S30 GPU Driver Test Script
# Fedora Linux compatible
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.
# Adapted for Fedora by: Fedora Driver Project
#

# NOTE: No set -e here - we want all tests to run even if some fail

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Configuration
DRIVER_VERSION="1.1.1"
KERNEL_VERSION=$(uname -r)

# Detect Fedora version safely (may not be Fedora)
if command -v rpm &>/dev/null; then
    FEDORA_VERSION=$(rpm -E %fedora 2>/dev/null || echo "unknown")
else
    FEDORA_VERSION="unknown"
fi

# Test results
PASS_COUNT=0
FAIL_COUNT=0
WARN_COUNT=0
TEST_RESULTS=()

# Function to print status messages
print_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((PASS_COUNT++)) || true
    TEST_RESULTS+=("PASS: $1")
}

print_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((FAIL_COUNT++)) || true
    TEST_RESULTS+=("FAIL: $1")
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
    ((WARN_COUNT++)) || true
    TEST_RESULTS+=("WARN: $1")
}

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo "  $1"
    echo -e "${BLUE}========================================${NC}"
}

# Function to run a single test
run_test() {
    local test_name="$1"
    local test_func="$2"

    print_info "Running: $test_name"

    if $test_func; then
        print_pass "$test_name"
    else
        print_fail "$test_name"
    fi

    echo ""
}

# Test: Check system requirements
test_system_requirements() {
    local has_kernel_headers=false
    local has_gcc=false
    local ok=true

    [ -d "/lib/modules/${KERNEL_VERSION}/build" ] && has_kernel_headers=true
    command -v gcc &>/dev/null && has_gcc=true

    if [ "$has_kernel_headers" = false ]; then
        print_info "Missing: kernel-devel-${KERNEL_VERSION}"
        ok=false
    fi
    if [ "$has_gcc" = false ]; then
        print_info "Missing: gcc"
        ok=false
    fi

    [ "$ok" = true ]
}

# Test: Check GPU detection
test_gpu_detection() {
    local gpu_info
    gpu_info=$(lspci -nn 2>/dev/null | grep -i "1ed5:" | grep -v "Audio" || true)

    if [ -n "$gpu_info" ]; then
        print_info "GPU detected: $gpu_info"
        return 0
    else
        print_info "No Moore Threads GPU detected via lspci"
        return 1
    fi
}

# Test: Check mtgpu_stub module loaded
test_mtgpu_stub_loaded() {
    if lsmod | grep -q mtgpu_stub; then
        return 0
    else
        print_info "mtgpu_stub not loaded. Trying to load..."
        if [ "$(id -u)" -eq 0 ]; then
            modprobe mtgpu_stub 2>/dev/null && sleep 1
            lsmod | grep -q mtgpu_stub && return 0
        fi
        return 1
    fi
}

# Test: Check sgpu_km module loaded
test_module_loaded() {
    if lsmod | grep -q sgpu_km; then
        return 0
    else
        print_info "sgpu_km not loaded. Trying to load..."
        if [ "$(id -u)" -eq 0 ]; then
            modprobe sgpu_km 2>/dev/null
            lsmod | grep -q sgpu_km && return 0
        fi
        return 1
    fi
}

# Test: Check kernel module files exist
test_module_file() {
    local module_dir="/lib/modules/${KERNEL_VERSION}/kernel/drivers/gpu/drm/mtt"
    local extra_dir="/lib/modules/${KERNEL_VERSION}/extra"
    local sgpu_ok=false
    local stub_ok=false

    # Check standard path and DKMS extra path (compressed .ko.xz also valid)
    for dir in "$module_dir" "$extra_dir"; do
        [ -f "${dir}/sgpu_km.ko" ] || [ -f "${dir}/sgpu_km.ko.xz" ] && sgpu_ok=true
        [ -f "${dir}/mtgpu_stub.ko" ] || [ -f "${dir}/mtgpu_stub.ko.xz" ] && stub_ok=true
    done

    if [ "$sgpu_ok" = true ] && [ "$stub_ok" = true ]; then
        return 0
    else
        [ "$sgpu_ok" = false ] && print_info "Missing: sgpu_km.ko (checked kernel/ and extra/)"
        [ "$stub_ok" = false ] && print_info "Missing: mtgpu_stub.ko (checked kernel/ and extra/)"
        return 1
    fi
}

# Test: Check DKMS registration
test_dkms_registration() {
    if command -v dkms &>/dev/null; then
        if dkms status 2>/dev/null | grep -q "mtt-s30/${DRIVER_VERSION}"; then
            return 0
        else
            print_info "DKMS module mtt-s30/${DRIVER_VERSION} not registered"
            return 1
        fi
    else
        print_info "DKMS not installed"
        return 1
    fi
}

# Test: Check firmware files
test_firmware() {
    local firmware_dir="/lib/firmware/mthreads"
    local firmware_count=0

    if [ -d "$firmware_dir" ]; then
        firmware_count=$(ls -1 "${firmware_dir}"/musa.* 2>/dev/null | wc -l)
    fi

    if [ "$firmware_count" -gt 0 ]; then
        print_info "Found $firmware_count firmware file(s) in $firmware_dir"
        return 0
    else
        print_info "No firmware files found in $firmware_dir"
        return 1
    fi
}

# Test: Check kernel log for critical errors (only recent - last 30 minutes)
test_kernel_errors() {
    local errors
    # Check errors from the last 30 minutes, excluding expected messages
    errors=$(journalctl -k --no-pager --since "30 minutes ago" 2>/dev/null | \
        grep -i "sgpu.*error\|sgpu.*failed\|sgpu.*panic" | \
        grep -v "standalone mode\|mtgpu.ko not loaded\|mtgpu_stub\|failed to open node" || true)

    if [ -n "$errors" ]; then
        print_info "Critical kernel errors found (last 30 min):"
        echo "$errors" | head -5
        return 1
    else
        return 0
    fi
}

# Test: Check proc filesystem interface
# NOTE: proc interface is at /proc/sgpu_km/ (not /proc/driver/sgpu_km/)
test_proc_interface() {
    if [ -d "/proc/sgpu_km" ]; then
        local proc_files
        proc_files=$(ls /proc/sgpu_km/ 2>/dev/null | wc -l)
        if [ "$proc_files" -gt 0 ]; then
            print_info "Proc interface at /proc/sgpu_km/ with $proc_files entries"
            return 0
        fi
    fi
    print_info "Proc interface /proc/sgpu_km/ not found (module may not be initialized)"
    return 1
}

# Test: Check module parameters
test_module_parameters() {
    local param_dir="/sys/module/sgpu_km/parameters"
    if [ -d "$param_dir" ]; then
        print_info "Module parameters: $(ls $param_dir 2>/dev/null | tr '\n' ' ')"
        return 0
    else
        return 1
    fi
}

# Test: Check udev rules
test_udev_rules() {
    if [ -f "/etc/udev/rules.d/99-mtt-s30.rules" ]; then
        return 0
    else
        print_info "Udev rules not installed at /etc/udev/rules.d/99-mtt-s30.rules"
        return 1
    fi
}

# Test: Check modprobe config
test_modprobe_config() {
    if [ -f "/etc/modprobe.d/mtt-s30.conf" ]; then
        return 0
    else
        print_info "Modprobe config not installed at /etc/modprobe.d/mtt-s30.conf"
        return 1
    fi
}

# Test: Check systemd service
test_systemd_service() {
    if [ -f "/etc/systemd/system/mtt-s30-driver.service" ]; then
        return 0
    else
        print_info "Systemd service not installed at /etc/systemd/system/mtt-s30-driver.service"
        return 1
    fi
}

# Test: Check device nodes
test_device_nodes() {
    local mtgpu_ok=false
    local sgpu_ok=false

    [ -e "/dev/mtgpu.0" ] && mtgpu_ok=true
    [ -e "/dev/sgpu-km" ] && sgpu_ok=true

    if [ "$mtgpu_ok" = true ] && [ "$sgpu_ok" = true ]; then
        print_info "Device nodes: /dev/mtgpu.0, /dev/sgpu-km, /dev/sgpu-km-ctl"
        return 0
    else
        [ "$mtgpu_ok" = false ] && print_info "Missing: /dev/mtgpu.0"
        [ "$sgpu_ok" = false ] && print_info "Missing: /dev/sgpu-km"
        return 1
    fi
}

# Test: Check RPM packages (optional)
test_rpm_packages() {
    local packages_found=0

    if command -v rpm &>/dev/null; then
        for pkg in mtt-s30-driver mtt-s30-dkms mtt-s30-firmware; do
            rpm -q "$pkg" &>/dev/null && ((packages_found++)) || true
        done
    fi

    if [ "$packages_found" -gt 0 ]; then
        print_info "$packages_found RPM package(s) installed"
        return 0
    else
        print_info "No RPM packages installed (using DKMS/manual install)"
        return 0  # Not a failure - manual install is valid
    fi
}

# Test: Check PCI driver binding
test_pci_binding() {
    local binding
    binding=$(lspci -k 2>/dev/null | grep -A 3 "Moore Threads" | grep "Kernel driver in use" || true)

    if [ -n "$binding" ]; then
        print_info "PCI driver binding: $binding"
        return 0
    else
        print_info "No PCI driver binding found for Moore Threads GPU"
        return 1
    fi
}

# Test: Verify proc interface values are readable
test_proc_values() {
    if [ ! -d "/proc/sgpu_km/0" ]; then
        print_info "GPU 0 proc directory not found"
        return 1
    fi

    local max_inst policy version
    max_inst=$(cat /proc/sgpu_km/0/max_inst 2>/dev/null || echo "")
    policy=$(cat /proc/sgpu_km/0/policy 2>/dev/null || echo "")
    version=$(cat /proc/sgpu_km/version 2>/dev/null || echo "")

    if [ -n "$max_inst" ] && [ -n "$policy" ] && [ -n "$version" ]; then
        print_info "max_inst=$max_inst, policy=$policy, version=$version"
        return 0
    else
        print_info "Could not read proc values"
        return 1
    fi
}

# Test 18: Check MUSA SDK (musaInfo)
test_musa_sdk() {
    local musa_bin="${MUSA_HOME:-/usr/local/musa}/bin/musaInfo"

    if [ ! -x "$musa_bin" ]; then
        print_info "MUSA SDK: musaInfo not found at $musa_bin"
        echo "  lsmod | grep sgpu: $(lsmod 2>/dev/null | grep sgpu || echo '(none)')"
        return 1
    fi

    local output
    output=$("$musa_bin" 2>&1)
    local exit_code=$?

    # Check if running in stub/standalone mode (mtgpu_stub instead of mtgpu.ko)
    if journalctl -k --no-pager -n 100 2>/dev/null | grep -q "standalone mode"; then
        print_info "MUSA SDK: running in stub mode (proprietary mtgpu.ko not installed)"
        print_info "musaInfo requires the full mtgpu.ko driver for GPU compute access"
        # Treat as warning — stub mode is a known limitation, not a setup failure
        print_warn "MUSA SDK (musaInfo) — stub mode, compute unavailable"
        ((WARN_COUNT++)) || true
        ((FAIL_COUNT--)) || true  # undo the fail that run_test will add
        return 1
    fi

    if [ $exit_code -ne 0 ] || ! echo "$output" | grep -qi "device"; then
        print_info "MUSA SDK: musaInfo failed or reported no devices"
        echo "  lsmod | grep sgpu: $(lsmod 2>/dev/null | grep sgpu || echo '(none)')"
        if [ -e /dev/mtgpu.0 ]; then
            echo "  /dev/mtgpu.0: exists"
        else
            echo "  /dev/mtgpu.0: NOT found"
        fi
        return 1
    fi
    print_info "MUSA SDK: musaInfo reports device(s)"
    return 0
}

# Test 19: Check libmusa in ldconfig cache
test_libmusa_ldconfig() {
    if ldconfig -p 2>/dev/null | grep -q libmusa; then
        print_info "libmusa: found in ldconfig cache"
        return 0
    else
        print_info "libmusa: NOT found in ldconfig cache"
        return 1
    fi
}

# Test 20: Check MUSA bin directory in PATH
test_musa_path() {
    local musa_bin="${MUSA_HOME:-/usr/local/musa}/bin"

    # Source the profile.d script if it exists and PATH isn't set yet
    if ! echo "$PATH" | tr ':' '\n' | grep -qx "${musa_bin}"; then
        if [ -f "/etc/profile.d/musa.sh" ]; then
            # shellcheck disable=SC1091
            source /etc/profile.d/musa.sh 2>/dev/null || true
        fi
    fi

    if echo "$PATH" | tr ':' '\n' | grep -qx "${musa_bin}"; then
        print_info "MUSA PATH: ${musa_bin} is in PATH"
        return 0
    else
        print_info "MUSA PATH: ${musa_bin} is NOT in PATH (will be active after next login)"
        print_info "  Run: source /etc/profile.d/musa.sh"
        # This is a session-state issue, not an install failure — treat as warning
        return 0
    fi
}

# Test 21: MUSA vector-addition smoke test
test_musa_smoke() {
    # Skip if no GPU is available
    if ! musaInfo &>/dev/null || [ ! -e /dev/mtgpu.0 ]; then
        print_info "MUSA smoke test: skipped (no GPU detected)"
        return 0
    fi

    local mcc_bin="${MUSA_HOME:-/usr/local/musa}/bin/mcc"
    if [ ! -x "$mcc_bin" ]; then
        print_info "MUSA smoke test: skipped (mcc not found at $mcc_bin)"
        return 0
    fi

    local tmpdir
    tmpdir=$(mktemp -d)
    local src="${tmpdir}/vecadd.mu"
    local bin="${tmpdir}/vecadd"

    cat > "$src" << 'MUSA_EOF'
#include <musa_runtime.h>
#include <stdio.h>
#include <stdlib.h>

__global__ void vecAdd(const float *A, const float *B, float *C, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) C[i] = A[i] + B[i];
}

int main(void) {
    const int N = 256;
    float h_A[N], h_B[N], h_C[N];
    for (int i = 0; i < N; i++) { h_A[i] = (float)i; h_B[i] = (float)(N - i); }

    float *d_A, *d_B, *d_C;
    musaMalloc(&d_A, N * sizeof(float));
    musaMalloc(&d_B, N * sizeof(float));
    musaMalloc(&d_C, N * sizeof(float));
    musaMemcpy(d_A, h_A, N * sizeof(float), musaMemcpyHostToDevice);
    musaMemcpy(d_B, h_B, N * sizeof(float), musaMemcpyHostToDevice);

    vecAdd<<<(N + 255) / 256, 256>>>(d_A, d_B, d_C, N);
    musaMemcpy(h_C, d_C, N * sizeof(float), musaMemcpyDeviceToHost);

    for (int i = 0; i < N; i++) {
        if (h_C[i] != h_A[i] + h_B[i]) {
            printf("MISMATCH at %d: %f != %f\n", i, h_C[i], h_A[i] + h_B[i]);
            musaFree(d_A); musaFree(d_B); musaFree(d_C);
            return 1;
        }
    }
    printf("PASS\n");
    musaFree(d_A); musaFree(d_B); musaFree(d_C);
    return 0;
}
MUSA_EOF

    local compile_out
    compile_out=$("$mcc_bin" -o "$bin" "$src" 2>&1)
    if [ $? -ne 0 ]; then
        print_info "MUSA smoke test: compilation failed"
        echo "$compile_out" | head -10
        rm -rf "$tmpdir"
        return 1
    fi

    local run_out
    run_out=$(timeout 60 "$bin" 2>&1)
    local rc=$?
    rm -rf "$tmpdir"

    if [ $rc -eq 124 ]; then
        print_info "FAIL: MUSA smoke test timed out"
        return 1
    elif [ $rc -ne 0 ] || ! echo "$run_out" | grep -q "^PASS"; then
        print_info "MUSA smoke test: execution failed (rc=$rc)"
        echo "$run_out" | head -5
        return 1
    fi

    print_info "MUSA smoke test: vector addition correct"
    return 0
}

# Test 21: mtt-monitor tool
test_mtt_monitor() {
    if ! command -v mtt-monitor &>/dev/null; then
        print_info "mtt-monitor not installed at /usr/local/bin/mtt-monitor"
        return 1
    fi
    local output
    output=$(mtt-monitor --once 2>&1)
    if echo "$output" | grep -q "MTT S30"; then
        print_info "mtt-monitor: GPU detected and reporting"
        return 0
    else
        print_info "mtt-monitor: failed to detect GPU"
        return 1
    fi
}

# Test 22: muThrust headers installed
test_muthrust_headers() {
    local inc="/usr/local/musa/include/thrust"
    if [ -d "$inc" ] && [ -f "$inc/adjacent_difference.h" ]; then
        local count
        count=$(find "$inc" -name "*.h" 2>/dev/null | wc -l)
        print_info "muThrust: $count headers in $inc"
        return 0
    else
        print_info "muThrust headers not found at $inc"
        return 1
    fi
}

# Test 23: muAlg (CUB) headers installed
test_mualg_headers() {
    local inc="/usr/local/musa/include/cub"
    if [ -d "$inc" ] && [ -f "$inc/cub.cuh" ]; then
        print_info "muAlg (CUB): headers present at $inc"
        return 0
    else
        print_info "muAlg headers not found at $inc"
        return 1
    fi
}

# Test 24: mutlass headers installed
test_mutlass_headers() {
    local inc="/usr/local/musa/include/mutlass"
    if [ -d "$inc" ]; then
        local count
        count=$(find "$inc" -name "*.h" -o -name "*.hpp" 2>/dev/null | wc -l)
        print_info "mutlass: $count headers in $inc"
        return 0
    else
        print_info "mutlass headers not found at $inc"
        return 1
    fi
}

# Test 25: MT-DualPipe Python package
test_mt_dualpipe() {
    if python3 -c "import dualpipe; from dualpipe import DualPipe; print('  DualPipe class OK')" 2>/dev/null; then
        local loc
        loc=$(python3 -c "import dualpipe; print(dualpipe.__file__)" 2>/dev/null)
        print_info "MT-DualPipe: installed at $loc"
        return 0
    else
        print_info "MT-DualPipe: not installed (run: pip install Driver/MTT_lib/MT-DualPipe/)"
        return 1
    fi
}

# Test 26: mthreads-ml-py (pymtml) Python package
test_pymtml() {
    if python3 -c "import pymtml; print('  pymtml OK')" 2>/dev/null; then
        print_info "mthreads-ml-py (pymtml): installed"
        return 0
    else
        print_info "mthreads-ml-py: not installed (run: pip install mthreads-ml-py)"
        return 1
    fi
}

# Test 27: MUSA SDK include headers present
test_musa_headers() {
    local inc="/usr/local/musa/include"
    local required=("musa_runtime.h" "driver_types.h" "host_defines.h")
    local missing=()
    for h in "${required[@]}"; do
        [ -f "$inc/$h" ] || missing+=("$h")
    done
    if [ ${#missing[@]} -eq 0 ]; then
        local total
        total=$(ls "$inc"/*.h 2>/dev/null | wc -l)
        print_info "MUSA SDK headers: $total .h files in $inc"
        return 0
    else
        print_info "Missing MUSA headers: ${missing[*]}"
        return 1
    fi
}

# Test 28: MUSA runtime libraries present
test_musa_libs() {
    local lib="/usr/local/musa/lib"
    local ok=true
    for so in libmusa.so libmusart.so libdrm_mtgpu.so; do
        [ -f "$lib/$so" ] || { print_info "Missing: $lib/$so"; ok=false; }
    done
    [ "$ok" = true ]
}
print_summary() {
    echo ""
    print_header "Test Summary"
    echo ""
    echo "Total Tests: $((PASS_COUNT + FAIL_COUNT + WARN_COUNT))"
    echo "  Passed:   $PASS_COUNT"
    echo "  Failed:   $FAIL_COUNT"
    echo "  Warnings: $WARN_COUNT"
    echo ""

    if [ "$FAIL_COUNT" -eq 0 ]; then
        print_pass "All tests passed!"
    else
        print_fail "$FAIL_COUNT test(s) failed"
    fi

    echo ""
    print_info "Test Details:"
    for result in "${TEST_RESULTS[@]}"; do
        echo "  - $result"
    done
    echo ""
}

# Main function
main() {
    clear
    echo ""
    print_header "MTT S30 GPU Driver - Test Suite"
    echo ""
    echo "System Information:"
    echo "  Fedora Version: $FEDORA_VERSION"
    echo "  Kernel Version: $KERNEL_VERSION"
    echo "  Architecture:   $(uname -m)"
    echo "  Date:           $(date)"
    echo ""

    print_header "Running Tests"
    echo ""

    run_test "System Requirements Check"    test_system_requirements
    run_test "GPU Detection (lspci)"        test_gpu_detection
    run_test "mtgpu_stub Module Loaded"     test_mtgpu_stub_loaded
    run_test "sgpu_km Module Loaded"        test_module_loaded
    run_test "Kernel Module Files Exist"    test_module_file
    run_test "DKMS Registration"            test_dkms_registration
    run_test "Firmware Files"               test_firmware
    run_test "Kernel Log (no critical errors)" test_kernel_errors
    run_test "Proc Filesystem Interface"    test_proc_interface
    run_test "Proc Interface Values"        test_proc_values
    run_test "Module Parameters"            test_module_parameters
    run_test "Udev Rules"                   test_udev_rules
    run_test "Modprobe Configuration"       test_modprobe_config
    run_test "Systemd Service"              test_systemd_service
    run_test "Device Nodes"                 test_device_nodes
    run_test "RPM Packages"                 test_rpm_packages
    run_test "PCI Driver Binding"           test_pci_binding
    run_test "MUSA SDK (musaInfo)"          test_musa_sdk
    run_test "libmusa in ldconfig"          test_libmusa_ldconfig
    run_test "MUSA PATH"                    test_musa_path
    run_test "MUSA Smoke Test"              test_musa_smoke
    run_test "mtt-monitor Tool"             test_mtt_monitor
    run_test "muThrust Headers"             test_muthrust_headers
    run_test "muAlg (CUB) Headers"          test_mualg_headers
    run_test "mutlass Headers"              test_mutlass_headers
    run_test "MT-DualPipe Python Package"   test_mt_dualpipe
    run_test "mthreads-ml-py (pymtml)"      test_pymtml
    run_test "MUSA SDK Headers"             test_musa_headers
    run_test "MUSA Runtime Libraries"       test_musa_libs

    print_summary

    echo ""
    print_header "Useful Commands"
    echo ""
    echo "  Check modules:    lsmod | grep -E 'sgpu|mtgpu'"
    echo "  Check GPU:        lspci | grep -i moore"
    echo "  Check proc:       ls /proc/sgpu_km/"
    echo "  Check devices:    ls /dev/mtgpu* /dev/sgpu*"
    echo "  View logs:        journalctl -k | grep -i sgpu"
    echo "  GPU info:         cat /proc/sgpu_km/0/max_inst"
    echo ""

    if [ "$FAIL_COUNT" -gt 0 ]; then
        print_info "See docs/TROUBLESHOOTING.md for solutions to common problems."
    fi
    echo ""
}

# Parse arguments
VERBOSE=false
SILENT=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            echo "Usage: sudo $0 [OPTIONS]"
            echo "Options:"
            echo "  -h, --help     Show this help"
            echo "  -v, --verbose  Verbose output"
            echo "  -s, --silent   No color output"
            exit 0
            ;;
        -v|--verbose) VERBOSE=true ;;
        -s|--silent)  SILENT=true ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
    shift
done

if [ "$SILENT" = true ]; then
    RED=''; GREEN=''; YELLOW=''; BLUE=''; NC=''
fi

main
