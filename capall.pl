#!/usr/bin/perl -w
#
#  This is a demo how to capture EPG data from multiple channels: The
#  script will tune all the channels given in the list. For each channel
#  it will capture teletext pages in the given ranges for 90 seconds, then
#  try scraping TV schedules from them and write the result to a file in
#  XMLTV format (while merging the new EPG data with the one present in
#  the file). The latter is done in parallel for all channels sharing a
#  transponder. At the end all XML files in the current directory are
#  merged into one using helper script merge.pl
#
#  This script does not parse command line parameters. Please edit the
#  configuration variables at the top of the script as needed.
#
#  Tool "util/tune_dvb.c" is used for tuning the DVB front-end: use "make
#  tune_dvb" for building the executable. Note currently the tool only
#  supports channel tables in VDR format, as this is the only one that
#  includes the teletext PID of each channel.
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

#
# Configuration variables
#
my $channels_conf = "$ENV{HOME}/.vdr/channels.conf";
my $device = "/dev/dvb/adapter0/demux0";
my $def_duration = 90;
my $def_pages = "300-399";

my @Nets = (
   ["Das Erste", "ard"],
   ["ZDF", "zdf"],
   ["arte", "arte", "300-699"],    # today on 300-300, next 600-699
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
   ["ServusTV", "servus"],
   ["DMAX", "dmax"],
   #["MTV", "mtv"],    # no teletext
   ["MDR", "mdr"],
   ["NDR FS MV", "ndr"],
   ["SWR RP", "swr"],
   ["BR3", "br3"],
   ["hr-fernsehen", "hr3", undef, 120],
   ["rbb Berlin", "rbb"],
   ["ARD-alpha", "bralpha"],
   ["phoenix", "phoenix"],
   ["kabel eins", "kabel1"],
   ["SUPER RTL", "srtl"],
   ["Eurosport 1", "eurosport"],
   #["TV 5 Monde", "tv5"],       # no teletext
   ["n-tv", "ntv", "500-599"],
   #["CNN", "cnni", "200-299"],  # no teletext
);

# catch signal "INT" for removing "*.tmp" files created for tv_grab_ttx output redirection
my @SigTmps = ();
sub sig_handler {
   my ($sig) = @_;
   print "Caught a SIG$sig - shutting down\n";
   unlink($_) foreach (@SigTmps);
   @SigTmps = ();
   exit(1);
};
$SIG{'INT'}  = \&sig_handler;

while (@Nets) {
   # get name of first channel that's not done yet
   my $chan = $Nets[0][0];

   # query for list of all channels sharing the transponder of this channel
   my @tids = ();
   if (open(TUNE, "./tune_dvb -c \"$channels_conf\" -P \"$chan\"|")) {
      while (<TUNE>)
      {
         if (/(\d+)\t(.*)/) {
            # search returned channel name in the "Nets" list
            for (my $idx = 0; $idx <= $#Nets; $idx++) {
               if ($Nets[$idx][0] eq $2) {
                  # match -> schedule this channel in this iteration
                  push(@tids, [$1, $Nets[$idx][1], $Nets[$idx][2], $Nets[$idx][3]]);
                  # remove channel from "Nets" list as it is done after this iteration
                  splice(@Nets, $idx, 1);
                  last;
               }
            }
         }
      }
   }
   if (@tids) {
      # tuning option for analog TV card (obsolete)
      #system "v4lctl -c $device setstation \"$chan\"";

      # tune the transponder of the selected channels
      # FIXME open does not return error when tuning fails
      if (open(TUNE, "|./tune_dvb -c \"$channels_conf\" \"$chan\"")) {

         print "Capturing from $chan (".scalar(@tids)." PIDs)\n";
         my @pids = ();
         # run tv_grab_ttx for each of the channels as background process
         foreach (@tids) {
            my ($tid, $fname, $pages, $duration) = @$_;
            $pages = $def_pages if !defined $pages;
            $duration = $def_duration if !defined $duration;
            print "- capturing $fname (PID $tid, pages $pages, duration $duration)\n";
            my $out_file = "ttx-$fname.xml";
            push @SigTmps, "${out_file}.tmp";

            my $pid = fork();
            die "fork: $!\n" unless defined $pid;

            if ($pid == 0) {
               # child process: build the grabber command line & exec tv_grab_ttx
               # - capture into temporary file
               # - merge output with previous XML file of same name, if one exists
               open(STDOUT, '>', "${out_file}.tmp") or die "Can't redirect STDOUT >${out_file}.tmp: $!";

               my @cmd  = ("./tv_grab_ttx", "-dev", $device,
                                            "-duration", $duration,
                                            "-page", $pages);
               push(@cmd, "-dvbpid", $tid);
               push(@cmd, "-merge", $out_file) if -e $out_file && !-z $out_file;
               exec @cmd;
               die "exec @cmd: $!\n";
            }
            else {
               # parent process: remember the child PID for waiting below
               push @pids, [$pid, $out_file];
            }
         }
         # wait for all tv_grab_ttx processes to be done
         foreach my $pid (@pids) {
            if (waitpid($pid->[0], 0) == $pid->[0]) {
               my $stat = $?;
               my $out_file = $pid->[1];

               # check exit code of tv_grab_ttx: discard output if not zero
               if (($stat >> 8) == 0) {
                  if (-e "${out_file}.tmp") {
                     # replace XML file for that channel with "*.tmp" file created by tv_grab_ttx output
                     rename("${out_file}.tmp", $out_file) || warn "error renaming/replacing $out_file: $!\n";
                  }
                  else {
                     warn "missing output file ${out_file}.tmp\n";
                  }
               }
               else {
                  warn "tv_grab_ttx failed for $out_file\n";
                  unlink "${out_file}.tmp";
               }
            }
         }
         @SigTmps = ();
         close(TUNE);
      }
      else {
         #print "Failed to tune $chan\n";  # printed by sub-command
      }
   }
   else {
      print "No teletext PID for $chan\n";
      splice(@Nets, 0, 1);
   }
}

print "Finally merging all captured XMLTV files into tv.xml\n";
system "./merge.pl ttx-*.xml > tv.xml";
