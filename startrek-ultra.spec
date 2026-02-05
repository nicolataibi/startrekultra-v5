Name:           startrek-ultra
Version:        2026.02.05
Release:        1%{?dist}
Summary:        Star Trek Ultra: 3D Multi-User Client-Server Edition

%define debug_package %{nil}

License:        GPLv3
URL:            https://github.com/nicolataibi/startrekultra-v5
Source0:        %{name}-%{version}.tar.gz

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
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/%{name}

# Install binaries
install -m 0755 trek_server %{buildroot}%{_bindir}/
install -m 0755 trek_client %{buildroot}%{_bindir}/
install -m 0755 trek_3dview %{buildroot}%{_bindir}/
install -m 0755 trek_galaxy_viewer %{buildroot}%{_bindir}/

# Install data files
install -m 0644 LICENSE.txt %{buildroot}%{_datadir}/%{name}/
install -m 0644 *.jpg %{buildroot}%{_datadir}/%{name}/

%files
%license LICENSE.txt
%doc README.md README_en.md
%{_bindir}/trek_server
%{_bindir}/trek_client
%{_bindir}/trek_3dview
%{_bindir}/trek_galaxy_viewer
%{_datadir}/%{name}/

%changelog
* Thu Feb 05 2026 Nicola Taibi <nicola.taibi@example.com> - 2026.02.05-1
- Initial RPM release
- Added technical cloaking effect with blue glowing shader
- Integrated PQC (Post-Quantum Cryptography) for subspace telemetry
- Added multi-user tactical viewer support
