#!/usr/bin/perl -w
#
#  This is a demo how to capture EPG data from multiple channels. Tool
#  The script will tune all the channels given in the list and capture
#  teletext from it for 90 seconds, then try scraping EPG from it and
#  write it to an XML file. At the end all XML files are merged into one.
#
#  Tool "tune_dvb.c" is used for channel changing: use "make tune_dvb"
#  for building the executable.
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
#  Copyright 2006-2008,2020 by Tom Zoerner (tomzo at users.sf.net)
#

use strict;

my $channels_conf = "/home/tom/.vdr/channels.conf";

my @Nets = (
   ["Das Erste", "ard"],
   ["ZDF", "zdf"],
   ["arte", "arte"],
   ["KiKA", "kika"],
   ["3sat", "3sat"],
   ["ONE", "ard-one"],
   ["WDR", "wdr"],
   ["TELE 5", "tele5"],
   ["SAT.1", "sat1"],
   ["RTL", "rtl"],
   ["RTLplus", "rtl-plus"],
   ["ProSieben", "pro7"],
   ["RTL II", "rtl2"],
   ["VOX", "vox"],
   ["VOXup", "vox-up"],
   ["sixx", "sixx"],
   ["ONE", "ard-one"],
   ["ServusTV", "servus"],
   ["DMAX", "dmax"],
   #["MTV", "mtv"],    # no teletext
   #["VIVA", "viva"],  # not received
   ["MDR", "mdr"],
   ["NDR FS MV", "ndr"],
   ["SWR RP", "swr"],
   ["BR3", "br3"],
   ["hr-fernsehen", "hr3"],
   ["rbb Berlin", "rbb"],
   ["ARD-alpha", "bralpha"],
   ["phoenix", "phoenix"],
   ["kabel eins", "kabel1"],
   ["SUPER RTL", "srtl"],
   #["DSF", "dsf"],  # not received
   ["Eurosport 1", "eurosport"],
   #["TV 5 Monde", "tv5"],       # no teletext
   ["n-tv", "ntv", "500-599"],
   #["n24 Doku", "n24"],         # not received
   #["CNN", "cnni", "200-299"],  # no teletext
   #["9live", "9live"],          # not received
);

my $device = "/dev/dvb/adapter0/demux0";
my $duration = 90;

for (my $idx = 0; $idx <= $#Nets; $idx++) {
   # get parameters from the list above
   my ($chan, $fname, $pages) = @{$Nets[$idx]};
   $pages = "300-399" if !defined $pages;

   # query the Teletext PID
   my $tid = 0;
   if (open(TUNE, "./tune_dvb -c \"$channels_conf\" -p \"$chan\"|")) {
      $tid = <TUNE>;
      chomp($tid);
   }
   if ($tid != 0) {
      # tune the channel
      #system "v4lctl -c $device setstation \"$chan\"";
      if (open(TUNE, "|./tune_dvb -c \"$channels_conf\" \"$chan\"")) {
         print "Capturing from $chan (PID:$tid, pages $pages)\n";

         # build the grabber command line
         # - capture into temporary file
         # - merge output with previous XML file of same name, if one exists
         my $out_file = "ttx-$fname.xml";
         my $cmd  = "./tv_grab_ttx -dev $device -duration $duration";
         $cmd .= " -dvbpid $tid";
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
         close(TUNE);
      }
      else {
         #print "Failed to tune $chan\n";  # printed by sub-command
      }
   }
   else {
      print "No teletext PID for $chan\n";
   }
}

system "./merge.pl ttx-*.xml > tv.xml";
