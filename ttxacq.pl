#!/usr/bin/perl -w
#
#  This script grabs TV schedules from teletext pages and exports them
#  in XMLTV format (DTD version 0.5)  Input is a file with pre-processed
#  VBI data captured from a TV card.
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
#  $Id: ttxacq.pl,v 1.1 2006/04/23 17:39:56 tom Exp tom $
#

use POSIX;
use locale;
use strict;

my %Pkg;
my %PgCnt;
my %PgSub;
my %PgLang;
my $VbiCaptureTime;

my $opt_infile;
my $opt_chname;
my $opt_chid;
my $opt_dump = 0;
my $opt_debug = 0;
my $opt_verify = 0;
my $opt_tv_start = 0x301;
my $opt_tv_end = 0x399;

# ------------------------------------------------------------------------------
# Command line argument parsing
#
sub ParseArgv {
   my $usage = "Usage: $0 [-page <NNN>-<MMM>] [-chn_name <name>] [-chn_id <ID>] <file>\n".
               "Additional debug options: -debug -dump -dumpraw -test <file>\n";

   while ($_ = shift @ARGV) {
      # -chn_name: display name in XMLTV <channel> tag
      if (/^-chn_name$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_chname = shift @ARGV;

      # -chn_name: channel ID in XMLTV <channel> tag
      } elsif (/^-chn_id$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_chid = shift @ARGV;

      # -page <NNN>-<MMM>: specify range of programme overview pages
      } elsif (/^-pages?$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $_ = shift @ARGV;
         if (m#^([0-9]{3})\-([0-9]{3})$#) {
            $opt_tv_start = hex($1);
            $opt_tv_end = hex($2);
            die "-page: not a valid start page number: $opt_tv_start" if (($opt_tv_start < 0x100) || ($opt_tv_start > 0x899));
            die "-page: not a valid end page number: $opt_tv_end" if (($opt_tv_end < 0x100) || ($opt_tv_end > 0x899));
            die "-page: start page must be <= end page" if ($opt_tv_end < $opt_tv_start);
         } else {
            die "-page: invalid parameter: $_, expecting two 3-digit ttx page numbers\n";
         }

      # -debug: enable debug output
      } elsif (/^-debug$/) {
         $opt_debug = 1;

      # -dump: print all teletext pages, then exit (for debugging only)
      } elsif (/^-dump$/) {
         $opt_dump = 1;

      # -dumpraw: write teletext pages in binary, suitable for verification
      } elsif (/^-dumpraw$/) {
         $opt_dump = 2;

      # -test: read pre-processed teletext from file (for verification only)
      } elsif (/^-verify$/) {
         $opt_verify = 1;

      } elsif (/^-(help|\?)$/) {
         print STDERR $usage;
         exit;

      } elsif (/^-/) {
         die "$_: unknown argument\n";

      } else {
         $opt_infile = $_;
         last;
      }
   }

   die $usage unless defined($opt_infile) && $#ARGV<0;
}

# ------------------------------------------------------------------------------
# Read VBI data from a file
# - the data has already been pre-processed by cap.pl
# - each record has a small header plus 40 bytes payload (see unpack below)
# - TODO: use MIP to find TV overview pages
# - TODO: read CNI from VPS/PDC to detect channel name and ID
#
sub ReadVbi {
   my @CurPage = (-1, -1, -1, -1, -1, -1, -1, -1);
   my @CurSub = (0,0,0,0,0,0,0,0,0);
   my @CurPkg = (0,0,0,0,0,0,0,0,0);
   my $cur_mag = 0;
   my $buf;
   my $intv;
   my $line_off;
   my $rstat;
   my $mag_serial = 0;
   my ($mag, $page, $sub, $ctl1, $ctl2, $pkg, $data);

   open(TTX, "<$opt_infile") || die "open $opt_infile: $!\n";
   $VbiCaptureTime = (stat($opt_infile))[9];
   $intv = 0;
   while (1) {
      $line_off = 0;
      while (($rstat = sysread(TTX, $buf, 46 - $line_off, $line_off)) > 0) {
         $line_off += $rstat;
         last if $line_off >= 46;
      }
      last if $rstat <= 0;

      ($page, $ctl1, $ctl2, $pkg, $data) = unpack("SSCCa40", $buf);
      $page += 0x800 if $page < 0x100;
      $mag = ($page >> 8) & 7;
      $sub = $ctl1 & 0x3f7f;
      #printf "%03X.%04X, %2d %.40s\n", $page, $sub, $pkg, $data;

      if ($mag_serial && ($mag != $cur_mag)) {
         $CurPage[$cur_mag] = -1;
      }

      if ($pkg == 0) {
         # erase page control bit (C4) - ignored, some channels abuse this
         #if ($ctl1 & 0x80) {
         #   VbiPageErase($page);
         #}
         # inhibit display control bit (C10)
         #if ($ctl2 & 0x08) {
         #   $CurPage[$mag] = -1;
         #   next;
         #}
         if ($ctl2 & 0x01) {
            $data = "";
         }
         if ( (($page & 0x0F) > 9) || ((($page >> 4) & 0x0f) > 9) ) {
            $page = $CurPage[$mag] = -1;
            next;
         }
         # magazine serial mode (C11: 1=serial 0=parallel)
         $mag_serial = (($ctl2 & 0x10) != 0);
         if (!defined($PgCnt{$page})) {
            # new page
            $Pkg{$page|($sub<<12)} = [];
            $PgCnt{$page} = 1;
            $PgSub{$page} = $sub;
            # store G0 char set (bits must be reversed: C12,C13,C14)
            $PgLang{$page} = (($ctl2 >> 7) & 1) | (($ctl2 >> 5) & 2) | (($ctl2 >> 3) & 4);
         } elsif (($page != $CurPage[$mag]) || ($sub != $CurSub[$mag])) {
            # repeat page (and not just repeated page header, e.g. as filler packet)
            # TODO count sub-pages separately
            $PgCnt{$page} += 1;
            $PgSub{$page} = $sub if $sub >= $PgSub{$page};
         }

         # check if every page was received twice in average
         # (only do the check every 50th page to save CPU)
         $intv += 1;
         if ($intv >= 50) {
            my $page_cnt = 0;
            my $page_rep = 0;
            foreach (keys %PgCnt) {
               $page_cnt += 1;
               $page_rep += $PgCnt{$_};
            }
            last if ($page_cnt > 0) && (($page_rep / $page_cnt) >= 10);
            $intv = 0;
         }

         $CurPage[$mag] = $page;
         $CurSub[$mag] = $sub;
         $CurPkg[$mag] = 0;
         $cur_mag = $mag;
      } else {
         $page = $CurPage[$mag];
         $sub = $CurSub[$mag];
         # page body packets are supposed to be in order
         if (($page != -1) && ($pkg < $CurPkg[$mag])) {
            $page = $CurPage[$mag] = -1;
         }
         $CurPkg[$mag] = $pkg if $pkg < 26;
      }
      if (($pkg == 0) || ($page != -1))
      {
         $Pkg{$page|($sub<<12)}->[$pkg] = $data;
      }
   }
   close(TTX);
}

# Erase the page with the given number from memory
# used to handle the "erase" control bit in the TTX header
sub VbiPageErase {
   my ($page) = @_;
   my $sub;
   my $pkg;
   my $handle;

   if (defined($PgSub{$page})) {
      for ($sub = 0; $sub <= $PgSub{$page}; $sub++) {
         delete $Pkg{$page|($sub<<12)};
      }
      delete $PgCnt{$page};
      delete $PgSub{$page};
   }
}

# ------------------------------------------------------------------------------
# Dump all loaded teletext pages as text
# - teletext control characters and mosaic is replaced by space
# - used for -dump option, intended for debugging only
#
sub DumpAllPages {
   my $page;

   foreach $page (sort {$a<=>$b} keys(%PgCnt)) {
      my $pkg;
      my ($sub, $sub1, $sub2);
      my $handle;

      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         $handle = $page | ($sub << 12);
         printf "PAGE %03X.%04X\n", $page, $sub;
         for ($pkg = 1; $pkg <= 23; $pkg++) {
            if (defined($Pkg{$handle}->[$pkg])) {
               print TtxToLatin1($Pkg{$handle}->[$pkg], $PgLang{$page}) . "\n";
            } else {
               print "\n";
            }
         }
         print "\n";
      }
   }
}

