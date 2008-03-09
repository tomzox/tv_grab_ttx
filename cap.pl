#!/usr/bin/perl
#
#  This script grabs VBI data from a TV card and appends each packet to a
#  file.  VBI acquisition is done via the Video-ZVBI module from CPAN.org
#  (which in turn depends on the libzvbi library.) This script is intended
#  for use with the teletext grabber only. Note that the output format is
#  preliminary and will probably change (e.g. to include parity decoding
#  error counts and VPS.)
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
#  $Id: cap.pl,v 1.6 2008/03/08 17:18:38 tom Exp tom $
#

use Video::ZVBI qw(/^VBI_/);

# default values for optional command line parameters
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
$last_pg = -1;
$vbi_fd = $cap->fd();

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
         } elsif ($tmpl[1] & VBI_SLICED_VPS) {
            feed_vps($tmpl[0]);
         }
      }

      if (($opt_duration != 0) && ((time - $start_t) > $opt_duration)) {
         # capture duration limit reached - done
         exit(0);
      }

   } elsif ($ret < 0) {
      die "capture error: $!\n";
   }
}

sub feed_vps {
   #my $cni = Video::ZVBI::decode_vps_cni($_[0]); # only since libzvbi 0.2.22

   my $cd_02 = ord(substr($_[0],  2, 1));
   my $cd_08 = ord(substr($_[0],  8, 1));
   my $cd_10 = ord(substr($_[0], 10, 1));
   my $cd_11 = ord(substr($_[0], 11, 1));

   my $cni = (($cd_10 & 0x3) << 10) | (($cd_11 & 0xc0) << 2) |
             ($cd_08 & 0xc0) | ($cd_11 & 0x3f);

   if ($cni == 0xDC3)
   {  # special case: "ARD/ZDF Gemeinsames Vormittagsprogramm"
      $cni = ($cd_02 & 0x20) ? 0xDC1 : 0xDC2;
   }
   if (($cni != 0) && ($cni != 0xfff))
   {
      my $buf = pack("SSCCSx18x20", 0, 0, 0, 32, $cni);
      syswrite(STDOUT, $buf);
   }
}

sub feed {
   my ($data) = @_;
   my $lpage = -1;
   my $mpag = Video::ZVBI::unham16p($data);
   my $mag = $mpag & 7;
   my $y = ($mpag & 0xf8) >> 3;
   if ($y == 0) {
      # teletext page header (packet #0)
      my $page = ($mag << 8) | Video::ZVBI::unham16p($data, 2);
      my $ctrl = (Video::ZVBI::unham16p($data, 4)) |
                 (Video::ZVBI::unham16p($data, 6) << 8) |
                 (Video::ZVBI::unham16p($data, 8) << 16);
      # drop filler packets (xFF and repetition of identical pkg 0)
      if ( (($page & 0xFF) != 0xFF) && ($page != $lpage) ) {
         Video::ZVBI::unpar_str($data);
         $lpage = $page;
         $sub = $ctrl & 0xffff;
         # format: pageno, ctrl bis, ctrl bits, pkgno
         my $buf = pack("SSCCa40", $page, ($ctrl) & 0xFFFF, ($ctrl)>>16, $y, substr($data, 2, 40));
         syswrite(STDOUT, $buf);
         my $spg = ($page << 16) | ($ctrl & 0x3f7f);
         if ($spg != $last_pg) {
            print STDERR sprintf("%03X.%04X\n", $page, $ctrl & 0x3f7f);
            $last_pg = $spg;
         }
      }

   } elsif($y<=25) {
      # regular teletext packet (lines 1-25)
      Video::ZVBI::unpar_str($data);
      my $buf = pack("SSCCa40", $mag << 8, 0, 0, $y, substr($data, 2, 40));
      syswrite(STDOUT, $buf);
      $lpage = -1;
   } elsif($y==30) {
      my $dc = (Video::ZVBI::unham16p($data, 2) & 0x0F) >> 1;
      if ($dc == 0) {
         # packet 8/30/1
         my $cni = Video::ZVBI::rev16p($data, 2+7);
         if (($cni != 0) && ($cni != 0xffff)) {
            my $buf = pack("SSCCSx18a20", $mag << 8, 0, 0, $y, $cni, substr($data, 2+20, 20));
            syswrite(STDOUT, $buf);
         }
      } elsif ($dc == 1) {
         # packet 8/30/2
         my $c0 = Video::ZVBI::rev8(Video::ZVBI::unham16p($data, 2+9+0));
         my $c6 = Video::ZVBI::rev8(Video::ZVBI::unham16p($data, 2+9+6));
         my $c8 = Video::ZVBI::rev8(Video::ZVBI::unham16p($data, 2+9+8));

         my $cni = (($c0 & 0xf0) << 8) |
                   (($c0 & 0x0c) << 4) |
                   (($c6 & 0x30) << 6) |
                   (($c6 & 0x0c) << 6) |
                   (($c6 & 0x03) << 4) |
                   (($c8 & 0xf0) >> 4);
         $cni &= 0x0FFF if (($cni & 0xF000) == 0xF000);
         if (($cni != 0) && ($cni != 0xffff)) {
            my $buf = pack("SSCCSx18x20", $mag << 8, 0, 0, $y, $cni);
            syswrite(STDOUT, $buf);
         }
      }
   } else {
      $lpage = -1;
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
