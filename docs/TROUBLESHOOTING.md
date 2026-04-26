# Troubleshooting Guide for MTT S30 GPU Driver on Fedora Linux

## Common Issues and Solutions

### 1. Module Load Failures

#### Symptom: `modprobe: FATAL: Module sgpu_km not found`

**Cause**: Module not built or not in module path

**Solution**:
```bash
# Build the module
cd Driver/kernel
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Install to kernel modules directory
sudo mkdir -p /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/mtt/
sudo cp sgpu_km.ko /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/mtt/
sudo depmod -a

# Try loading again
sudo modprobe sgpu_km
```

#### Symptom: `modprobe: ERROR: could not insert 'sgpu_km': Unknown symbol in module`

**Cause**: Missing kernel symbols or ABI mismatch

**Solution**:
```bash
# Check the specific symbol error
dmesg | tail -20

# Ensure matching kernel headers are installed
sudo dnf install -y kernel-devel-$(uname -r)

# Rebuild the module
cd Driver/kernel
make clean
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

Common missing symbols and their packages:
- `dma_fence_*`: `linux/dma-fence.h` - part of kernel DRM
- `pci_*`: `linux/pci.h` - part of kernel
- `filp_open`: `linux/fs.h` - part of kernel

#### Symptom: `modprobe: ERROR: could not insert 'sgpu_km': Operation not permitted`

**Cause**: Secure Boot enabled and module not signed

**Solution**:
Option 1: Disable Secure Boot in BIOS
Option 2: Sign the module
```bash
# Install signing tools
sudo dnf install -y pesign

# Create machine owner key (MOK)
openssl req -new -x509 -newkey rsa:2048 -keyout MOK.priv -outform DER -out MOK.der -nodes -days 3650 -subj "/CN=MTT S30 Driver/"

# Sign the module
sudo /usr/src/kernels/$(uname -r)/scripts/sign-file sha256 ./MOK.priv ./MOK.der sgpu_km.ko

# Enroll MOK in shim
sudo mokutil --import MOK.der
# Reboot and follow prompts to enroll MOK
```

### 2. GPU Detection Issues

#### Symptom: `lspci` shows GPU but driver doesn't detect it

**Diagnosis**:
```bash
# Check if GPU is detected by kernel
lspci -nn | grep -i moore

# Check if driver binds to device
lspci -k | grep -A 5 -i moore

# Check mtgpu driver (proprietary driver must be loaded first)
lsmod | grep mtgpu
```

**Solution**: The sGPU driver (`sgpu_km`) depends on the MTGPU driver (`mtgpu.ko`) being loaded first. This is a proprietary driver from Moore Threads.

1. Install the MTGPU driver first (from Moore Threads)
2. Load mtgpu module: `sudo modprobe mtgpu`
3. Then load sgpu_km: `sudo modprobe sgpu_km`

#### Symptom: PCI device not found

**Diagnosis**:
```bash
lspci | grep -i 1ed5
```

**Solution**: 
- Ensure GPU is properly seated
- Check BIOS settings for PCIe
- Try different PCIe slot
- Check `dmesg` for PCIe errors

### 3. Build Failures

#### Symptom: `make: *** /lib/modules/$(uname -r)/build: No such file or directory`

**Solution**:
```bash
# Install kernel development packages
sudo dnf install -y kernel-devel-$(uname -r) kernel-headers-$(uname -r)
```

#### Symptom: `error: implicit declaration of function`

**Cause**: Missing header includes or kernel API changes

**Solution**: Add the appropriate header or check kernel version compatibility. Common headers:
- `#include <linux/dma-fence.h>` for fence functions
- `#include <drm/drm.h>` for DRM functions
- `#include <linux/pci.h>` for PCI functions

#### Symptom: `error: unknown field` in structure definitions

**Cause**: Structure definitions changed in newer kernels

**Solution**: Update the code to use current kernel structures or use compatibility macros.

### 4. DKMS Issues

#### Symptom: `dkms: error: patch file not found`

**Solution**: Ensure DKMS configuration is correct:
```bash
# Check dkms.conf
cat /usr/src/mtt-s30-1.1.1/dkms.conf

# Should contain:
PACKAGE_NAME="mtt-s30"
PACKAGE_VERSION="1.1.1"
CLEAN="make clean KVER=${kernelver}"
MAKE[0]="make all KVER=${kernelver}"
BUILT_MODULE_NAME[0]="sgpu_km"
DEST_MODULE_LOCATION[0]="/updates"
AUTOINSTALL="yes"
```

#### Symptom: DKMS module not auto-rebuilding on kernel update

**Solution**:
```bash
# Check DKMS status
sudo dkms status

# If not registered, add it
sudo dkms add -m mtt-s30 -v 1.1.1
sudo dkms build -m mtt-s30 -v 1.1.1
sudo dkms install -m mtt-s30 -v 1.1.1
```

### 5. Permission Issues

#### Symptom: `Permission denied` accessing /dev/sgpu* or /dev/dri/*

