#
# RPM Specification for Moore Threads MTT S30 Driver
# Fedora Linux compatible
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.
# Adapted for Fedora by: Fedora Driver Project
#

Name:           mtt-s30-driver
Version:        1.1.1
Release:        1%{?dist}
Summary:        Moore Threads MTT S30 sGPU Kernel Module

License:        Proprietary
URL:            https://developer.mthreads.com/
Source0:        %{name}-%{version}.tar.gz

# Vendor information
Vendor:         Moore Threads
Packager:       Moore Threads <mt-sw-cloudcomputing@mthreads.com>

# Build architecture
BuildArch:      x86_64
ExclusiveArch:  x86_64

# Dependencies
Requires:      kernel >= 6.0
Requires:      dkms >= 1.95
Requires:      pciutils
Requires:      pkgconfig

# Conflicts
Conflicts:     mtt-s30-dkms < %{version}
Conflicts:     sgpu-dkms

# Auto-load on install
%define _use_internal_dependency_generator 0
%define __find_requires /usr/lib/rpm/find-requires
%define __find_provides /usr/lib/rpm/find-provides

%description
The Moore Threads MTT S30 sGPU Driver provides virtualization capabilities
for Moore Threads GPUs, enabling multiple virtual GPU instances to share
a single physical GPU. This package contains the kernel module and
configuration files for the sGPU driver.

Features:
- Support for MTT S30 (2-core and 4-core variants)
- Support for other Moore Threads GPUs (S10, S50, S60, S70, S80, S100, etc.)
- Memory management with overcommit support
- Scheduling policies for multi-tenant GPU usage
- Proc filesystem interface for monitoring

Note: This driver requires the MTGPU proprietary driver (mtgpu.ko) to be
loaded first for proper functionality.

%prep
%setup -q -n %{name}-%{version}

%build
# Build the kernel module
cd kernel/src
make clean
make all KVER=%{_kernel_ver}

%install
# Install kernel module
install -D -m 644 kernel/src/sgpu_km.ko \
    %{buildroot}%{_libdir}/modules/%{_kernel_ver}/kernel/drivers/gpu/drm/mtt/sgpu_km.ko

# Install DKMS source
install -d %{buildroot}/usr/src
cp -r kernel %{buildroot}/usr/src/%{name}-%{version}

# Install dkms.conf
install -D -m 644 kernel/src/dkms.conf \
    %{buildroot}/usr/src/%{name}-%{version}/dkms.conf

# Install module files
install -D -m 644 kernel/src/mtgpu.h \
    %{buildroot}/usr/src/%{name}-%{version}/mtgpu.h
install -D -m 644 kernel/src/mtgpu_errors.h \
    %{buildroot}/usr/src/%{name}-%{version}/mtgpu_errors.h
install -D -m 644 kernel/src/version.h \
    %{buildroot}/usr/src/%{name}-%{version}/version.h

# Install udev rules
install -d %{buildroot}%{_udevrulesdir}
cat > %{buildroot}%{_udevrulesdir}/99-mtt-s30.rules << EOF
# Moore Threads MTT S30 sGPU udev rules
SUBSYSTEM=="drm", ATTR{vendor]=="0x1ed5", GROUP="video", MODE="0660"
SUBSYSTEM=="sgpu", GROUP="video", MODE="0660"
EOF

# Install modprobe configuration
install -d %{buildroot}%{_sysconfdir}/modprobe.d
cat > %{buildroot}%{_sysconfdir}/modprobe.d/mtt-s30.conf << EOF
# Moore Threads MTT S30 sGPU Driver Configuration
# Load mtgpu first, then sgpu_km
# softdep sgpu_km pre: mtgpu
options sgpu_km total_gpu_num=1
EOF

# Install systemd service for auto-loading
install -d %{buildroot}%{_unitdir}
cat > %{buildroot}%{_unitdir}/mtt-s30-driver.service << EOF
[Unit]
Description=Load Moore Threads MTT S30 GPU Driver
After=sysinit.target
Before=multi-user.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/sbin/modprobe sgpu_km

[Install]
WantedBy=multi-user.target
EOF

%pre
# Check for required kernel headers
if [ ! -d "/lib/modules/%{_kernel_ver}/build" ]; then
    echo "Error: kernel-devel-%{_kernel_ver} is required but not installed."
    echo "Please install it with: sudo dnf install kernel-devel-%{_kernel_ver}"
    exit 1
fi

%post
# Register with DKMS
if [ -x /usr/sbin/dkms ]; then
    /usr/sbin/dkms add -m %{name} -v %{version} || :
    /usr/sbin/dkms build -m %{name} -v %{version} || :
    /usr/sbin/dkms install -m %{name} -v %{version} || :
fi

# Reload udev rules
/usr/bin/udevadm control --reload-rules || :
/usr/bin/udevadm trigger || :

# Print installation message
echo ""
echo "Moore Threads MTT S30 Driver installed successfully."
echo "To load the module manually: sudo modprobe sgpu_km"
echo "To enable auto-loading: sudo systemctl enable mtt-s30-driver.service"
echo ""

%postun
# Unregister from DKMS
if [ "$1" = "0" ]; then
    if [ -x /usr/sbin/dkms ]; then
        /usr/sbin/dkms remove -m %{name} -v %{version} --all || :
    fi
fi

# Reload udev rules on removal
/usr/bin/udevadm control --reload-rules || :

%preun
# Unload module before removal
if [ "$1" = "0" ]; then
    /sbin/rmmod sgpu_km 2>/dev/null || :
fi

%files
%license LICENSE
%doc README.md docs/

# Kernel module
%{_libdir}/modules/%{_kernel_ver}/kernel/drivers/gpu/drm/mtt/sgpu_km.ko

# DKMS source
/usr/src/%{name}-%{version}/kernel/src/*.c
/usr/src/%{name}-%{version}/kernel/src/*.h
/usr/src/%{name}-%{version}/kernel/src/Makefile
/usr/src/%{name}-%{version}/kernel/src/dkms.conf
/usr/src/%{name}-%{version}/mtgpu.h
/usr/src/%{name}-%{version}/mtgpu_errors.h
/usr/src/%{name}-%{version}/version.h

# Udev rules
%config(%{_udevrulesdir}) %{_udevrulesdir}/99-mtt-s30.rules

# Modprobe configuration
%config(noreplace) %{_sysconfdir}/modprobe.d/mtt-s30.conf

# Systemd service
%config %{_unitdir}/mtt-s30-driver.service

%changelog
* Tue Apr 30 2024 Moore Threads <mt-sw-cloudcomputing@mthreads.com> - 1.1.1-1
- Initial RPM release for Fedora Linux
- Adapted from Ubuntu/Debian DKMS package
- Version 1.1.1 (sGPU driver)
