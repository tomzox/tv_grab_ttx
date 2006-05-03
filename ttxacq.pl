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
#  $Id: ttxacq.pl,v 1.3 2006/05/03 19:55:32 tom Exp tom $
#

use POSIX;
use locale;
use strict;

my %Pkg;
my %PgCnt;
my %PgSub;
my %PgLang;
my %PageCtrl;
my %PageText;
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
               "Additional debug options: -debug -dump -dumpraw <page_range> -test <file>\n";

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
            die "-page: invalid parameter: \"$_\", expecting two 3-digit ttx page numbers\n";
         }

      # -debug: enable debug output
      } elsif (/^-debug$/) {
         $opt_debug = 1;

      # -dump: print all teletext pages, then exit (for debugging only)
      } elsif (/^-dump$/) {
         $opt_dump = 1;

      # -dumpraw: write teletext pages in binary, suitable for verification
      } elsif (/^-dumpraw$/) {
         die "Missing argument for $_\n$usage" unless $#ARGV>=0;
         $opt_dump = 2;
         $_ = shift @ARGV;
         if (m#^([0-9]{3})\-([0-9]{3})$#) {
            $opt_tv_start = hex($1);
            $opt_tv_end = hex($2);
         } else {
            die "-dumpraw: invalid parameter: \"$_\", expecting two 3-digit ttx page numbers\n";
         }

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
      my $line;
      my ($sub, $sub1, $sub2);
      my $pgtext;

      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         printf "PAGE %03X.%04X\n", $page, $sub;

         PageToLatin($page, $sub);
         $pgtext = $PageText{$page | ($sub << 12)};

         for ($line = 1; $line <= 23; $line++) {
            print $pgtext->[$line] . "\n";
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

      next unless (($page >= $opt_tv_start) && ($page <= $opt_tv_end));

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

         for ($pkg = 0; $pkg <= 23; $pkg++) {
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
    '�$@����#�����',
    # German (100%)
    '#$����^_�����',
    # Swedish/Finnish/Hungarian (100%)
    '#������_�����',
    # Italian (100%)
    '�$�绬#�����',
    # French (100%)
    '�������#�����',
    # Portuguese/Spanish (100%)
    '�$�����������',
    # Czech/Slovak (60%)
    '#uctz��r��e�s',
    # reserved (English mapping)
    '�$@����#�����',
    # Polish (6%: all but '#' missing)
    '#naZSLcoezslz',
    # German (100%)
    '#$����^_�����',
    # Swedish/Finnish/Hungarian (100%)
    '#������_�����',
    # Italian (100%)
    '$�绬#�����',
    # French (100%)
    '������#�����',
    # reserved (English mapping)
    '$@����#�����',
    # Czech/Slovak 
    'uctz��r��e�s',
    # reserved (English mapping)
    '$@����#�����',
    # English 
    '$@����#�����',
    # German
    '$����^_�����',
    # Swedish/Finnish/Hungarian (100%)
    '������_�����',
    # Italian (100%'
    '$�绬#�����',
    # French (100%)
    '������#�����',
    # Portuguese/Spanish (100%)
    '$�����������',
    # Turkish (50%: 7 missing)
    'gIS���Gis���',
    # reserved (English mapping)
    '$@����#�����'
);

my @DelSpcAttr = (
    0,0,0,0,0,0,0,0,    # 0-7: alpha color
    1,1,1,1,            # 8-B: flash, box
    0,0,0,0,            # C-F: size
    1,1,1,1,1,1,1,1,    # 10-17: mosaic color
    0,                  # 18:  conceal
    1,1,                # 19-1A: continguous mosaic
    0,0,0,              # 1B-1D: charset, bg color
    1,1                 # 1E-1F: hold mosaic
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
         #substr($line, $idx, 1) = ' ';

      # mosaic color code
      } elsif (($val >= 0x10) && ($val <= 0x17)) {
         $is_g1 = 1;
         substr($line, $idx, 1) = ' ';

      # other control character or mosaic character
      } elsif ($val < 0x20) {
         if ($DelSpcAttr[$val]) {
            substr($line, $idx, 1) = ' ';
         }

      # mosaic charset
      } elsif ($is_g1) {
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
   my $handle = $page | ($sub << 12);

   if (!defined($PageText{$handle})) {
      my @PgText = ("");
      my @PgCtrl = ("");
      my $lang = $PgLang{$page};
      my $pkgs = $Pkg{$handle};
      my $idx;

      for ($idx = 1; $idx <= 23; $idx++) {
         if (defined($pkgs->[$idx])) {
            $_ = TtxToLatin1($pkgs->[$idx], $lang);
            push @PgCtrl, $_;
            s#[\x00-\x1F\x7F]# #g;
            push @PgText, $_;
         } else {
            $_ = " " x 40;
            push @PgCtrl, $_;
            push @PgText, $_;
         }
      }
      $PageCtrl{$handle} = \@PgCtrl;
      $PageText{$handle} = \@PgText;
   }
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
   "januar" => [(1<<1),1,1], "februar" => [(1<<1),2,1], "m�rz" => [(1<<1),3,1],
   "april" => [(1<<1)|(1<<0),4,1], "mai" => [(1<<4)|(1<<1),5,3], "juni" => [(1<<1),6,1],
   "juli" => [(1<<1),7,1], "august" => [(1<<1)|(1<<0),8,1], "september" => [(1<<1)|(1<<0),9,1],
   "oktober" => [(1<<1),10,1], "november" => [(1<<1)|(1<<0),11,1], "dezember" => [(1<<1),12,1],
   "jan" => [(1<<1)|(1<<0),1,2], "feb" => [(1<<1)|(1<<0),2,2], "m�r" => [(1<<1),3,2],
   "apr" => [(1<<1)|(1<<0),4,2], "jun" => [(1<<1)|(1<<0),6,2], "jul" => [(1<<1)|(1<<0),7,2],
   "aug" => [(1<<1)|(1<<0),8,2], "sep" => [(1<<1)|(1<<0),9,2], "okt" => [(1<<1),10,2],
   "nov" => [(1<<1)|(1<<0),11,2], "dez" => [(1<<1),12,2],
   # French
   "janvier" => [(1<<4),1,1], "f�vrier" => [(1<<4),2,1], "mars" => [(1<<4),3,1],
   "avril" => [(1<<4),4,1],
   "juin" => [(1<<4),6,1], "juillet" => [(1<<4),7,1], "ao�t" => [(1<<4),8,1],
   "septembre" => [(1<<4),9,1], "octobre" => [(1<<4),10,1],
   "novembre" => [(1<<4),11,1], "d�cembre" => [(1<<4),12,1]
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
   "sa" => [(1<<1),6,2], "so" => [(1<<1),0,2], "mo" => [(1<<1),1,2], "di" => [(1<<1),2,2],
   "mi" => [(1<<1),3,2], "do" => [(1<<1),4,2], "fr" => [(1<<1),5,2],
   "samstag" => [(1<<1),6,1], "sonnabend" => [(1<<1),6,1], "sonntag" => [(1<<1),0,1],
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
   "heute" => [(1<<1),0,1], "morgen" => [(1<<1),1,1], "�bermorgen" => [(1<<1),2,1],
   "aujourd'hui" => [(1<<4),0,1], "demain" => [(1<<4),1,1], "apr�s-demain" => [(1<<4),2,1]
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
         PageToLatin($page, $sub);

         DetectOvFormatParse(\%FmtStat, \%SubtStat, $page, $sub);
      }
   }

   my @FmtSort = sort { $FmtStat{$b} <=> $FmtStat{$a} } keys %FmtStat;
   my $fmt;
   if (defined($FmtSort[0])) {
      $fmt = $FmtSort[0];
      my @SubtSort = grep(substr($_, length($_)-length($fmt)) eq $fmt, keys(%SubtStat));
      @SubtSort = sort { $SubtStat{$b} <=> $SubtStat{$a} } @SubtSort;
      if ($#SubtSort >= 0) {
         $fmt .= ",". (split(/,/, $SubtSort[0]))[0];
      } else {
         # fall back to title offset
         $fmt .= ",". (split(/,/, $fmt))[0];
      }
      print "auto-detected overview format: $fmt\n" if $opt_debug;
   } else {
      $fmt = undef;
      print "auto-detected overview failed, pages ".join(",",@Pages)."\n" if $opt_debug;
   }
   return $fmt;
}

