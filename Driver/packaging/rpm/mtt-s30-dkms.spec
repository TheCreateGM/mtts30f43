#
# RPM Specification for Moore Threads MTT S30 DKMS Package
# Fedora Linux compatible
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.
# Adapted for Fedora by: Fedora Driver Project
#

Name:           mtt-s30-dkms
Version:        1.1.1
Release:        1%{?dist}
Summary:        Moore Threads MTT S30 sGPU Driver DKMS Package

License:        Proprietary
URL:            https://developer.mthreads.com/
Source0:        %{name}-%{version}.tar.gz

# Vendor information
Vendor:         Moore Threads
Packager:       Moore Threads <mt-sw-cloudcomputing@mthreads.com>

# Build architecture
BuildArch:      noarch

# Dependencies
Requires:      kernel >= 6.0
Requires:      dkms >= 1.95
Requires:      mtt-s30-driver = %{version}

# Provides virtual package for DKMS
Provides:      sgpu-dkms = %{version}

# Conflicts
Conflicts:     sgpu-dkms

%description
The Moore Threads MTT S30 DKMS package provides automatic kernel module
rebuilding support for the sGPU driver. When a new kernel is installed,
DKMS will automatically rebuild the sgpu_km module to match the new kernel ABI.

This package ensures that the MTT S30 GPU driver remains functional
after kernel updates without manual intervention.

Features:
- Automatic module rebuilding on kernel updates
- Support for multiple kernel versions simultaneously
- Easy uninstallation and cleanup

Note: This package depends on the mtt-s30-driver package which contains
the actual source code.

%prep
%setup -q -n %{name}-%{version}

%build
# Nothing to build - this is a source-only package

%install
# Install DKMS configuration
install -d %{buildroot}/usr/src/%{name}-%{version}/kernel/src

# Copy source files
for f in kernel/src/*.c kernel/src/*.h kernel/src/Makefile kernel/src/dkms.conf; do
    install -D -m 644 $f %{buildroot}/usr/src/%{name}-%{version}/$f
done

# Copy header files
install -D -m 644 kernel/src/mtgpu.h %{buildroot}/usr/src/%{name}-%{version}/mtgpu.h
install -D -m 644 kernel/src/mtgpu_errors.h %{buildroot}/usr/src/%{name}-%{version}/mtgpu_errors.h
install -D -m 644 kernel/src/version.h %{buildroot}/usr/src/%{name}-%{version}/version.h

# Install DKMS configuration
install -D -m 644 kernel/src/dkms.conf \
    %{buildroot}/usr/src/%{name}-%{version}/dkms.conf

%pre
# Check for DKMS
if ! command -v dkms &>/dev/null; then
    echo "Error: DKMS is required but not installed."
    echo "Please install it with: sudo dnf install dkms"
    exit 1
fi

%post
# Register with DKMS
if [ -x /usr/sbin/dkms ]; then
    /usr/sbin/dkms add -m %{name} -v %{version} || :
    /usr/sbin/dkms build -m %{name} -v %{version} || :
    /usr/sbin/dkms install -m %{name} -v %{version} || :
fi

echo ""
echo "MTT S30 DKMS package installed."
echo "The sgpu_km module will be automatically rebuilt on kernel updates."
echo ""

%postun
# Unregister from DKMS on removal
if [ "$1" = "0" ]; then
    if [ -x /usr/sbin/dkms ]; then
        /usr/sbin/dkms remove -m %{name} -v %{version} --all || :
    fi
fi

%preun
# Unload module before removal
if [ "$1" = "0" ]; then
    /sbin/rmmod sgpu_km 2>/dev/null || :
fi

%files
%license LICENSE
%doc README.md docs/

# DKMS source tree
/usr/src/%{name}-%{version}/kernel/src/*.c
/usr/src/%{name}-%{version}/kernel/src/*.h
/usr/src/%{name}-%{version}/kernel/src/Makefile
/usr/src/%{name}-%{version}/kernel/src/dkms.conf
/usr/src/%{name}-%{version}/dkms.conf
/usr/src/%{name}-%{version}/mtgpu.h
/usr/src/%{name}-%{version}/mtgpu_errors.h
/usr/src/%{name}-%{version}/version.h

%changelog
* Tue Apr 30 2024 Moore Threads <mt-sw-cloudcomputing@mthreads.com> - 1.1.1-1
- Initial DKMS RPM release for Fedora Linux
- Version 1.1.1 (sGPU driver)
