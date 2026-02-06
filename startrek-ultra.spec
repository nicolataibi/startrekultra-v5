%global rel 1
Name:           startrek-ultra
Version:        2026.02.05
Release:        %{rel}%{?dist}
Summary:        Star Trek Ultra: 3D Multi-User Client-Server Edition

# Disable debuginfo to keep the package simple for this project
%define debug_package %{nil}

License:        GPLv3
URL:            https://github.com/nicolataibi/startrekultra-v5
Source0:        %{name}-%{version}-%{rel}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  freeglut-devel
BuildRequires:  mesa-libGLU-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  openssl-devel

Requires:       freeglut
Requires:       mesa-libGLU
Requires:       mesa-libGL
Requires:       openssl

%description
Star Trek Ultra is a high-performance 3D multi-user client-server game engine.
It features real-time galaxy synchronization via shared memory (SHM),
advanced cryptographic communication frequencies (AES, PQC, etc.),
and a technical 3D visualizer based on OpenGL and FreeGLUT.

%prep
%setup -q

%build
# Use the provided Makefile but ensure we pass optimization flags
make %{?_smp_mflags} CFLAGS="%{optflags} -Iinclude -std=c2x -D_XOPEN_SOURCE=700"

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/%{name}

# Install binaries
install -p -m 0755 trek_server %{buildroot}%{_bindir}/
install -p -m 0755 trek_client %{buildroot}%{_bindir}/
install -p -m 0755 trek_3dview %{buildroot}%{_bindir}/
install -p -m 0755 trek_galaxy_viewer %{buildroot}%{_bindir}/

# Install helper scripts as user commands
install -p -m 0755 run_server.sh %{buildroot}%{_bindir}/%{name}-server
install -p -m 0755 run_client.sh %{buildroot}%{_bindir}/%{name}-client

# Install data and templates
install -p -m 0644 LICENSE.txt %{buildroot}%{_datadir}/%{name}/

%files
%license LICENSE.txt
%doc README.md README_en.md *.jpg
%{_bindir}/trek_server
%{_bindir}/trek_client
%{_bindir}/trek_3dview
%{_bindir}/trek_galaxy_viewer
%{_bindir}/%{name}-server
%{_bindir}/%{name}-client
%{_datadir}/%{name}/

%changelog
* Fri Feb 06 2026 Nicola Taibi <nicola.taibi.1967@gmail.com> - 2026.02.05-2
- Improved helper scripts and build flags
