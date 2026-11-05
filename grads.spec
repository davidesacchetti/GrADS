# RPM spec file for grads
# 
# NOTE: this package requires epel-release to be installed
# and powertools enabled but cannot be put in requirements since
# RPM tries to resolve all dependencies before installing anything
#
# Options:
# 1. specify __install_path to change default (/usr)
#
# 2. Specify module_dir to setup GrADS as a modulefile
#    and not a system-wide application (default: empty)
#
# Build example:
# 
#     rpmbuild -ba grads.spec --define "__install_path /opt/grads" --define "module_dir /usr/share/Modules/modulefiles"
# 
# 20251120 Paolo Oliveri
#

Name:           grads
Version:        2.2.5
Release:        1%{?dist}
Summary:        Tool for easy acces, manipulation, and visualization of data

Group:          Applications/Engineering
# gxeps is under the MIT, other programs are GPLv2
License:        GPLv2 and MIT
URL:            http://http://cola.gmu.edu/grads

# GrADS sources
Source0:        https://github.com/paolo-oliveri/GrADS/archive/refs/tags/v%{version}.tar.gz

BuildRequires:  gcc gcc-c++ gcc-gfortran automake autoconf libtool make which pcre-devel
BuildRequires:  jasper-devel cairo-devel
BuildRequires:  hdf-devel hdf5-devel netcdf-devel libxml2-devel udunits2-devel libdap-devel libtirpc-devel
BuildRequires:  readline-devel ncurses-devel libtirpc-devel
BuildRequires:  shapelib-devel gd-devel libtiff-devel libgeotiff-devel
BuildRequires:  zlib-devel libjpeg-devel libpng-devel
BuildRequires:  libXext-devel libXt-devel libXmu-devel libXaw-devel Xaw3d-devel libXft-devel
BuildRequires:  environment-modules
# Custom build packages
BuildRequires:  libXaw3dXft-devel libsx gadap g2clib-devel

Requires:  jasper-libs cairo
Requires:  hdf udunits2 hdf5 netcdf
Requires:  readline libtirpc libdap libaec proj harfbuzz graphite2
Requires:  libpng libgeotiff libtiff shapelib libwebp jbigkit-libs
Requires:  libXmu libXext libXpm libSM libICE libXft libxcb libXrender libXau
Requires:  environment-modules
# Custom build packages
Requires:  libXaw3dXft libsx gadap


%description
The Grid Analysis and Display System (GrADS) is a tool for fast and easy access, manipulation, analysis, and visualization of Earth science data. GrADS has two data models for handling both gridded and station data, and supports all of the standard data file formats. GrADS uses a 5-Dimensional data environment: the four conventional dimensions (longitude, latitude, vertical level, and time) plus an optional fifth dimension for grids that is generally implemented but designed to be used for ensembles. Analysis operations are executed via algebraic expressions, which are evaluated recursively so that expressions may be nested. A rich set of built-in functions are provided, but users may also add their own functions as external plug-ins that may be written in any programming language. GrADS has a programmable interface (scripting language) that allows for sophisticated analysis and display applications. GrADS can be run interactively or in batch mode.

%prep
%setup -q -n GrADS-%{version}

%build

autoreconf -fi

# Final directory prefix
%{!?__install_path: %define __install_path /usr }
%define _prefix %{__install_path}
%define _libdir %{_prefix}/lib

# Fix libtool issue given by compiler_flags variable
%define _hardened_ldflags -specs=/usr/lib/rpm/redhat/redhat-hardened-ld

# Fix loading global variables in GrADS functions
%global _general_options %{_general_options} -O1

# Configure
%configure --with-gadap

# Build process
make
rm -rf __dist_docs
mkdir __dist_docs
cp -a doc __dist_docs/html

