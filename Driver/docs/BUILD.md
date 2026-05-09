# Build Guide

## Prerequisites

### Fedora 42-44
```bash
sudo dnf install -y kernel-devel kernel-headers gcc make dkms rpm-build
```

Verify kernel headers:
```bash
ls /lib/modules/$(uname -r)/build
```

## Quick Build

```bash
# From Driver/ root:
./scripts/build.sh

# Build kernel module only:
./scripts/build.sh -k

# Build RPM packages only:
./scripts/build.sh -r

# Clean build artifacts:
./scripts/build.sh -c
```

## Manual Kernel Module Build

```bash
cd kernel/src
make
```

Module output:
- `sgpu_km.ko` — sGPU virtualization module
- `mtgpu_stub.ko` — DRM stub driver

## RPM Package Build

```bash
./scripts/build.sh -r
```

RPMs are produced in `build/rpm/`:
- `mtt-s30-firmware-1.1.1-1.noarch.rpm`
- `mtt-s30-driver-1.1.1-1.x86_64.rpm`
- `mtt-s30-dkms-1.1.1-1.noarch.rpm`

### Build options

Pass kernel version for cross-build:
```bash
make KVER=6.8.5-301.fc40.x86_64
```

Enable debug:
```bash
make DEBUG=1
```

## Userspace Tool Build

```bash
cd userspace/mtt-tool
make
sudo make install
```

## COPR Build

The project builds automatically via COPR:
https://copr.fedorainfracloud.org/coprs/axogm/mtt-s30-driver/

## Common Build Issues

| Issue | Solution |
|-------|----------|
| `kernel-devel not found` | Install matching kernel-devel: `sudo dnf install kernel-devel-$(uname -r)` |
| `Module.symvers missing` | Build the running kernel's modules first or run `make modules_prepare` |
| `implicit declaration` | Kernel API changed; check kernel version compatibility |
| `SELinux: denied module load` | Install SELinux policy: `sudo semodule -i mtt_s30.pp` |
