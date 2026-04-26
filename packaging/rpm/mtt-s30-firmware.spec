#
# RPM Specification for Moore Threads MTT S30 Firmware
# Fedora Linux compatible
#
# Copyright © 2022-2024 Moore Threads Inc. All rights reserved.
# Adapted for Fedora by: Fedora Driver Project
#

Name:           mtt-s30-firmware
Version:        1.1.1
Release:        1%{?dist}
Summary:        Firmware files for Moore Threads MTT S30 GPU

License:        Proprietary
URL:            https://developer.mthreads.com/
Source0:        %{name}-%{version}.tar.gz

# Vendor information
Vendor:         Moore Threads
Packager:       Moore Threads <mt-sw-cloudcomputing@mthreads.com>

# Build architecture
BuildArch:      noarch

# Dependencies
Requires:      systemd

# Provides firmware for MTT GPUs
Provides:      firmware(mthreads)

%description
Firmware files for Moore Threads MTT S30 and other compatible GPUs.
These firmware files are required for the GPU to function properly.

Included firmware:
- musa.fw.1.0.0.0: Firmware version 1.0.0.0
- musa.fw.1.1.0.0: Firmware version 1.1.0.0
- musa.fw.2.0.0.0: Firmware version 2.0.0.0 (latest)
- musa.sh.1.0.0.0: Shader firmware version 1.0.0.0

Supported GPUs:
- MTT S30 (2-core and 4-core)
- MTT S10, S50, S60, S70, S80, S100, S1000, S2000, S3000, S4000
- QuYuan series GPUs

%prep
%setup -q -n %{name}-%{version}

%build
# Nothing to build - firmware files are pre-built

%install
# Install firmware directory
install -d %{buildroot}%{_firmwaredir}/mthreads

# Install firmware files
install -m 644 firmware/mthreads/musa.fw.1.0.0.0 \
    %{buildroot}%{_firmwaredir}/mthreads/musa.fw.1.0.0.0
install -m 644 firmware/mthreads/musa.fw.1.1.0.0 \
    %{buildroot}%{_firmwaredir}/mthreads/musa.fw.1.1.0.0
install -m 644 firmware/mthreads/musa.fw.2.0.0.0 \
    %{buildroot}%{_firmwaredir}/mthreads/musa.fw.2.0.0.0
install -m 644 firmware/mthreads/musa.sh.1.0.0.0 \
    %{buildroot}%{_firmwaredir}/mthreads/musa.sh.1.0.0.0

%pre
# Nothing to do in pre-install

%post
# Update initramfs to include new firmware
if [ -x /usr/bin/dracut ]; then
    /usr/bin/dracut --force || :
fi

# Reload udev rules to detect new firmware
/usr/bin/udevadm control --reload-rules || :
/usr/bin/udevadm trigger || :

echo ""
echo "MTT S30 Firmware installed successfully."
echo "Firmware files: /lib/firmware/mthreads/"
echo ""

%postun
# Update initramfs when removing
if [ "$1" = "0" ]; then
    if [ -x /usr/bin/dracut ]; then
        /usr/bin/dracut --force || :
    fi
    /usr/bin/udevadm control --reload-rules || :
fi

%files
%license LICENSE
%doc README.md docs/

# Firmware files
%{_firmwaredir}/mthreads/musa.fw.1.0.0.0
%{_firmwaredir}/mthreads/musa.fw.1.1.0.0
%{_firmwaredir}/mthreads/musa.fw.2.0.0.0
%{_firmwaredir}/mthreads/musa.sh.1.0.0.0

%changelog
* Tue Apr 30 2024 Moore Threads <mt-sw-cloudcomputing@mthreads.com> - 1.1.1-1
- Initial firmware RPM release for Fedora Linux
- Includes firmware versions 1.0.0.0, 1.1.0.0, 2.0.0.0
- Version 1.1.1
