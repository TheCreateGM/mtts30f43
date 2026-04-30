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

# Disable debuginfo for source-only package
%global debug_package %{nil}

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
# Nothing to build here - kernel module is built via DKMS

%install
# Install DKMS source
install -d %{buildroot}/usr/src/%{name}-%{version}/kernel/src
cp kernel/src/*.c kernel/src/*.h kernel/src/Makefile kernel/src/dkms.conf %{buildroot}/usr/src/%{name}-%{version}/kernel/src/ 2>/dev/null || true

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

# Udev rules
install -d %{buildroot}/usr/lib/udev/rules.d
cat > %{buildroot}/usr/lib/udev/rules.d/99-mtt-s30.rules << EOF
# Moore Threads MTT S30 sGPU udev rules
SUBSYSTEM=="drm", ATTR{vendor}=="0x1ed5", GROUP="video", MODE="0660"
SUBSYSTEM=="sgpu", GROUP="video", MODE="0660"
EOF

# Modprobe configuration
install -d %{buildroot}/etc/modprobe.d
cat > %{buildroot}/etc/modprobe.d/mtt-s30.conf << EOF
# Moore Threads MTT S30 sGPU Driver Configuration
# Load mtgpu first, then sgpu_km
# softdep sgpu_km pre: mtgpu
options sgpu_km total_gpu_num=1
EOF

# Systemd service for auto-loading
install -d %{buildroot}/usr/lib/systemd/system
cat > %{buildroot}/usr/lib/systemd/system/mtt-s30-driver.service << EOF
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
# Nothing to do in pre-install for DKMS package

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

# DKMS source
/usr/src/%{name}-%{version}/kernel/src/*.c
/usr/src/%{name}-%{version}/kernel/src/*.h
/usr/src/%{name}-%{version}/kernel/src/Makefile
/usr/src/%{name}-%{version}/kernel/src/dkms.conf
/usr/src/%{name}-%{version}/dkms.conf
/usr/src/%{name}-%{version}/mtgpu.h
/usr/src/%{name}-%{version}/mtgpu_errors.h
/usr/src/%{name}-%{version}/version.h

# Udev rules
%config /usr/lib/udev/rules.d/99-mtt-s30.rules

# Modprobe configuration
%config(noreplace) /etc/modprobe.d/mtt-s30.conf

# Systemd service
%config /usr/lib/systemd/system/mtt-s30-driver.service

%changelog
* Tue Apr 30 2024 Moore Threads <mt-sw-cloudcomputing@mthreads.com> - 1.1.1-1
- Initial RPM release for Fedora Linux
- Adapted from Ubuntu/Debian DKMS package
- Version 1.1.1 (sGPU driver)