# ------------------------------------------------------------------------------
# Dump all loaded teletext data as Perl script
# - the generated script can be loaded with the -verify option
#
sub DumpRawTeletext {
   my $page;

   print "#!/usr/bin/perl -w\n".
         "\$VbiCaptureTime = $VbiCaptureTime;\n";

   foreach $page (sort {$a<=>$b} keys(%PgCnt)) {
      my $pkg;
      my ($sub, $sub1, $sub2);
      my $handle;

      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         $handle = $page | ($sub << 12);

         printf("\$PgCnt{0x%03X} = %d;\n", $page, $PgCnt{$page});
         printf("\$PgSub{0x%03X} = %d;\n", $page, $PgSub{$page});
         printf("\$PgLang{0x%03X} = %d;\n", $page, $PgLang{$page});

         printf("\$Pkg{0x%03X|(0x%04X<<12)} =\n\[\n", $page, $sub);

         for ($pkg = 1; $pkg <= 23; $pkg++) {
            $_ = $Pkg{$handle}->[$pkg];
            if (defined($_)) {
               # quote Perl special characters
               s#([\"\$\@\%\\])#\\$1#g;
               # quote binary characters
               s#([\x00-\x1F\x7F])#"\\x".sprintf("%02X",ord($1))#eg;
               print "  \"$_\",\n";
            } else {
               print "  undef,\n";
            }
         }
         print "\];\n";
      }
   }
   # return TRUE to allow to "require" the file
   print "1;\n";
}

# ------------------------------------------------------------------------------
# Import a data file generated by DumpRawTeletext
#
sub ImportRawDump {
   #require $opt_infile;
   my $file = "";

   open(DUMP, "<$opt_infile") || die "open $opt_infile: $!\n";
   while ($_ = <DUMP>) {
      $file .= $_;
   }
   close(DUMP);

   eval $file || die "syntax error in $opt_infile: $@\n";
}

# ------------------------------------------------------------------------------
# Conversion of teletext G0 charset into ISO-8859-1
#
my @NationalOptionsMatrix =
(
   #0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x00
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x10
   -1, -1, -1,  0,  1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x20
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x30
    2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x40
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  3,  4,  5,  6,  7,  # 0x50
    8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  # 0x60
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  9, 10, 11, 12, -1   # 0x70
);

my @NatOptMaps =
(
    # for latin-1 font
    # English (100%)
    '£$@«½»¬#­¼¦¾÷',
    # German (100%)
    '#$§ÄÖÜ^_°äöüß',
    # Swedish/Finnish/Hungarian (100%)
    '#¤ÉÄÖÅÜ_éäöåü',
    # Italian (100%)
    '£$é°ç»¬#ùàòèì',
    # French (100%)
    'éïàëêùî#èâôûç',
    # Portuguese/Spanish (100%)
    'ç$¡áéíóú¿üñèà',
    # Czech/Slovak (60%)
    '#uctzýíréáeús',
    # reserved (English mapping)
    '£$@«½»¬#­¼¦¾÷',
    # Polish (6%: all but '#' missing)
    '#naZSLcoezslz',
    # German (100%)
    '#$§ÄÖÜ^_°äöüß',
    # Swedish/Finnish/Hungarian (100%)
    '#¤ÉÄÖÅÜ_éäöåü',
    # Italian (100%)
    '$é°ç»¬#ùàòèì',
    # French (100%)
    'ïàëêùî#èâôûç',
    # reserved (English mapping)
    '$@«½»¬#­¼¦¾÷',
    # Czech/Slovak 
    'uctzýíréáeús',
    # reserved (English mapping)
    '$@«½»¬#­¼¦¾÷',
    # English 
    '$@«½»¬#­¼¦¾÷',
    # German
    '$§ÄÖÜ^_°äöüß',
    # Swedish/Finnish/Hungarian (100%)
    '¤ÉÄÖÅÜ_éäöåü',
    # Italian (100%'
    '$é°ç»¬#ùàòèì',
    # French (100%)
    'ïàëêùî#èâôûç',
    # Portuguese/Spanish (100%)
    '$¡áéíóú¿üñèà',
    # Turkish (50%: 7 missing)
    'gISÖÇÜGisöçü',
    # reserved (English mapping)
    '$@«½»¬#­¼¦¾÷'
);

# TODO: evaluate packet 26 and 28
sub TtxToLatin1 {
   my($line, $g0_set) = @_;
   my $idx;
   my $val;

   my $is_g1 = 0;
   for ($idx = 0; $idx < length($line); $idx++) {
      $val = ord(substr($line, $idx, 1));

      # alpha color code
      if ($val <= 0x07) {
         $is_g1 = 0;
         substr($line, $idx, 1) = ' ';

      # mosaic color code
      } elsif (($val >= 0x10) && ($val <= 0x17)) {
         $is_g1 = 1;
         substr($line, $idx, 1) = ' ';

      # other control character or mosaic character
      } elsif (($val < 0x20) || ($val == 0x7f) || $is_g1) {
         substr($line, $idx, 1) = ' ';

      } elsif ( ($val < 0x80) && ($NationalOptionsMatrix[$val] >= 0) ) {
         if ($g0_set <= $#NatOptMaps) {
            $val = substr($NatOptMaps[$g0_set], $NationalOptionsMatrix[$val], 1);
         } else {
            $val = ' ';
         }
         substr($line, $idx, 1) = $val;
      }
   }
   return $line;
}

sub PageToLatin {
   my ($page, $sub) = @_;
   my $pkg;
   my $handle;
   my @PgText = ("");

   $handle = sprintf("%03X.%04X.0", $page, $sub);
   for ($pkg = 1; $pkg <= 23; $pkg++) {
      $handle = $page | ($sub << 12);
      if (defined($Pkg{$handle}->[$pkg])) {
         push @PgText, TtxToLatin1($Pkg{$handle}->[$pkg], $PgLang{$page});
      } else {
         push @PgText, " " x 40;
      }
   }
   return @PgText;
}

# ------------------------------------------------------------------------------
# Tables to support parsing of dates in various languages
# - the hash tables map each word to an array with 3 elements:
#   0: bitfield of language (language indices as in teletext header)
#      use a bitfield because some words are identical in different languages
#   1: value (i.e. month or weekday index)
#   2: bitfield abbreviation: 1:non-abbr., 2:abbr., 3:no change by abbr.
#      use a bitfield because some short words are not abbreviated (e.g. "may")

# map month names to month indices 1..12
my %MonthNames =
(
   # English (minus april, august, september)
   "january" => [(1<<0),1,1], "february" => [(1<<0),2,1], "march" => [(1<<0),3,1],
   "may" => [(1<<0),5,3], "june" => [(1<<0),6,1], "july" => [(1<<0),7,1],
   "october" => [(1<<0),10,1], "december" => [(1<<0),12,1],
   "mar" => [(1<<0),3,2], "oct" => [(1<<0),10,2], "dec" => [(1<<0),12,1],
   # German
   "januar" => [(1<<1),1,1], "februar" => [(1<<1),2,1], "märz" => [(1<<1),3,1],
   "april" => [(1<<1)|(1<<0),4,1], "mai" => [(1<<4)|(1<<1),5,3], "juni" => [(1<<1),6,1],
   "juli" => [(1<<1),7,1], "august" => [(1<<1)|(1<<0),8,1], "september" => [(1<<1)|(1<<0),9,1],
   "oktober" => [(1<<1),10,1], "november" => [(1<<1)|(1<<0),11,1], "dezember" => [(1<<1),12,1],
   "jan" => [(1<<1)|(1<<0),1,2], "feb" => [(1<<1)|(1<<0),2,2], "mär" => [(1<<1),3,2],
   "apr" => [(1<<1)|(1<<0),4,2], "jun" => [(1<<1)|(1<<0),6,2], "jul" => [(1<<1)|(1<<0),7,2],
   "aug" => [(1<<1)|(1<<0),8,2], "sep" => [(1<<1)|(1<<0),9,2], "okt" => [(1<<1),10,2],
   "nov" => [(1<<1)|(1<<0),11,2], "dez" => [(1<<1),12,2],
   # French
   "janvier" => [(1<<4),1,1], "février" => [(1<<4),2,1], "mars" => [(1<<4),3,1],
   "avril" => [(1<<4),4,1],
   "juin" => [(1<<4),6,1], "juillet" => [(1<<4),7,1], "août" => [(1<<4),8,1],
   "septembre" => [(1<<4),9,1], "octobre" => [(1<<4),10,1],
   "novembre" => [(1<<4),11,1], "décembre" => [(1<<4),12,1]
);

