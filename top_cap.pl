#!/usr/bin/perl
#
#  This script captures and dumps the TOP table (BTT, page 1F0)
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
#  Copyright 2006,2008 by Tom Zoerner (tomzo at users.sf.net)
#
#  $Id: top_cap.pl,v 1.3 2010/04/14 19:22:56 tom Exp $
#

use Video::ZVBI qw(/^VBI_/);

$opt_duration = 0;
$opt_device = "/dev/vbi0";
$opt_dvbpid = undef;
$opt_debug = 0;

ParseArgv();

my $err;
my $srv = VBI_SLICED_TELETEXT_B | VBI_SLICED_VPS;
my $cap;
if (defined($opt_dvbpid)) {
   $cap = Video::ZVBI::capture::dvb_new($opt_device, 0, $srv, 0, $err, $opt_debug);
   $cap->dvb_filter($opt_dvbpid) 
} else {
   $cap = Video::ZVBI::capture::v4l2_new($opt_device, 6, $srv, 0, $err, $opt_debug);
}

die "Failed to open capture device: $err\n" unless $cap;

$start_t = time;
$vbi_fd = $cap->fd();
$is_btt = 0;
$is_aip = 0;
$is_mpt = 0;

for(;;) {
   my $rin="";
   vec($rin, $vbi_fd, 1) = 1;
   select($rin, undef, undef, 1);

   my ($buf, $lcount, $ts);
   my $ret = $cap->pull_sliced($buf, $lcount, $ts, 0);
   if (($ret > 0) && ($lcount > 0)) {
      for (my $idx = 0; $idx < $lcount; $idx++) {
         my @tmpl = Video::ZVBI::get_sliced_line($buf, $idx);
         if ($tmpl[1] & VBI_SLICED_TELETEXT_B) {
            feed($tmpl[0]);
         }
      }

      exit(0) if ($opt_duration != 0) && ((time - $start_t) > $opt_duration);
   }
}

sub feed {
   my ($data) = @_;
   my $mpag = Video::ZVBI::unham16p($data);
   my $mag = $mpag & 7;
   my $y = ($mpag & 0xf8) >> 3;

   if ($y == 0) {
      # teletext page header (packet #0)
      my $page = ($mag << 8) | Video::ZVBI::unham16p($data, 2);

      $is_btt = ($page == 0x1F0);
      $is_mpt = ($page == 0x1F1);
      $is_aip = ($page == 0x1F2);

   } elsif ($is_btt) {
      if (($y >= 1) && ($y <= 20)) {
         # lines 1-20: page function (coded Hamming-8/4) - see 9.4.2.1
         printf "%03d: ", (($y-1)*40) + 100;
         foreach (unpack("x2C40", $data)) {
            printf " %d", Video::ZVBI::unham8($_);
         }
         print "\n";

      } elsif (($y >= 21) && ($y <= 23)) {
         printf "LINK #%d: ", $y-21;
         for (my $i=0; $i < 20; $i++) {
            printf "%X,", Video::ZVBI::unham16p($data, 2 + 2*$i)
         }
         print "\n";

      } else {
         # ignore
      }
   } elsif ($is_mpt) {
      if (($y >= 1) && ($y <= 20)) {
         # lines 1-20: multi-page (coded Hamming-8/4)
         printf "%03d: ", ($y-1)*40 + 100;
         foreach (unpack("x2C40", $data)) {
            printf " %d", Video::ZVBI::unham8($_);
         }
         print "\n";
      }
   } elsif ($is_aip) {
      if (($y >= 1) && ($y <= 22)) {
         my $txt = $data;
         Video::ZVBI::unpar_str($txt);
         printf "%02d: ", $y;
         foreach (unpack("x2C8", $data)) {
            printf "%x", Video::ZVBI::unham8($_);
         }
         printf " %s - ", substr($txt, 2+8, 12);
         foreach (unpack("x22C8", $data)) {
            printf "%x", Video::ZVBI::unham8($_);
         }
         printf " %s\n", substr($txt, 2+20+8, 12);
      }
   }
}

sub ParseArgv {
   my $usage = "Usage: $0 [-duration <sec>] [-dev <path>] [-dvbpid <PID>] [-debug]\n".
               "Output is written to STDOUT\n";

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
      die "-dev $opt_device: doesn't exist\n" unless -e $opt_device;
      die "-dev $opt_device: not a character device\n" unless -c $opt_device;

    # -dvbpid <number>: capture from DVB device from a stream with the given PID
    } elsif (/^-dvbpid$/) {
      die "Missing argument for $_\n$usage" unless $#ARGV>=0;
      $opt_dvbpid = shift @ARGV;
      die "$_ requires a numeric argument\n" if $opt_dvbpid !~ m#^[0-9]+$#;

    } elsif (/^-debug$/) {
      $opt_debug = 1;

   } elsif (/^-(help|\?)$/) {
      print STDERR $usage;
      exit;

    } else {
       die "$_: unknown argument\n";
    }
  }
}
