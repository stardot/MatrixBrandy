Summary: A BBC BASIC interpreter for Linux
Name: MatrixBrandy
Version: 1.21.13
Release: 1
License: GPLv2+
Group: Development/Tools
Source: http://brandy.matrixnetwork.co.uk/releases/%{name}-%{version}.tar.gz
URL: http://brandy.matrixnetwork.co.uk/
BuildRequires: SDL-devel

%description
Brandy is an interpreter for BBC BASIC V that runs under a variety
of operating systems. Basic V is the version of BASIC supplied
with desktop computers running RISC OS. These were originally made
by Acorn Computers but are now designed and manufactured by
companies such as Advantage Six and Castle Technology.

The Matrix Brandy fork includes support for much of the grahics modes
offered by RISC OS including Mode 7 (Teletext), and basic networking
both of which are used by the bundled "telstar" example.

%prep
%setup -q 
chmod 0644 docs/*

%build
make clean %{?_smp_mflags}
make %{?_smp_mflags}
make -f makefile.text clean %{?_smp_mflags}
make -f makefile.text %{?_smp_mflags}

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/%{name}-%{version}/examples
install -m 0755 brandy %{buildroot}%{_bindir}
install -m 0755 sbrandy %{buildroot}%{_bindir}
install -m 0755 tbrandy %{buildroot}%{_bindir}
cp -r examples/* %{buildroot}%{_datadir}/%{name}-%{version}/examples

%clean
rm -rf %{buildroot}

%files
%doc COPYING READ.ME docs/ChangeLog docs/README docs/*.txt
%{_bindir}/brandy
%{_bindir}/sbrandy
%{_bindir}/tbrandy
%{_datadir}/%{name}-%{version}

%changelog
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