# map week day names to indices 0..6 (0=sunday)
my %WDayNames =
(
   # English
   "sat" => [(1<<0),6,2], "sun" => [(1<<0),0,2], "mon" => [(1<<0),1,2], "tue" => [(1<<0),2,2],
   "wed" => [(1<<0),3,2], "thu" => [(1<<0),4,2], "fri" => [(1<<0),5,2],
   "saturday" => [(1<<0),6,1], "sunday" => [(1<<0),0,1], "monday" => [(1<<0),1,1], "tuesday" => [(1<<0),2,1],
   "wednesday" => [(1<<0),3,1], "thursday" => [(1<<0),4,1], "friday" => [(1<<0),5,1],
   # German
   "sa" => [(1<<1),6,2], "so" => [(1<<1),1,2], "mo" => [(1<<1),1,2], "di" => [(1<<1),2,2],
   "mi" => [(1<<1),3,2], "do" => [(1<<1),4,2], "fr" => [(1<<1),5,2],
   "samstag" => [(1<<1),6,1], "sonnabend" => [(1<<1),6,1], "sonntag" => [(1<<1),1,1],
   "montag" => [(1<<1),1,1], "dienstag" => [(1<<1),2,1], "mittwoch" => [(1<<1),3,1],
   "donnerstag" => [(1<<1),4,1], "freitag" => [(1<<1),5,1],
   # French
   "samedi" => [(1<<4),6,1], "dimanche" => [(1<<4),0,1], "lundi" => [(1<<4),1,1], "mardi" => [(1<<4),2,1],
   "mercredi" => [(1<<4),3,1], "jeudi" => [(1<<4),4,1], "vendredi" => [(1<<4),5,1]
);

# map today, tomorrow etc. to day offsets 0,1,...
my %RelDateNames =
(
   "today" => [(1<<0),0,1], "tomorrow" => [(1<<0),1,1],
   "heute" => [(1<<1),0,1], "morgen" => [(1<<1),1,1], "übermorgen" => [(1<<1),2,1],
   "aujourd'hui" => [(1<<4),0,1], "demain" => [(1<<4),1,1], "aprés-demain" => [(1<<4),2,1]
);

# return a reg.exp. pattern which matches all names of a given language
sub GetDateNameRegExp {
   my ($href, $lang, $abbrv) = @_;
   my $def;

   $lang = 1 << $lang;

   my @Pat = ();
   foreach (keys(%{$href})) {
      $def = $href->{$_};
      if (($def->[0] & $lang) != 0) {
         if (!defined($abbrv) || (($def->[2] & $abbrv) != 0)) {
            push @Pat, $_;
         }
      }
   }
   return join("|", sort(@Pat));
}

# ------------------------------------------------------------------------------
#  Determine layout of programme overview pages
#  (to allow stricter parsing)
#  - TODO: detect color used to mark features (e.g. yellow on ARD) (WDR: ttx ref same color as title)
#  - TODO: detect color used to distinguish title from subtitle/category (WDR,Tele5)
#
sub DetectOvFormat {
   my ($page, $pkg);
   my ($sub, $sub1, $sub2);
   my %FmtStat = ();
   my %SubtStat = ();
   my @Page;

   # look at the first 5 pages (default start at page 301)
   my @Pages = grep {($_ >= $opt_tv_start)&&($_ <= $opt_tv_end)} keys(%PgCnt);
   @Pages = sort {$a<=>$b} @Pages;
   splice(@Pages, 5) if $#Pages >= 5;

   foreach $page (@Pages) {
      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         @Page = PageToLatin($page, $sub);

         #DetectOvVPS($page, $sub, @Page);
         DetectOvFormatParse(\%FmtStat, \%SubtStat, @Page);
      }
   }

   my @FmtSort = sort { $FmtStat{$b} <=> $FmtStat{$a} } keys %FmtStat;
   my @SubtSort = sort { $SubtStat{$b} <=> $SubtStat{$a} } keys %SubtStat;
   my $fmt;
   if (defined($FmtSort[0])) {
      $fmt = $FmtSort[0];
      $fmt .= ",". (defined($SubtSort[0]) ? $SubtSort[0] : 40);
      print "auto-detected overview format: $fmt\n" if $opt_debug;
   } else {
      $fmt = undef;
      print "auto-detected overview failed, pages ".join(",",@Pages)."\n" if $opt_debug;
   }
   return $fmt;
}

