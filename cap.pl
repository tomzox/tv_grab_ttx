#!/usr/bin/perl
#
#  This script grabs VBI data from a TV card and appends each packet to a
#  file.  Grabbing VBI requires the V4l module from CPAN.org.  This script
#  is intended for use with my teletext grabber only. Note that the output
#  format is preliminary and will definitly change (e.g. to include parity
#  decoding error counts and VPS.)
#
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License Version 2 as
#  published by the Free Software Foundation. You find the full text here:
#  http://www.fsf.org/copyleft/gpl.html
#
#  THIS PROGRAM IS DISTRIBUTED IN THE HOPE THAT IT WILL BE USEFUL,
#  BUT WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED WARRANTY OF
#  MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  Author: Tom Zoerner (tomzo at users.sf.net)
#
#  $Id: cap.pl,v 1.1 2006/04/23 17:39:56 tom Exp tom $
#

use Video::Capture::V4l;
use Video::Capture::VBI;

$opt_duration = 0;
$opt_device = "/dev/vbi0";

ParseArgv();

$vbi = new Video::Capture::V4l::VBI($opt_device) or die;

# the next line is optional (it enables buffering)
$vbi->backlog(25); # max. 1 second backlog (~900kB)

$start_t = time;
$vbi_fd = $vbi->fileno;

for(;;) {
   my $r="";
   vec($r,$vbi_fd,1)=1;
   select $r,undef,undef,0.04;

   while ($vbi->queued) {
      $a = $vbi->field;
      feed(decode_field($a, VBI_VT));

      exit(0) if ($opt_duration != 0) && ((time - $start_t) > $opt_duration);
   }
}

sub feed {
   my $lpage = -1;
   for (@_) {
      if ($_->[0] == VBI_VT) {
         my $y = $_->[2];
         if ($y == 0) {
            $page = $_->[4];
            # drop filler packets (xFF and repetition of identical pkg 0)
            if ( (($page & 0xFF) != 0xFF) && ($page != $lpage) ) {
               my @c = split(//, $_->[3]);
               my $idx;
               for ($idx = 0; $idx <= $#c; $idx++) {
                  $c[$idx] = ord($c[$idx]) & 0x7f;
               }
               $lpage = $page;
               $sub = $_->[5] & 0xffff;
               # format: pageno, ctrl bis, ctrl bits, pkgno
               my $buf = pack("SSCCC40", $page, ($_->[5]) & 0xFFFF, ($_->[5])>>16, $_->[2], @c);
               syswrite(STDOUT, $buf);
               print STDERR sprintf("%03X.%04X\n", $page, $_->[5] & 0x3f7f);
            }

         } elsif($y<=25) {
            my @c = split(//, $_->[3]);
            my $idx;
            for ($idx = 0; $idx <= $#c; $idx++) {
               $c[$idx] = ord($c[$idx]) & 0x7f;
            }
            my $buf = pack("SSCCC40", ($_->[1])<<8, 0, 0, $_->[2], @c);
            syswrite(STDOUT, $buf);
            $lpage = -1;
         } else {
            $lpage = -1;
         }
      }
   }
}

sub ParseArgv {
   my $usage = "Usage: $0 [-duration <sec>] [-dev <path>] <file>\n";

  while ($_ = shift @ARGV) {
    # -duration <seconds>: terminate acquisition after the given time
    if (/^-duration$/) {
      die "Missing argument for $_\n$usage" unless $#ARGV>=0;
      $opt_duration = shift @ARGV;
      die "$_ requires a numeric argument\n" if $opt_duration !~ m#^[0-9]+$#;
      die "Invalid duration valid: $opt_duration\n" if $opt_duration == 0;

    # -dev <device>: specify VBI device
    } elsif (/^-dev(ice)?$/) {
      die "Missing argument for $_\n$usage" unless $#ARGV>=0;
      $opt_device = shift @ARGV;
      die "-dev $_: doesn't exist\n" unless -e $opt_device;
      die "-dev $_: not a character device\n" unless -c $opt_device;

   } elsif (/^-(help|\?)$/) {
      print STDERR $usage;
      exit;

    } else {
       die "$_: unknown argument\n";
    }
  }
}
