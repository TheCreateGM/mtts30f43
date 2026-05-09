# Architecture

## Overview

The MTT S30 GPU Driver for Fedora Linux consists of two kernel modules, userspace
tooling, firmware files, and system integration components.

```
 ┌─────────────────────────────────────────────────────┐
 │                  Userspace                           │
 │  mtt-tool (CLI)    mtt-monitor    MUSA Runtime       │
 └──────────┬──────────────────────────────┬────────────┘
            │ ioctl                        │ mmap / dma-buf
 ┌──────────▼──────────────────────────────▼────────────┐
 │              Kernel Space                              │
 │  ┌─────────────────┐    ┌─────────────────────────┐  │
 │  │   sgpu_km.ko    │◄──►│    mtgpu_stub.ko        │  │
 │  │  (sGPU driver)  │    │  (DRM stub / hwmon)     │  │
 │  └────────┬────────┘    └───────────┬─────────────┘  │
 │           │                         │                 │
 │    /proc/sgpu_km/            /dev/mtgpu.N            │
 │    /dev/sgpu-km              /sys/class/drm/cardN    │
 │                              /sys/class/hwmon/       │
 └──────────┬──────────────────────────────┬────────────┘
            │ PCI                          │ firmware
 ┌──────────▼──────────────────────────────▼────────────┐
 │                  Hardware                              │
 │              MTT S30 GPU (PCI 1ed5:0102/0103)          │
 │              + Firmware (/lib/firmware/mthreads/)      │
 └─────────────────────────────────────────────────────┘
```

## Kernel Modules

### mtgpu_stub (GPL v2)

A lightweight DRM stub driver that:
- Registers as a DRM driver (DRIVER_RENDER | DRIVER_GEM)
- Creates `/dev/dri/cardN` (recognized by nvtop/btop)
- Exposes `/dev/mtgpu.N` character devices for sgpu_km
- Provides hwmon interface for temperature/power monitoring
- Creates sysfs entries for GPU detection
- PCI IDs: 0x1ED5:0x0102, 0x1ED5:0x0103

**Source**: `kernel/src/mtgpu_stub.c`

### sgpu_km (Dual MIT/GPL)

The sGPU (shared GPU) kernel module that provides GPU virtualization:
- Physical GPU detection and initialization
- Virtual GPU instance management (up to 16 instances / GPU)
- IOCTL interface for userspace communication
- Memory allocation and tracking with overcommit support
- Fair scheduling for multi-tenant workloads
- Proc filesystem interface at `/proc/sgpu_km/`

**Module parameters**:
- `total_gpu_num=N` — Number of GPUs to manage (default: 1)
- `gpu_ids=ID1,ID2,...` — Specific GPU PCI IDs to bind

**Source files**:
| File | Purpose |
|------|---------|
| sgpu.c | Module init/exit, PCI driver registration |
| sgpu.h | Internal data structures and macros |
| sgpu_linux.c | Linux-specific OS wrappers |
| sgpu_linux.h | Linux-specific definitions |
| sgpu_ioctl.c | IOCTL handler dispatch |
| sgpu_sched.c | GPU scheduling policies |
| sgpu_procfs.c | Proc filesystem interface |
| sgpu_procfs_ioctl.c | Proc-mediated IOCTL forwarding |
| sgpu_fence.c | Fence/synchronization primitives |
| utils.c | Shared utility functions |
| utils.h | Utility macros and helpers |
| mtgpu.h | MTGPU hardware definitions |
| mtgpu_errors.h | Error code definitions |
| version.h | Driver version constants |

## Userspace Tools

### mtt-tool

A C CLI tool (`userspace/mtt-tool/mtt-tool.c`) that provides:
- `mtt-tool info` — Display GPU information via lspci + proc
- `mtt-tool list` — List device nodes and DRM devices
- `mtt-tool status` — Show module and driver status
- `mtt-tool monitor [N]` — Continuous GPU monitoring

### mtt-monitor

A Bash monitor script (`userspace/tools/mtt-monitor`) for real-time GPU metrics.

## System Integration

### udev (99-mtt-s30.rules)
Creates device nodes with proper group/mode for drm, mtgpu, and sgpu devices.

### SELinux (mtt_s30.te)
Policy module allowing module loading, device access, and firmware loading.

### systemd (mtt-s30-driver.service)
Oneshot service that loads modules at boot, before display-manager.

### DKMS
Source tree at `/usr/src/mtt-s30-{version}/` for automatic rebuild on kernel updates.

## Directory Layout

```
Driver/
├── kernel/src/       # Kernel module source
├── userspace/        # CLI tools and libraries
├── firmware/         # GPU firmware blobs
├── udev/             # udev rules
├── selinux/          # SELinux policy
├── systemd/          # systemd service files
├── packaging/rpm/    # RPM spec files
├── scripts/          # Build/install/uninstall/test scripts
└── docs/             # Documentation
```

## Boot Sequence

1. Kernel loads mtgpu_stub.ko (via softdep or systemd)
2. mtgpu_stub creates /dev/mtgpu.N and DRM device
3. Kernel loads sgpu_km.ko
4. sgpu_km opens /dev/mtgpu.N, initializes GPU
5. sgpu_km creates /proc/sgpu_km/ and /dev/sgpu-km
6. Userspace tools can now interact with the GPU

## License Notes

- **sgpu_km**: Dual MIT / GPL v2 (based on Moore Threads sGPU 1.1.1)
- **mtgpu_stub**: GPL v2 (custom stub, no proprietary code)
- **mtt-tool**: GPL v2
- **Firmware**: Proprietary (Moore Threads)
