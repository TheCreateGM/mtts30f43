# Moore Threads MTT S30 GPU Driver for Fedora Linux

## Overview

This is a Fedora-compatible GPU driver for the Moore Threads MTT S30 GPU. The driver is based on the official Moore Threads sGPU (Shared GPU) kernel module and has been adapted for Fedora Linux versions 42, 43, and 44.

## COPR Repository (Recommended Installation)

The easiest way to install the driver is via the official COPR repository:

```bash
# Enable the COPR repository
sudo dnf copr enable axogm/mtt-s30-driver

# Install the driver packages
sudo dnf install mtt-s30-firmware mtt-s30-driver mtt-s30-dkms

# Load the kernel module
sudo modprobe sgpu_km
```

**COPR Project:** https://copr.fedorainfracloud.org/coprs/axogm/mtt-s30-driver/

**Available Packages:**
- `mtt-s30-firmware` - Firmware files for MTT GPUs
- `mtt-s30-driver` - Kernel module source and configuration files
- `mtt-s30-dkms` - DKMS package for automatic kernel module rebuilding

**Supported Architectures:** x86_64

## Features

- **Kernel Module**: sGPU kernel module (`sgpu_km`) for virtualizing Moore Threads GPUs
- **Multiple GPU Support**: Supports MTT S30 (2-core and 4-core variants), S10, S50, S60, S70, S80, S100, S1000, S2000, S3000, S4000, S5000, and S6000
- **Memory Management**: Virtual memory management with overcommit support
- **Scheduling**: Fair scheduling policies for multi-tenant GPU usage
- **Firmware Support**: Includes necessary firmware files for MTT GPUs
- **DKMS Integration**: Dynamic Kernel Module Support for automatic rebuild on kernel updates

## Architecture

### Kernel Module (sgpu_km)
The kernel module provides the core functionality:
- PCI device detection and initialization
- sGPU instance management (up to 16 instances per GPU)
- Memory allocation and tracking
- IOCTL interface for userspace communication
- Scheduling and resource management

### Userspace Components
- Library interfaces for MUSA (Moore Threads Unified System Architecture)
- Management utilities

### Supported Devices

| Device ID | Device Name | Architecture |
|-----------|-------------|--------------|
| 0x102 | MTT S30 (2-core) | SUDI |
| 0x103 | MTT S30 (4-core) | SUDI |
| 0x101 | MTT S10 | SUDI |
| 0x105 | MTT S50 | SUDI |
| 0x106 | MTT S60 | SUDI |
| 0x111 | MTT S100 | SUDI |
| 0x122 | MTT S1000 | SUDI |
| 0x123 | MTT S2000 | SUDI |
| 0x200 | QuYuan 1 | QuYuan |
| 0x201 | MTT S80 | QuYuan |
| 0x202 | MTT S70 | QuYuan |
| 0x222 | MTT S3000 | QuYuan |

## Directory Structure

```
Driver/
├── kernel/                    # Kernel module source
│   └── src/                  # Source files
│       ├── sgpu.c           # Main driver initialization
│       ├── sgpu.h           # Header definitions
│       ├── sgpu_linux.c    # Linux-specific implementations
│       ├── sgpu_linux.h    # Linux-specific headers
│       ├── sgpu_ioctl.c    # IOCTL handling
│       ├── sgpu_sched.c    # Scheduling logic
│       ├── sgpu_procfs.c   # Proc filesystem interface
│       ├── sgpu_fence.c    # Fence synchronization
│       ├── utils.c         # Utility functions
│       ├── mtgpu.h         # Moore Threads GPU definitions
│       ├── mtgpu_errors.h  # Error codes
│       ├── version.h       # Version information
│       └── Makefile        # Build configuration
│
├── userspace/                # Userspace components
│   ├── lib/                # Libraries
│   └── tools/              # CLI tools
│
├── firmware/                 # Firmware files
│   └── mthreads/           # Moore Threads firmware
│
├── build/                   # Build scripts and makefiles
│
├── packaging/                # Package definitions
│   └── rpm/                # RPM spec files
│
└── docs/                    # Documentation
    ├── INSTALL.md         # Installation guide
    ├── BUILD.md           # Build instructions
    └── TROUBLESHOOTING.md  # Troubleshooting guide
```

## Quick Start

### Option 1: Install from COPR (Recommended)

```bash
# Enable the COPR repository
sudo dnf copr enable axogm/mtt-s30-driver

# Install the driver packages
sudo dnf install mtt-s30-firmware mtt-s30-driver mtt-s30-dkms

# Load the kernel module
sudo modprobe sgpu_km

# Verify installation
lspci | grep -i moore
lsmod | grep sgpu_km
dmesg | grep sgpu
```