**Solution**:
```bash
# Check current permissions
ls -la /dev/sgpu* /dev/dri/*

# Add user to video group
sudo usermod -aG video $USER
sudo usermod -aG render $USER

# Log out and log back in for group changes to take effect

# Alternatively, create udev rules
echo 'SUBSYSTEM=="drm", GROUP="video", MODE="0660"' | sudo tee /etc/udev/rules.d/99-mtt-s30.rules
echo 'SUBSYSTEM=="sgpu", GROUP="video", MODE="0660"' | sudo tee -a /etc/udev/rules.d/99-mtt-s30.rules

# Reload udev rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### 6. Firmware Issues

#### Symptom: `firmware: failed to load mthreads/musa.fw.*`

**Solution**:
```bash
# Check if firmware is installed
ls /lib/firmware/mthreads/

# Copy firmware files
sudo cp -r Driver/firmware/mthreads/* /lib/firmware/mthreads/
sudo chmod 644 /lib/firmware/mthreads/*

# Update initramfs
sudo dracut --force
```

#### Symptom: `direct firmware load for mthreads/musa.fw.*.* failed`

**Solution**: Firmware version mismatch. Install the correct version for your GPU.

### 7. Kernel Log Errors

#### Symptom: Various errors in `dmesg`

**Diagnosis**:
```bash
# View recent kernel messages
sudo dmesg -T | grep -i sgpu | tail -50

# View full log
journalctl -k | grep -i sgpu | tail -100
```

**Common Errors**:

1. **`failed to open node /dev/mtgpu.X`**
   - Cause: MTGPU driver not loaded
   - Solution: Load mtgpu.ko first, then sgpu_km.ko

2. **`failed to find device for gpu X`**
   - Cause: No GPU with that ID or mtgpu driver not bound
   - Solution: Check `lspci` and adjust `gpu_ids` module parameter

3. **`unknown gpu device arch`**
   - Cause: Unsupported device ID
   - Solution: Update device ID list in sgpu.c

### 8. Performance Issues

#### Symptom: Poor GPU performance

**Diagnosis**:
```bash
# Check scheduling parameters
grep sgpu /proc/driver/sgpu_km/*

# Check memory usage
cat /proc/driver/sgpu_km/cores
```

**Optimization**:
```bash
# Adjust module parameters
echo "options sgpu_km policy=1 time_slice_maximum=2000 time_slice_minimum=1000 overcommit_ratio=150" | sudo tee /etc/modprobe.d/mtt-s30-performance.conf

# Reload module
sudo rmmod sgpu_km
sudo modprobe sgpu_km
```

Policy options:
- 0: Best effort (default)
- 1: Fair preemption
- 2: Fair fixed

### 9. System Stability Issues

#### Symptom: Kernel panic or system freeze

**Diagnosis**:
```bash
# Check for kernel crashes
journalctl -k | grep -i "oops\|panic\|segfault" | tail -20

# Check system log
journalctl -b | grep -i error | tail -50
```

**Solution**:
- Enable debug logging: `sudo modprobe sgpu_km debug=1`
- Check `dmesg` for specific errors
- Try with fewer sGPU instances (reduce `max_inst` parameter)
- Disable overcommit: `overcommit_ratio=100`

### 10. Multi-GPU Issues

#### Symptom: Only one GPU detected when multiple are present

**Solution**:
```bash
# Check all MTT GPUs
lspci | grep -i moore

# Pass gpu_ids parameter
sudo modprobe sgpu_km total_gpu_num=2 gpu_ids=0,1
```

#### Symptom: sGPU instances not created across GPUs

**Solution**: Check the `total_gpu_num` and `gpu_ids` parameters:
```bash
# View current parameters
cat /sys/module/sgpu_km/parameters/total_gpu_num
cat /sys/module/sgpu_km/parameters/gpu_ids

# Set parameters
sudo modprobe -r sgpu_km
sudo modprobe sgpu_km total_gpu_num=2 gpu_ids=0,1
```

## Debug Mode

Enable verbose logging for troubleshooting:

```bash
# Build with debug enabled
cd Driver/kernel
make clean
make DEBUG=1 -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Load with debug
sudo modprobe -r sgpu_km
sudo modprobe sgpu_km

# View debug messages
dmesg -w | grep -i sgpu
```

## Collecting Information for Support

When reporting issues, please provide:

1. **System Information**
```bash
uname -a
cat /etc/fedora-release
```

2. **GPU Information**
```bash
lspci -vnn | grep -A 15 -i moore
```

3. **Driver Information**
```bash
modinfo sgpu_km
lsmod | grep sgpu_km
```

4. **Kernel Logs**
```bash
dmesg | grep -i sgpu | tail -100
```

5. **Module Parameters**
```bash
cat /sys/module/sgpu_km/parameters/*
```

6. **DKMS Status**
```bash
sudo dkms status
```

7. **Firmware Files**
```bash
ls -la /lib/firmware/mthreads/
```

## Known Limitations

1. **Proprietary Dependencies**: The sgpu_km driver requires the mtgpu.ko proprietary driver to be loaded first
2. **GPU Support**: Officially supports MTT S30, but may work with other Moore Threads GPUs
3. **Kernel Version**: Tested on Fedora 42-44 with kernel 6.x
4. **Wayland**: Graphics rendering may be limited under Wayland
5. **Suspend/Resume**: GPU may not properly resume after suspend to RAM
6. **Hibernation**: Not tested with hibernation

## Contact Support

For official support, contact Moore Threads:
- Developer Portal: https://developer.mthreads.com/
- Email: mt-sw-cloudcomputing@mthreads.com

For community support:
- GitHub Issues (if applicable)
- Fedora Forums
