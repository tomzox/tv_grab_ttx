#!/usr/bin/perl -w
#
# Script to automatically generate Debian binary package
#
use strict;

my $version = "2.3";
my $name = "libxmltv-ttx";
my $arch = "amd64";

mkdir "deb";
mkdir "deb/DEBIAN";
mkdir "deb/usr";
mkdir "deb/usr/share";
mkdir "deb/usr/share/doc";
mkdir "deb/usr/share/doc/$name";
mkdir "deb/usr/share/man";
mkdir "deb/usr/share/man/man1";
mkdir "deb/usr/bin";

open(CTRL, ">deb/DEBIAN/control") || die;
print CTRL <<EoF;
Package: $name
Priority: optional
Section: utils
Maintainer: T. Zoerner <tomzo\@users.sourceforge.net>
Homepage: https://github.com/tomzox/tv_grab_ttx
Architecture: $arch
Version: $version
Depends: libzvbi0, libstdc++6, libc6
Recommends: dvb-tools
Description: Grab TV listings from teletext via a TV card
 This EPG grabber allows extracting TV programme listings in XMLTV
 format from teletext pages as broadcast by most European TV networks.
 Note the current parser only supports German and few French networks.
EoF
close(CTRL);

open(CTRL, ">deb/usr/share/doc/$name/copyright") || die;
print CTRL <<EoF;
Copyright (C) 2006-2011,2020-2021 T. Zoerner. All rights reserved.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

For a copy of the GNU General Public License see:
/usr/share/common-licenses/GPL-3

Alternatively, see <http://www.gnu.org/licenses/>.
EoF
close CTRL;

# copy doc files
system "cp", "README.md", "deb/usr/share/doc/$name/README.md";
system "gzip -n -9 -c changelog > deb/usr/share/doc/$name/changelog.gz";

# copy executable to bin dir
die "executable missing - forgot to run 'make'?" unless -e "tv_grab_ttx";
system qw(cp tv_grab_ttx deb/usr/bin/tv_grab_ttx);
system qw(strip deb/usr/bin/tv_grab_ttx);

# copy & compress manual page
die "manual page missing - 'make' not run through?" unless -e "tv_grab_ttx.1";
system "gzip -n -9 -c tv_grab_ttx.1 > deb/usr/share/man/man1/tv_grab_ttx.1.gz";

# build package
system "cd deb; find usr -type f | xargs md5sum > DEBIAN/md5sums";
system "fakeroot dpkg-deb --build deb ${name}_${version}_${arch}.deb";
system "lintian ${name}_${version}_${arch}.deb";