### Option 2: Build from Source

#### Prerequisites

```bash
# Fedora 42-44
sudo dnf install -y kernel-devel kernel-headers gcc make dkms rpm-build
```

#### Build and Install

```bash
# Step 1: Build the driver
./build.sh

# Step 2: Install the driver
sudo ./install.sh

# Step 3: Load the kernel module
sudo modprobe sgpu_km

# Step 4: Verify installation
lspci | grep -i moore
lsmod | grep sgpu_km
dmesg | grep sgpu
```

### Uninstall

```bash
# If installed via COPR
sudo dnf remove mtt-s30-firmware mtt-s30-driver mtt-s30-dkms
sudo dnf copr disable axogm/mtt-s30-driver

# If installed from source
sudo ./uninstall.sh
```

## Fedora-Specific Adaptations

This driver has been adapted from the original Ubuntu/Debian packages to work with Fedora:

1. **Package Format**: Uses RPM (.rpm) instead of Debian (.deb)
2. **Kernel ABI**: Compatible with Fedora kernel versions
3. **Library Paths**: Uses Fedora standard library paths
4. **DKMS Integration**: Uses Fedora's DKMS infrastructure
5. **Systemd Integration**: Systemd service files for autoloading
6. **SELinux**: SELinux policy support included

## Compatibility Matrix

| Fedora Version | Kernel Version | Architecture | COPR Status |
|---------------|----------------|--------------|-------------|
| Fedora 42 | 6.x | x86_64 | [![COPR Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://copr.fedorainfracloud.org/coprs/axogm/mtt-s30-driver/) |
| Fedora 43 | 6.x | x86_64 | [![COPR Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://copr.fedorainfracloud.org/coprs/axogm/mtt-s30-driver/) |
| Fedora 44 | 6.x | x86_64 | [![COPR Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://copr.fedorainfracloud.org/coprs/axogm/mtt-s30-driver/) |

## License

This driver is based on the Moore Threads sGPU driver (version 1.1.1) which is copyright © 2022-2024 Moore Threads Inc. All rights reserved.

The adaptation for Fedora Linux is provided as-is without warranty.

## Issues and Limitations

1. **Binary Firmware**: Requires pre-built firmware files from Moore Threads
2. **Propietary Components**: The MTGPU kernel module (mtgpu.ko) is proprietary and not included
3. **Userspace Dependencies**: MUSA runtime libraries are required for full functionality
4. **GPU Support**: Only tested with MTT S30 GPUs, other models may require additional configuration

## Building RPM Packages

You can build RPM packages locally for development or testing:

```bash
# Create source tarballs
mkdir -p build/rpm/SOURCES

# Driver package
tar --exclude='*.o' --exclude='*.ko' --exclude='.*.cmd' --exclude='.git' \
    -czf build/rpm/SOURCES/mtt-s30-driver-1.1.1.tar.gz \
    kernel/ LICENSE README.md docs/ packaging/

# Firmware package  
tar -czf build/rpm/SOURCES/mtt-s30-firmware-1.1.1.tar.gz \
    firmware/ LICENSE README.md docs/

# DKMS package
tar --exclude='*.o' --exclude='*.ko' --exclude='.*.cmd' --exclude='.git' \
    -czf build/rpm/SOURCES/mtt-s30-dkms-1.1.1.tar.gz \
    kernel/ LICENSE README.md docs/ packaging/

# Build SRPMs
rpmbuild -bs packaging/rpm/mtt-s30-driver.spec \
    --define "_topdir $(pwd)/build/rpm" \
    --define "_sourcedir $(pwd)/build/rpm/SOURCES"

rpmbuild -bs packaging/rpm/mtt-s30-firmware.spec \
    --define "_topdir $(pwd)/build/rpm" \
    --define "_sourcedir $(pwd)/build/rpm/SOURCES"

rpmbuild -bs packaging/rpm/mtt-s30-dkms.spec \
    --define "_topdir $(pwd)/build/rpm" \
    --define "_sourcedir $(pwd)/build/rpm/SOURCES"

# Build RPMs
rpmbuild -bb packaging/rpm/mtt-s30-driver.spec \
    --define "_topdir $(pwd)/build/rpm" \
    --define "_sourcedir $(pwd)/build/rpm/SOURCES"
```

## Contributing

Contributions are welcome! Please report issues and submit pull requests with improvements.

## References

- [COPR Repository](https://copr.fedorainfracloud.org/coprs/axogm/mtt-s30-driver/)
- [Moore Threads Developer Portal](https://developer.mthreads.com/)
- [MUSA Documentation](https://developer.mthreads.com/documentation/)
- [MTT S30 Product Page](https://developer.mthreads.com/products/mtt-s30/)
