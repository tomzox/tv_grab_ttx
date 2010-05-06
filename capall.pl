#!/usr/bin/perl -w
#
#  This is a demo how to capture EPG data from multiple channels.
#  "v4lctl" is used for channel changing (part of the xawtv package)
#  Note you need to create sub-directories "raw" and "test".
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
#  Copyright 2006-2008 by Tom Zoerner (tomzo at users.sf.net)
#
#  $Id: capall.pl,v 1.3 2011/01/03 13:50:37 tom Exp $
#

use strict;

my @Nets = (
   ["arte", "arte"],
   ["KiKa", "kika"],
   ["3sat", "3sat"],
   ["WDR", "wdr"],
   ["Tele-5", "tele5"],
   ["ARD", "ard"],
   ["ZDF", "zdf"],
   ["Sat.1", "sat1"],
   ["RTL", "rtl"],
   ["Pro 7", "pro7"],
   ["RTL 2", "rtl2"],
   ["VOX", "vox"],
   ["MTV", "mtv"],
   ["VIVA", "viva"],
   ["MDR", "mdr"],
   ["SWR", "swr"],
   ["BR3", "br3"],
   ["BR-alpha", "bralpha"],
   ["Phoenix", "phoenix"],
   ["Kabel", "kabel1"],
   ["S-RTL", "srtl"],
   ["DSF", "dsf"],
   ["Eurosport", "eurosport"],
   ["TV5", "tv5"],
   ["n-tv", "ntv", "500-599"],
   ["n24", "n24"],
   ["CNN", "cnni", "200-299"],
   ["9live", "9live"],
);

my $device = "/dev/vbi0";
my $duration = 90;

for (my $idx = 0; $idx < $#Nets; $idx++) {
   # get parameters from the list above
   my ($chan, $fname, $pages) = @{$Nets[$idx]};
   $pages = "300-399" if !defined $pages;
   print "Capturing $chan\n";
   # tune the channel
   system "v4lctl -c $device setstation \"$chan\"";
   # build the grabber command line
   # - capture into temporary file
   # - merge output with previous XML file of same name, if one exists
   my $out_file = "ttx-$fname.xml";
   my $cmd  = "./tv_grab_ttx -dev $device -duration $duration";
   $cmd .= " -merge $out_file" if -e $out_file && !-z $out_file;
   $cmd .= " > ${out_file}.tmp && mv ${out_file}.tmp ${out_file}";
   system $cmd;
   my $stat = $?;
   unlink "${out_file}.tmp";
   # check grabber result status: if interrupted (CTRL-C), stop this script also
   if ($stat & 127) {
      print "Interrupted with signal ".($stat&127)." - exiting\n";
      exit 1;
   }
}

system "./merge.pl ttx-*.xml > tv.xml";