# ! 20.15  Eine Chance f�r die Liebe UT         ARD
#           Spielfilm, D 2006 ........ 344
#                     ARD-Themenwoche Krebs         
#
# 19.35  1925  WISO ................. 317       ZDF
# 20.15        Zwei gegen Zwei           
#              Fernsehfilm D/2005 UT  318
#
# 10:00 WORLD NEWS                              CNNi (week overview)
# News bulletins on the hour (...)
# 10:30 WORLD SPORT                      
#
sub DetectOvFormatParse {
   my ($fmt, $subt, $page, $sub) = @_;
   my ($off, $time_off, $vps_off, $title_off, $subt_off);
   my $line;
   my $handle;
   my $pgtext = $PageText{$page | ($sub << 12)};

   for ($line = 5; $line <= 21; $line++) {
      $_ = $pgtext->[$line];
      # look for a line containing a start time
      # TODO allow start-stop times "10:00-11:00"?
      if ( (m#^(( *| *\! +)(\d\d)[\.\:](\d\d) +)#) &&
           ($3 < 24) && ($4 < 60) ) {

         $off = length $1;
         $time_off = length $2;

         # TODO VPS must be magenta or concealed
         if (substr($_, $off) =~ m#^(\d{4} +)#) {
            $vps_off = $off;
            $off += length $1;
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

         if ( ($vps_off == -1) ?
              ($pgtext->[$line + 1] =~ m#^( *| *\d{4} +)[[:alpha:]]#) :
              ($pgtext->[$line + 1] =~ m#^( *)[[:alpha:]]#) ) {

            $subt_off = length($1);
            $handle = "$subt_off,$time_off,$vps_off,$title_off";
            if (defined ($subt->{$handle})) {
               $subt->{$handle} += 1;
            } else {
               $subt->{$handle} = 1;
            }
         }
      }
   }
}

# ------------------------------------------------------------------------------
# Parse date in an overview page
# - similar to dates on description pages
#
sub ParseOvHeaderDate {
   my ($page, $sub, $pgdate) = @_;
   my $reldate;
   my ($day_of_month, $month, $year);

   my $pgtext = $PageText{$page | ($sub << 12)};
   my $lang = $PgLang{$page};
   my $wday_match = GetDateNameRegExp(\%WDayNames, $lang, 1);
   my $wday_abbrv_match = GetDateNameRegExp(\%WDayNames, $lang, 2);
   my $mname_match = GetDateNameRegExp(\%MonthNames, $lang, undef);
   my $relday_match = GetDateNameRegExp(\%RelDateNames, $lang, undef);

   PAGE_LOOP:
   for (my $line = 1; $line < $pgdate->{head_end}; $line++) {
      $_ = $pgtext->[$line];

      PARSE_DATE: {
         # [Mo.]13.04.2006
         if (m#(^| |($wday_abbrv_match)\.)(\d{1,2})\.(\d{1,2})\.(\d{2}|\d{4})?([ ,:]|$)#i) {
            my @NewDate = CheckDate($3, $4, $5, undef, undef);
            if (@NewDate) {
               ($day_of_month, $month, $year) = @NewDate;
               last PARSE_DATE;
            }
         }
         # 13.April [2006]
         if (m#(^| )(\d{1,2})\. ?($mname_match)( (\d{2}|\d{4}))?([ ,:]|$)#i) {
            my @NewDate = CheckDate($2, undef, $5, undef, $3);
            if (@NewDate) {
               ($day_of_month, $month, $year) = @NewDate;
               last PARSE_DATE;
            }
         }
         # Sunday 22 April (i.e. no dot after month)
         if ( m#(^| )($wday_match)(, ?| )(\d{1,2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|$)#i ||
              m#(^| )($wday_abbrv_match)(\.,? ?|, ?| )(\d{1,2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|$)#i) {
            my @NewDate = CheckDate($4, undef, $7, $2, $5);
            if (@NewDate) {
               ($day_of_month, $month, $year) = @NewDate;
               last PARSE_DATE;
            }
         }
         # today, tomorrow, ...
         if (m#(^| )($relday_match)([ ,:]|$)#i) {
            my $rel_name = lc($2);
            $reldate = $RelDateNames{$rel_name}->[1] if defined($RelDateNames{$rel_name});
            last PARSE_DATE;
         }
         # monday, tuesday, ... (non-abbreviated only)
         if (m#(^| )($wday_match)([ ,:]|$)#i) {
            my $off = GetWeekDayOffset($2);
            $reldate = $off if $off >= 0;
            last PARSE_DATE;
         }
         # "Do. 21-22 Uhr" (e.g. on VIVA)
         # TODO internationalize "Uhr"
         if (m#(^| )(($wday_abbrv_match)\.?|($wday_match)) \d{1,2}([\.\:]\d{2})?(\-| \- )\d{1,2}([\.\:]\d{2})?( +Uhr| ?h( |$))#i) {
            my $wday_name = $2;
            $wday_name = $3 if !defined($2);
            my $off = GetWeekDayOffset($wday_name);
            $reldate = $off if $off >= 0;
            last PARSE_DATE;
         }
      }
   }
   if (!defined($day_of_month) && defined($reldate)) {
      ($day_of_month, $month, $year) = (localtime($VbiCaptureTime + $reldate*24*60*60))[3,4,5];
      $month += 1;
      $year += 1900;
   } elsif (defined($year) && ($year < 100)) {
      my $cur_year = (localtime($VbiCaptureTime))[5] + 1900;
      $year = ($cur_year - $cur_year%100);
   }

   # return results via the struct
   $pgdate->{day_of_month} = $day_of_month;
   $pgdate->{month} = $month;
   $pgdate->{year} = $year;
   $pgdate->{date_off} = 0;
}

# ------------------------------------------------------------------------------
# Chop footer
# - footers usually contain advertisments (or more precisely: references
#   to teletext pages with ads) and can simply be discarded

#   Schule. Dazu Arbeitslosigkeit von    
#   bis zu 30 Prozent. Wir berichten �-  
#   ber Menschen in Ostdeutschland.  318>
#    Ab in die Sonne!................500 

#   Teilnahmebedingungen: Seite 881      
#     T�rkei-Urlaub jetzt buchen! ...504 


sub ParseFooter {
   my ($page, $sub, $head) = @_;
   my $foot;

   my $pgtext = $PageText{$page | ($sub << 12)};
   my $pgctrl = $PageCtrl{$page | ($sub << 12)};

   for ($foot = $#{$pgtext} ; $foot > $head; $foot--) {
      # note missing lines are treated as empty lines
      $_ = $pgtext->[$foot];

      # stop at lines which look like separators
      if (m#^ *\-{10,} *$#) {
         $foot -= 1;
         last;
      }
      if (!m#\S#) {
         if ($pgctrl->[$foot - 1] !~ m#[\x0D\x0F][^\x0C]*[^ ]#) {
            $foot -= 1;
            last;
         }
         # ignore this empty line since there's double-height chars above
         next;
      }
      # check for a teletext reference
      # TODO internationalize
      if ( m#^ *(seite |page |\<+ *)?[1-8][0-9][0-9][^\d]#i ||
           m#[^d][1-8][0-9][0-9]([\.\!\?\:\,]|\>+)? *$# ) {
           #|| (($sub != 0) && m#(^ *<|> *$)#) ) {
      } else {
         last;
      }
   }
   # plausibility check (i.e. don't discard the entire page)
   if ($foot < $#{$pgtext} - 5) {
      $foot = $#{$pgtext};
   }
   return $foot;
}

# Try to identify footer lines by a different background color
# - bg color is changed by first changing the fg color (codes 0x0-0x7),
#   then switching copying fg over bg color (code 0x1D)
sub ParseFooterByColor {
   my ($page, $sub) = @_;
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

   # TODO: merge with other footer function: require TTX ref in every skipped segment with the same bg color

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
# - the layout has already been determined in advance, i.e. we assume that we
#   have a tables with strict columns for times and titles; rows that don't
#   have the expected format are skipped (normally only header & footer)
#
sub ParseOvList {
   my ($page, $sub, $pgfmt, $pgdate) = @_;
   my ($hour, $min, $title, $is_tip);
   my $line;
   my $new_title;
   my $in_title = 0;
   my $slot = {};
   my $vps_data = {};
   my @Slots = ();

   my ($time_off, $vps_off, $title_off, $subt_off) = split(",", $pgfmt);
   my $pgtext = $PageText{$page | ($sub << 12)};
   my $pgctrl = $PageCtrl{$page | ($sub << 12)};

   for ($line = 1; $line <= 23; $line++) {
      $_ = $pgtext->[$line];

      # extract and remove VPS labels
      # (labels not asigned here since we don't know yet if a new title starts in this line)
      $vps_data->{new_vps_time} = 0;
      if ($pgctrl->[$line] =~ m#[\x05\x18]+ *[A-Fa-f0-9]#) {
         ParseVpsLabel($_, $pgctrl->[$line], $vps_data, 0);
      }

      $new_title = 0;
      if ($vps_off >= 0) {
         # TODO: Phoenix wraps titles into 2nd line, appends subtitle with "-"
         # m#^ {0,2}(\d\d)[\.\:](\d\d)( {1,3}(\d{4}))? +#
         if (substr($_, 0, $time_off) =~ m#^ *([\*\!] +)?$#) {
            $is_tip = $1;
            if (substr($_, $time_off, $vps_off - $time_off) =~ m#^(\d\d)[\.\:](\d\d) +$#) {
               $hour = $1;
               $min = $2;
               # VPS time has already been extractied above
               if (substr($_, $vps_off, $title_off - $vps_off) =~ m#^ +$#) {
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
               $title = substr($_, $title_off);
               $new_title = 1;
            }
         }
      }

      if ($new_title) {
         # remember end of page header for date parser
         $pgdate->{head_end} = $line unless defined $pgdate->{head_end};

         # push previous title
         if (defined $slot->{title}) {
            push(@Slots, $slot);
            if (($hour < $slot->{hour}) && !$vps_data->{new_vps_date}) {
               $vps_data->{vps_date} = undef;
            }
         }
         $vps_data->{new_vps_date} = 0;

         $slot = {};
         $slot->{hour} = $hour;
         $slot->{min} = $min;
         $slot->{is_tip} = $is_tip if defined $is_tip;
         $slot->{vps_time} = $vps_data->{vps_time} if $vps_data->{new_vps_time};
         $slot->{vps_date} = $vps_data->{vps_date};
         $slot->{vps_cni} = $vps_data->{vps_cni};

         $title =~ s#^ +##;
         $title =~ s# +$##;
         $title =~ s#  +# #g;
         ParseTrailingFeat($title, $slot);
         print "OV TITLE: \"$title\", $slot->{hour}:$slot->{min}\n" if $opt_debug;

         # kika special: subtitle appended to title
         if ($title =~ m#(.*\(\d+( *[\&\-] *\d+)?\))\/ *(\S.*)#) {
            $title = $1;
            $slot->{subtitle} = $3;
            $slot->{subtitle} =~ s# +$# #;
         }
         $slot->{title} = $title;
         $in_title = 1;
         #print "ADD  $slot->{day_of_month}.$slot->{month} $slot->{hour}.$slot->{min} $slot->{title}\n";

      } elsif ($in_title) {

         $slot->{vps_time} = $vps_data->{vps_time} if $vps_data->{new_vps_time};

         # check if last line specifies and end time
         # (usually last line of page)
         # TODO internationalize "bis", "Uhr"
         if ( m#^ *(\(bis |\- ?)(\d{1,2})[\.\:](\d{2})( Uhr| ?h)( |\)|$)# ||   # ARD,SWR()
              m#^( *)(\d\d)[\.\:](\d\d) (oo)? *$# ) {                          # arte, RTL
            $slot->{end_hour} = $2;
            $slot->{end_min} = $3;
            $new_title = 1;
            print "Overview end time: $slot->{end_hour}:$slot->{end_min}\n" if $opt_debug;

         # check if we're still in a continuation of the previous title
         # time column must be empty (possible VPS labels were already removed above)
         } else {
            if ( (substr($_, 0, $subt_off) =~ m#[^ ]#) ||
                 (substr($_, $subt_off) =~ m#^ *$#) ) {
               $new_title = 1;
            }
         }
         if ($new_title == 0) {
            ParseTrailingFeat($_, $slot);
            s#^ +##;
            s# +$# #;
            s#  +# #;
            # combine words separated by line end with "-"
            if ( !defined($slot->{subtitle}) &&
                 ($slot->{title} =~ m#\S\-$#) && m#^[[:lower:]]#) {
               $slot->{title} =~ s#\-$##;
               $slot->{title} .= $_;
            } else {
               if ( defined($slot->{subtitle}) &&
                    ($slot->{subtitle} =~ m#\S\-$#) && m#^[[:lower:]]#) {
                  $slot->{subtitle} =~ s#\-$##;
                  $slot->{subtitle} .= $_;
               } elsif (defined($slot->{subtitle})) {
                  $slot->{subtitle} .= " " . $_;
               } else {
                  $slot->{subtitle} = $_;
               }
            }
         } else {
            push(@Slots, $slot) if defined $slot->{title};
            $slot = {};
            $in_title = 0;
         }
      }
   }
   push(@Slots, $slot) if defined $slot->{title};

   return @Slots;
}

# ------------------------------------------------------------------------------
# Parse and remove VPS indicators
# - format as defined in ETS 300 231, ch. 7.3.1.3
# - for performance reasons the caller should check if there's a magenta
#   or conceal character in the line before calling this function
#
sub ParseVpsLabel {
   my ($foo, $pgctrl, $vps_data, $is_desc) = @_;

   # time
   # discard any concealed/magenta labels which follow
   if ($pgctrl =~ m#^(.*)([\x05\x18]+(VPS[\x05\x18 ]+)?(\d{4})([\x05\x18 ]+[\dA-Fs]*)*([\x00-\x04\x06\x07]|$))#) {
      $vps_data->{vps_time} = $3;
      $vps_data->{new_vps_time} = 1;
      # blank out the same area in the text-only string
      substr($_[0], length($1), length($2), " " x length($2));
      print "VPS time found: $vps_data->{vps_time}\n" if $opt_debug

   # CNI and date "1D102 120406 F9" (ignoring checksum)
   } elsif ($pgctrl =~ m#^(.*)([\x05\x18]+([0-9A-F]{2}\d{3})[\x05\x18 ]+(\d{6})([\x05\x18 ]+[\dA-Fs]*)*([\x00-\x04\x06\x07]|$))#) {
      my $cni_str = $3;
      $vps_data->{vps_date} = $4;
      $vps_data->{new_vps_date} = 1;
      substr($_[0], length($1), length($2), " " x length($2));
      $vps_data->{vps_cni} = ConvertVpsCni($cni_str);
      printf("VPS date and CNI: 0x%X, %d\n", $vps_data->{vps_cni}, $vps_data->{vps_date}) if $opt_debug

   # date
   } elsif ($pgctrl =~ m#^(.*)([\x05\x18]+(\d{6})([\x05\x18 ]+[\dA-Fs]*)*([\x00-\x04\x06\x07]|$))#) {
      $vps_data->{vps_date} = $3;
      $vps_data->{new_vps_date} = 1;
      substr($_[0], length($1), length($2), " " x length($2));
      print "VPS date: $3\n" if $opt_debug

   # end time (RTL special - not standardized)
   } elsif (!$is_desc &&
            $pgctrl =~ m#^(.*)([\x05\x18]+(\d{2}[.:]\d{2}) oo *([\x00-\x04\x06\x07]|$))#) {
      # detected by the OV parser without evaluating the conceal code
      #$vps_data->{page_end_time} = $3;
      #print "VPS(pseudo) page end time: $vps_data->{page_end_time}\n" if $opt_debug

   # "VPS" marker string
   } elsif ($pgctrl =~ m#^(.*)([\x05\x18][\x05\x18 ]*VPS *([\x00-\x04\x06\x07]|$))#) {
      substr($_[0], length($1), length($2), " " x length($2));

   } else {
      print "VPS label unrecognized in line \"$_[0]\"\n" if $opt_debug;

      # on description page replacing all concealed text with blanks
      # FIXME text can also be concealed by setting fg := bg (e.g. on desc pages of MDR)
      if ($is_desc && $pgctrl =~ m#^(.*)(\x18[^\x00-\x07\x10-\x17]*)#) {
         substr($_[0], length($1), length($2), " " x length($2));
      }
   }
}

# ------------------------------------------------------------------------------
# Convert a "traditional" VPS CNI into a 16-bit PDC channel ID
# - first two digits are the PDC country code in hex (e.g. 1D,1A,24 for D,A,CH)
# - 3rd digit is an "address area", indicating network code tables 1 to 4
# - the final 3 digits are decimal and the index into the network table
#
sub ConvertVpsCni {
   my($cni_str) = @_;

   if ($cni_str =~ m#([0-9A-F]{2})([1-4])(\d{2})#) {
      return (hex($1)<<8) | ((4 - $2) << 6) | ($3 & 0x3F);
   } else {
      return undef;
   }

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
# 13.15  1315  Heilkraft aus der W�ste   
#              (1/2) Geheimnisse der     
#              Buschm�nner 16:9 oo       
#
# TODO: some tags need to be localized (i.e. same as month names etc.)
my %FeatToFlagMap =
(
   "untertitel" => "has_subtitles",
   "ut" => "has_subtitles",
   "omu" => "is_omu",
   "s/w" => "is_bw",
   "16:9" => "is_aspect_16_9",
   "oo" => "is_stereo",
   "stereo" => "is_stereo",
   "2k" => "is_2chan",
   "2k-ton" => "is_2chan",
   "dolby" => "is_dolby",
   "surround" => "is_dolby",
   "stereo" => "is_stereo",
   "mono" => "is_mono",
   "tipp" => "is_top"
);
# note: must correct $n below if () are added to pattern
my $FeatPat = "UT(( auf | )?[1-8][0-9][0-9])?|".
              "[Uu]ntertitel|[Hh]�rfilm|HF|".
              "s\/w|S\/W|tlw. s\/w|AD|oo|��|OmU|16:9|".
              "2K|2K-Ton|[Mm]ono|[Ss]tereo|[Dd]olby|[Ss]urround|".
              "Wh\.|Wdh\.|Tipp!";

sub ParseTrailingFeat {
   my ($foo, $slot) = @_;

   # teletext reference
   # these are always right-most, so parse these first and outside of the loop
   if ($_[0] =~ s#( \.+|\.* +|\.\.+|\.+\>|\>\>?|\-\-?\>)([1-8][0-9][0-9]) *$##) {
      my $ref = $2;
      $_[0] =~ s# +$##;
      $slot->{ttx_ref} = ((ord(substr($ref, 0, 1)) - 0x30)<<8) |
                             ((ord(substr($ref, 1, 1)) - 0x30)<<4) |
                             (ord(substr($ref, 2, 1)) - 0x30);
   }
   # features (e.g. WDR: "Stereo, UT")
   while (1) {
      if ($_[0] =~ s#,? +($FeatPat) *$##) {
         my $feat = lc($1);
         $feat =~ s#^ut.*#ut#;
         my $flag = $FeatToFlagMap{$feat};
         if (defined($flag)) {
            print "FEAT \"$feat\" -> $flag on TITLE $_[0]\n" if $opt_debug;
            $slot->{$flag} = 1;
         } else {
            print "FEAT dropping \"$feat\" on TITLE $_[0]\n" if $opt_debug;
         }
      }
      # ARTE: (oo) (2K) (Mono)
      # SWR: "(16:9/UT)", "UT 150", "UT auf 150"
      # (Note on 2nd match: using $6 due to two () inside feature pattern!)
      elsif ( ($_[0] =~ s# +\(($FeatPat)\) *$##) ||
              ($_[0] =~ s# +\(($FeatPat)((, ?|\/| )($FeatPat))*\) *$# ($6)#) ) { # $6: see note
         my $feat = lc($1);
         $feat =~ s#^ut.*#ut#;
         my $flag = $FeatToFlagMap{$feat};
         if (defined($flag)) {
            print "FEAT \"$feat\" -> $flag on TITLE $_[0]\n" if $opt_debug;
            $slot->{$flag} = 1;
         } else {
            print "FEAT dropping \"$feat\" on TITLE $_[0]\n" if $opt_debug;
         }
      } else {
         last;
      }
   } 
   $_[0] =~ s# +$##;
}

# ------------------------------------------------------------------------------
# Parse programme overview table
# - 1: retrieve programme list (i.e. start times and titles)
# - 2: retrieve date from the page header
# - 3: determine dates
# 
sub ParseOv {
   my($page, $sub, $fmt, $pgdate, $prev_pgdate, $prev_slot1, $prev_slot2) = @_;
   my ($head, $foot);
   my ($day_of_month, $month, $year);
   my @Slots;

   printf("OVERVIEW PAGE %03X.%04X\n", $page, $sub) if $opt_debug;

   PageToLatin($page, $sub);

   @Slots = ParseOvList($page, $sub, $fmt, $pgdate);
   if ($#Slots >= 0) {
      ParseOvHeaderDate($page, $sub, $pgdate);
      if (CalculateDates($pgdate, $prev_pgdate, $prev_slot1, $prev_slot2, \@Slots) == 0) {
         @Slots = ();
      }
   }
   return @Slots;
}

# ------------------------------------------------------------------------------
# Retrieve programme data from an overview page
# - 1: compare several overview pages to identify the layout
# - 2: parse all overview pages, retrieving titles and ttx references
# - 3: export into XML and include references descriptions
#
sub ParseAllOvPages {
   my $page;
   my ($sub, $sub1, $sub2);
   my %PgDate;
   my ($pgdate, $prev_pgdate, $prev_slot1, $prev_slot2);
   my @Slots = ();
   my $slot;

   my $fmt = DetectOvFormat();
   if ($fmt) {

      my @Pages = grep {($_>=$opt_tv_start) && ($_<=$opt_tv_end)} keys(%PgCnt);
      @Pages = sort {$a<=>$b} @Pages;

      foreach $page (@Pages) {
         if ($PgSub{$page} == 0) {
            $sub1 = $sub2 = 0;
         } else {
            $sub1 = 1;
            $sub2 = $PgSub{$page};
         }
         for ($sub = $sub1; $sub <= $sub2; $sub++) {
            $pgdate = {'page' => $page, 'sub_page' => $sub };
            my @TmpSlots = ParseOv($page, $sub, $fmt, $pgdate, $prev_pgdate, $prev_slot1, $prev_slot2);
            push @Slots, @TmpSlots;

            if ($#TmpSlots >= 0) {
               $prev_pgdate = $pgdate;
               $prev_slot1 = $TmpSlots[0];
               $prev_slot2 = $TmpSlots[$#TmpSlots];
            } else {
               $prev_pgdate = $prev_slot1 = $prev_slot2 = undef;
            }
         }
      }
   } else {
      print STDERR "No overview pages found\n";
   }

   # guess missing stop times
   CalculateStopTimes(\@Slots);

   # retrieve descriptions from references teletext pages
   foreach $slot (@Slots) {
      if (defined($slot->{ttx_ref})) {
         $slot->{desc} = ParseDescPage($slot);
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
   foreach $slot (@Slots) {
      ExportTitle($slot, $ch_id);
   }
   print "</tv>\n";
}

# ------------------------------------------------------------------------------
# Determine dates of programmes on an overview page
# - TODO: arte: remove overlapping: the first one encloses multiple following programmes; possibly recognizable by the VP label 2500
#  "\x1814.40\x05\x182500\x07\x07THEMA: DAS \"NEUE GROSSE   ",
#  "              SPIEL\"                    ",
#  "\x0714.40\x05\x051445\x02\x02USBEKISTAN - ABWEHR DER   ",
#  "            \x02\x02WAHABITEN  (2K)           ",
#
sub CalculateDates {
   my ($pgdate, $prev_pgdate, $prev_slot1, $prev_slot2, $slot_list) = @_;
   my $retval = 0;

   my $date_off = 0;
   if (defined($prev_pgdate)) {
      # check if the page number of the previous page is adjacent
      # and as consistency check require that the prev page covers less than a day
      if ( ($prev_slot2->{start_t} - $prev_slot1->{start_t} < 22*60*60) &&
           CheckIfPagesAdjacent($prev_pgdate->{page}, $prev_pgdate->{sub_page},
                                 $pgdate->{page}, $pgdate->{sub_page}) ) {

         my $prev_delta = 0;
         # check if there's a date on the current page
         # if yes, get the delta to the previous one in days (e.g. tomorrow - today = 1)
         # if not, silently fall back to the previous page's date and assume date delta zero
         if (defined($pgdate->{year})) {
            $prev_delta = CalculateDateDelta($prev_pgdate, $pgdate);
         }
         if ($prev_delta == 0) {
            # check if our start date should be different from the one of the previous page
            # -> check if we're starting on a new date (smaller hour)
            # (note: comparing with the 1st slot of the prev. page, not the last!)
            if ($slot_list->[0]->{hour} < $prev_slot1->{hour}) {
               # TODO: check continuity to slot2: gap may have 6h max.
               # TODO: check end hour
               $date_off = 1;
            }
            # else: same date as the last programme on the prev page
            # may have been a date change inside the prev. page but our header date may still be unchanged
            # historically this is only done up to 6 o'clock (i.e. 0:00 to 6:00 counts as the old day)
            #} elsif ($slot_list->[0]->{hour} <= 6) {
            #   # check continuity
            #   if ( defined($prev_slot2->{end_hour}) ?
            #          ( ($slot_list->[0]->{hour} >= $prev_slot2->{end_hour}) &&
            #            ($slot_list->[0]->{hour} - $prev_slot2->{end_hour} <= 1) ) :
            #          ( ($slot_list->[0]->{hour} >= $prev_slot2->{hour}) &&
            #            ($slot_list->[0]->{hour} - $prev_slot2->{hour} <= 6) ) ) {
            #      # OK
            #   } else {
            #      # 
            #   }
            #}
            # TODO: check for date change within the previous page
         } else {
            $prev_pgdate = undef;
         }
         # TODO: date may also be wrong by +1 (e.g. when starting at 23:55 with date for 00:00)
      } else {
         # not adjacent -> disregard the info
         printf("OV DATE: prev page %03X.%04X not adjacent - not used for date check\n", $prev_pgdate->{page}, $prev_pgdate->{sub_page}) if $opt_debug;
         $prev_pgdate = undef;
      }
   }

   my $day_of_month;
   my $month;
   my $year;

   if (defined($pgdate->{year})) {
      $day_of_month = $pgdate->{day_of_month};
      $month = $pgdate->{month};
      $year = $pgdate->{year};
      # store date offset in page meta data (to calculate delta of subsequent pages)
      $pgdate->{date_off} = $date_off;
      print "OV DATE: using page header date $day_of_month.$month.$year, DELTA $date_off\n" if $opt_debug;

   } elsif (defined($prev_pgdate)) {
      # copy date from previous page
      $pgdate->{day_of_month} = $day_of_month = $prev_pgdate->{day_of_month};
      $pgdate->{month} = $month = $prev_pgdate->{month};
      $pgdate->{year} = $year = $prev_pgdate->{year};
      # add date offset if a date change was detected
      $date_off += $prev_pgdate->{date_off};
      $pgdate->{date_off} = $date_off;
      print "OV DATE: using predecessor date $day_of_month.$month.$year, DELTA $date_off\n" if $opt_debug;
   }

   # finally assign the date to all programmes on this page (with auto-increment at hour wraps)
   if (defined($year)) {
      my $prev_hour;
      for (my $idx = 0; $idx <= $#$slot_list; $idx++) {
         my $slot = $slot_list->[$idx];

         # detect date change (hour wrap at midnight)
         if (defined($prev_hour) && ($prev_hour > $slot->{hour})) {
            $date_off += 1;
         }
         $prev_hour = $slot->{hour};

         $slot->{date_off} = $date_off;
         $slot->{day_of_month} = $pgdate->{day_of_month};
         $slot->{month} = $pgdate->{month};
         $slot->{year} = $pgdate->{year};
         $slot->{vps_date} = $pgdate->{vps_date};

         $slot->{start_t} = ConvertStartTime($slot);
      }
      $retval = 1;
   } else {
      print "OV missing date - discarding programmes\n" if $opt_debug;
   }
   return $retval;
}

# ------------------------------------------------------------------------------
# Check if two given teletext pages are adjacent
# - both page numbers must have decimal digits only (i.e. match /[1-8][1-9][1-9]/)
#
sub CheckIfPagesAdjacent {
   my ($prev_page, $prev_sub, $page, $sub) = @_;
   my $result = 0;

   if ( ($prev_page == $page) && ($prev_sub + 1 == $sub) ) {
      # next sub-page on same page
      $result = 1;

   } else {
      # check for jump from last sub-page of prev page to first of new page
      my $next;

      # calculate number of page following the previous one
      # visible pages only use decimal numbers, so we jump from 9 to 16
      if (($prev_page & 0xF) == 9) {
         $next = $prev_page + (0x010 - 0x009);
      } else {
         $next = $prev_page + 1;
      }
      # same skip for the 2nd digit, if there was a wrap in the 3rd digit
      if (($next & 0xF0) == 0x0A0) {
         $next = $next + (0x100 - 0x0A0);
      }

      if ( ($page == $next) &&
           ($prev_sub == $PgSub{$prev_page}) &&
           ($sub <= 1) ) {
         $result = 1;
      } 
   }
   return $result;
}

# ------------------------------------------------------------------------------
# Determine stop times
# - assuming that in overview tables the stop time is equal to the start
#   of the following programme & that this also holds true inbetween pages
# - if in doubt, leave it undefined (this is allowed in XMLTV)
# - TODO: restart at non-adjacent text pages
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
            # check for a day break between start and end
            if ( ($slot->{end_hour} < $slot->{hour}) ||
                 ( ($slot->{end_hour} == $slot->{hour}) &&
                   ($slot->{end_min} < $slot->{min}) )) {
               $date_off += 1;
            }

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

# ------------------------------------------------------------------------------
# Convert discrete start times into UNIX epoch format
# - implies a conversion from local time zone into GMT
#
sub ConvertStartTime {
   my ($slot) = @_;

   return POSIX::mktime(0, $slot->{min}, $slot->{hour},
                           $slot->{day_of_month} + $slot->{date_off},
                           $slot->{month} - 1,
                           $slot->{year} - 1900,
                           0, 0, -1);
}

# ------------------------------------------------------------------------------
# Calculate delta in days between the given programme slot and a discrete date
# - works both on program "slot" and "pgdate" which have the same member names
#   (since Perl doesn't have strict typechecking for structs)
#
sub CalculateDateDelta {
   my ($slot1, $slot2) = @_;

   my $date1 = POSIX::mktime(0, 0, 0, $slot1->{day_of_month} + $slot1->{date_off},
                             $slot1->{month} - 1, $slot1->{year} - 1900, 0, 0, -1);

   my $date2 = POSIX::mktime(0, 0, 0, $slot2->{day_of_month} + $slot2->{date_off},
                             $slot2->{month} - 1, $slot2->{year} - 1900, 0, 0, -1);

   # add 2 hours to allow for shorter days during daylight saving time change
   return int(($date2 + 2*60*60 - $date1) / (24*60*60));
}

# ------------------------------------------------------------------------------
# Determine LTO for a given time and date
# - LTO depends on the local time zone and daylight saving time being in effect or not
#
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
      if (defined($slot_ref->{vps_time})) {
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
      if (defined($slot_ref->{is_omu})) {
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

   if (defined($slot->{vps_date})) {
      $vps_str = $slot->{vps_date};
   } else {
      $vps_str = sprintf("%04d%02d%02d", $slot->{year}, $slot->{day_of_month}, $slot->{month});
   }
   $vps_str .= $slot->{vps_time} .
               sprintf("00 %+03d%02d", $lto / (60*60), abs($lto / 60) % 60);
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
#         Ehen auf dem Pr�fstand                 

#ZDF:
#  ZDFtext          ZDF.aspekte
#  Programm  Freitag, 21.April   [no time!]

#MDR:
#Mittwoch, 12.04.2006, 14.30-15.30 Uhr  
#                                        
# LexiTV - Wissen f�r alle               
# H - wie H�hner                        

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
# HR3:   Montag 8.45-9.15 Uhr
#        Montag // 9.15-9.45 (i.e. 2-line format)
# Kabl1: Fr 14.04 20:15-21:11
# Tele5: Fr 19:15-20:15

sub ParseDescDate {
   my ($page, $sub, $slot) = @_;
   my ($lmday, $lmonth, $lyear, $lhour, $lmin, $lend_hour, $lend_min);
   my ($new_date, $check_time);
   my $line;

   my $pgtext = $PageText{$page | ($sub << 12)};
   my $lang = $PgLang{$page};
   my $wday_match = GetDateNameRegExp(\%WDayNames, $lang, 1);
   my $wday_abbrv_match = GetDateNameRegExp(\%WDayNames, $lang, 2);
   my $mname_match = GetDateNameRegExp(\%MonthNames, $lang, undef);

   # search date and time
   for ($line = 0; $line <= 23; $line++) {
      $_ = $pgtext->[$line];
      $new_date = 0;

      PARSE_DATE: {
         # [Fr] 14.04.[06]
         if (m#(^| )(\d{2})\.(\d{2})\.(\d{4}|\d{2})?( |,|\:|$)#) {
            my @NewDate = CheckDate($2, $3, $4, undef, undef);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last PARSE_DATE;
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
               last PARSE_DATE;
            }
         }
         # 14.[ ]April [2006]
         if ( m#(^| )(\d{1,2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|\:|$)#i) {
            my @NewDate = CheckDate($2, undef, $5, undef, $3);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last PARSE_DATE;
            }
         }
         # Sunday 22 April (i.e. no dot after day)
         if ( m#(^| )($wday_match)(, ?| )(\d{1,2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|\:|$)#i ||
              m#(^| )($wday_abbrv_match)(\.,? ?|, ?| )(\d{1,2})\.? ?($mname_match)( (\d{4}|\d{2}))?( |,|\:|$)#i) {
            my @NewDate = CheckDate($4, undef, $7, $2, $5);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last PARSE_DATE;
            }
         }
         # TODO: "So, 23." (n-tv)

         # Fr[,] 18:00 [-19:00] [Uhr|h]
         # TODO parse time (i.e. allow omission of "Uhr")
         if (m#(^| )($wday_match)(, ?| )(\d{1,2}[\.\:]\d{2}((-| - )\d{1,2}[\.\:]\d{2})?(h| |,|\:|$))#i ||
             m#(^| )($wday_abbrv_match)(\.,? ?|, ?| )(\d{1,2}[\.\:]\d{2}((-| - )\d{1,2}[\.\:]\d{2})?(h| |,|\:|$))#i) {
            my @NewDate = CheckDate(undef, undef, undef, $2, undef);
            if (@NewDate) {
               ($lmday, $lmonth, $lyear) = @NewDate;
               $new_date = 1;
               last PARSE_DATE;
            }
         }
         # TODO: 21h (i.e. no minute value: TV5)

         # TODO: make exact match between VPS date and time from overview page
         # TODO: allow garbage before or after label; check reveal and magenta codes (ETS 300 231 ch. 7.3)
         # VPS label "1D102 120406 F9"
         if (s#^ +[0-9A-F]{2}\d{3} (\d{2})(\d{2})(\d{2}) [0-9A-F]{2} *$##) {
            ($lmday, $lmonth, $lyear) = ($1, $2, $3 + 2000);
            last PARSE_DATE;
         }
      }

      # TODO: time should be optional if only one subpage
      # 15.40 [VPS 1540] - 19.15 [Uhr|h]
      $check_time = 0;
      if (m#(^| )(\d{1,2})[\.\:](\d{1,2})( +VPS +(\d{4}))?(-| - | bis )(\d{1,2})[\.\:](\d{1,2})(h| |,|\:|$)# ||
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
      } elsif (m#(^| )(\d{1,2})[\.\:](\d{1,2})( ?h| Uhr)( |,|\:|$)# ||
               m#(^| )(\d{1,2})h(\d{2})( |,|\:|$)#) {
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
      ($mday, $month, $year) = (localtime($VbiCaptureTime + $reldate*24*60*60))[3,4,5];
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
         $year = (localtime($VbiCaptureTime))[5] + 1900;
      } elsif ($year < 100) {
         my $cur_year = (localtime($VbiCaptureTime))[5] + 1900;
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
      my $cur_wday_idx = (localtime($VbiCaptureTime))[6];
      if ($wday_idx >= $cur_wday_idx) {
         $reldate = $wday_idx - $cur_wday_idx;
      } else {
         $reldate = (7 - $cur_wday_idx) + $wday_idx;
      }
   }
   return $reldate;
}

sub ParseDescDateTitle {
   my ($page, $sub, $slot) = @_;
   my $title;
   my $line;
   my ($l, $l1, $l2);

   my $pgtext = $PageText{$page | ($sub << 12)};

   $title = $slot->{title};
   $l1 = $pgtext->[0];
   $l1 =~ s#(^ +| +$)##g;
   $l1 =~ s#  +##g;

   for ($line = 0; $line <= 23/2; $line++) {
      $l2 = $pgtext->[$line + 1];
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
   my ($page, $sub, $head, $foot) = @_;
   my $vps_data = {};
   my $line;

   my $pgtext = $PageText{$page | ($sub << 12)};
   my $pgctrl = $PageCtrl{$page | ($sub << 12)};
   my $is_nl = 0;
   my $desc = "";

   for ($line = $head; $line <= $foot; $line++) {
      $_ = $pgtext->[$line];

      # TODO parse features behind date, title or subtitle

      # extract and remove VPS labels and all concealed text
      if ($pgctrl->[$line] =~ m#[\x05\x18]#) {
         ParseVpsLabel($_, $pgctrl->[$line], $vps_data, 1);
      }

      # remove VPS time and date labels
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

# TODO: check for all referenced text pages where no match was found if they
#       describe a yet unknown programme (e.g. next instance of a weekly series)

sub ParseDescPage {
   my ($slot_ref) = @_;
   my ($sub, $sub1, $sub2);
   my $desc = "";
   my $found = 0;
   my $page = $slot_ref->{ttx_ref};

   if ($opt_debug) {
      my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday) = localtime($slot_ref->{start_t});
      printf("DESC parse page %03X: search match for %s \"%s\"\n", $page,
             POSIX::strftime("%d.%m.%Y %H:%M", 0, $min, $hour, $mday,$mon,$year,$wday,$yday,-1),
             $slot_ref->{title});
   }

   if (defined($PgCnt{$page})) {
      if ($PgSub{$page} == 0) {
         $sub1 = $sub2 = 0;
      } else {
         $sub1 = 1;
         $sub2 = $PgSub{$page};
      }
      for ($sub = $sub1; $sub <= $sub2; $sub++) {
         my($head, $foot, $foot2);

         PageToLatin($page, $sub);
         # TODO: multiple sub-pages may belong to same title, but have no date

         if (ParseDescDate($page, $sub, $slot_ref)) {
            $head = ParseDescDateTitle($page, $sub, $slot_ref);

            $foot = ParseFooterByColor($page, $sub);
            $foot2 = ParseFooter($page, $sub, $head);
            $foot = ($foot2 < $foot) ? $foot2 : $foot;
            printf("DESC page %03X.%04X match found: lines $head-$foot\n", $page, $sub) if $opt_debug;

            $desc .= "\n" if $found;
            $desc .= ParseDescContent($page, $sub, $head, $foot);

            $found = 1;
         } else {
            printf("DESC page %03X.%04X no match found\n", $page, $sub) if $opt_debug;
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

