Name: @PLUGIN_NAME@
Version: @VERSION@
Release: 1%{?dist}
Summary: Color monitor plugin for OBS Studio
License: GPLv2+

Source0: %{name}-%{version}.tar.bz2
BuildRequires: cmake, gcc, gcc-c++
BuildRequires: obs-studio-devel
BuildRequires: qt5-qtbase-devel qt5-qtbase-private-devel

%description
Color monitor plugin contains vectorscope, waveform, histogram, zebra, and
false color to help color correction.

%prep
%autosetup -p1

%build
%{cmake} -DLINUX_PORTABLE=OFF
%{cmake_build}

%install
%{cmake_install}

%files
%{_libdir}/obs-plugins/%{name}.so
%{_datadir}/obs/obs-plugins/%{name}/
