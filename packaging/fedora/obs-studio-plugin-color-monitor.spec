Name: obs-studio-plugin-color-monitor
Version: @VERSION@
Release: @RELEASE@%{?dist}
Summary: Color monitor plugin for OBS Studio providing vectorscope, waveform, histogram, zebra, false color
License: GPLv2+

Source0: %{name}-%{version}.tar.bz2
BuildRequires: cmake, gcc, gcc-c++
BuildRequires: obs-studio-devel
BuildRequires: qt6-qtbase-devel qt6-qtbase-private-devel
Obsoletes: obs-color-monitor < %{version}

%description
Color monitor plugin contains vectorscope, waveform, histogram, zebra, and
false color to help color correction.

%prep
%autosetup -p1

%build
%{cmake} -DQT_VERSION=6 -DINSTALL_LICENSE_FILES:BOOL=OFF
%{cmake_build}

%install
%{cmake_install}

%files
%{_libdir}/obs-plugins/*.so
%{_datadir}/obs/obs-plugins/*/
%license LICENSE
