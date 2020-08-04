#!/usr/bin/perl -w
#
#  This script merges several XMLTV files (DTD 0.5) generated by the teletext
#  EPG grabber.
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
#  Copyright 2006-2008, 2020 by Tom Zoerner (tomzo at users.sf.net)
#

use strict;
use POSIX;

my %XmlData;
my %ChnOrder;
my @ChnTable;

my $opt_expire = 1;

# ------------------------------------------------------------------------------
# Read an XMLTV input file
# - note this is NOT a full XML parser
# - will only work with XMLTV files generated by the teletext EPG grabber
#
sub ReadXmlFile {
   my ($fname, $fidx) = @_;
   my $state = 0;
   my $tag_data;
   my $chn_id;
   my $start_t;
   my %CurChnMap;
   #my $exp_thresh = time - $opt_expire * 24*60*60;

   open(XML, "<$fname") || die "$0: cannot open $fname: $!\n";
   while ($_ = <XML>) {
      # handling XML header
      if ($state == 0) {
         if (/^\s*\<(\?xml|\!)[^>]*\>\s*$/i) {
         } elsif (/^\s*\<tv[^>]*>\s*$/i) {
            $state = 2;
         } elsif (/\S/) {
            chomp;
            die "$0: FATAL: Unexpected line '$_' in $fname\n";
         }
      } elsif ($state == 1) {
         die "$0: FATAL: Unexpected line '$_' following </tv> in $fname\n";

      # handling main section in-between <tv> </tv>
      } elsif ($state == 2) {
         if (/^\s*\<\/tv>\s*$/i) {
            $state = 1;
         } elsif (/^\s*\<channel/i) {
            $tag_data = $_;
            if ($tag_data =~ /id=\"([^"]*)\"/i) {
               $chn_id = $1;
               die "$0: FATAL: duplicate channel ID $chn_id within $fname\n" if defined($CurChnMap{$chn_id});
               $CurChnMap{$chn_id} = $chn_id;
            } else {
               die "$0: FATAL: Missing 'id' attribute in '$_' in $fname\n";
            }
            if (defined($ChnOrder{$chn_id})) {
               warn "$0: Warning: channel ID $chn_id seen 2nd time in $fname\n" if defined($ChnOrder{$chn_id});
               my $apx = 2;
               ++$apx while defined($ChnOrder{$chn_id . "__$apx"});
               $CurChnMap{$chn_id} = $chn_id . "__$apx";
               $chn_id .= "__$apx";
               $tag_data =~ s/id=\"([^"]*)\"/id="$chn_id"/i
            }
            $state = 3;
         } elsif (/^\s*\<programme/i) {
            $tag_data = $_;
            if (/start=\"([^"]*)\".*channel=\"([^"]*)\"/i) {
               $start_t = ParseTimestamp($1);
            } else {
               die "$0: FATAL: Missing 'start' and 'channel' attributes in '$_' in $fname\n";
            }
            $chn_id = $2;
            die "$0: Fatal: programme with unknown channel ID $chn_id in $fname\n" if !defined($ChnOrder{$chn_id});
            # replace duplicate IDs, where necessary
            if ($CurChnMap{$chn_id} ne $chn_id) {
               $chn_id = $CurChnMap{$chn_id};
               $tag_data =~ s/channel=\"([^"]*)\"/channel=\"$chn_id\"/i;
            }
            $state = 4;
         } elsif (/\S/) {
            die "$0: FATAL: Unexpected tag '$_' in $fname; expect <channel> or <programme>\n";
         }

      # handling channel data
      } elsif ($state == 3) {
         $tag_data .= $_;
         if (/^\s*\<\/channel>\s*$/i) {
            # FIXME? multiple channels in same XML file get same index
            $ChnOrder{$chn_id} = $fidx;
            push @ChnTable, $tag_data;
            $ChnOrder{$chn_id} = $tag_data;
            $state = 2;
            undef $chn_id;
         }

      # handling programme data
      } elsif ($state == 4) {
         $tag_data .= $_;
         if (/^\s*\<\/programme>\s*$/i) {
            #if ($start_t >= $exp_thresh) {
               $XmlData{"$start_t;$chn_id"} = $tag_data;
            #}
            $state = 2;
         }
      }

   }
   close(XML);
}

# ------------------------------------------------------------------------------
# Parse an XMLTV timestamp (DTD 0.5)
# - we expect local timezone only (since this is what the ttx grabber generates)
#
sub ParseTimestamp {
   # format "YYYYMMDDhhmmss ZZzzz"
   my ($Y,$M,$D,$h,$m,$s) = ($_[0] =~ /^(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2}) \+0000$/);

   return POSIX::mktime($s, $m, $h, $D, $M - 1, $Y - 1900, 0, 0, -1);
}

# ------------------------------------------------------------------------------
# Command line argument parsing
#
sub ParseArgv {
   my $usage = "Usage: $0 [-expire <days>] <file> ...\n";

   if ($#ARGV < 0) {
      die "$0: no input files given\n";
   }

   while ($ARGV[0] =~ /^-/) {
      $_ = shift @ARGV;

      # -expire: number of days which to allow
      if (/^-expire$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_expire = shift @ARGV;
         die "$_ requires a numerical argument\n$usage" unless $opt_expire =~ /^\d+$/;

      } elsif (/^-h(elp)?$/) {
         die $usage;
      } else {
         die "$_: unknown argument\n$usage";
      }
   }
}

# ------------------------------------------------------------------------------
# Main
#

# parse command line arguments
ParseArgv();

my $file;
my $idx = 0;
foreach $file (@ARGV) {
   ReadXmlFile($file, $idx++);
}

#sub SortXmlData {
#   my @A = split(/;/, $a);
#   my @B = split(/;/, $b);
#   if ($A[0] == $B[0]) {
#      return $A[1] cmp $B[1];
#   } else {
#      return $A[0] <=> $B[0];
#   }
#}

print "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n".
      "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n".
      "<tv>\n";
print join "", @ChnTable;
#foreach (sort SortXmlData keys(%XmlData)) {
foreach (sort keys(%XmlData)) {
   print "\n";
   print $XmlData{$_};
}
print "</tv>\n";
