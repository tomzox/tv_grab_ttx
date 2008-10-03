#!/usr/bin/perl
#
# Script to automatically generate Debian binary package
#
use strict;

my $version = "1.1";
my $name = "libxmltv-ttx";

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
Priority: extra
Section: utils
Maintainer: Tom Zoerner <tomzo\@users.sourceforge.net>
Architecture: all
Version: $version
Depends: perl
Recommends: libzvbi-perl
Description: Grab TV listings from teletext via a TV card
 This EPG grabber allows to extract TV programme listings in XMLTV
 format from teletext pages as broadcast by most European TV networks.
EoF
close(CTRL);

open(CTRL, ">deb/usr/share/doc/$name/copyright") || die;
print CTRL <<EoF;
Copyright (C) 2006-2008 Tom Zoerner. All rights reserved.

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
system "cp", "README", "deb/usr/share/doc/$name/README";
system "cp", "changelog", "deb/usr/share/doc/$name/changelog";
system "gzip", "-9f", "deb/usr/share/doc/$name/changelog";

# copy / rename script files to bin dir
system qw(cp tv_grab_ttx.pl deb/usr/bin/tv_grab_ttx);
system qw(cp cap.pl         deb/usr/bin/tv_grab_ttx_cap);

# tv_grab_ttx manual page
system "pod2man -section 1 -center \"Teletext EPG grabber\" -release v$version ".
       "tv_grab_ttx.pl > deb/usr/share/man/man1/tv_grab_ttx.1";
system qw(gzip -9f deb/usr/share/man/man1/tv_grab_ttx.1);

# cap.pl / tv_grab_ttx_cap manual page
system "pod2man -section 1 -center \"Teletext EPG grabber\" -release v$version ".
       "cap.pl > deb/usr/share/man/man1/tv_grab_ttx_cap.1";
system qw(gzip -9f deb/usr/share/man/man1/tv_grab_ttx_cap.1);

# build package
system "cd deb; find usr -type f | xargs md5sum > DEBIAN/md5sums";
system "fakeroot dpkg-deb --build deb ${name}_${version}_all.deb";
system "lintian ${name}_${version}_all.deb";