%install
# Clean build root
rm -rf $RPM_BUILD_ROOT
# Install GrADS
make install DESTDIR=$RPM_BUILD_ROOT
# Install libraries
libtool --finish $RPM_BUILD_ROOT%{_libdir}
# Install header files
%{__install} -d -m755 $RPM_BUILD_ROOT%{_includedir}
%{__install} -p -m644 src/*.h src/*.inc $RPM_BUILD_ROOT%{_includedir}
# Creating data directory (/share)
%{__install} -d -m755 $RPM_BUILD_ROOT%{_datadir}/%{name}
# Populate files and tables into data directory
%{__install} -d -m755 $RPM_BUILD_ROOT%{_datadir}/%{name}/tables
find data -type f -exec install -p -m644 "{}" $RPM_BUILD_ROOT%{_datadir}/%{name} \;
find data/tables -type f -exec install -p -m644 "{}" $RPM_BUILD_ROOT%{_datadir}/%{name}/tables \;

# Remove obsolete .la files
rm -f %{buildroot}%{_libdir}/*.la

# Reset udpt file to final target
cat udpt | sed -e 's|GRADSPREFIX|%{_prefix}|g' > $RPM_BUILD_ROOT%{_datadir}/%{name}/udpt

# if module_dir is empty install in global mode
%{!?module_dir: %define module_dir %{nil} }

%if "%{module_dir}" != ""
    # Installing GrADS environment module
    %{__install} -d -m755 $RPM_BUILD_ROOT%{module_dir}/%{name}
    cat module_template.tcl | sed -e 's/GRADSVERSION/%{version}/g' | sed -e 's|GRADSPREFIX|%{_prefix}|g' > $RPM_BUILD_ROOT/usr/share/Modules/modulefiles/%{name}/%{version}
%else
    # Installing grads.sh in /etc/profile.d
    %{__install} -d -m755 $RPM_BUILD_ROOT/etc/profile.d
    cat grads_template.sh | sed -e 's|GRADSPREFIX|%{_prefix}|g' > $RPM_BUILD_ROOT/etc/profile.d/grads.sh
%endif

%files
%doc COPYRIGHT __dist_docs/html
%{_bindir}/bufrscan
%{_bindir}/grads
%{_bindir}/gribmap
%{_bindir}/grib2scan
%{_bindir}/gribscan
%{_bindir}/stnmap
%{_libdir}/libgradspy.so
%{_libdir}/libgradspy.so.2
%{_libdir}/libgradspy.so.2.0.0
%{_libdir}/libgxdCairo.so
%{_libdir}/libgxdCairo.so.2
%{_libdir}/libgxdCairo.so.2.0.0
%{_libdir}/libgxdummy.so
%{_libdir}/libgxdummy.so.0
%{_libdir}/libgxdummy.so.0.0.0
%{_libdir}/libgxdX11.so
%{_libdir}/libgxdX11.so.2
%{_libdir}/libgxdX11.so.2.0.0
%{_libdir}/libgxpCairo.so
%{_libdir}/libgxpCairo.so.2
%{_libdir}/libgxpCairo.so.2.0.0
%{_libdir}/libgxpGD.so
%{_libdir}/libgxpGD.so.2
%{_libdir}/libgxpGD.so.2.0.0
%{_includedir}/
%{_datadir}/grads/

# Da testare
%if "%{module_dir}" != ""
    %{module_dir}/%{name}/%{version}
%else
    /etc/profile.d/grads.sh
%endif

%changelog
* Thu Nov 05 2026 ARPAL Sistemi CMI <sistemi.cmi@arpal.liguria.it> - 2.2.5-1
- Cairo display: long drop menus fit below the header; the release of
  the click opening a menu no longer selects an item
- collect accepts all dimensions fixed; coll2gr with only X varying
  returns a 1-D section (oblique sections of 2-D variables, define)
* Thu Nov 05 2026 ARPAL Sistemi CMI <sistemi.cmi@arpal.liguria.it> - 2.2.5-0
- Cairo display: restored widgets (buttons, rubber bands, drop menus),
  dialog boxes and screen save/show/free
* Wed Nov 19 2025 Paolo Oliveri <paul@oliveri.info> - 2.2.4-0
- Updated codebase
* Wed Jun 4  2025 Paolo Oliveri <paul@oliveri.info> 2.2.3-4
- Rebuilt for RHEL 9
* Fri May 30 2025 Paolo Oliveri <paul@oliveri.info> 2.2.3-4
- Rebuilt for RHEL 10
* Thu May 29 2025 Paolo Oliveri <paul@oliveri.info> - 2.2.3-4
- Bugfix on grads.c for multiple window handling on GrADSPy, Rebuilt for Fedora 42
* Thu Oct 17 2024 Paolo Oliveri <paul@oliveri.info> - 2.2.3-3
- Added font4.dat, stddev.gsf, reset message now can be silenced, bugfix in prevdir.gsf
* Tue May 7  2024 Paolo Oliveri <paul@oliveri.info> - 2.2.3-2
- Rebuilt for Fedora 40 and RHEL 9
* Tue Apr 23 2024 Paolo Oliveri <paul@oliveri.info> - 2.2.3-2
- Rebuilt for RHEL 8
* Mon Apr 22 2024 Paolo Oliveri <paul@oliveri.info> - 2.2.3-2
- Rebuilt for usage of newer G2CLIB package with jasper compression and changed GASCRP environment
* Wed Nov 29 2023 Paolo Oliveri <paul@oliveri.info> - 2.2.3-1
- Patched gex.mk for overriding compiler flags and set optimization flag O1 (with O2 global variable loading in grads scripts or function resulted in a segmentation fault)
* Mon Nov 20 2023 Paolo Oliveri <paul@oliveri.info> - 2.2.3-0
- Rebuilt for Fedora 39
* Fri Nov 17 2023 Paolo Oliveri <paul@oliveri.info> - 2.2.3-0
- Rebuilt for Fedora 38
* Fri Nov 17 2023 Paolo Oliveri <paul@oliveri.info> - 2.2.3-0
- Rebuilt for RHEL 7
* Thu Nov 16 2023 Paolo Oliveri <paul@oliveri.info> - 2.2.3-0
- Rebuilt for RHEL 8
* Tue Oct 24 2023 Paolo Oliveri <paul@oliveri.info> - 2.2.3-0
- Build for RHEL 9
