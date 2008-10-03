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
#  $Id: cap.pl,v 1.7 2008/10/03 17:13:51 tom Exp $
#

use strict;
use Video::ZVBI qw(/^VBI_/);

my $version = 'Teletext EPG grabber capture front-end, v1.1';
my $copyright = 'Copyright 2006-2008 Tom Zoerner';
my $home_url = 'http://nxtvepg.sourceforge.net/tv_grab_ttx';

# default values for optional command line parameters
my $opt_duration = 0;
my $opt_device = "/dev/vbi0";
my $opt_dvbpid = undef;
my $opt_debug = 0;
my $opt_verbose = 0;

=head1 NAME

tv_grab_ttx_cap - capture teletext packets from a TV card

=head1 SYNOPSIS

tv_grab_ttx_cap [OPTIONS]

=head1 DESCRIPTION

B<tv_grab_ttx_cap> is a front-end for B<tv_grab_ttx> to capture
teletext and VPS data packets from a I<VBI device>. This program is
implemented as Perl Script, based on the Video::ZVBI module (and thus
the "zvbi" library) to perform the capturing.

Each captured packet is simply forwarded to the output.  This script is
intended for use with B<tv_grab_ttx> only. Its purpose is to avoid
a dependency on external modules in the grabber script, so that it
can also be used in other contexts (e.g. inside of nxtvepg, which
performs teletext capturing internally.) Note that the output format
is preliminary and may change between versions.

Note before the start, the TV channel must be switched to the network
from which data is to be captured. This has to be done by other tools,
such as L<v4lctl(1)> or manually via a TV application.

=head1 OPTIONS

=over 4

=item B<-dev> I<path>

This option can be used to specify the VBI device from which teletext
is captured. By default F</dev/vbi0> is used.

=item B<-dvbpid> I<PID>

This option enables data acquisition from a DVB device and specifies
the PID which is the identifier of the data stream which contains
teletext data. The PID must be determined by external means. Likewise
to analog sources, the TV channel also must be tuned with an external
application.

=item B<-duration> I<minutes>

This option can be used to limit capturing to the given number of
minutes. The script will exit with result code 0 after this duration.
By default there's no limit, so the script only stops when terminated
by means of a signal (e.g. manually via the "Control-C" key.)

=item B<-verbose>

This option can be used to enable output of the number of each
captured teletext page. This allows monitoring the capture progress.

=item B<-debug>

This option can be used to enable output of debugging information
to standard output. You can use this to get more diagnostic output
in case of trouble with the capture device. Note most of the
messages originate directly from the I<ZVBI> library (libzvbi)

=item B<-version>

This option prints version and license information, then exits.

=item B<-help>

This option prints the command line syntax, then exits.

=back

=head1 FILES

=over 4

=item B</dev/vbi0>, B</dev/v4l/vbi0>

Device files from which teletext data is being read during acquisition
when using an analog TV card on Linux.  Different paths can be selected
with the B<-dev> command line option.  Depending on your Linux version,
the device files may be located directly beneath C</dev> or inside
C</dev/v4l>. Other operating systems may use different names.

=item B</dev/dvb/adapter0/demux0>

Device files from which teletext data is being read during acquisition
when using a DVB device, i.e. when the B<-dvbpid> option is present.
Different paths can be selected with the B<-dev> command line option.
(If you have multiple DVB cards, increment the device index after
I<adapter> to get the second card etc.)

=back

=head1 SEE ALSO

L<tv_grab_ttx(1)>,
L<v4lctl(1)>,
L<zvbid(1)>,
L<alevt(1)>,
L<nxtvepg(1)>,
L<Video::ZVBI(3pm)>,
L<perl(1)>

=head1 AUTHOR

The teletext EPG grabber as well as the supporting I<Video::ZVBI> Perl
module were written by Tom Zoerner (tomzo at users.sourceforge.net)
since 2006 as part of the nxtvepg project.

The official homepage is L<http://nxtvepg.sourceforge.net/tv_grab_ttx>

=head1 COPYRIGHT

Copyright 2006-2008 Tom Zoerner.

This is free software. You may redistribute copies of it under the terms
of the GNU General Public License version 3 or later
L<http://www.gnu.org/licenses/gpl.html>.
There is B<no warranty>, to the extent permitted by law.

