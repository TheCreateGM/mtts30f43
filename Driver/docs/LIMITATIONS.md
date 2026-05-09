# Limitations

## Hardware

- **Tested GPUs**: Only MTT S30 (PCI IDs 0x1ED5:0x0102 and 0x0103). Other
  Moore Threads GPUs (S10, S50-S6000, QuYuan) are listed in the driver but
  not tested on Fedora.
- **Single GPU**: Primary testing with one GPU. Multi-GPU configurations
  may require parameter tuning (`total_gpu_num=N`).
- **Resizable BAR**: Not required but recommended for optimal performance.

## Software

### Kernel Module

- **mtgpu.ko dependency**: The sgpu_km module requires a proprietary MTGPU
  driver (`mtgpu.ko`) for full hardware access. This proprietary module is
  NOT included. `mtgpu_stub.ko` provides basic DRM registration but may
  lack some hardware-specific functionality.
- **Kernel ABI**: Modules compiled for one kernel version may not load on
  another. DKMS is strongly recommended.
- **Secure Boot**: Kernel modules must be signed for Secure Boot systems.
  The build/install scripts warn if Secure Boot is detected without signatures.
- **module.symvers**: If the kernel was built with different options, the
  modules may fail due to symbol version mismatches.

### sGPU Virtualization

- **Maximum instances**: 16 virtual GPU instances per physical GPU.
- **Memory overcommit**: The driver supports overcommit; actual available
  memory depends on physical GPU memory and workload.
- **Scheduling**: Fair scheduling is provided but no QoS guarantees.
- **No GPU reset**: If a virtual instance crashes, the physical GPU may
  need a full module reload.

### Userspace

- **MUSA Runtime**: Required for full GPU compute. Not included in this
  package. Available from the Moore Threads developer portal.
- **mtt-tool**: Basic monitoring only. No GPU configuration or tuning.
  Requires root or video group membership for device access.
- **mtt-monitor**: Bash script, requires proc interface to be mounted.

### Fedora-Specific

- **SELinux**: The provided SELinux policy (`selinux/mtt_s30.te`) is a
  starting point. Custom workloads may trigger AVC denials that require
  policy updates.
- **Wayland**: DRM stub may not expose all properties expected by
  Wayland compositors. The GPU is primarily for compute, not display.
- **DKMS**: Only registers with DKMS if dkms package is installed *before*
  running the installer. Install DKMS first: `sudo dnf install dkms`.

## Performance

- **Not benchmarked**: No performance guarantees. The sGPU module may
  introduce virtualization overhead.
- **Power management**: Basic hwmon support via mtgpu_stub. Advanced
  power management features may not be exposed.

## Support

- This is a community adaptation of the Moore Threads sGPU driver (v1.1.1).
- Official support: Moore Threads developer portal (https://developer.mthreads.com/)
- Fedora packaging issues: COPR project page (https://copr.fedorainfracloud.org/coprs/axogm/mtt-s30-driver/)
