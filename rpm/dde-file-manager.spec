Name:           dde-file-manager
Version:        5.0.0
Release:        1%{?dist}
Summary:        DDE File Manager for Lingmo OS
License:        GPL-3.0-or-later
URL:            https://github.com/LingmoOS/dde-file-manager
Source0:        dde-file-manager-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.10
BuildRequires:  pkgconfig(Qt6Core)
BuildRequires:  pkgconfig(Qt6Gui)
BuildRequires:  pkgconfig(Qt6Widgets)
BuildRequires:  pkgconfig(Qt6Quick)
BuildRequires:  pkgconfig(Qt6DBus)
BuildRequires:  pkgconfig(Qt6Concurrent)
BuildRequires:  pkgconfig(dtk6core)
BuildRequires:  pkgconfig(dtk6gui)
BuildRequires:  pkgconfig(dtk6widget)
BuildRequires:  pkgconfig(gio-2.0)
BuildRequires:  pkgconfig(gio-unix-2.0)

%description
DDE File Manager is the default file manager for the
Lingmo desktop environment, providing file browsing and management.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

%files
%doc README.md
%license LICENSE*
%{_bindir}/dde-file-manager
%{_bindir}/dfm
%{_libdir}/dde-file-manager/
%{_datadir}/dde-file-manager/
%{_datadir}/dbus-1/services/*.service
%{_datadir}/applications/dde-file-manager.desktop

%changelog
* Tue Jun 18 2025 LingmoOS Build System <dev@lingmo.os> - %{version}-1
- Initial RPM packaging for local source build
