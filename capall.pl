#!/usr/bin/perl -w
#
#  This is a demo how to capture EPG data from multiple channels.
#  "v4lctl" is used for channel changing (part of the xawtv package)
#  Note you need to create sub-directories "raw" and "test".
#
#  Author: Tom Zoerner (tomzo at users.sf.net)
#

@Nets = (
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

$device = "/dev/vbi1";
$duration = 90;

for ($idx = 0; $idx < $#Nets; $idx++) {
   my ($chan, $fname, $pages) = @{$Nets[$idx]};
   $pages = "300-399" if !defined $pages;
   print "Capturing $chan\n";
   system "v4lctl -c $device setstation \"$chan\"";
   $start = time;
   system "./cap.pl -dev $device -duration $duration > raw.ttx.tmp";
   if (time - $start >= $duration-10) {
      system "mv raw.ttx.tmp raw/$fname.ttx";
      system "./ttxacq.pl -dumpraw $pages raw/$fname.ttx > test/$fname.in";
      system "./ttxacq.pl -verify test/$fname.in > test/$fname.xml";
      #system "./ttxacq.pl -page $pages raw/$fname.ttx > test/$fname.xml";
   }
}

system "./merge.pl test/*.xml > tv.xml";

