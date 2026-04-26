# Installation Guide for MTT S30 GPU Driver on Fedora Linux

## Overview

This guide provides step-by-step instructions for installing the Moore Threads MTT S30 GPU driver on Fedora Linux versions 42, 43, and 44.

## Prerequisites

### Required Packages

Before installation, ensure the following packages are installed:

```bash
# Update system
sudo dnf update -y

# Install build dependencies
sudo dnf install -y \
    kernel-devel \
    kernel-headers \
    gcc \
    make \
    dkms \
    rpm-build \
    rpmdevtools \
    git \
    tar \
    xz \
    wget \
    curl \
    pciutils \
    elfutils-libelf-devel
```

### Verify GPU Detection

Check if your MTT S30 GPU is detected:

```bash
lspci | grep -i moore
```

Expected output (MTT S30):
```
01:00.0 VGA compatible controller: Moore Threads MTT S30 [MTT S30]
```

Check PCI device IDs:
```bash
lspci -nn | grep -i moore
```

Expected output:
- MTT S30 (2-core): `Vendor: 1ed5, Device: 102`
- MTT S30 (4-core): `Vendor: 1ed5, Device: 103`

### Kernel Version Compatibility

Verify your kernel version is compatible:

```bash
uname -r
```

The driver supports Fedora kernels 6.0 and later.

## Installation Methods

There are two installation methods available:

### Method 1: Automatic Installation (Recommended)

The easiest way to install the driver is using the provided `install.sh` script:

```bash
# Make scripts executable
chmod +x build.sh install.sh uninstall.sh tests/test.sh

# Build the driver packages
./build.sh

# Install the driver (requires root)
sudo ./install.sh
```

This script will:
1. Build the kernel module
2. Install firmware files
3. Set up DKMS
4. Load the kernel module
5. Create necessary device nodes

### Method 2: Manual Installation

For advanced users who want to install components separately.

#### Step 1: Install Firmware

Copy firmware files to the system firmware directory:

```bash
sudo cp -r Driver/firmware/mthreads/* /lib/firmware/mthreads/
sudo chmod 644 /lib/firmware/mthreads/*
```

Verify firmware:
```bash
ls -la /lib/firmware/mthreads/
```

#### Step 2: Build and Install Kernel Module

Build the kernel module:

```bash
cd Driver/kernel
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

Install the kernel module:

```bash
sudo cp sgpu_km.ko /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/mtt/
sudo depmod -a
```

Load the module:

```bash
sudo modprobe sgpu_km
```

Verify the module is loaded:

```bash
lsmod | grep sgpu_km
dmesg | grep sgpu
```

#### Step 3: DKMS Installation

To ensure the driver is automatically rebuilt on kernel updates:

```bash
# Install DKMS package
sudo dnf install -y ./packaging/rpm/mtt-s30-dkms-1.1.1-1.fc$(rpm -E %fedora).noarch.rpm

# Or manually add to DKMS
sudo cp -r Driver/kernel /usr/src/mtt-s30-1.1.1
sudo dkms add -m mtt-s30 -v 1.1.1
sudo dkms build -m mtt-s30 -v 1.1.1
sudo dkms install -m mtt-s30 -v 1.1.1
```

#### Step 4: Install RPM Packages

Install the built RPM packages:

```bash
# Install main driver package
sudo dnf install -y ./mtt-s30-driver-1.1.1-1.fc$(rpm -E %fedora).x86_64.rpm

# Install firmware package
sudo dnf install -y ./mtt-s30-firmware-1.1.1-1.fc$(rpm -E %fedora).noarch.rpm

# Install DKMS package
sudo dnf install -y ./mtt-s30-dkms-1.1.1-1.fc$(rms -E %fedora).noarch.rpm
```

## Post-Installation Verification

### Check Module Loading

```bash
# Check if module is loaded
lsmod | grep sgpu_km

# Check kernel messages
sudo dmesg | grep -i sgpu

