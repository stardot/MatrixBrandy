Summary: A cross-platform BBC BASIC interpreter with SDL graphics
Name: brandy
Version: 1.23.5
Release: %{extraverdata}.matrix%{?dist}
License: GPLv2+
Group: Development/Tools
Source: https://brandy.matrixnetwork.co.uk/releases/MatrixBrandy-%{version}.tar.xz
URL: https://brandy.matrixnetwork.co.uk/
# Dirty hack to ensure we have SDL-devel or sdl12-compat-devel
BuildRequires: /usr/include/SDL/SDL.h
Requires: brandy-docs = %{version}-%{release}
Requires: brandy-examples = %{version}-%{release}

%define debug_package %{nil}

%description
Brandy is an interpreter for BBC BASIC VI that runs under a variety of
operating systems. BASIC V and BASIC VI are versions of BASIC supplied with
computers running RISC OS. These were originally made by Acorn Computers and
more recently designed and manufactured by companies such as Advantage Six
and Castle Technology.

The Matrix Brandy fork includes support for much of the grahics modes
offered by RISC OS including Mode 7 (Teletext), and basic networking
both of which are used by the bundled "telstar" example. Many bugs are fixed
and mathematics are brought more in line with Acorn's BBC BASIC VI.
Some BASIC extensions from Richard Russell's BB4W and BBCSDL are also
supported, as are a few from Steve Drain's Basalt add-on for RISC OS.

BBC BASIC is a trademark of the British Broadcasting Corporation.
Matrix Brandy does not claim to be "BBC BASIC", however it aims to be an
interpreter of the BBC BASIC dialect of BASIC.  The term "BBC BASIC" in
the documentation is used in reference to the dialect, and other
implementations where the name is used under licence (e.g. by Acorn/RISC OS
and the interpreters by Richard Russell).

%package docs
Summary: Documentation for Matrix Brandy
BuildArch: noarch

%package examples
Summary: Example programs for Matrix Brandy
BuildArch: noarch

%package text
Summary: A cross-platform BBC BASIC interpreter (text mode)
Requires: brandy-docs = %{version}-%{release}
Requires: brandy-examples = %{version}-%{release}

%package telstar
Summary: Desktop launcher for Matrix Brandy's videotex/viewdata client
Requires: brandy = %{version}-%{release}
Requires: brandy-examples = %{version}-%{release}
BuildArch: noarch

%description docs
This package contains the documentation files for Matrix Brandy.

%description examples
This package contains the example programs for Matrix Brandy.

%description telstar
This package contains the desktop shortcut file, icon and launcher script
for Matrix Brandy's Telstar viewdata/videotex client.  The Telstar program
itself is one of the examples in the main brandy package, this sub-package
simply provides a GNOME desktop link and icon.

%description text
Brandy is an interpreter for BBC BASIC VI that runs under a variety of
operating systems. BASIC V and BASIC VI are versions of BASIC supplied with
computers running RISC OS. These were originally made by Acorn Computers and
more recently designed and manufactured by companies such as Advantage Six
and Castle Technology.

This package contains the text-mode builds of Matrix Brandy, and aside for
Tektronix support with some terminals, these builds do not support graphics.

BBC BASIC is a trademark of the British Broadcasting Corporation.
Matrix Brandy does not claim to be "BBC BASIC", however it aims to be an
interpreter of the BBC BASIC dialect of BASIC.  The term "BBC BASIC" in
the documentation is used in reference to the dialect, and other
implementations where the name is used under licence (e.g. by Acorn/RISC OS
and the interpreters by Richard Russell).



%prep
%setup -q -n MatrixBrandy-%{version}
chmod 0644 docs/*

%build
make clean %{?_smp_mflags}
make %{?_smp_mflags}
make -f makefile.text clean %{?_smp_mflags}
make -f makefile.text %{?_smp_mflags}

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_datadir}/%{name}-%{version}/examples
mkdir -p %{buildroot}%{_datadir}/applications
mkdir -p %{buildroot}%{_datadir}/pixmaps
install -s -m 0755 brandy %{buildroot}%{_bindir}
install -s -m 0755 sbrandy %{buildroot}%{_bindir}
install -s -m 0755 tbrandy %{buildroot}%{_bindir}
install -m 0755 desktop/telstar %{buildroot}%{_bindir}
install -m 0644 desktop/brandy.desktop %{buildroot}%{_datadir}/applications
install -m 0644 desktop/telstar.desktop %{buildroot}%{_datadir}/applications
install -m 0644 desktop/brandy.png %{buildroot}%{_datadir}/pixmaps
install -m 0644 desktop/telstar.png %{buildroot}%{_datadir}/pixmaps
cp -r examples/* %{buildroot}%{_datadir}/%{name}-%{version}/examples

%clean
rm -rf %{buildroot}

%files
%{_bindir}/brandy
%{_datadir}/pixmaps/brandy.png
%{_datadir}/applications/brandy.desktop

%files docs
%doc READ.ME docs/ChangeLog docs/README docs/*.txt

%files examples
%{_datadir}/%{name}-%{version}/examples

%files telstar
%{_bindir}/telstar
%{_datadir}/pixmaps/telstar.png
%{_datadir}/applications/telstar.desktop

%files text
%{_bindir}/sbrandy
%{_bindir}/tbrandy

%changelog
* Fri Jun 20 2025 Michael McConnell <mike@matrixnetwork.co.uk> - 1.23.6
- Added sub-package "telstar".
* Sat Aug 01 2020 Michael McConnell <mike@matrixnetwork.co.uk> - 1.22.7
- Removed BrandyApp from package as build mechanism has changed.
* Tue Jul 23 2019 Michael McConnell <mike@matrixnetwork.co.uk> - 1.22.0
- Re-tag as BASIC VI
* Sun Sep 02 2018 Michael McConnell <mike@matrixnetwork.co.uk> - 1.21.12
- Build both SDL and text-mode variants.
* Thu Aug 23 2018 Michael McConnell <mike@matrixnetwork.co.uk> - 1.21.11
- Adapted for Matrix Brandy.
* Fri Nov 18 2016 Huaren Zhong <huaren.zhong@gmail.com> 1.20.1
- Rebuild for Fedora
* Mon Feb 07 2011 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.0.19-9
- Rebuilt for https://fedoraproject.org/wiki/Fedora_15_Mass_Rebuild
* Fri Jul 24 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.0.19-8
- Rebuilt for https://fedoraproject.org/wiki/Fedora_12_Mass_Rebuild
* Mon Feb 23 2009 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 1.0.19-7
- Rebuilt for https://fedoraproject.org/wiki/Fedora_11_Mass_Rebuild
* Mon Jul 14 2008 Tom &quot;spot&quot; Callaway <tcallawa@redhat.com> - 1.0.19-6
- fix license tag
* Tue Feb 19 2008 Fedora Release Engineering <rel-eng@fedoraproject.org> - 1.0.19-5
- Autorebuild for GCC 4.3
* Thu Sep 14 2006 Paul F. Johnson <paul@all-the-johnsons.co.uk> - 1.0.19-4
- rebuild
* Wed Aug 16 2006 Paul F. Johnson <paul@all-the-johnsons.co.uk> - 1.0.19-3
- Added perl hack for proper flags going to gcc (Thanks Tibbs)
* Sun Aug 13 2006 Paul F. Johnson <paul@all-the-johnsons.co.uk> - 1.0.19-2
- Fix for examples being correctly copied
- altered %%doc
- corrected initial import date
- added %%defattr
* Thu Aug 10 2006 Paul F. Johnson <paul@all-the-johnsons.co.uk> - 1.0.19-1
- Initial import into FE
