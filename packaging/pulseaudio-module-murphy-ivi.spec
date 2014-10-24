Summary:  Pulseaudio module for enforcing policy decisions in the audio domain
Name:     pulseaudio-module-murphy-ivi
Version:  0.9.31
Release:  0
License:  LGPL-2.1
Group:    Automotive/Resource Policy
URL:      https://github.com/otcshare/pulseaudio-module-murphy-ivi
Source0:  %{name}-%{version}.tar.gz
BuildRequires: pkgconfig(pulseaudio-module-devel)
BuildRequires: pkgconfig(libpulse)
BuildRequires: pkgconfig(murphy-common)
Buildrequires: pkgconfig(json)
BuildRequires: pkgconfig(murphy-lua-utils)
BuildRequires: pkgconfig(lua)
BuildRequires: pkgconfig(dbus-1)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig
BuildRequires: libtool-ltdl-devel
Buildrequires: pkgconfig(json)
BuildRequires: config(pulseaudio)
BuildRequires: pulseaudio >= 5.0
BuildRequires: pkgconfig(murphy-pulse)
BuildRequires: murphy-pulse
BuildRequires: pkgconfig(aul)
Requires:      pulseaudio >= 5.0
Requires:      aul
Conflicts:     pulseaudio-module-combine-sink
Conflicts:     pulseaudio-module-augment-properties

%description
This package contains a pulseaudio module that enforces (mostly audio) routing,
corking and muting policy decisions.

%prep
%setup -q

%build
PAVER="`/usr/bin/pkg-config --silence-errors --modversion libpulse | \
tr -d \\n | sed -e 's/\([0123456789.]\+\).*/\1/'`"
./bootstrap.sh

unset LD_AS_NEEDED
%configure --disable-static \
--with-module-dir=%{_libdir}/pulse-$PAVER/modules \
--with-dbus \
--with-documentation=no \
--with-murphyif
%__make

%install
rm -rf $RPM_BUILD_ROOT
%make_install
rm -f %{_libdir}/pulse-*/modules/module-*.la

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/pulse-*/modules/module-*.so
%config %{_sysconfdir}/dbus-1/system.d/pulseaudio-murphy-ivi.conf
%{_sysconfdir}/pulse/murphy-ivi.lua
%license COPYING