=cut

sub ParseArgv {
   my $usage = "Usage: $0 [-duration <sec>] [-dev <path>] [-dvbpid <PID>]\n".
               "Other options: -help -version -verbose -debug\n".
               "Output is written to standard out, append >FILE to redirect into a file.\n";

   while ($_ = shift @ARGV) {
      # -duration <seconds>: terminate acquisition after the given time
      if (/^-duration$/) {
         die "$0: missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_duration = shift @ARGV;
         die "$0: $_ requires a numeric argument\n" if $opt_duration !~ m#^[0-9]+$#;
         die "$0: Invalid -duration value: $opt_duration\n" if $opt_duration == 0;

      # -dev <device>: specify VBI device
      } elsif (/^-dev(ice)?$/) {
         die "$0: missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_device = shift @ARGV;
         die "$0: -dev $opt_device: doesn't exist\n" unless -e $opt_device;
         die "$0: -dev $opt_device: not a character device\n" unless -c $opt_device;

      # -dvbpid <number>: capture from DVB device from a stream with the given PID
      } elsif (/^-dvbpid$/) {
         die "$0: missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_dvbpid = shift @ARGV;
         die "$_ requires a numeric argument\n" if $opt_dvbpid !~ m#^[0-9]+$#;

      } elsif (/^-verbose$/) {
         $opt_verbose = 1;

      } elsif (/^-debug$/) {
         $opt_debug = 1;

      } elsif (/^-version$/) {
         print STDERR "$version\n$copyright" .
                      "\nThis is free software with ABSOLUTELY NO WARRANTY.\n" .
                      "Licensed under the GNU Public License version 3 or later.\n" .
                      "For details see <http://www.gnu.org/licenses/\n";
         exit 0;

      } elsif (/^-(help|\?)$/) {
         print STDERR $usage;
         exit;

      } else {
         die "$0: $_: unknown argument\n";
      }
   }
}

sub FeedVps {
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

sub FeedTtxPkg {
   my ($last_pg, $data) = @_;
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
         #$sub = $ctrl & 0xffff;
         # format: pageno, ctrl bis, ctrl bits, pkgno
         my $buf = pack("SSCCa40", $page, ($ctrl) & 0xFFFF, ($ctrl)>>16, $y, substr($data, 2, 40));
         syswrite(STDOUT, $buf);
         my $spg = ($page << 16) | ($ctrl & 0x3f7f);
         if ($spg != $$last_pg) {
            print STDERR sprintf("%03X.%04X\n", $page, $ctrl & 0x3f7f) if $opt_verbose;
            $$last_pg = $spg;
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

sub Capture {
   my $err;
   my $srv = VBI_SLICED_VPS | VBI_SLICED_TELETEXT_B | VBI_SLICED_TELETEXT_B_525;
   my $cap;
   if (defined($opt_dvbpid)) {
      $cap = Video::ZVBI::capture::dvb_new($opt_device, 0, $srv, 0, $err, $opt_debug);
      $cap->dvb_filter($opt_dvbpid) 
   } else {
      $cap = Video::ZVBI::capture::v4l2_new($opt_device, 6, $srv, 0, $err, $opt_debug);
   }

   die "Failed to open capture device: $err\n" unless $cap;

   my $start_t = time;
   my $last_pg = -1;
   my $vbi_fd = $cap->fd();

   for (;;) {
      my $rin="";
      vec($rin, $vbi_fd, 1) = 1;
      select($rin, undef, undef, 1);

      my ($buf, $lcount, $ts);
      my $ret = $cap->pull_sliced($buf, $lcount, $ts, 0);
      if (($ret > 0) && ($lcount > 0)) {
         for (my $idx = 0; $idx < $lcount; $idx++) {
            my @tmpl = Video::ZVBI::get_sliced_line($buf, $idx);
            if ($tmpl[1] & (VBI_SLICED_TELETEXT_B | VBI_SLICED_TELETEXT_B_525)) {
               FeedTtxPkg(\$last_pg, $tmpl[0]);
            } elsif ($tmpl[1] & VBI_SLICED_VPS) {
               FeedVps($tmpl[0]);
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
}

ParseArgv();
Capture();

