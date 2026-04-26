# Moore Threads MTT S30 GPU Driver for Fedora Linux

## Overview

This is a Fedora-compatible GPU driver for the Moore Threads MTT S30 GPU. The driver is based on the official Moore Threads sGPU (Shared GPU) kernel module and has been adapted for Fedora Linux versions 42, 43, and 44.

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

### Prerequisites

```bash
# Fedora 42-44
sudo dnf install -y kernel-devel kernel-headers gcc make dkms rpm-build
```

### Build and Install

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

| Fedora Version | Kernel Version | Status |
|---------------|----------------|--------|
| Fedora 42 | 6.x | ✅ Supported |
| Fedora 43 | 6.x | ✅ Supported |
| Fedora 44 | 6.x | ✅ Supported |

## License

This driver is based on the Moore Threads sGPU driver (version 1.1.1) which is copyright © 2022-2024 Moore Threads Inc. All rights reserved.

The adaptation for Fedora Linux is provided as-is without warranty.

## Issues and Limitations

1. **Binary Firmware**: Requires pre-built firmware files from Moore Threads
2. **Propietary Components**: The MTGPU kernel module (mtgpu.ko) is proprietary and not included
3. **Userspace Dependencies**: MUSA runtime libraries are required for full functionality
4. **GPU Support**: Only tested with MTT S30 GPUs, other models may require additional configuration

## Contributing

Contributions are welcome! Please report issues and submit pull requests with improvements.

## References

- [Moore Threads Developer Portal](https://developer.mthreads.com/)
- [MUSA Documentation](https://developer.mthreads.com/documentation/)
- [MTT S30 Product Page](https://developer.mthreads.com/products/mtt-s30/)