# ! 20.15  Eine Chance für die Liebe UT         ARD
#           Spielfilm, D 2006 ........ 344
#                     ARD-Themenwoche Krebs         
#
# 19.35  1925  WISO ................. 317       ZDF
# 20.15        Zwei gegen Zwei           
#              Fernsehfilm D/2005 UT  318
#
# 10:00 WORLD NEWS                              CNNi
# News bulletins on the hour (...)
# 10:30 WORLD SPORT                      
#
sub DetectOvFormatParse {
   my ($fmt, $subt, @Page) = @_;
   my ($off, $time_off, $vps_off, $title_off, $subt_off);
   my ($line, $handle);

   for ($line = 7; $line <= 20; $line++) {
      $_ = $Page[$line];
      # look for a line containing a start time
      # TODO allow start-stop times "10:00-11:00"
      if ( (m#^( *| *\! +)(\d\d)[\.\:](\d\d) +#) &&
           ($2 < 24) && ($3 < 60) ) {

         $off = length $&;
         $time_off = length $1;

         # TODO VPS must be magenta or concealed
         if (substr($_, $off) =~ m#^(\d{4}) +#) {
            $vps_off = $off;
            $off += length $&;
            $title_off = $off;
         } else {
            $vps_off = -1;
            $title_off = $off;
         }
         $handle = "$time_off,$vps_off,$title_off";
         if (defined ($fmt->{$handle})) {
            $fmt->{$handle} += 1;
         } else {
            $fmt->{$handle} = 1;
         }

         if ($Page[$line + 1] =~ m#^( *| *\d{4} +)[[:alpha:]]#) {
            $subt_off = length($1);
            if (defined ($subt->{$subt_off})) {
               $subt->{$subt_off} += 1;
            } else {
               $subt->{$subt_off} = 1;
            }
         }
      }
   }
}

# ------------------------------------------------------------------------------
# Search a page for the "VPS" identifier (i.e. the string "VPS)
# - the label marks the position of VPS times in an overview table
#
sub DetectOvVPS {
   my ($page, $sub, @Page) = @_;
   my ($line, $handle);
   my $vps_off = -1;

   for ($line = 1; $line <= 23; $line++) {
      $handle = $page | ($sub << 12);
      if (defined($Pkg{$handle}->[$line])) {
         # "VPS" is always in magenta (color code 5)
         if ($Pkg{$handle}->[$line] =~ m#^([ \x00-\x1F]*\x05[ \x08-\x0f\x18-\x1C]*)VPS#) {
            $vps_off += length($1);
            last;
         }
      }
   }
   return $vps_off;
}

# ------------------------------------------------------------------------------
# Chop header & parse date
#
# Mi 05.04.06
# Mittwoch, 5. April 2006
# TODO: use m##g (also for desc page parsing)
#
sub ParseOvHeader {
   my ($page, $sub, $pgfmt, @Page) = @_;
   my $head;
   my $reldate;
   my ($day_of_month, $month, $year);
   my ($time_off, $vps_off, $title_off, $subt_off) = split(",", $pgfmt);

   my $lang = $PgLang{$page};
   my $wday_match = GetDateNameRegExp(\%WDayNames, $lang, 1);
   my $wday_abbrv_match = GetDateNameRegExp(\%WDayNames, $lang, 2);
   my $mname_match = GetDateNameRegExp(\%MonthNames, $lang, undef);
   my $relday_match = GetDateNameRegExp(\%RelDateNames, $lang, undef);

   for ($head = 1; $head <= $#Page; $head++) {
      $_ = $Page[$head];

      # TODO dont use if/else, try multiple matches per line
      # stop parsing before first programme list entry
      if ($vps_off >= 0) {
         if ( (substr($_, 0, $time_off) =~ m#^ *([\*\!] +)?$#) &&
              (substr($_, $time_off, $vps_off - $time_off) =~ m#^(\d\d)[\.\:](\d\d) +$#) &&
              (substr($_, $vps_off, $title_off - $vps_off) =~ m#^(\d{4})? +$#) ) {
            last;
         }
      } else {
         if ( (substr($_, 0, $time_off) =~ m#^ *([\*\!] +)?$#) &&
              (substr($_, $time_off, $title_off - $time_off) =~ m#^(\d\d)[\.\:](\d\d) +$#) ) {
            last;
         }
      }

      # VPS label "1D102 120406 F9"
      #           "1D133 170406 C3"
      if (m#^ +[0-9A-F]{2}\d{3} (\d{2})(\d{2})(\d{2}) [0-9A-F]{2} *$#) {
         ($day_of_month, $month, $year) = ($1, $2, $3 + 2000);
         $head += 1;
         last;

      # [Mo.]13.04.2006
      } elsif (m#(^| |($wday_abbrv_match)\.)(\d{1,2})\.(\d{1,2})\.(\d{2}|\d{4})?([ ,:]|$)#i) {
         my @NewDate = CheckDate($3, $4, $5, undef, undef);
         if (@NewDate) {
            ($day_of_month, $month, $year) = @NewDate;
         }

      # 13.April [2006]
      } elsif (m#(^| )(\d{1,2})\. ?($mname_match)( (\d{2}|\d{4}))?([ ,:]|$)#i) {
         my @NewDate = CheckDate($2, undef, $5, undef, $3);
         if (@NewDate) {
            ($day_of_month, $month, $year) = @NewDate;
         }

      # Sunday 22 April (i.e. no dot after month)
      } elsif ( m#(^| )($wday_match)(, ?| )(\d{2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|$)#i ||
           m#(^| )($wday_abbrv_match)(\.,? ?|, ?| )(\d{2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|$)#i) {
         my @NewDate = CheckDate($4, undef, $7, $2, $5);
         if (@NewDate) {
            ($day_of_month, $month, $year) = @NewDate;
         }

      # today, tomorrow, ...
      } elsif (m#(^| )($relday_match)([ ,:]|$)#i) {
         my $rel_name = lc($2);
         $reldate = $RelDateNames{$rel_name}->[1] if defined($RelDateNames{$rel_name});

      # monday, tuesday, ... (non-abbreviated only)
      } elsif (m#(^| )($wday_match)([ ,:]|$)#i) {
         my $off = GetWeekDayOffset($2);
         $reldate = $off if $off >= 0;

      # "Do. 21-22 Uhr" (e.g. on VIVA)
      # TODO translate "Uhr"
      } elsif (m#(^| )(($wday_abbrv_match)\.?|($wday_match)) \d{2}([\.\:]\d{2})?(\-| \- )\d{2}([\.\:]\d{2})?( +Uhr| ?h( |$))#i) {
         my $wday_name = $2;
         $wday_name = $3 if !defined($2);
         my $off = GetWeekDayOffset($wday_name);
         $reldate = $off if $off >= 0;
      }
   }
   if (!defined($day_of_month) && defined($reldate)) {
      ($day_of_month, $month, $year) = (localtime(time + $reldate*24*60*60))[3,4,5];
      $month += 1;
      $year += 1900;
   } elsif (defined($year) && ($year < 100)) {
      my $cur_year = (localtime(time))[5] + 1900;
      $year = ($cur_year - $cur_year%100);
   }
   return ($head, $day_of_month, $month, $year);
}

# ------------------------------------------------------------------------------
# Chop footer
# - footers usually contain advertisments (or more precisely: references
#   to teletext pages with ads) and can simply be discarded

# Example: some references may be relevant to the programme:
# ARD-Themenwoche Krebs >> 850       
# #######################################
# 300 <<                 Tagesschau > 310

# Example: some channels don't have a gap inbetween content and footer
# 17.13 Star Trek - Das nächste          
#       Jahrhundert                      
#       Die jungen Greise            >366
#   301<06-10 Uhr         18-24 Uhr>303  
#    Premiere 1 Monat gratis........ 707 

# note: possible TTX ref's above the footer are not skipped
sub ParseFooter {
   my ($head, @Page) = @_;
   my $foot;

   # TODO detect double-height (usually followed by empty/missing line)

   for ($foot = $#Page ; $foot > $head; $foot--) {
      # note missing lines are treated as empty lines
      $_ = $Page[$foot];
      if (!m#\S# || m#^ *\-{10,} *$#) {
         $foot -= 1;
         last;
      }
   }
   # if no separator found in 5 lines, skip just one line (FIXME only if ttx ref)
   if ($foot < $#Page - 5) {
      $foot = $#Page - 1;
   }
   return $foot;
}

# Try to identify footer lines by a different background color
# - bg color is changed by first changing the fg color (codes 0x0-0x7),
#   then switching copying fg over bg color (code 0x1D)
sub ParseFooterByColor {
   my ($page, $sub, @Page) = @_;
   my $handle;
   my $line;
   my @ColCount = (0, 0, 0, 0, 0, 0, 0, 0);
   my @LineCol;

   # check which color is used for the main part of the page
   # - ignore top-most header and one footer line
   # - ignore missing lines (although they would display black (except for
   #   double-height) but we don't know if they're intentionally missing)
   $handle = $page | ($sub << 12);
   for ($line = 4; $line <= 22; $line++) {
      if (defined($Pkg{$handle}->[$line])) {
         # get first non-blank char; skip non-color control chars
         if ($Pkg{$handle}->[$line] =~ m#^[ \x00-\x1F]*([\x00-\x07\x10-\x17])\x1D#) {
            my $c = ord($1);
            if ($c <= 0x07) {
               $ColCount[$c] += 1;
            }
            $LineCol[$line] = $c;
            # else: ignore mosaic
         } else {
            # background color unchanged
            $ColCount[0] += 1;
            $LineCol[$line] = 0;
         }
      }
   }

   # search most used color
   my $max_idx = 0;
   my $col_idx;
   for ($col_idx = 0; $col_idx < 8; $col_idx++) {
      if ($ColCount[$col_idx] > $ColCount[$max_idx]) {
         $max_idx = $col_idx;
      }
   }

   # count how many consecutive lines have this color
   # FIXME allow also use of two different colors in the middle and different ones for footer
   # FIXME: skip only one color? (i.e. until first color change)
   my $max_count = 0;
   my $count = 0;
   for ($line = 4; $line <= 23; $line++) {
      if (!defined($LineCol[$line]) || ($LineCol[$line] == $max_idx)) {
         $count += 1;
         $max_count = $count if $count > $max_count;
      } else {
         $count = 0;
      }
   }

   if ($max_count >= 8) {
      # skip footer until normal color is reached
      for ($line = 23; $line >= 0; $line--) {
         if (defined($LineCol[$line]) && ($LineCol[$line] == $max_idx)) {
            last;
         }
      }
   } else {
      # not reliable - too many color changes, don't skip any footer lines
      $line = 23;
   }
   return $line;
}

# ------------------------------------------------------------------------------
# Retrieve programme entries from an overview page
# - the layout has already been determined in advance
# - TODO: if same date, but start time jumps to 00:00-06:00, assume date++
#
sub ParseOvList {
   my ($pgfmt, $head, $foot, $day_of_month, $month, $year, @Page) = @_;
   my ($line);
   my ($hour, $min, $title, $vps, $is_tip, $new_title);
   my $date_off = 0;
   my $in_title = 0;
   my $vps_mark = 0;
   my $slot_ref = {};
   my @Slots = ();

   my @Date = ('day_of_month', $day_of_month,'month', $month,'year', $year);
   my ($time_off, $vps_off, $title_off, $subt_off) = split(",", $pgfmt);

   for ($line = $head; $line <= $foot; $line++) {
      $_ = $Page[$line];
      $_ = "" unless defined $_;

      # VPS date label
      if ( m#^ +(\d{2})(\d{2})(\d{2}) *$# ) {
         if ( ($1 <= 31) && ($1 > 0) && ($2 <= 12) && ($2 > 0) ) {
            ($day_of_month, $month, $year) = ($1, $2, $3 + 2000);
            @Date = ('day_of_month', $day_of_month,'month', $month,'year', $year);
            $vps_mark = 1;
            $date_off = 0;
         }
      }

      $new_title = 0;
      if ($vps_off >= 0) {
         # ZDF + Phoenix format: MM.HH VPS TITLE
         # TODO: Phoenix wraps titles into 2nd line, appends subtitle with "-"
         # m#^ {0,2}(\d\d)[\.\:](\d\d)( {1,3}(\d{4}))? +#
         if (substr($_, 0, $time_off) =~ m#^ *([\*\!] +)?$#) {
            $is_tip = $1;
            if (substr($_, $time_off, $vps_off - $time_off) =~ m#^(\d\d)[\.\:](\d\d) +$#) {
               $hour = $1;
               $min = $2;
               if (substr($_, $vps_off, $title_off - $vps_off) =~ m#^(\d{4})? +$#) {
                  $vps = $1;
                  $title = substr($_, $title_off);
                  $new_title = 1;
               }
            }
         }
      } else {
         # m#^( {0,5}| {0,3}\! {1,3})(\d\d)[\.\:](\d\d) +#
         if (substr($_, 0, $time_off) =~ m#^ *([\*\!] +)?$#) {
            $is_tip = $1;
            if (substr($_, $time_off, $title_off - $time_off) =~ m#^(\d\d)[\.\:](\d\d) +$#) {
               $hour = $1;
               $min = $2;
               $vps = undef;
               $title = substr($_, $title_off);
               $new_title = 1;
            }
         }
      }

      if ($new_title) {
         # push previous title
         push(@Slots, $slot_ref) if defined $slot_ref->{title};
         $slot_ref = {@Date};

         $slot_ref->{hour} = $hour;
         $slot_ref->{min} = $min;
         $slot_ref->{vps} = $vps if defined $vps;
         $slot_ref->{is_tip} = $is_tip if defined $is_tip;

         # detect date changes within a page
         if (($#Slots >= 0) && ($Slots[$#Slots]->{hour} > $hour) && !$vps_mark) {
            $date_off += 1;
         }
         $slot_ref->{date_off} = $date_off;
         SetStartTime($slot_ref);

         $title =~ s#^ +##;
         $title =~ s# +$##;
         $title =~ s#  +# #g;
         ParseTrailingFeat($title, $slot_ref);

         # kika special: subtitle appended to title
         if ($title =~ m#(.*\(\d+( *[\&\-] *\d+)?\))\/ *(\S.*)#) {
            $title = $1;
            $slot_ref->{subtitle} = $3;
            $slot_ref->{subtitle} =~ s# +$# #;
         }
         $slot_ref->{title} = $title;
         $in_title = 1;
         #print "ADD  $slot_ref->{day_of_month}.$slot_ref->{month} $slot_ref->{hour}.$slot_ref->{min} $slot_ref->{title}\n";

      } elsif ($in_title) {

         # check if last line specifies and end time
         # (usually last line of page)
         # TODO translate "bis", "Uhr"
         if ( m#^ *(VPS +)?(\(bis |\- ?)(\d\d)[\.\:](\d\d)( Uhr| ?h)( |\)|$)# ||   # ARD,SWR()
              m#^ *(\d\d)[\.\:](\d\d) *$# ) {                                      # arte
            $slot_ref->{end_hour} = $3;
            $slot_ref->{end_min} = $4;
            $new_title = 1;

         # check if we're still in a continuation of the previous title
         # time column must be empty, except for a possible VPS label
         } elsif ($vps_off >= 0) {
            if ( (substr($_, 0, $subt_off) =~ m#[^ ]#) ||
                 (substr($_, $subt_off) =~ m#^ *$#) ) {
               $new_title = 1;
            }
         } else {
            # TODO VPS must be magenta or concealed
            if (!defined $slot_ref->{vps} &&
                (substr($_, 0, $subt_off) =~ m#^ *(\d{4}) +$#)) {
               $slot_ref->{vps} = $1;
            } elsif ( (substr($_, 0, $subt_off) =~ m#[^ ]#) ||
                      (substr($_, $subt_off) =~ m#^ *$#) ) {
               $new_title = 1;
            }
         }
         if ($new_title == 0) {
            ParseTrailingFeat($_, $slot_ref);
            s#^ +##;
            s# +$# #;
            s#  +# #;
            # combine words separated by line end with "-"
            if ( !defined($slot_ref->{subtitle}) &&
                 ($slot_ref->{title} =~ m#\S\-$#) && m#^[[:lower:]]#) {
               $slot_ref->{title} =~ s#\-$##;
               $slot_ref->{title} .= $_;
            } else {
               if ( defined($slot_ref->{subtitle}) &&
                    ($slot_ref->{subtitle} =~ m#\S\-$#) && m#^[[:lower:]]#) {
                  $slot_ref->{subtitle} =~ s#\-$##;
                  $slot_ref->{subtitle} .= $_;
               } elsif (defined($slot_ref->{subtitle})) {
                  $slot_ref->{subtitle} .= " " . $_;
               } else {
                  $slot_ref->{subtitle} = $_;
               }
            }
         } else {
            push(@Slots, $slot_ref) if defined $slot_ref->{title};
            $slot_ref = {@Date};
            $in_title = 0;
         }
      }
   }
   push(@Slots, $slot_ref) if defined $slot_ref->{title};
   return @Slots;
}

# ------------------------------------------------------------------------------
# Parse feature indicators in the overview table
# - main task to extrace references to teletext pages with longer descriptions
#   unfortunately these references come in a great varity of formats, so we
#   must mostly rely on the teletext number (risking to strip off 3-digit
#   numbers at the end of titles in rare cases)
# - it's mandatory to remove features becaue otherwise they might show up in
#   the middle of a wrapped title
# - most feature strings are unique enough so that they can be identified
#   by a simple pattern match (e.g. 16:9, UT)
# - TODO: use color to identify features; make a first sweep to check if
#   most unique features always have a special color (often yellow) and then
#   remove all words in that color inside title strings

# Examples:
#
# 06.05 # logo!                     >331
#
# 13.15  1315  Heilkraft aus der Wüste   
#              (1/2) Geheimnisse der     
#              Buschmänner 16:9 oo       
#
# TODO: some tags need to be localized (i.e. same as month names etc.)
my %FeatToFlagMap = ( "UT" => "has_subtitles",
                      "OmU" => "is_omu",
                      "s/w" => "is_bw",
                      "16:9" => "is_aspect_16_9",
                      "oo" => "is_stereo", "Stereo" => "is_stereo",
                      "2K" => "is_2chan", "2K-Ton" => "is_2chan",
                      "Dolby" => "is_dolby",
                      "Mono" => "is_mono" );

sub ParseTrailingFeat {
   my ($foo, $slot_ref) = @_;

   # teletext reference
   # these are always right-most, so parse these first and outside of the loop
   if ($_[0] =~ m#( \.+|\.* +|\.\.+|\.+\>|\>\>?|\-\-?\>)([1-8][0-9][0-9]) *$#) {
      my $ref = $2;
      ($_[0] = $`) =~ s# +$##;
      $slot_ref->{ttx_ref} = ((ord(substr($ref, 0, 1)) - 0x30)<<8) |
                             ((ord(substr($ref, 1, 1)) - 0x30)<<4) |
                             (ord(substr($ref, 2, 1)) - 0x30);
   }
   # features (e.g. WDR: "Stereo, UT")
   # TODO: SWR: "(16:9/UT)", "UT 150", "UT auf 150"
   while (1) {
      if ($_[0] =~ m# (UT|s\/w|HF|AD|oo|°°|OmU|16:9|Tipp!|Wh\.|Wdh\.) *$#) {
         $_[0] = $`;
         $_[0] =~ s#,$##;
         my $feat = $FeatToFlagMap{$1};
         if (defined($feat)) {
            $slot_ref->{$feat} = 1;
         }
      }
      # ARTE: (oo) (2K) (Mono)
      elsif ($_[0] =~ m# \((UT|s\/w|HF|AD|oo|°°|OmU|16:9|2K|2K-Ton|Mono|Dolby|Wh\.|Wdh\.)\) *$#) {
         $_[0] = $`;
         my $feat = $FeatToFlagMap{$1};
         if (defined($feat)) {
            $slot_ref->{$feat} = 1;
         }
      } else {
         last;
      }
   } 
   $_[0] =~ s# +$##;
}

# ------------------------------------------------------------------------------
# Parse programme overview table
# - 1: retrieve start date from the page header
# - 2: discard footer lines with non-programme related content
# - 3: retrieve programme data from table
# 
sub ParseOv {
   my($page, $sub, $fmt) = @_;
   my @Page;
   my ($head, $foot);
   my ($day_of_month, $month, $year);

   @Page = PageToLatin($page, $sub);

   ($head, $day_of_month, $month, $year) = ParseOvHeader($page, $sub, $fmt, @Page);
   if (defined($day_of_month)) {
      $foot = ParseFooter($head, @Page);

      printf("OVERVIEW PAGE %03X.%04X: DATE $day_of_month.$month.$year lines $head-$foot\n", $page, $sub) if $opt_debug;

      return ParseOvList($fmt, $head, $foot, $day_of_month, $month, $year, @Page);
   } else {
      printf("OVERVIEW PAGE %03X.%04X: skipped - no date found in header\n", $page, $sub) if $opt_debug;
      return ();
   }
}

# ------------------------------------------------------------------------------
# Retrieve programme data from an overview page
# - 1: compare several overview pages to identify the layout
# - 2: parse all overview pages, retrieving titles and ttx references
# - 3: export into XML and include references descriptions
#
sub ParseAllOvPages {
   my $page;
   my @Slots = ();
   my $slot_ref;

   my $fmt = DetectOvFormat();
   if ($fmt) {

      my @Pages = grep {($_>=$opt_tv_start) && ($_<=$opt_tv_end)} keys(%PgCnt);
      @Pages = sort {$a<=>$b} @Pages;

      foreach $page (@Pages) {
         my $pkg;
         my $sub;

         if ($PgSub{$page} == 0) {
            push @Slots, ParseOv($page, 0, $fmt);

         } else {
            for ($sub = 1; $sub <= $PgSub{$page}; $sub++) {
               push @Slots, ParseOv($page, $sub, $fmt);
            }
         }
      }
   } else {
      print STDERR "No overview pages found\n";
   }

   # guess missing stop times
   CalculateStopTimes(\@Slots);

   # retrieve descriptions from references teletext pages
   foreach $slot_ref (@Slots) {
      if (defined($slot_ref->{ttx_ref})) {
         $slot_ref->{desc} = ParseDescPage($slot_ref);
      }
   }

   # get channel name from teletext header packets
   my $ch_name = $opt_chname;
   $ch_name = ParseChannelName() if !defined $ch_name;
   $ch_name = "???" if $ch_name eq "";

   my $ch_id = $opt_chid;
   $ch_id = ParseChannelId() if !defined $ch_id;
   $ch_id = $ch_name if $ch_id eq "";

   print "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n".
         "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n".
         "<tv>\n<channel id=\"$ch_id\">\n\t<display-name>$ch_name</display-name>\n</channel>\n";
   foreach $slot_ref (@Slots) {
      ExportTitle($slot_ref, $ch_id);
   }
   print "</tv>\n";
}

# ------------------------------------------------------------------------------
# Determine stop times
# - assuming that in overview tables the stop time is equal to the start
#   of the following programme & that this also holds true inbetween pages
# - if in doubt, leave it undefined (this is allowed in XMLTV)
# 
sub CalculateStopTimes {
   my($arr_ref) = @_;
   my $arr_len = $#{$arr_ref} + 1;
   my ($next, $slot);
   my $idx;

   if ($arr_len > 0) {
      for ($idx = 0; $idx < $arr_len; $idx++) {
         $slot = $arr_ref->[$idx];

         if (defined($slot->{end_min})) {
            # there was an end time in the overview or a description page -> use that
            my $date_off = $slot->{date_off};
            $date_off += 1 if ($slot->{hour} > $slot->{end_hour});

            $slot->{stop_t} = POSIX::mktime(0, $slot->{end_min}, $slot->{end_hour},
                                               $slot->{day_of_month} + $date_off,
                                               $slot->{month} - 1,
                                               $slot->{year} - 1900,
                                               0, 0, -1);
         } elsif ($idx + 1 < $arr_len) {
            # no end time: use start time of the following programme if less than 12h away
            $next = $arr_ref->[$idx + 1];
            if ( ($next->{start_t} > $slot->{start_t}) &&
                 ($next->{start_t} - $slot->{start_t} < 12*60*60) ) {

               $slot->{stop_t} = $next->{start_t};
            }
         }
      }
   }
}

# convert discrete start times into UNIX epoch format
# implies a conversion from local time zone into GMT
sub SetStartTime {
   my ($slot) = @_;

   $slot->{start_t} = POSIX::mktime(0, $slot->{min}, $slot->{hour},
                                       $slot->{day_of_month} + $slot->{date_off},
                                       $slot->{month} - 1,
                                       $slot->{year} - 1900,
                                       0, 0, -1);
}

sub DetermineLto {
   my ($time_t) = @_;
   my ($sec, $min, $hour, $mday, $mon, $year, @foo);
   my $gmt;
   my $lto;

   # LTO := Difference between GMT and local time
   # (note: for this to work it's mandatory to pass is_dst=-1 to mktime)
   ($sec,$min,$hour,$mday,$mon,$year,@foo) = gmtime($time_t);
   $gmt = mktime($sec, $min, $hour, $mday, $mon, $year, 0, 0, -1);

   if (defined($gmt)) {
      $lto = $time_t - $gmt;
   } else {
      $lto = 0;
   }
   return $lto;
}

# ------------------------------------------------------------------------------
# Export programme data into XMLTV format
#
sub ExportTitle {
   my($slot_ref, $ch_id) = @_;

   if (defined($slot_ref->{title}) &&
       defined($slot_ref->{day_of_month}) &&
       defined($slot_ref->{month}) &&
       defined($slot_ref->{year}) &&
       defined($slot_ref->{hour}) &&
       defined($slot_ref->{min}))
   {
      my $start_str = GetXmlTimestamp($slot_ref->{start_t});
      my $stop_str;
      my $vps_str;
      if (defined $slot_ref->{stop_t}) {
         $stop_str = " stop=\"". GetXmlTimestamp($slot_ref->{stop_t}) ."\"";
      } else {
         $stop_str = "";
      }
      if (defined($slot_ref->{vps})) {
         $vps_str = " pdc-start=\"". GetXmlVpsTimestamp($slot_ref) ."\"";
      } else {
         $vps_str = "";
      }
      my $title = $slot_ref->{title};
      # convert all-upper-case titles to lower-case
      if ($title !~ m#[[:lower:]]#) {
         $title = lc($title);
      }
      Latin1ToXml($title);

      print "\n<programme start=\"$start_str\"$stop_str$vps_str channel=\"$ch_id\">\n".
            "\t<title>$title</title>\n";
      if (defined($slot_ref->{subtitle})) {
         # repeat the title because often the subtitle is actually just the 2nd part
         # of an overlong title (there's no way to distinguish this from series subtitles etc.)
         # TODO: some channel distinguish title from subtitle by making title all-caps (e.g. TV5)
         my $subtitle = $slot_ref->{title} ." ". $slot_ref->{subtitle};
         Latin1ToXml($subtitle);
         print "\t<sub-title>$subtitle</sub-title>\n";
      }
      if (defined($slot_ref->{ttx_ref})) {
         if (defined($slot_ref->{desc}) && ($slot_ref->{desc} ne "")) {
            printf("\t<!-- TTX %03X -->\n", $slot_ref->{ttx_ref});
            my $desc = $slot_ref->{desc};
            Latin1ToXml($desc);
            print "\t<desc>". $desc ."</desc>\n";
         } else {
            # page not captured or no matching date/title found
            printf("\t<desc>(teletext %03X)</desc>\n", $slot_ref->{ttx_ref});
         }
      }
      # video
      if (defined($slot_ref->{is_bw})||defined($slot_ref->{is_aspect_16_9})) {
         print "\t<video>\n";
         if (defined($slot_ref->{is_bw})) {
            print "\t\t<colour>no</colour>\n";
         }
         if (defined($slot_ref->{is_aspect_16_9})) {
            print "\t\t<aspect>16:9</aspect>\n";
         }
         print "\t</video>\n";
      }
      # audio
      if (defined($slot_ref->{is_dolby})) {
         print "\t<audio>\n\t\t<stereo>surround</stereo>\n\t</audio>\n";
      } elsif (defined($slot_ref->{is_stereo})) {
         print "\t<audio>\n\t\t<stereo>stereo</stereo>\n\t</audio>\n";
      } elsif (defined($slot_ref->{is_mono})) {
         print "\t<audio>\n\t\t<stereo>mono</stereo>\n\t</audio>\n";
      }
      # subtitles
      if (defined($slot_ref->{has_subtitles})) {
         print "\t<subtitles type=\"onscreen\"/>\n";
      } elsif (defined($slot_ref->{has_subtitles})) {
         print "\t<subtitles type=\"teletext\"/>\n";
      }
      # tip/highlight (ARD only)
      if (defined($slot_ref->{is_tip})) {
         print "\t<star-rating>\n\t\t<value>1/1</value>\n\t</star-rating>\n";
      }
      print "</programme>\n";

   } else {
      print "SKIPPING PARTIAL $slot_ref->{title} $slot_ref->{hour}.$slot_ref->{min}\n" if $opt_debug;
   }
}

# ------------------------------------------------------------------------------
# Convert a UNIX epoch timestamp into XMLTV format
#
sub GetXmlTimestamp {
   my ($time_t) = @_;
   my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday);
   my $t_str;

   # get GMT in broken-down format
   ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday) = gmtime($time_t);

   $t_str = POSIX::strftime("%Y%m%d%H%M%S +0000",
                            0, $min, $hour,
                            $mday,$mon,$year,
                            $wday, $yday, -1);

   return $t_str;
}

sub GetXmlVpsTimestamp {
   my ($slot) = @_;
   my $vps_str;
   my $lto = DetermineLto($slot->{start_t});

   # TODO: parse VPS date from program overview
   #       " 140406 " (line all blank, color magenta) (ARD)
   #       " 1D102 130406 BE"  (ZDF)
   $vps_str = "$slot->{year}$slot->{day_of_month}$slot->{month}".
              "$slot->{vps}00".
              sprintf(" %+03d%02d", $lto / (60*60), abs($lto / 60) % 60);
}

# ------------------------------------------------------------------------------
# Convert a text string for inclusion in XML as CDATA
# - the result is not suitable for attributes since quotes are not escaped
# - input must already have teletext control characters removed
#
sub Latin1ToXml {
   # note: & replacement must be first
   $_[0] =~ s#&#\&amp\;#g;
   $_[0] =~ s#<#\&lt\;#g;
   $_[0] =~ s#>#\&gt\;#g;
}

# ------------------------------------------------------------------------------
# Parse description pages
#

#ARD:
#                          Mi 12.04.06 Das Erste 
#                                                
#         11.15 - 12.00 Uhr                      
#         In aller Freundschaft           16:9/UT
#         Ehen auf dem Prüfstand                 

#ZDF:
#  ZDFtext          ZDF.aspekte
#  Programm  Freitag, 21.April   [no time!]

#MDR:
#Mittwoch, 12.04.2006, 14.30-15.30 Uhr  
#                                        
# LexiTV - Wissen für alle               
# H - wie Hühner                        

#3sat:
#      Programm         Heute            
#      Mittwoch, 12. April               
#                         1D107 120406 3B
#  07.00 - 07.30 Uhr            VPS 0700 
#  nano Wh.                              

# Date format:
# ARD:   Fr 14.04.06 // 15.40 - 19.15 Uhr
# ZDF:   Freitag, 14.April // 15.35 - 19.00 Uhr
# Sat.1: Freitag 14.04. // 16:00-17:00
# RTL:   Fr[.] 14.04. 13:30 Uhr
# Pro7:  So 16.04 06:32-07:54 (note: date not on 2/2, only title rep.)
# RTL2:  Freitag, 14.04.06; 12.05 Uhr (at bottom) (1/6+2/6 for movie)
# VOX:   Fr 18:00 Uhr
# MTV:   Fr 14.04 18:00-18:30 // Sa 15.04 18:00-18:30
# VIVA:  Mo - Fr, 20:30 - 21:00 Uhr // Mo - Fr, 14:30 - 15:00 Uhr (also w/o date)
#        Mo 21h; Mi 17h (Wh); So 15h
#        Di, 18:30 - 19:00 h
#        Sa. 15.4. 17:30h
# arte:  Fr 14.04. 00.40 [VPS  0035] bis 20.40 (double-height)
# 3sat:  Freitag, 14. April // 06.20 - 07.00 Uhr
# CNN:   Sunday 23 April, 13:00 & 19:30 CET
#        Saturday 22, Sunday 23 April
#        daily at 11:00 CET
# MDR:   Sonnabend, 15.04.06 // 00.00 - 02.55 Uhr
# KiKa:  14. April 2006 // 06.40-07.05 Uhr
# SWR:   14. April, 00.55 - 02.50 Uhr
# WDR:   Freitag, 14.04.06   13.40-14.40
# BR3:   Freitag, 14.04.06 // 13.30-15.05
# Kabl1: Fr 14.04 20:15-21:11
# Tele5: Fr 19:15-20:15

sub ParseDescDate {
   my ($slot, $page, $sub, @Page) = @_;
   my ($lmday, $lmonth, $lyear, $lhour, $lmin, $lend_hour, $lend_min);
   my ($new_date, $check_time);
   my $line;

   my $lang = $PgLang{$page};
   my $wday_match = GetDateNameRegExp(\%WDayNames, $lang, 1);
   my $wday_abbrv_match = GetDateNameRegExp(\%WDayNames, $lang, 2);
   my $mname_match = GetDateNameRegExp(\%MonthNames, $lang, undef);

   # search date and time
   for ($line = 0; $line <= 23; $line++) {
      $_ = $Page[$line];
      $new_date = 0;

      while (1) {
         # [Fr] 14.04.[06]
         if (m#(^| )(\d{2})\.(\d{2})\.(\d{4}|\d{2})?( |,|\:|$)#) {
            my @NewDate = CheckDate($2, $3, $4, undef, undef);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last;
            }
         }
         # Fr 14.04 (i.e. no dot after month)
         # here's a risk to match on a time, so we must require a weekday name
         if (m#(^| )($wday_match)(, ?| )(\d{2})\.(\d{2})( |,|$)#i ||
             m#(^| )($wday_abbrv_match)(\.,? ?|, ?| )(\d{2})\.(\d{2})( |,|\:|$)#i) {
            my @NewDate = CheckDate($4, $5, undef, $2, undef);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last;
            }
         }
         # 14.[ ]April [2006]
         if ( m#(^| )(\d{2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|\:|$)#i) {
            my @NewDate = CheckDate($2, undef, $5, undef, $3);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last;
            }
         }
         # Sunday 22 April (i.e. no dot after month)
         if ( m#(^| )($wday_match)(, ?| )(\d{2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|\:|$)#i ||
              m#(^| )($wday_abbrv_match)(\.,? ?|, ?| )(\d{2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|\:|$)#i) {
            my @NewDate = CheckDate($4, undef, $7, $2, $5);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last;
            }
         }
         # TODO: "So, 23." (n-tv)

         # Fr[,] 18:00 [-19:00] [Uhr|h]
         # TODO parse time (i.e. allow omission of "Uhr")
         if (m#(^| )($wday_match)(, ?| )(\d{2}[\.\:]\d{2}((-| - )\d{2}[\.\:]\d{2})?(h| |,|\:|$))#i ||
             m#(^| )($wday_abbrv_match)(\.,? ?|, ?| )(\d{2}[\.\:]\d{2}((-| - )\d{2}[\.\:]\d{2})?(h| |,|\:|$))#i) {
            my @NewDate = CheckDate(undef, undef, undef, $2, undef);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last;
            }
         }
         # TODO: 21h (i.e. no minute value: TV5)

         # TODO: make exact match between VPS date and time from overview page
         # TODO: allow garbage before or after label; check reveal and magenta codes (ETS 300 231 ch. 7.3)
         # VPS label "1D102 120406 F9"
         if (s#^ +[0-9A-F]{2}\d{3} (\d{2})(\d{2})(\d{2}) [0-9A-F]{2} *$##) {
            ($lmday, $lmonth, $lyear) = ($1, $2, $3 + 2000);
            last;
         }
         last;
      }

      # TODO: time should be optional if only one subpage
      # 15.40 [VPS 1540] - 19.15 [Uhr|h]
      $check_time = 0;
      if (m#(^| )(\d{2})[\.\:](\d{2})( +VPS +(\d{4}))?(-| - | bis )(\d{2})[\.\:](\d{2})(h| |,|\:|$)# ||
          m#(^| )(\d{2})h(\d{2})( +VPS +(\d{4}))?(-| - )(\d{2})h(\d{2})( |,|\:|$)#) {
         my $hour = $2;
         my $min = $3;
         my $end_hour = $7;
         my $end_min = $8;
         # my $vps = $5;
         print "DESC TIME $hour.$min - $end_hour.$end_min\n" if $opt_debug;
         if (($hour < 24) && ($min < 60) &&
             ($end_hour < 24) && ($end_min < 60)) {
            ($lhour, $lmin, $lend_hour, $lend_min) = ($hour, $min, $end_hour, $end_min);
            $check_time = 1;
         }

      # 15.40 (Uhr|h)
      } elsif (m#(^| )(\d{2})[\.\:](\d{2})( ?h| Uhr)( |,|\:|$)# ||
               m#(^| )(\d{2})h(\d{2})( |,|\:|$)#) {
         my $hour = $2;
         my $min = $3;
         print "DESC TIME $hour.$min\n" if $opt_debug;
         if (($hour < 24) && ($min < 60)) {
            ($lhour, $lmin) = ($hour, $min);
            $check_time = 1;
         }
      }

      if ($check_time && defined($lyear)) {
         # TODO: allow slight mismatch of <5min? (for TV5: list says 17:03, desc says 17:05)
         my $start_t = POSIX::mktime(0, $lmin, $lhour, $lmday, $lmonth - 1, $lyear - 1900, 0, 0, -1);
         if (defined($start_t) && ($start_t == $slot->{start_t})) {
            # match found
            if (defined($lend_hour) && !defined($slot->{end_hour})) {
               # add end time to the slot data
               # TODO: also add VPS time
               $slot->{end_hour} = $lend_hour;
               $slot->{end_min} = $lend_min;
            }
            return 1;
         } else {
            print "MISMATCH: ".GetXmlTimestamp($start_t)." ".GetXmlTimestamp($slot->{start_t})."\n" if $opt_debug;
            $lend_hour = undef;
            if ($new_date) {
               # date on same line as time: invalidate both upon mismatch
               $lyear = undef;
            }
         }
      }
   }
   return 0;
}

sub CheckDate {
   my ($mday, $month, $year, $wday_name, $month_name) = @_;

   # first check all provided values
   return () if (defined($mday) && !(($mday <= 31) && ($mday > 0)));
   return () if (defined($month) && !(($month <= 12) && ($month > 0)));
   return () if (defined($year) && !(($year < 100) || (($year >= 2006) && ($year < 2031))));

   if (defined($wday_name) && !defined($mday)) {

      # determine date from weekday name alone
      my $reldate = GetWeekDayOffset($wday_name);
      return () if $reldate < 0;
      ($mday, $month, $year) = (localtime(time + $reldate*24*60*60))[3,4,5];
      $month += 1;
      $year += 1900;
      print "DESC DATE $wday_name $mday.$month.$year\n" if $opt_debug;

   } elsif (defined($mday)) {
      if (defined($wday_name)) {
         $wday_name = lc($wday_name);
         return () if !defined($WDayNames{$wday_name});
      }
      if (!defined($month) && defined($month_name)) {
         $month_name = lc($month_name);
         return () if !defined($MonthNames{$month_name});
         $month = $MonthNames{$month_name}->[1];
      } elsif (!defined($month)) {
         return ();
      }
      if (!defined($year)) {
         $year = (localtime(time))[5] + 1900;
      } elsif ($year < 100) {
         my $cur_year = (localtime(time))[5] + 1900;
         $year += $cur_year - ($cur_year % 100);
      }
      print "DESC DATE $mday.$month.$year\n" if $opt_debug;
   } else {
      return ();
   }
   return ($mday, $month, $year);
}

sub GetWeekDayOffset {
   my ($wday_name) = @_;
   my $wday_idx;
   my $cur_wday_idx;
   my $reldate = -1;

   $wday_name = lc($wday_name);
   if (defined($wday_name) && defined($WDayNames{$wday_name})) {
      $wday_idx = $WDayNames{$wday_name}->[1];
      my $cur_wday_idx = (localtime(time))[6];
      if ($wday_idx >= $cur_wday_idx) {
         $reldate = $wday_idx - $cur_wday_idx;
      } else {
         $reldate = (7 - $cur_wday_idx) + $wday_idx;
      }
   }
   return $reldate;
}

sub ParseDescDateTitle {
   my ($slot, @Page) = @_;
   my $title;
   my $line;
   my ($l, $l1, $l2);

   $title = $slot->{title};
   $l1 = $Page[0];
   $l1 =~ s#(^ +| +$)##g;
   $l1 =~ s#  +##g;

   for ($line = 0; $line <= 23/2; $line++) {
      $l2 = $Page[$line + 1];
      $l2 =~ s#(^ +| +$)##g;
      $l2 =~ s#  +# #g;
      $l = $l1 ." ". $l2;
      #TODO: join lines if l1 ends with "-" ?
      #TODO: correct title/subtitle separation of overview

      # TODO: one of the compared title strings may be all uppercase (SWR)

      if ($l1 =~ m#^ *\Q$title\E#) {
         # Done: discard everything above
         # TODO: back up if there's text in the previous line with the same indentation
         return $line;
      }
      $l1 = $l2;
   }
   #print "NOT FOUND $title\n";
   return 0;
}

sub ParseDescContent {
   my ($head, $foot, @Page) = @_;
   my $line;

   my $is_nl = 0;
   my $desc = "";
   for ($line = $head; $line <= $foot; $line++) {
      $_ = $Page[$line];

      # remove VPS time and date labels
      # TODO parse features behind date, title or subtitle
      s#^ +[0-9A-F]{2}\d{3} (\d{2})(\d{2})(\d{2}) [0-9A-F]{2} *$##;
      s# +VPS \d{4} *$##;

      # TODO: replace 0x7f (i.e. square) at line start with "-"
      # line consisting only of "-": treat as paragraph break
      s#^ *[\-\_\+]{20,} *$##;

      if (m#[^ ]#) {
         s#(^ +| +$)##g;
         s#  +# #g;
         $desc .= "\n" if $is_nl;
         if (($desc =~ m#\S-$#) && m#^[[:lower:]]#) {
            $desc =~ s#-$##;
            $desc .= $_;
         } elsif ((length($desc) > 0) && !$is_nl) {
            $desc .= " " . $_;
         } else {
            $desc .= $_;
         }
         $is_nl = 0;
      } else {
         # empty line: paragraph break
         if (length($desc) > 0) {
            $is_nl = 1;
         }
      }
   }
   return $desc;
}

sub ParseDescPage {
   my ($slot_ref) = @_;
   my ($sub, $sub1, $sub2);
   my $desc = "";
   my $found = 0;
   my $page = $slot_ref->{ttx_ref};

   if (defined($PgCnt{$page})) {
      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         my @Page = PageToLatin($page, $sub);
         my($head, $foot, $foot2);

         # TODO: multiple sub-pages may belong to same title, but have no date
         # TODO: following page may belong to the same title (ARD: "Darsteller > 330")

         if (ParseDescDate($slot_ref, $page, $sub, @Page)) {
            $head = ParseDescDateTitle($slot_ref, @Page);

            $foot = ParseFooterByColor($page, $sub, @Page);
            $foot2 = ParseFooter($head, @Page);
            $foot = ($foot2 < $foot) ? $foot2 : $foot;

            $desc .= "\n" if $found;
            $desc .= ParseDescContent($head, $foot, @Page);

            $found = 1;
         } else {
            printf("DESC page %03X.%04X no date found\n", $page, $sub) if $opt_debug;
            last if $found;
         }
      }
   } else {
      printf("DESC page %03X not found\n", $page) if $opt_debug;
   }
   return $desc;
}

# ------------------------------------------------------------------------------
# Determine channel name and ID from teletext header packets
# - remove clock, date and page number: rest should be channel name
#
sub ParseChannelName {
   my %ChName;
   my $page;
   my $name;
   my $pgn;
   my $wday_match;
   my $mname_match;

   my @Pages = grep {((($_>>4)&0xF)<=9) && (($_&0xF)<=9)} keys(%PgCnt);
   @Pages = sort {$a<=>$b} @Pages;
   my $found = 0;
   my $lang = -1;

   foreach $page (@Pages) {
      $_ = $Pkg{$page}->[0];
      if (defined($_) && (length($_) > 0)) {
         if ($PgLang{$page} != $lang) {
            $lang = $PgLang{$page};
            $wday_match = GetDateNameRegExp(\%WDayNames, $lang, undef);
            $mname_match = GetDateNameRegExp(\%MonthNames, $lang, undef);
         }
         $_ = TtxToLatin1(substr($_, 8), $lang);
         $pgn = sprintf("%03X", $page);
         # remove page number
         # remove date and time
         if (s#(^| )\Q$pgn\E( |$)#$1$2# &&
             s# \d{2}[\.\: ]?\d{2}([\.\: ]\d{2}) *$# #) {

            # remove date
            s#($wday_match)?\.? ?\d{2}(\.\d{2}|[ \.]($mname_match))(\.|[ \.]\d{2,4})? *$##i;
            # remove and compress whitespace
            s#(^ +| +$)##g;
            s#  +# #g;
            # remove possible "text" postfix
            s#[ \.\-]?text$##i;

            if (defined($ChName{$_})) {
               $ChName{$_} += 1;
               last if $ChName{$_} >= 50;
            } else {
               $ChName{$_} = 1;
               $found = 1;
            }
         }
      }
   }
   if ($found) {
      $name = (sort {$ChName{$a}<=>$ChName{$b}} keys(%ChName))[0];
   } else {
      $name = "";
   }
   return $name;
}

sub ParseChannelId {
   return "";
}

# ------------------------------------------------------------------------------
# Main

# enable locale so that case conversions work outside of ASCII too
setlocale(LC_CTYPE, "");

# parse command line arguments
ParseArgv();

# read teletext data into memory
if ($opt_verify == 0) {
   ReadVbi();
} else {
   ImportRawDump();
}

# parse and export programme data
if ($opt_dump == 0) {
   ParseAllOvPages();

# debug only: dump teletext data into file
} elsif ($opt_dump == 1) {
   DumpAllPages();
} else {
   DumpRawTeletext();
}