# Check for errors
sudo dmesg | grep -i error
```

### Check Device Nodes

```bash
# Check for sGPU device nodes
ls -la /dev/sgpu*

# Check for DRM device nodes
ls -la /dev/dri/by-path/ | grep -i pci
```

### Verify GPU Detection

```bash
# Check lspci
lspci -vnn | grep -A 12 -i moore

# Check driver binding
lspci -k | grep -A 5 -i moore
```

## Configuration

### Module Parameters

The following module parameters are available:

| Parameter | Description | Default | Example |
|-----------|-------------|---------|---------|
| `total_gpu_num` | Total number of GPUs to use | 1 | `total_gpu_num=2` |
| `gpu_ids` | Array of GPU IDs to use | none | `gpu_ids=0,1` |

To set module parameters:

```bash
# Temporarily set parameters at load time
sudo modprobe sgpu_km total_gpu_num=2 gpu_ids=0,1

# Permanently set parameters (create config file)
echo "options sgpu_km total_gpu_num=2 gpu_ids=0,1" | sudo tee /etc/modprobe.d/mtt-s30.conf
```

### Persist Module Loading

To load the module at boot:

```bash
# Create systemd service
echo '[Unit]
Description=Load MTT S30 GPU Driver
After=sysinit.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/sbin/modprobe sgpu_km

[Install]
WantedBy=multi-user.target' | sudo tee /etc/systemd/system/mtt-s30-driver.service

# Enable the service
sudo systemctl enable mtt-s30-driver.service
sudo systemctl start mtt-s30-driver.service
```

## Uninstallation

### Using uninstall.sh

```bash
sudo ./uninstall.sh
```

### Manual Uninstallation

```bash
# Unload module
sudo rmmod sgpu_km

# Remove DKMS
sudo dkms remove -m mtt-s30 -v 1.1.1 --all

# Remove firmware
sudo rm -rf /lib/firmware/mthreads/

# Remove device nodes (if any)
sudo rm -f /dev/sgpu*

# Remove packages
sudo dnf remove -y mtt-s30-driver mtt-s30-dkms mtt-s30-firmware
```

## Troubleshooting

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for common issues and solutions.

### Quick Checks

1. **Module fails to load**: Check `dmesg` for errors
   ```bash
   sudo dmesg | tail -50
   ```

2. **GPU not detected**: Verify PCI detection
   ```bash
   lspci | grep -i moore
   ```

3. **DKMS build fails**: Check kernel headers
   ```bash
   sudo dnf install -y kernel-devel-$(uname -r)
   ```

4. **Firmware missing**: Verify firmware files
   ```bash
   ls /lib/firmware/mthreads/
   ```

## SELinux Considerations

Fedora uses SELinux by default. The driver should work with SELinux in enforcing mode, but if you encounter issues:

```bash
# Check SELinux status
getenforce

# Temporarily set to permissive mode (for testing)
sudo setenforce 0

# Permanently set to permissive mode (edit /etc/selinux/config)
# SELINUX=permissive
```

## Secure Boot

If Secure Boot is enabled, you may need to sign the kernel module:

```bash
# Install signing tools
sudo dnf install -y pesign

# Generate keys (follow Fedora documentation)
# Sign the module
pesign -S -k /path/to/key.db -c "MTT S30 Driver" sgpu_km.ko
```

## Known Issues

1. **Kernel API changes**: The driver may need small adjustments for newer kernel versions
2. **Proprietary dependencies**: The MTGPU kernel module (`mtgpu.ko`) is proprietary and not included in this package
3. **Wayland**: Graphics support may be limited under Wayland
4. **Suspend/Resume**: GPU may not properly resume after suspend

## Support

For support, please refer to:
- [Moore Threads Developer Portal](https://developer.mthreads.com/)
- [MTT S30 Documentation](https://developer.mthreads.com/docs/mtt-s30/)
- GitHub Issues (if applicable)
