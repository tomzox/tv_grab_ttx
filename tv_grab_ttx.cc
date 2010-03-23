/**
 * This program captures teletext from a VBI device, scrapes TV programme
 * schedules and descriptions from captured pages and exports them in
 * XMLTV format (DTD version 0.5)
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2006-2010 by Tom Zoerner (tomzo at users.sf.net)
 *
 * Id: tv_grab_ttx.pl,v 1.23 2009/05/03 16:25:39 tom Exp tom 
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <sstream>
#include <map>
#include <string>
#include <deque>
#include <algorithm>

#include "libzvbi.h"
#include "boost/regex.h"
#include "boost/regex.hpp"

using namespace std;
using namespace boost;

const char version[] = "Teletext EPG grabber, v2.0";
const char copyright[] = "Copyright 2006-2010 Tom Zoerner";
const char home_url[] = "http://nxtvepg.sourceforge.net/tv_grab_ttx";

#define VT_PKG_RAW_LEN 40
#define VT_PKG_PG_CNT 30
typedef uint8_t vt_pkg_raw[VT_PKG_RAW_LEN];
typedef char    vt_pkg_txt[VT_PKG_RAW_LEN + 1];
struct vt_page
{
  vt_pkg_txt    m_line[VT_PKG_PG_CNT];
  uint32_t      m_defined;
public:
  vt_page() : m_defined(0) {}
  void add_line(int idx, const uint8_t * p_data) {
     assert(idx < VT_PKG_PG_CNT);
     memcpy(m_line[idx], p_data, VT_PKG_RAW_LEN);
     m_line[idx][VT_PKG_RAW_LEN] = 0;
     m_defined |= 1 << idx;
  }
};
struct vt_pkg_dec
{
   uint16_t     m_page_no;
   uint16_t     m_ctrl_lo;
   uint8_t      m_ctrl_hi;
   uint8_t      m_pkg;
   vt_pkg_raw   m_data;
public:
   // dummy constructor - invalid packet
   vt_pkg_dec() : m_page_no(0xFFFFU) {};
   // constructor for header packet
   void set_pg_header(int page, int ctrl, const uint8_t * data) {
      m_page_no = page;
      m_ctrl_lo = ctrl & 0xFFFFU;
      m_ctrl_hi = ctrl >> 16;
      m_pkg = 0;
      memcpy(m_data, data, VT_PKG_RAW_LEN);
   };
   // constructor for body packet
   void set_pg_row(int mag, int pkg, const uint8_t * data) {
      m_page_no = mag << 8;
      m_ctrl_lo = 0;
      m_ctrl_hi = 0;
      m_pkg = pkg;
      memcpy(m_data, data, VT_PKG_RAW_LEN);
   }
   // constructor for P8/30/1
   void set_cni(int mag, int pkg, int cni) {
      m_page_no = mag << 8;
      m_ctrl_lo = 0;
      m_ctrl_hi = 0;
      m_pkg = pkg;

      uint16_t l_cni = cni;
      memcpy(m_data, &l_cni, sizeof(l_cni));
      memset(m_data + sizeof(l_cni), 0, VT_PKG_RAW_LEN - sizeof(l_cni));
   }
};

class TV_SLOT
{
public:
   TV_SLOT() { clear(); }
   void clear() {
      m_hour = -1;
      m_min = -1;
      m_mday = -1;
      m_month = -1;
      m_year = -1;
      m_date_off = 0;
      m_vps_date.clear();
      m_vps_time.clear();
      m_vps_cni = -1;
      m_end_hour = -1;
      m_end_min = -1;
      m_ttx_ref = -1;
      m_title.clear();
      m_subtitle.clear();
      m_desc.clear();
      m_start_t = -1;
      m_stop_t = -1;

      m_has_subtitles = false;
      m_is_2chan = false;
      m_is_aspect_16_9 = false;
      m_is_video_hd = false;
      m_is_video_bw = false;
      m_is_dolby = false;
      m_is_mono = false;
      m_is_omu = false;
      m_is_stereo = false;
      m_is_tip = false;
   }
   bool is_valid() const { return !m_title.empty(); }
public:
   int          m_hour;
   int          m_min;
   int          m_mday;
   int          m_month;
   int          m_year;
   int          m_date_off;
   string       m_vps_time;
   string       m_vps_date;
   int          m_vps_cni;
   int          m_end_hour;
   int          m_end_min;
   int          m_ttx_ref;
   time_t       m_start_t;
   time_t       m_stop_t;

   bool         m_has_subtitles;
   bool         m_is_2chan;
   bool         m_is_aspect_16_9;
   bool         m_is_video_hd;
   bool         m_is_video_bw;
   bool         m_is_dolby;
   bool         m_is_mono;
   bool         m_is_omu;
   bool         m_is_stereo;
   bool         m_is_tip;

   string       m_title;
   string       m_subtitle;
   string       m_desc;
};

class T_PG_DATE
{
public:
   T_PG_DATE() { clear(); };
   void clear() {
      m_page = -1;
      m_sub_page = -1;
      m_mday = -1;
      m_month = -1;
      m_year = -1;
      m_date_off = 0;
      m_head_end = -1;
      m_sub_page_skip = 0;
   };
   bool is_valid() const { return m_year != -1; };
public:
   int          m_page;
   int          m_sub_page;
   int          m_mday;
   int          m_month;
   int          m_year;
   int          m_date_off;
   int          m_head_end;
   mutable int  m_sub_page_skip;
};

class T_VPS_TIME
{
public:
   string       m_vps_time;
   bool         m_new_vps_time;
   string       m_vps_date;
   bool         m_new_vps_date;
   int          m_vps_cni;
};

map<int,vt_page> Pkg;
map<int,int> PkgCni;
map<int,int> PgCnt;
map<int,int> PgSub;
map<int,int> PgLang;
map<int,vt_page> PageCtrl;
map<int,vt_page> PageText;
time_t VbiCaptureTime;

// command line options for capturing from device
int opt_duration = 0;
const char * opt_device = "/dev/vbi0";
int opt_dvbpid = -1;
int opt_verbose = 0;
int opt_debug = 0;

// command line options for grabbing
const char * opt_infile = 0;
const char * opt_outfile = 0;
const char * opt_mergefile = 0;
const char * opt_chname = "";
const char * opt_chid = "";
int opt_dump = 0;
bool opt_verify = false;
int opt_tv_start = 0x301;
int opt_tv_end = 0x399;
int opt_expire = 120;

// forward declaratins
void PageToLatin(int page, int sub);

/* ------------------------------------------------------------------------------
 * Command line argument parsing
 */
void ParseArgv(int argc, char *argv[])
{
   const char usage[] =
               "Usage: [OPTIONS] [<file>]\n"
               "  -device <path>\t: VBI device used for input (when no file given)\n"
               "  -dvbpid <PID>\t\t: Use DVB stream with given PID\n"
               "  -duration <secs>\t\t: Capture duration in seconds\n"
               "  -page <NNN>-<MMM>\t: Page range for TV schedules\n"
               "  -chn_name <name>\t: display name for XMLTV \"<channel>\" tag\n"
               "  -chn_id <name>\t: channel ID for XMLTV \"<channel>\" tag\n"
               "  -outfile <path>\t: Redirect XMLTV or dump output into the given file\n"
               "  -merge <path>\t\t: Merge output with programmes from file\n"
               "  -expire <N>\t\t: Omit programmes which ended X minutes ago\n"
               "  -dumpvbi\t\t: Dump captured VBI data; no grabbing\n"
               "  -dump\t\t\t: Dump teletext pages as clear text; no grabbing\n"
               "  -dumpraw <NNN>-<MMM>\t: Dump teletext packets in given range; no grabbing\n"
               "  -verify\t\t: Load previously dumped \"raw\" teletext packets\n"
               "  -verbose\t\t: Print teletext page numbers during capturing\n"
               "  -debug\t\t: Emit debug messages during grabbing and device open\n"
               "  -version\t\t: Print version number only, then exit\n"
               "  -help\t\t\t: Print this text, then exit\n"
               "  file or \"-\"\t\t: Read VBI input from file instead of device\n";
   int idx = 1;

   while (idx < argc) {
      // -chn_name: display name in XMLTV <channel> tag
      if (strcmp(argv[idx], "-chn_name") == 0) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -chn_name\n%s", argv[0], usage);
            exit(1);
         }
         opt_chname = argv[idx + 1];
         idx += 2;
      }
      // -chn_name: channel ID in XMLTV <channel> tag
      else if (strcmp(argv[idx], "-chn_id") == 0) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -chn_id\n%s", argv[0], usage);
            exit(1);
         }
         opt_chid = argv[idx + 1];
         idx += 2;
      }
      // -page <NNN>-<MMM>: specify range of programme overview pages
      else if (strcmp(argv[idx], "-pages") == 0) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -pages\n%s", argv[0], usage);
            exit(1);
         }
         int val_len;
         if (   (sscanf(argv[idx + 1], "%3x-%3x%n", &opt_tv_start, &opt_tv_end, &val_len) >= 2)
             && (argv[idx + 1][val_len] == 0) ) {
            if ((opt_tv_start < 0x100) || (opt_tv_start > 0x899)) {
               fprintf(stderr, "-page: not a valid start page number: %03X (range 100-899)\n", opt_tv_start);
               exit(1);
            }
            if ((opt_tv_end < 0x100) || (opt_tv_end > 0x899)) {
               fprintf(stderr, "-page: not a valid end page number: %03X (range 100-899)\n", opt_tv_end);
               exit(1);
            }
            if (opt_tv_end < opt_tv_start) {
               fprintf(stderr, "-page: start page (%03X) must be <= end page (%03X)\n", opt_tv_start, opt_tv_end);
               exit(1);
            }
         }
         else {
            fprintf(stderr, "-page: invalid parameter: '%s', expecting two 3-digit ttx page numbers\n", argv[idx + 1]);
         }
         idx += 2;
      }
      // -outfile <name>: specify output file (to replace use of stdout)
      else if (   (strcmp(argv[idx], "-outfile") == 0)
               || (strcmp(argv[idx], "-o") == 0) ) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -outfile\n%s", argv[0], usage);
            exit(1);
         }
         opt_outfile = argv[idx + 1];
         struct stat st;
         if (stat(opt_outfile, &st) == 0) {
            fprintf(stderr, "Output file %s already exists\n", opt_outfile);
            exit(1);
         }
         idx += 2;
      }
      // -merge <name>: specify XMLTV input file with which to merge grabbed data
      else if (strcmp(argv[idx], "-merge") == 0) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -merge\n%s", argv[0], usage);
            exit(1);
         }
         opt_mergefile = argv[idx + 1];
         idx += 2;
      }
      // -expire <hours>: skip (or remove in merge) programmes older than X minutes
      else if (strcmp(argv[idx], "-expire") == 0) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -expire\n%s", argv[0], usage);
            exit(1);
         }
         char * pe;
         opt_expire = strtol(argv[idx + 1], &pe, 0);
         if (*pe != 0) {
            fprintf(stderr, "-expire: requires a numerical argument\n%s", usage);
            exit(1);
         }
         idx += 2;
      }
      // -debug: enable debug output
      else if (strcmp(argv[idx], "-debug") == 0) {
         opt_debug = 1;
         idx += 1;
      }
      // -dump: print all teletext pages, then exit (for debugging only)
      else if (strcmp(argv[idx], "-dump") == 0) {
         opt_dump = 1;
         idx += 1;
      }
      // -dumpraw: write teletext pages in binary, suitable for verification
      else if (strcmp(argv[idx], "-dumpraw") == 0) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -dumpraw\n%s", argv[0], usage);
            exit(1);
         }
         opt_dump = 2;

         int val_len;
         if (   (sscanf(argv[idx + 1], "%3x-%3x%n", &opt_tv_start, &opt_tv_end, &val_len) >= 2)
             && (argv[idx + 1][val_len] == 0) ) {
            if ((opt_tv_start < 0x100) || (opt_tv_start > 0x899)) {
               fprintf(stderr, "-page: not a valid start page number: %03X (range 100-899)\n", opt_tv_start);
               exit(1);
            }
            if ((opt_tv_end < 0x100) || (opt_tv_end > 0x899)) {
               fprintf(stderr, "-page: not a valid end page number: %03X (range 100-899)\n", opt_tv_end);
               exit(1);
            }
            if (opt_tv_end < opt_tv_start) {
               fprintf(stderr, "-page: start page (%03X) must be <= end page (%03X)\n", opt_tv_start, opt_tv_end);
               exit(1);
            }
         }
         else {
            fprintf(stderr, "-dumpraw: invalid parameter: '%s', expecting two 3-digit ttx page numbers\n", argv[idx + 1]);
            exit(1);
         }
         idx += 2;
      }
      // -dump: write all teletext and VPS packets to file
      else if (strcmp(argv[idx], "-dumpvbi") == 0) {
         opt_dump = 3;
         idx += 1;
      }
      // -verify: read pre-processed teletext from file (for verification only)
      else if (strcmp(argv[idx], "-verify") == 0) {
         opt_verify = true;
         idx += 1;
      }
      // -duration <seconds|"auto">: stop capturing from VBI device after the given time
      else if (strcmp(argv[idx], "-duration") == 0) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -duration\n%s", argv[0], usage);
            exit(1);
         }
         char * pe;
         opt_duration = strtol(argv[idx + 1], &pe, 0);
         if (*pe != 0) {
            fprintf(stderr, "-duration: requires a numerical argument\n%s", usage);
            exit(1);
         }
         if (opt_duration <= 0) {
            fprintf(stderr, "%s: Invalid -duration value: %d\n", argv[0], opt_duration);
            exit(1);
         }
         idx += 2;
      }
      // -dev <device>: specify VBI device
      else if (   (strcmp(argv[idx], "-device") == 0)
               || (strcmp(argv[idx], "-dev") == 0) ) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -device\n%s", argv[0], usage);
            exit(1);
         }
         opt_device = argv[idx + 1];
         struct stat st;
         if (stat(opt_device, &st) != 0) {
            fprintf(stderr, "%s: -dev %s: doesn't exist\n", argv[0], opt_device);
            exit(1);
         }
         if (!S_ISCHR(st.st_mode)) {
            fprintf(stderr, "%s: -dev %s: not a character device\n", argv[0], opt_device);
            exit(1);
         }
         idx += 2;
      }
      // -dvbpid <number>: capture from DVB device from a stream with the given PID
      else if (strcmp(argv[idx], "-dvbpid") == 0) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -dvbpid\n%s", argv[0], usage);
            exit(1);
         }
         char * pe;
         opt_dvbpid = strtol(argv[idx + 1], &pe, 0);
         if (*pe != 0) {
            fprintf(stderr, "-dvbpid: requires a numerical argument\n%s", usage);
            exit(1);
         }
         if (opt_dvbpid < 0) {
            fprintf(stderr, "%s: Invalid -dvbpid value: %d\n", argv[0], opt_dvbpid);
            exit(1);
         }
         idx += 2;
      }
      // -verbose: print page numbers during capturing, progress info during grabbing
      else if (strcmp(argv[idx], "-verbose") == 0) {
         opt_verbose = 1;
         idx += 1;
      }
      else if (strcmp(argv[idx], "-version") == 0) {
         fprintf(stderr, "%s\n%s"
                         "\nThis is free software with ABSOLUTELY NO WARRANTY.\n"
                         "Licensed under the GNU Public License version 3 or later.\n"
                         "For details see <http://www.gnu.org/licenses/\n",
                         version, copyright);
         exit(0);
      }
      // -help: print usage (i.e. command line syntax) and exit
      else if (   (strcmp(argv[idx], "-help") == 0)
               || (strcmp(argv[idx], "-h") == 0) ) {
         fprintf(stderr, "%s", usage);
         exit(0);
      }
      // sole "-": read input data from STDIN
      else if (strcmp(argv[idx], "-") == 0) {
         opt_infile = "";
         break;
      }
      else if (argv[idx][0] == '-') {
         fprintf(stderr, "%s: unknown argument \"%s\"\n%s", argv[0], argv[idx], usage);
         exit(1);
      }
      else {
         opt_infile = argv[idx];
         if (idx + 1 < argc) {
            fprintf(stderr, "%s: no more than one input file expected.\n"
                            "Error after '%s'\n%s",
                            argv[0], argv[idx], usage);
            exit(1);
         }
         break;
      }
   }
}

/** @file
tv_grab_ttx - Grab TV listings from teletext through a TV card

@par SYNOPSIS

tv_grab_ttx [options] [file]

@par DESCRIPTION

This EPG grabber allows to extract TV programme listings in XMLTV
format from teletext pages as broadcast by most European TV networks.
The grabber works by collecting teletext pages through a TV card,
locating pages holding programme schedule tables and then scraping
starting times, titles and attributes from those. Additionally,
the grabber follows references to further teletext pages in the
overviews to extract description texts. The grabber needs to be
started separately for each TV channel from which EPG data shall be
collected.

Teletext data is captured directly from I</dev/vbi0> or the device
given by the I<-dev> command line parameter, unless an input I<file>
is named on the command line. In case of the latter, the grabber expects
a stream of pre-processed VBI packets as generated when using
option I<-dumpvbi>.  Use file name C<-> to read from standard input
(i.e. from a pipe.)

The XMLTV output is written to standard out unless redirected to a
file by use of the I<-outfile> option. There are also "dump" options
which allow to write each VBI packet or assembled teletext input
pages in raw or clear text format. See L<"OPTIONS"> for details.

The EPG output generated by this grabber is compatible to all
applications which can process XML files which adhere to XMLTV
DTD version 0.5. See L<http://xmltv.org/> for a list of applications
and a copy of the DTD specification. In particular, the output can
be imported into L<nxtvepg(1)> and merged with I<Nextview EPG>.

@par OPTIONS

<dl>
<dt>B<-page> NNN-MMM
<dd>This limits the range of the teletext page numbers which are extracted
from the input stream. At the same time this defines the range of pages
which are scanned for programme schedule tables. The default page range
is 300 to 399.

<dt>B<-outfile> path
<dd>This option can be used to redirect the XMLTV output into a file.
By default the output is written to standard out.

<dt>B<-chn_name> name
<dd>This option can be used to set the value of the I<display-name> tag
in the channel table of the generated XMLTV output. By default the
name is extracted from teletext page headers.

<dt>B<-chn_id> id

<dd>This option can be used to set the value of the channel I<id> attribute
in the channel table of the generated XMLTV output.

By default the identifier is derived from the canonical network
identifier (CNI) which is broadcast as part of VPS and PDC.
The grabber uses an internal table to map these numerical values
to strings in the form suggested by RFC2838 (e.g. CNI C<1DC1>
is mapped to C<ard.de>)

<dt>B<-merge> file

<dd>This option can be used to merge the newly grabber data with previously
grabbed data, or with data grabbed from other channels.

B<Caution:> This option only allows to merge input files which have
been written by the same version of the grabber. This is because
the grabber does not include a complete XMLTV parser, so the input
is expected in the exact format in which the grabber has written it.

<dt>B<-expire> minutes

<dd>This option can be used to omit programmes which have completed
more than the given number of minutes ago. Note for programmes
for which the stop time is unknown the start time plus 120 minutes
is used. By default the expire time is 120 minutes.

This option is particularily intended for use together with
the I<-merge> option.

<dt>B<-dumpvbi>

<dd>This option can be used to omit almost all processing after
digitization ("slicing") of VBI data and write incoming teletext and
VPS packets to the output. The output can later be read by the grabber
as input file.

In the output, each data record consists of 46 bytes of which the first
two contain the magazine and page number (0x000..0x7FF), the next three
the header control bits including sub-page number, the next byte the
packet number (0 for page header, 1..29 for teletext packets, 30 for
PDC/NI packets, 32 for VPS) and finally the last 40 bytes the payload
data (only 16-bit CNI value in case of VPS/PDC/NI) Note the byte-order
of 16-bit values is platform-dependent.

<dt>B<-dump>
<dd>This option can be used to omit grabbing and instead print all
teletext pages received in the input stream as text
in Latin-1 encoding. Note control characters and characters which
cannot be represented in Latin-1 are replaced with spaces. This
option is intended for debugging purposes only.

<dt>B<-dumpraw> NNN-MMM
<dd>This option can be used to disable grabbing and instead write all
teletext pages in the given page range to the output stream in a
specific format which can later be imported again via option I<-verify>.
The output includes the raw teletext packets for each page including
all control characters. Additionally, the output contains VPS/PDC
packets and a timestamp which indicates the capture time. (The latter
is required when parsing relative time and date specifications.)

<dt>B<-verify>
<dd>This option can be used to read input data in the format as written
by use of option I<dumpraw> instead of the standard VBI packet stream.
As the name indicates, this mode is used in regression testing to
compare the grabber output for pre-defined input page sequences with
stored output files.

<dt>B<-dev> I<path>
<dd>This option can be used to specify the VBI device from which teletext
is captured. By default F</dev/vbi0> is used.

<dt>B<-dvbpid> I<PID>
<dd>This option enables data acquisition from a DVB device and specifies
the PID which is the identifier of the data stream which contains
teletext data. The PID must be determined by external means. Likewise
to analog sources, the TV channel also must be tuned with an external
application.

<dt>B<-duration> I<seconds>
<dd>This option can be used to limit capturing from the VBI device to the
given number of seconds. EPG grabbing will start afterwards.
If this option is not present, capturing stops automatically after a
sufficient number of pages have been read.  
This option has no effect when reading VBI data from a file.

<dt>B<-verbose>
<dd>This option can be used to enable output of the number of each
teletext page when capturing from a VBI device. This allows monitoring
the capture progress.

<dt>B<-debug>
<dd>This option can be used to enable output of debugging information
to standard output. You can use this to get additional diagnostic
messages in case of trouble with the capture device. (Note most of
these messages originate directly from the "ZVBI" library I<libzvbi>.)
Additionally this option enables messages during parsing teletext
pages; these are probably only helpful for developers.

<dt>B<-version>
<dd>This option prints version and license information, then exits.

<dt>B<-help>
<dd>This option prints the command line syntax, then exits.

</dl>

@par GRABBING PROCESS

This chapter describes the process in which EPG information is extracted
from teletext pages. This information is not needed to use the grabber
but may help you to understand the goals and limitations. The main
challenge of the grabber is that source data is not formatted
consistently. Formats used for tables, dates and other attributes may
vary widely between networks and may also vary over time or even
between pages in a cycle for a specific network. So far the grabber
avoids any network-specific hacks and instead attempts to address
the problem algorithmically.

Before starting the actual grabbing, all teletext packets are read
from the input file or stream into memory and associated with
teletext page numbers. In case the same packet is received more than
once, the one with the lower parity error count is selected.

The first step of grabbing is identification of the overview pages
which contain tables with start times and programme titles. This is
done by searching all pages for lines starting with a time value,
but allowing for exceptions such as markers or hidden VPS labels:
@code
   20.00  Tagesschau UT ............ 310
 ! 20.15  Die Stein (10/13) UT ..... 324
          Zwischen Baum und Borke
          Fernsehserie, D 2008
 ! 21.05  In aller Freundschaft UT . 326
          Grossmuetiges Herz (D, 2008)
 ! 21.50  Plusminus UT ............. 328
@endcode

Of course there may be unrelated rows which also start with a
time value. Hence the grabber builds statistics about the number
of occurences of lines where the time and title start in the same
column. In the end, the format found most often is selected as the
one used for detecting time tables.

In the second step, the grabber revisits all pages and extracts
start times and titles from all lines which are formatted in the
way selected in step 1 above. Additionally, the title lines are 
broken apart into time, title, episode title, feature attributes
(stereo, sub-titles etc.), description page references. Afterwards
stop times are calculated by asserting the start of subsequent
programmes as the stop time of the previous one.

Finally the date is derived. In some cases the data is printed in
full on top of the page, but often the data is just given as "Today",
"Tomorrow" etc. or a weekday name. Even more difficult, a day on
these pages sometimes is considered to span until apx. 6:00 on
the next day, so finding "today" after midnight may actually mean
"yesterday".  The latter is resolved by comparing dates between
adjacent pages.

Format of dates is understood in many different formats. Examples:
@code
  Heute
  29.04.06
  Sa 29.04.06
  Sa,29.04.
  Samstag, 29.April
  Sonnabend, 29.04.06
  29.04.2006
@endcode

In the third step, descriptions are grabbed from all pages which
were referenced by the overview tables. Often, different sub-pages
of the same page are used for descriptions belonging to different
programmes. Hence the grabber must make a match between the
referencing programmes start time and title and the content on
the description page to identify the correct sub-page. Often this
gets difficult when repetitions of an episode refer to the same
description page (most often without listing only the original
air date). Example:
@code
  Relic Hunter - Die Schatzj{gerin
  Der magische Handschuh
  
  Sa 06.05 18:01-18:59           (vorbei)
  So 07.05 05:10-05:57           (vorbei)
@endcode

In the final step, the channel name and identifier are derived from
teletext page headers and channel identification codes. Once more
statistics are used to filter out transmission errors. Since the
teletext page header usually contains additional text after the
actual network name (e.g. "ARDtext Sa 10.06.06" instead of "ARD")
the name is derived by stripping all appendices which match known
time/date formats or other common postfixes.

The data collected in all the above steps is then formatted in XMLTV,
optionally merged with an input XMLTV file, and printed to the output
file.

@par FILES

<dl>
<dt>@c /dev/vbi0, @c /dev/v4l/vbi0
<dd>Device files from which teletext data is being read during acquisition
when using an analog TV card on Linux.  Different paths can be selected
with the B<-dev> command line option.  Depending on your Linux version,
the device files may be located directly beneath C</dev> or inside
C</dev/v4l>. Other operating systems may use different names.

<dt>@c /dev/dvb/adapter0/demux0

Device files from which teletext data is being read during acquisition
when using a DVB device, i.e. when the B<-dvbpid> option is present.
Different paths can be selected with the B<-dev> command line option.
(If you have multiple DVB cards, increment the device index after
I<adapter> to get the second card etc.)

</dl>

@par SEE ALSO

@c xmltv(5),
@c tv_cat(1),
@c nxtvepg(1),
@c v4lctl(1),
@c zvbid(1),
@c alevt(1),
@c Video::ZVBI(3pm),
@c perl(1).

@par AUTHOR

Written by Tom Zoerner (tomzo at users.sourceforge.net) since 2006
as part of the nxtvepg project.

The official homepage is http://nxtvepg.sourceforge.net/tv_grab_ttx

@par COPYRIGHT

Copyright 2006-2010 Tom Zoerner.

This is free software. You may redistribute copies of it under the terms
of the GNU General Public License version 3 or later
(see http://www.gnu.org/licenses/gpl.html)
There is @b no @b warranty, to the extent permitted by law.

*/

bool pkg_defined(int handle)
{
   return (Pkg.find(handle) != Pkg.end());
}

bool pkg_defined(int handle, int pkg_idx)
{
   map<int,vt_page>::iterator p = Pkg.find(handle);
   return (p != Pkg.end()) && (p->second.m_defined & (1<<pkg_idx));
}

/* Check if the given sub-page of the given page exists
 */
bool TtxSubPageDefined(int page, int sub)
{
   return pkg_defined(page | (sub << 12), 0);
}


bool map_defined(map<int,int>& m, int page)
{
   return (m.find(page) != m.end());
}

template<class IT>
int atoi_substr(const IT& first, const IT& second)
{
   IT p = first;
   int val = 0;
   while (p != second) {
      val = (val * 10) + (*(p++) - '0');
   }
   return val;
}

template<class MATCH>
int atoi_substr(const MATCH& match)
{
   return match.matched ? atoi_substr(match.first, match.second) : -1;
}

/* ------------------------------------------------------------------------------
 * Write a single TTX packet or CNI to STDOUT in a packed binary format
 */
bool dump_raw_pkg(const vt_pkg_dec * p_dec)
{
   const uint8_t * p_data = reinterpret_cast<const uint8_t*>(p_dec);
   int len = sizeof(vt_pkg_dec);
   ssize_t wstat;

   do {
      wstat = write(1, p_data, len);
      if (wstat > 0) {
        p_data += wstat;
        len -= wstat;
      }
   } while ( ((wstat == -1) && (errno == EINTR)) || (len > 0) );

   if (wstat == -1) {
      fprintf(stderr, "dump write error: %s\n", strerror(errno));
   }
 
   return (wstat >= 0);
}

/* ------------------------------------------------------------------------------
 * Decoding of teletext packets (esp. hamming decoding)
 * - for page header (packet 0) the page number is derived
 * - for teletext packets (packet 1..25) the payload data is just copied
 * - for PDC and NI (packet 30) the CNI is derived
 * - returns a list with 5 elements: page/16, ctrl/16, ctrl/8, pkg/8, data/40*8
 */
vt_pkg_dec FeedTtxPkg(int *p_last_pg, uint8_t * data)
{
   vt_pkg_dec dec;

   int mpag = vbi_unham16p(data);
   int mag = mpag & 7;
   int y = (mpag & 0xf8) >> 3;

   if (y == 0) {
      // teletext page header (packet #0)
      int page = (mag << 8) | vbi_unham16p(data + 2);
      int ctrl = (vbi_unham16p(data + 4)) |
                 (vbi_unham16p(data + 6) << 8) |
                 (vbi_unham16p(data + 8) << 16);
      // drop filler packets
      if ((page & 0xFF) != 0xFF) {
         vbi_unpar(data + 2, 40);
         dec.set_pg_header(page, ctrl, data + 2);
         if (opt_dump == 3) {
            dump_raw_pkg(&dec);
         }
         int spg = (page << 16) | (ctrl & 0x3f7f);
         if (spg != *p_last_pg) {
            if (opt_verbose) {
               fprintf(stderr, "%03X.%04X\n", page, ctrl & 0x3f7f);
            }
            *p_last_pg = spg;
         }
      }
   }
   else if (y<=25) {
      // regular teletext packet (lines 1-25)
      vbi_unpar(data + 2, 40);
      dec.set_pg_row(mag, y, data + 2);
      if (opt_dump == 3) {
         dump_raw_pkg(&dec);
      }
   }
   else if (y == 30) {
      int dc = (vbi_unham16p(data + 2) & 0x0F) >> 1;
      if (dc == 0) {
         // packet 8/30/1
         int cni = vbi_rev16p(data + 2+7);
         if ((cni != 0) && (cni != 0xffff)) {
            dec.set_cni(mag, 30, cni);
            if (opt_dump == 3) {
               dump_raw_pkg(&dec);
            }
         }
      }
      else if (dc == 1) {
         // packet 8/30/2
         int c0 = vbi_rev8(vbi_unham16p(data + 2+9+0));
         int c6 = vbi_rev8(vbi_unham16p(data + 2+9+6));
         int c8 = vbi_rev8(vbi_unham16p(data + 2+9+8));

         int cni = ((c0 & 0xf0) << 8) |
                   ((c0 & 0x0c) << 4) |
                   ((c6 & 0x30) << 6) |
                   ((c6 & 0x0c) << 6) |
                   ((c6 & 0x03) << 4) |
                   ((c8 & 0xf0) >> 4);
         if ((cni & 0xF000) == 0xF000)
            cni &= 0x0FFF;
         if ((cni != 0) && (cni != 0xffff)) {
            dec.set_cni(mag, 30, cni);
            if (opt_dump == 3) {
               dump_raw_pkg(&dec);
            }
         }
      }
   }
   return dec;
}

/* ------------------------------------------------------------------------------
 * Decoding of VPS packets
 * - returns a list of 5 elements (see TTX decoder), but data contains only 16-bit CNI
 */
vt_pkg_dec FeedVps(const uint8_t * data)
{
   vt_pkg_dec dec;
   //int cni = Video::ZVBI::decode_vps_cni(_[0]); # only since libzvbi 0.2.22

   uint8_t cd_02 = data[ 2];
   uint8_t cd_08 = data[ 8];
   uint8_t cd_10 = data[10];
   uint8_t cd_11 = data[11];

   int cni = ((cd_10 & 0x3) << 10) | ((cd_11 & 0xc0) << 2) |
             (cd_08 & 0xc0) | (cd_11 & 0x3f);

   if (cni == 0xDC3) {
      // special case: "ARD/ZDF Gemeinsames Vormittagsprogramm"
      cni = (cd_02 & 0x20) ? 0xDC1 : 0xDC2;
   }
   if ((cni != 0) && (cni != 0xfff)) {
      dec.set_cni(0, 32, cni);
      if (opt_dump == 3) {
         dump_raw_pkg(&dec);
      }
   }
   return dec;
}

/* ------------------------------------------------------------------------------
 * Open the VBI device for capturing, using the ZVBI library
 * - require is done here (and not via "use") to avoid dependency
 */
vbi_capture * DeviceOpen()
{
   unsigned srv = VBI_SLICED_VPS |
                  VBI_SLICED_TELETEXT_B |
                  VBI_SLICED_TELETEXT_B_525;
   char * err = 0;
   vbi_capture * cap;

   if (opt_dvbpid >= 0) {
      cap = vbi_capture_dvb_new2(opt_device, opt_dvbpid, &err, opt_debug);
   }
   else {
      cap = vbi_capture_v4l2_new(const_cast<char*>(opt_device), 6, &srv, 0, &err, opt_debug);
   }

   if (cap == 0) {
      fprintf(stderr, "Failed to open capture device %s: %s\n", opt_device, err);
      exit(1);
   }
   return cap;
}

/* ------------------------------------------------------------------------------
 * Read one frame's worth of teletext and VPS packets
 * - blocks until the device delivers the next packet
 * - returns a flat list; 5 elements in list for each teletext packet
 */
void DeviceRead(vbi_capture * cap, int *p_last_pg, deque<vt_pkg_dec>& ret_buf)
{
   int fd_ttx = vbi_capture_fd(cap);
   fd_set rd_fd;
   FD_ZERO(&rd_fd);
   FD_SET(fd_ttx, &rd_fd);

   select(fd_ttx + 1, &rd_fd, NULL, NULL, NULL);

   //int buf;
   //int lcount;
   //int ts;
   //int ret = cap->pull_sliced(buf, lcount, ts, 0);
   vbi_capture_buffer * buf = NULL;
   struct timeval to;
   to.tv_sec = 0;
   to.tv_usec = 0;
   int ret = vbi_capture_pull_sliced(cap, &buf, &to);
   if (ret > 0) {
      int lcount = buf->size / sizeof(vbi_sliced);
      for (int idx = 0; idx < lcount; idx++) {
         vbi_sliced * p_line = reinterpret_cast<vbi_sliced*>(buf->data) + idx;
         if (p_line->id & (VBI_SLICED_TELETEXT_B |
                           VBI_SLICED_TELETEXT_B_525)) {
            ret_buf.push_back( FeedTtxPkg(p_last_pg, p_line->data) );
         }
         else if (p_line->id & VBI_SLICED_VPS) {
            ret_buf.push_back( FeedVps(p_line->data) );
         }
      }
   }
   else if (ret < 0) {
      fprintf(stderr, "capture device error: %s: %s\n", strerror(errno), opt_device);
      exit(1);
   }
}

/* ------------------------------------------------------------------------------
 * Read VBI data from a device or file
 * - when reading from a file, each record has a small header plus 40 bytes payload
 * - TODO: use MIP to find TV overview pages
 */
void ReadVbi()
{
   int CurPage[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
   int CurSub[8+1] = {0,0,0,0,0,0,0,0,0};
   int CurPkg[8+1] = {0,0,0,0,0,0,0,0,0};
   int cur_mag = 0;
   int intv;
   int last_pg = -1;
   vbi_capture * cap = 0;
   int mag_serial = 0;
   int fd_ttx = -1;

   if (opt_infile != 0) {
      // read VBI input data from a stream or file
      if (*opt_infile == 0) {
         fd_ttx = dup(0);
         VbiCaptureTime = time(NULL);
      }
      else {
         fd_ttx = open(opt_infile, O_RDONLY);
         if (fd_ttx < 0) {
            fprintf(stderr, "Failed to open %s: %s\n", opt_infile, strerror(errno));
            exit(1);
         }
         struct stat st;
         if (fstat(fd_ttx, &st) != 0) {
            fprintf(stderr, "Failed to fstat opened %s: %s\n", opt_infile, strerror(errno));
            exit(1);
         }
         VbiCaptureTime = st.st_mtime;
      }
   }
   else {
      // capture input data from a VBI device
      VbiCaptureTime = time(NULL);
      cap = DeviceOpen();
      if (opt_outfile != 0) {
         close(1);
         if (open(opt_outfile, O_WRONLY|O_CREAT|O_EXCL, 0666) < 0) {
            fprintf(stderr, "Failed to create %s: %s\n", opt_outfile, strerror(errno));
            exit(1);
         }
      }
   }

   deque<vt_pkg_dec> CapPkgs;
   intv = 0;
   while (1) {
      if (fd_ttx >= 0) {
         size_t line_off = 0;
         ssize_t rstat;
         vt_pkg_dec buf;
         while ((rstat = read(fd_ttx, reinterpret_cast<uint8_t*>(&buf) + line_off,
                                      sizeof(vt_pkg_dec) - line_off)) > 0) {
            line_off += rstat;
            if (line_off >= sizeof(vt_pkg_dec))
               break;
         }
         if (rstat <= 0)
            goto END_FRAMELOOP;
         CapPkgs.push_back(buf);
      }
      else {
         while (CapPkgs.size() == 0) {
            if ((opt_duration > 0) && ((time(NULL) - VbiCaptureTime) > opt_duration)) {
               // capture duration limit reached -> terminate loop
               goto END_FRAMELOOP;
            }
            DeviceRead(cap, &last_pg, CapPkgs);
         }
      }

      int page = CapPkgs[0].m_page_no;
      if (page < 0x100)
         page += 0x800;
      int mag = (page >> 8) & 7;
      int sub = CapPkgs[0].m_ctrl_lo & 0x3f7f;
      int ctl2 = CapPkgs[0].m_ctrl_hi;
      int pkg = CapPkgs[0].m_pkg;
      const uint8_t * p_data = CapPkgs[0].m_data;
      //printf("%03X.%04X, %2d %.40s\n", page, sub, pkg, p_data);

      if (mag_serial && (mag != cur_mag) && (pkg <= 29)) {
         CurPage[cur_mag] = -1;
      }

      if (pkg == 0) {
         // erase page control bit (C4) - ignored, some channels abuse this
         //if (ctl1 & 0x80) {
         //   VbiPageErase(page);
         //}
         // inhibit display control bit (C10)
         //if (ctl2 & 0x08) {
         //   CurPage[mag] = -1;
         //   next;
         //}
         if (ctl2 & 0x01) {
            p_data = reinterpret_cast<const uint8_t*>("                                        "); // " " x 40
         }
         if ( ((page & 0x0F) > 9) || (((page >> 4) & 0x0f) > 9) ) {
            page = CurPage[mag] = -1;
            CapPkgs.pop_front();
            continue;
         }
         // magazine serial mode (C11: 1=serial 0=parallel)
         mag_serial = ((ctl2 & 0x10) != 0);
         if (!map_defined(PgCnt, page)) {
            // new page
            Pkg[page|(sub<<12)] = vt_page();
            PgCnt[page] = 1;
            PgSub[page] = sub;
            // store G0 char set (bits must be reversed: C12,C13,C14)
            PgLang[page] = ((ctl2 >> 7) & 1) | ((ctl2 >> 5) & 2) | ((ctl2 >> 3) & 4);
         }
         else if ((page != CurPage[mag]) || (sub != CurSub[mag])) {
            // repeat page (and not just repeated page header, e.g. as filler packet)
            // TODO count sub-pages separately
            PgCnt[page] += 1;
            if (sub >= PgSub[page])
               PgSub[page] = sub;
         }

         // check if every page (NOT sub-page) was received N times in average
         // (only do the check every 50th page to save CPU)
         if (opt_duration == 0) {
            intv += 1;
            if (intv >= 50) {
               int page_cnt = 0;
               int page_rep = 0;
               for (map<int,int>::iterator p = PgCnt.begin(); p != PgCnt.end(); p++) {
                  page_cnt += 1;
                  page_rep += p->second;
               }
               if ((page_cnt > 0) && ((page_rep / page_cnt) >= 10))
                  goto END_FRAMELOOP;
               intv = 0;
            }
         }

         CurPage[mag] = page;
         CurSub[mag] = sub;
         CurPkg[mag] = 0;
         cur_mag = mag;
         Pkg[page|(sub<<12)].add_line(0, p_data);
      }
      else if (pkg <= 27) {
         page = CurPage[mag];
         sub = CurSub[mag];
         if (page != -1) {
            // page body packets are supposed to be in order
            if (pkg < CurPkg[mag]) {
               page = CurPage[mag] = -1;
            }
            else {
               if (pkg < 26)
                  CurPkg[mag] = pkg;
               Pkg[page|(sub<<12)].add_line(pkg, p_data);
            }
         }
      }
      else if ((pkg == 30) || (pkg == 32)) {
         uint16_t cni;
         memcpy(&cni, p_data, sizeof(cni));
         PkgCni[cni]++;
      }
      CapPkgs.pop_front();
   }
END_FRAMELOOP:
   if (fd_ttx >= 0) {
      close(fd_ttx);
   }
}

/* Erase the page with the given number from memory
 * used to handle the "erase" control bit in the TTX header
 */
void VbiPageErase(int page)
{
   map<int,int>::iterator p3 = PgSub.find(page);
   if (p3 != PgCnt.end()) {
      for (int sub = 0; sub <= p3->second; sub++) {
         map<int,vt_page>::iterator p = Pkg.find(page|(sub<<12));
         if (p != Pkg.end()) {
            Pkg.erase(p);
         }
      }
      PgCnt.erase(p3);
   }
   map<int,int>::iterator p2 = PgCnt.find(page);
   if (p2 != PgCnt.end()) {
      PgCnt.erase(p2);
   }
}

/* ------------------------------------------------------------------------------
 * Dump all loaded teletext pages as plain text
 * - teletext control characters and mosaic is replaced by space
 * - used for -dump option, intended for debugging only
 */
void DumpTextPages()
{
   if (opt_outfile != 0) {
      close(1);
      if (open(opt_outfile, O_WRONLY|O_CREAT|O_EXCL, 0666) < 0) {
         fprintf(stderr, "Failed to create %s: %s\n", opt_outfile, strerror(errno));
         exit(1);
      }
   }

   //foreach page (sort {a<=>b} keys(%PgCnt)) {
   for (map<int,int>::iterator p = PgCnt.begin(); p != PgCnt.end(); p++) {
      int sub1;
      int sub2;
      int handle;
      int page = p->first;

      if (PgSub[page] == 0) {
         sub1 = sub2 = 0;
      }
      else {
         sub1 = 1;
         sub2 = PgSub[page];
      }
      for (int sub = sub1; sub <= sub2; sub++) {
         handle = page | (sub << 12);
         if (pkg_defined(handle)) {
            printf("PAGE %03X.%04X\n", page, sub);

            PageToLatin(page, sub);
            vt_page& pgtext = PageText[handle];

            for (int idx = 1; idx <= 23; idx++) {
               printf("%.40s\n", pgtext.m_line[idx]);
            }
            printf("\n");
         }
      }
   }
}

/* ------------------------------------------------------------------------------
 * Dump all loaded teletext data as Perl script
 * - the generated script can be loaded with the -verify option
 */
void DumpRawTeletext()
{
   FILE * fp;

   if (opt_outfile != 0) {
      fp = fopen(opt_outfile, "w");
      if (fp == NULL) {
         fprintf(stderr, "Failed to create %s: %s\n", opt_outfile, strerror(errno));
         exit(1);
      }
   } else {
      fp = stdout;
   }

   fprintf(fp, "#!/usr/bin/perl -w\n"
               "$VbiCaptureTime = %ld;\n", (long)VbiCaptureTime);

   for (map<int,int>::iterator p = PkgCni.begin(); p != PkgCni.end(); p++) {
      fprintf(fp, "$PkgCni{0x%X} = %d;\n", p->first, p->second);
   }

   for (map<int,int>::iterator p = PgCnt.begin(); p != PgCnt.end(); p++) {
      int page = p->first;
      if ((page >= opt_tv_start) && (page <= opt_tv_end)) {
         int sub1, sub2;
         if (PgSub[page] == 0) {
            sub1 = sub2 = 0;
         } else {
            sub1 = 1;
            sub2 = PgSub[page];
         }
         for (int sub = sub1; sub <= sub2; sub++) {
            int handle = page | (sub << 12);

            // skip missing sub-pages
            map<int,vt_page>::iterator it_pkg = Pkg.find(handle);
            if (it_pkg != Pkg.end()) {
               printf("$PgCnt{0x%03X} = %d;\n", page, PgCnt[page]);
               printf("$PgSub{0x%03X} = %d;\n", page, PgSub[page]);
               printf("$PgLang{0x%03X} = %d;\n", page, PgLang[page]);

               printf("$Pkg{0x%03X|(0x%04X<<12)} =\n[\n", page, sub);

               for (int pkg = 0; pkg <= 23; pkg++) {
                  const vt_pkg_txt& line = it_pkg->second.m_line[pkg];
                  if (it_pkg->second.m_defined & (1 << pkg)) {
                     fprintf(fp, "  \"");
                     for (uint cidx = 0; cidx < VT_PKG_RAW_LEN; cidx++) {
                        // quote binary characters
                        unsigned char c = line[cidx];
                        if ((c < 0x20) || (c == 0x7F)) {
                           fprintf(fp, "\\x%02X", c);
                        }
                        // backwards compatibility: quote C and Perl special characters
                        else if (   (line[cidx] == '@')
                                 || (line[cidx] == '$')
                                 || (line[cidx] == '%')
                                 || (line[cidx] == '"')
                                 || (line[cidx] == '\\') ) {
                           fprintf(fp, "\\%c", line[cidx]);
                        }
                        else {
                           fprintf(fp, "%c", line[cidx]);
                        }
                     }
                     fprintf(fp, "\",\n");
                  } else {
                     fprintf(fp, "  undef,\n");
                  }
               }
               fprintf(fp, "];\n");
            }
         }
      }
   }
   // return TRUE to allow to "require" the file
   fprintf(fp, "1;\n");
   fclose(fp);
}

/* ------------------------------------------------------------------------------
 * Import a data file generated by DumpRawTeletext
 */
void ImportRawDump()
{
   FILE * fp;

   if ((opt_infile == 0) || (*opt_infile == 0)) {
      fp = stdin;
   } else {
      fp = fopen(opt_infile, "r");
      if (fp == NULL) {
         fprintf(stderr, "Failed to open %s: %s\n", opt_infile, strerror(errno));
         exit(1);
      }
   }

   cmatch what;
   static const regex expr1("#!/usr/bin/perl -w\\s*");
   static const regex expr2("\\$VbiCaptureTime\\s*=\\s*(\\d+);\\s*");
   static const regex expr3("\\$PkgCni\\{0x([0-9A-Za-z]+)\\}\\s*=\\s*(\\d+);\\s*");
   static const regex expr4("\\$PgCnt\\{0x([0-9A-Za-z]+)\\}\\s*=\\s*(\\d+);\\s*");
   static const regex expr5("\\$PgSub\\{0x([0-9A-Za-z]+)\\}\\s*=\\s*(\\d+);\\s*");
   static const regex expr6("\\$PgLang\\{0x([0-9A-Za-z]+)\\}\\s*=\\s*(\\d+);\\s*");
   static const regex expr7("\\$Pkg\\{0x([0-9A-Za-z]+)\\|\\(0x([0-9A-Za-z]+)<<12\\)\\}\\s*=\\s*");
   static const regex expr8("\\[\\s*");
   static const regex expr9("\\s*undef,\\s*");
   static const regex expr10("\\s*\"(.*)\",\\s*");
   static const regex expr11("\\];\\s*");
   static const regex expr12("1;\\s*");
   static const regex expr13("(#.*\\s*|\\s*)");

   int file_line_no = 0;
   int handle = -1;
   int pkg_idx = 0;
   vt_page pg_data;

   char buf[256];
   while (fgets(buf, sizeof(buf), fp) != 0) {
      file_line_no ++;
      if (regex_match(buf, what, expr1)) {
      }
      else if (regex_match(buf, what, expr2)) {
         VbiCaptureTime = atol(string(what[1]).c_str());
      }
      else if (regex_match(buf, what, expr3)) {
         long page = strtol(string(what[1]).c_str(), NULL, 16);
         PkgCni[page] = atoi_substr(what[2]);
      }
      else if (regex_match(buf, what, expr4)) {
         long page = strtol(string(what[1]).c_str(), NULL, 16);
         PgCnt[page] = atoi_substr(what[2]);
      }
      else if (regex_match(buf, what, expr5)) {
         long page = strtol(string(what[1]).c_str(), NULL, 16);
         PgSub[page] = atoi_substr(what[2]);
      }
      else if (regex_match(buf, what, expr6)) {
         long page = strtol(string(what[1]).c_str(), NULL, 16);
         PgLang[page] = atoi_substr(what[2]);
      }
      else if (regex_match(buf, what, expr7)) {
         long page = strtol(string(what[1]).c_str(), NULL, 16);
         long sub = strtol(string(what[2]).c_str(), NULL, 16);
         handle = page | (sub << 12);
         pg_data = vt_page();
         pkg_idx = 0;
      }
      else if (regex_match(buf, what, expr8)) {
      }
      else if (regex_match(buf, what, expr9)) {
         // undef
         pkg_idx += 1;
      }
      else if (regex_match(buf, what, expr10)) {
         // line
         assert((handle != -1) && (pkg_idx < VT_PKG_PG_CNT));
         pg_data.m_defined |= 1 << pkg_idx;
         const char * p = &*what[1].first;
         int idx = 0;
         while ((*p != 0) && (idx < VT_PKG_RAW_LEN)) {
            int val, val_len;
            if ((*p == '\\') && (p[1] == 'x') &&
                (sscanf(p + 2, "%2x%n", &val, &val_len) >= 1) && (val_len == 2)) {
               pg_data.m_line[pkg_idx][idx++] = val;
               p += 4;
            } else if (*p == '\\') {
               pg_data.m_line[pkg_idx][idx++] = p[1];
               p += 2;
            } else {
               pg_data.m_line[pkg_idx][idx++] = *p;
               p += 1;
            }
         }
         pg_data.m_line[pkg_idx][VT_PKG_RAW_LEN] = 0;
         pkg_idx += 1;
      }
      else if (regex_match(buf, what, expr11)) {
         assert(handle != -1);
         Pkg[handle] = pg_data;
         handle = -1;
      }
      else if (regex_match(buf, what, expr12)) {
      }
      else if (regex_match(buf, what, expr13)) {
         // comment or empty line - ignored
      }
      else {
         fprintf(stderr, "Import parse error in line %d '%s'\n", file_line_no, buf);
         exit(1);
      }
   }
   fclose(fp);
}

/* ------------------------------------------------------------------------------
 * Conversion of teletext G0 charset into ISO-8859-1
 */
const signed char NationalOptionsMatrix[] =
{
//  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x00
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x10
   -1, -1, -1,  0,  1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x20
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x30
    2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x40
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  3,  4,  5,  6,  7,  // 0x50
    8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 0x60
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  9, 10, 11, 12, -1   // 0x70
};

const char * const NatOptMaps[] =
{
   // for latin-1 font
   // English (100%)
   "£$@«½»¬#­¼¦¾÷",
   // German (100%)
   "#$§ÄÖÜ^_°äöüß",
   // Swedish/Finnish/Hungarian (100%)
   "#¤ÉÄÖÅÜ_éäöåü",
   // Italian (100%)
   "£$é°ç»¬#ùàòèì",
   // French (100%)
   "éïàëêùî#èâôûç",
   // Portuguese/Spanish (100%)
   "ç$¡áéíóú¿üñèà",
   // Czech/Slovak (60%)
   "#uctzýíréáeús",
   // reserved (English mapping)
   "£$@«½»¬#­¼¦¾÷",
   // Polish (6%: all but '#' missing)
   "#naZSLcoezslz",
   // German (100%)
   "#$§ÄÖÜ^_°äöüß",
   // Swedish/Finnish/Hungarian (100%)
   "#¤ÉÄÖÅÜ_éäöåü",
   // Italian (100%)
   "$é°ç»¬#ùàòèì",
   // French (100%)
   "ïàëêùî#èâôûç",
   // reserved (English mapping)
   "$@«½»¬#­¼¦¾÷",
   // Czech/Slovak 
   "uctzýíréáeús",
   // reserved (English mapping)
   "$@«½»¬#­¼¦¾÷",
   // English 
   "$@«½»¬#­¼¦¾÷",
   // German
   "$§ÄÖÜ^_°äöüß",
   // Swedish/Finnish/Hungarian (100%)
   "¤ÉÄÖÅÜ_éäöåü",
   // Italian (100%'
   "$é°ç»¬#ùàòèì",
   // French (100%)
   "ïàëêùî#èâôûç",
   // Portuguese/Spanish (100%)
   "$¡áéíóú¿üñèà",
   // Turkish (50%: 7 missing)
   "gISÖÇÜGisöçü",
   // reserved (English mapping)
   "$@«½»¬#­¼¦¾÷"
};

const bool DelSpcAttr[] =
{
   0,0,0,0,0,0,0,0,    // 0-7: alpha color
   1,1,1,1,            // 8-B: flash, box
   0,0,0,0,            // C-F: size
   1,1,1,1,1,1,1,1,    // 10-17: mosaic color
   0,                  // 18:  conceal
   1,1,                // 19-1A: continguous mosaic
   0,0,0,              // 1B-1D: charset, bg color
   1,1                 // 1E-1F: hold mosaic
};

// TODO: evaluate packet 26 and 28
void TtxToLatin1(const vt_pkg_txt& line, vt_pkg_txt& out, unsigned g0_set)
{
   bool is_g1 = false;
   for (int idx = 0; idx < VT_PKG_RAW_LEN; idx++) {
      unsigned char val = line[idx];

      // alpha color code
      if (val <= 0x07) {
         is_g1 = false;
         //val = ' ';
      }
      // mosaic color code
      else if ((val >= 0x10) && (val <= 0x17)) {
         is_g1 = true;
         val = ' ';
      }
      // other control character or mosaic character
      else if (val < 0x20) {
         if (DelSpcAttr[val]) {
            val = ' ';
         }
      }
      // mosaic charset
      else if (is_g1) {
         val = ' ';
      }
      else if ( (val < 0x80) && (NationalOptionsMatrix[val] >= 0) ) {
         if (g0_set < sizeof(NatOptMaps)/sizeof(NatOptMaps[0])) {
            val = NatOptMaps[g0_set][NationalOptionsMatrix[val]];
         }
         else {
            val = ' ';
         }
      }
      out[idx] = val;
   }
   out[VT_PKG_RAW_LEN] = 0;
}

void PageToLatin(int page, int sub)
{
   int handle = page | (sub << 12);

   map<int,vt_page>::iterator p = Pkg.find(handle);
   if (p != Pkg.end()) {
      int lang = PgLang[page];
      vt_page * pkgs = &p->second;

      if (PageCtrl.find(handle) == PageCtrl.end()) {
         vt_page& PgCtrl = PageCtrl[handle];
         vt_page& PgText = PageText[handle];

         for (int idx = 0; idx <= 23; idx++) {
            if (pkgs->m_defined & (1 << idx)) {
               TtxToLatin1(pkgs->m_line[idx], PgCtrl.m_line[idx], lang);

               memcpy(PgText.m_line[idx], PgCtrl.m_line[idx], VT_PKG_RAW_LEN);
               for (int col = 0; col < VT_PKG_RAW_LEN; col++) {
                  unsigned char c = PgText.m_line[idx][col];
                  if ((c < 0x1F) || (c == 0x7F))
                     PgText.m_line[idx][col] = ' ';
               }
            }
            else {
               memset(PgCtrl.m_line[idx], ' ', VT_PKG_RAW_LEN);
               memset(PgText.m_line[idx], ' ', VT_PKG_RAW_LEN);
            }
            PgCtrl.m_line[idx][VT_PKG_RAW_LEN] = 0;
            PgText.m_line[idx][VT_PKG_RAW_LEN] = 0;
         }
         PgText.m_defined = 0xFFFFFF;  // lines 0..23
         PgText.m_defined = 0xFFFFFF;
      }
   } else {
      assert(false);  // caller should check if sub-page exists
   }
}

/* ------------------------------------------------------------------------------
 * Tables to support parsing of dates in various languages
 * - the hash tables map each word to an array with 3 elements:
 *   0: bitfield of language (language indices as in teletext header)
 *      use a bitfield because some words are identical in different languages
 *   1: value (i.e. month or weekday index)
 *   2: bitfield abbreviation: 1:non-abbr., 2:abbr., 3:no change by abbr.
 *      use a bitfield because some short words are not abbreviated (e.g. "may")
 */

enum DATE_NAME_TYPE
{
  DATE_NAME_NONE = 0,
  DATE_NAME_FULL = 1,
  DATE_NAME_ABBRV = 2,
  DATE_NAME_ANY = 3
};
struct T_DATE_NAME
{
  const char  * p_name;
  int           lang_bits;
  int           idx;
  DATE_NAME_TYPE abbrv;
};

// map month names to month indices 1..12
const T_DATE_NAME MonthNames[] =
{
   // English (minus april, august, september)
   {"january", (1<<0),1, DATE_NAME_FULL},
   {"february", (1<<0),2, DATE_NAME_FULL},
   {"march", (1<<0),3, DATE_NAME_FULL},
   {"may", (1<<0),5, DATE_NAME_ANY},  // abbreviation == full name
   {"june", (1<<0),6, DATE_NAME_FULL},
   {"july", (1<<0),7, DATE_NAME_FULL},
   {"october", (1<<0),10, DATE_NAME_FULL},
   {"december", (1<<0),12, DATE_NAME_FULL},
   {"mar", (1<<0),3, DATE_NAME_ABBRV},
   {"oct", (1<<0),10, DATE_NAME_ABBRV},
   {"dec", (1<<0),12, DATE_NAME_FULL},
   // German
   {"januar", (1<<1),1, DATE_NAME_FULL},
   {"februar", (1<<1),2, DATE_NAME_FULL},
   {"märz", (1<<1),3, DATE_NAME_FULL},
   {"april", (1<<1)|(1<<0),4, DATE_NAME_FULL},
   {"mai", (1<<4)|(1<<1),5, DATE_NAME_ANY},  // abbreviation == full name
   {"juni", (1<<1),6, DATE_NAME_FULL},
   {"juli", (1<<1),7, DATE_NAME_FULL},
   {"august", (1<<1)|(1<<0),8, DATE_NAME_FULL},
   {"september", (1<<1)|(1<<0),9, DATE_NAME_FULL},
   {"oktober", (1<<1),10, DATE_NAME_FULL},
   {"november", (1<<1)|(1<<0),11, DATE_NAME_FULL},
   {"dezember", (1<<1),12, DATE_NAME_FULL},
   {"jan", (1<<1)|(1<<0),1, DATE_NAME_ABBRV},
   {"feb", (1<<1)|(1<<0),2, DATE_NAME_ABBRV},
   {"mär", (1<<1),3, DATE_NAME_ABBRV},
   {"mar", (1<<1),3, DATE_NAME_ABBRV},
   {"apr", (1<<1)|(1<<0),4, DATE_NAME_ABBRV},
   {"jun", (1<<1)|(1<<0),6, DATE_NAME_ABBRV},
   {"jul", (1<<1)|(1<<0),7, DATE_NAME_ABBRV},
   {"aug", (1<<1)|(1<<0),8, DATE_NAME_ABBRV},
   {"sep", (1<<1)|(1<<0),9, DATE_NAME_ABBRV},
   {"okt", (1<<1),10, DATE_NAME_ABBRV},
   {"nov", (1<<1)|(1<<0),11, DATE_NAME_ABBRV},
   {"dez", (1<<1),12, DATE_NAME_ABBRV},
   // French
   {"janvier", (1<<4),1, DATE_NAME_FULL},
   {"février", (1<<4),2, DATE_NAME_FULL},
   {"mars", (1<<4),3, DATE_NAME_FULL},
   {"avril", (1<<4),4, DATE_NAME_FULL},
   {"juin", (1<<4),6, DATE_NAME_FULL},
   {"juillet", (1<<4),7, DATE_NAME_FULL},
   {"août", (1<<4),8, DATE_NAME_FULL},
   {"septembre", (1<<4),9, DATE_NAME_FULL},
   {"octobre", (1<<4),10, DATE_NAME_FULL},
   {"novembre", (1<<4),11, DATE_NAME_FULL},
   {"décembre", (1<<4),12, DATE_NAME_FULL},
   {NULL, 0, 0, DATE_NAME_NONE}
};

// map week day names to indices 0..6 (0=sunday)
const T_DATE_NAME WDayNames[] =
{
   // English
   {"sat", (1<<0),6, DATE_NAME_ABBRV},
   {"sun", (1<<0),0, DATE_NAME_ABBRV},
   {"mon", (1<<0),1, DATE_NAME_ABBRV},
   {"tue", (1<<0),2, DATE_NAME_ABBRV},
   {"wed", (1<<0),3, DATE_NAME_ABBRV},
   {"thu", (1<<0),4, DATE_NAME_ABBRV},
   {"fri", (1<<0),5, DATE_NAME_ABBRV},
   {"saturday", (1<<0),6, DATE_NAME_FULL},
   {"sunday", (1<<0),0, DATE_NAME_FULL},
   {"monday", (1<<0),1, DATE_NAME_FULL},
   {"tuesday", (1<<0),2, DATE_NAME_FULL},
   {"wednesday", (1<<0),3, DATE_NAME_FULL},
   {"thursday", (1<<0),4, DATE_NAME_FULL},
   {"friday", (1<<0),5, DATE_NAME_FULL},
   // German
   {"sa", (1<<1),6, DATE_NAME_ABBRV},
   {"so", (1<<1),0, DATE_NAME_ABBRV},
   {"mo", (1<<1),1, DATE_NAME_ABBRV},
   {"di", (1<<1),2, DATE_NAME_ABBRV},
   {"mi", (1<<1),3, DATE_NAME_ABBRV},
   {"do", (1<<1),4, DATE_NAME_ABBRV},
   {"fr", (1<<1),5, DATE_NAME_ABBRV},
   {"sam", (1<<1),6, DATE_NAME_ABBRV},
   {"son", (1<<1),0, DATE_NAME_ABBRV},
   {"mon", (1<<1),1, DATE_NAME_ABBRV},
   {"die", (1<<1),2, DATE_NAME_ABBRV},
   {"mit", (1<<1),3, DATE_NAME_ABBRV},
   {"don", (1<<1),4, DATE_NAME_ABBRV},
   {"fre", (1<<1),5, DATE_NAME_ABBRV},
   {"samstag", (1<<1),6, DATE_NAME_FULL},
   {"sonnabend", (1<<1),6, DATE_NAME_FULL},
   {"sonntag", (1<<1),0, DATE_NAME_FULL},
   {"montag", (1<<1),1, DATE_NAME_FULL},
   {"dienstag", (1<<1),2, DATE_NAME_FULL},
   {"mittwoch", (1<<1),3, DATE_NAME_FULL},
   {"donnerstag", (1<<1),4, DATE_NAME_FULL},
   {"freitag", (1<<1),5, DATE_NAME_FULL},
   // French
   {"samedi", (1<<4),6, DATE_NAME_FULL},
   {"dimanche", (1<<4),0, DATE_NAME_FULL},
   {"lundi", (1<<4),1, DATE_NAME_FULL},
   {"mardi", (1<<4),2, DATE_NAME_FULL},
   {"mercredi", (1<<4),3, DATE_NAME_FULL},
   {"jeudi", (1<<4),4, DATE_NAME_FULL},
   {"vendredi", (1<<4),5, DATE_NAME_FULL},
   {NULL}
};

// map today, tomorrow etc. to day offsets 0,1,...
const T_DATE_NAME RelDateNames[] =
{
   {"today", (1<<0),0, DATE_NAME_FULL},
   {"tomorrow", (1<<0),1, DATE_NAME_FULL},
   {"heute", (1<<1),0, DATE_NAME_FULL},
   {"morgen", (1<<1),1, DATE_NAME_FULL},
   {"übermorgen", (1<<1),2, DATE_NAME_FULL},
   {"aujourd'hui", (1<<4),0, DATE_NAME_FULL},
   {"demain", (1<<4),1, DATE_NAME_FULL},
   {"aprés-demain", (1<<4),2, DATE_NAME_FULL},
   {NULL}
};

// return a reg.exp. pattern which matches all names of a given language
string GetDateNameRegExp(const T_DATE_NAME * p_list, int lang, int abbrv)
{
   string pat;
   for (const T_DATE_NAME * p = p_list; p->p_name != NULL; p++) {
      if ((p->lang_bits & (1 << lang)) != 0) {
         if ((p->abbrv & abbrv) != 0) {
            if (pat.length() > 0)
               pat += "|";
            pat += p->p_name;
         }
      }
   }
   return pat;
}

const T_DATE_NAME * MapDateName(const char * p_name, const T_DATE_NAME * p_list)
{
   for (const T_DATE_NAME * p = p_list; p->p_name != NULL; p++) {
      if (strcasecmp(p->p_name, p_name) == 0)
         return p;
   }
   return 0;
}

int GetWeekDayOffset(string wday_name)
{
   int reldate = -1;

   if (wday_name.length() > 0) {
      const T_DATE_NAME * p = MapDateName(wday_name.c_str(), WDayNames);
      if (p != 0) {
         int wday_idx = p->idx;
         struct tm * ptm = localtime(&VbiCaptureTime);
         int cur_wday_idx = ptm->tm_wday;

         if (wday_idx >= cur_wday_idx) {
            reldate = wday_idx - cur_wday_idx;
         }
         else {
            reldate = (7 - cur_wday_idx) + wday_idx;
         }
      }
   }
   return reldate;
}

bool CheckDate(int mday, int month, int year,
               string wday_name, string month_name,
               int * ret_mday, int * ret_mon, int * ret_year)
{
   // first check all provided values
   if (   ((mday != -1) && !((mday <= 31) && (mday > 0)))
       || ((month != -1) && !((month <= 12) && (month > 0)))
       || ((year != -1) && !((year < 100) || ((year >= 2006) && (year < 2031)))) )
   {
      return false;
   }

   if ((wday_name.length() > 0) && (mday == -1))
   {
      // determine date from weekday name alone
      int reldate = GetWeekDayOffset(wday_name);
      if (reldate < 0)
         return false;

      time_t whence = VbiCaptureTime + reldate*24*60*60;
      struct tm * ptm = localtime(&whence);
      mday = ptm->tm_mday;
      month = ptm->tm_mon + 1;
      year = ptm->tm_year + 1900;
      if (opt_debug) printf("DESC DATE %s %d.%d.%d\n", wday_name.c_str(), mday, month, year);
   }
   else if (mday != -1)
   {
      if (wday_name.length() > 0) {
         if (!MapDateName(wday_name.c_str(), WDayNames))
            return false;
      }
      if ((month == -1) && (month_name.length() > 0)) {
         const T_DATE_NAME * p = MapDateName(month_name.c_str(), MonthNames);
         if (p == 0)
            return false;
         month = p->idx;
      }
      else if (month == -1) {
         return false;
      }
      if (year == -1) {
         struct tm * ptm = localtime(&VbiCaptureTime);
         year = ptm->tm_year + 1900;
      }
      else if (year < 100) {
         struct tm * ptm = localtime(&VbiCaptureTime);
         int cur_year = ptm->tm_year + 1900;
         year += (cur_year - cur_year % 100);
      }
      if (opt_debug) printf("DESC DATE %d.%d.%d\n", mday, month, year);
   }
   else {
      return false;
   }
   *ret_mday = mday;
   *ret_mon = month;
   *ret_year = year;
   return true;
}

/* ! 20.15  Eine Chance für die Liebe UT         ARD
 *           Spielfilm, D 2006 ........ 344
 *                     ARD-Themenwoche Krebs         
 *
 * 19.35  1925  WISO ................. 317       ZDF
 * 20.15        Zwei gegen Zwei           
 *              Fernsehfilm D/2005 UT  318
 *
 * 10:00 WORLD NEWS                              CNNi (week overview)
 * News bulletins on the hour (...)
 * 10:30 WORLD SPORT                      
 */
class T_OV_FMT_STAT
{
public:
   T_OV_FMT_STAT() : time_off(-1), vps_off(-1), title_off(-1), subt_off(-1) {}
   bool operator==(const T_OV_FMT_STAT& b) const {
      return (   (time_off == b.time_off)
              && (vps_off == b.vps_off)
              && (title_off == b.title_off));
   }
   bool operator<(const T_OV_FMT_STAT& b) const {
      return    (time_off < b.time_off)
             || (   (time_off == b.time_off)
                 && (   (vps_off < b.vps_off)
                     || (   (vps_off == b.vps_off)
                         && (title_off < b.title_off) )));
   }
   bool is_valid() const { return time_off >= 0; }
public:
   int time_off;
   int vps_off;
   int title_off;
   int subt_off;
};

void DetectOvFormatParse(vector<T_OV_FMT_STAT>& fmt_list, int page, int sub)
{
   vt_page& pgtext = PageText[page | (sub << 12)];
   cmatch what;

   for (int line = 5; line <= 21; line++) {
      char * text = pgtext.m_line[line];
      // look for a line containing a start time (hour 0-23 : minute 0-59)
      // TODO allow start-stop times "10:00-11:00"?
      static const regex expr1("^(( *| *! +)([01][0-9]|2[0-3])[\\.:]([0-5][0-9]) +)");
      if (regex_search(text, what, expr1)) {
         int off = what[1].length();
         struct T_OV_FMT_STAT fmt;
         fmt.time_off = what[2].length();
         fmt.vps_off = -1;
         fmt.title_off = off;
         fmt.subt_off = -1;

         // TODO VPS must be magenta or concealed
         static const regex expr2("^([0-2][0-9][0-5][0-9] +)");
         if (regex_search(text + off, what, expr2)) {
            fmt.vps_off = off;
            fmt.title_off = off + what[1].length();
         }
         else {
            fmt.vps_off = -1;
            fmt.title_off = off;
         }

         static const regex expr3("^( *| *\\d{4} +)[[:alpha:]]");
         static const regex expr4("^( *)[[:alpha:]]");
         char * text2 = pgtext.m_line[line + 1];
         if ( (fmt.vps_off == -1)
              ? regex_search(text2, what, expr3)
              : regex_search(text2, what, expr4) )
         {
            fmt.subt_off = what[1].second - what[1].first;
         }

         //printf("FMT: %d,%d,%d,%d\n", fmt.time_off, fmt.vps_off, fmt.title_off, fmt.subt_off);
         fmt_list.push_back(fmt);
      }
   }
}

/* ------------------------------------------------------------------------------
 *  Determine layout of programme overview pages
 *  (to allow stricter parsing)
 *  - TODO: detect color used to mark features (e.g. yellow on ARD) (WDR: ttx ref same color as title)
 *  - TODO: detect color used to distinguish title from subtitle/category (WDR,Tele5)
 */
T_OV_FMT_STAT DetectOvFormat()
{
   vector<T_OV_FMT_STAT> fmt_list;

   // look at the first 5 pages (default start at page 301)
   int cnt = 0;
   for (map<int,int>::iterator p = PgCnt.begin();
           (p != PgCnt.end()) && (cnt < 5);
              p++)
   {
      if ((p->first >= opt_tv_start) && (p->first <= opt_tv_end))
      {
         int page = p->first;
         int max_sub = PgSub[page];
         int sub1, sub2;
         if (max_sub == 0) {
            sub1 = sub2 = 0;
         }
         else {
            sub1 = 1;
            sub2 = max_sub;
         }
         for (int sub = sub1; sub <= sub2; sub++) {
            if (TtxSubPageDefined(page, sub)) {
               PageToLatin(page, sub);

               DetectOvFormatParse(fmt_list, page, sub);
            }
         }
         cnt++;
      }
   }

   T_OV_FMT_STAT fmt;
   if (!fmt_list.empty()) {
      // search the most used format (ignoreing "subt_off")
      map<T_OV_FMT_STAT,int> fmt_stats;
      int max_cnt = 0;
      int max_idx = -1;
      for (uint idx = 0; idx < fmt_list.size(); idx++) {
         map<T_OV_FMT_STAT,int>::iterator p = fmt_stats.find(fmt_list[idx]);
         if (p != fmt_stats.end()) {
            p->second += 1;
         }
         else {
            fmt_stats[fmt_list[idx]] = 1;
         }
         if (fmt_stats[fmt_list[idx]] > max_cnt) {
            max_cnt = fmt_stats[fmt_list[idx]];
            max_idx = idx;
         }
      }
      fmt = fmt_list[max_idx];

      // search the most used "subt_off" among the most used format
      map<int,int> fmt_subt;
      max_cnt = 0;
      for (uint idx = 0; idx < fmt_list.size(); idx++) {
         if (   (fmt_list[idx] == fmt)
             && (fmt_list[idx].subt_off != -1))
         {
            map<int,int>::iterator p = fmt_subt.find(fmt_list[idx].subt_off);
            if (p != fmt_subt.end()) {
               p->second += 1;
            }
            else {
               fmt_subt[fmt_list[idx].subt_off] = 1;
            }
            if (fmt_subt[fmt_list[idx].subt_off] > max_cnt) {
               max_cnt = fmt_subt[fmt_list[idx].subt_off];
               fmt.subt_off = fmt_list[idx].subt_off;
            }
         }
      }

      if (opt_debug)  {
         printf("auto-detected overview format: time:%d,vps:%d,title:%d,title2:%d\n",
                fmt.time_off, fmt.vps_off, fmt.title_off, fmt.subt_off);
      }
   }
   else {
      if (cnt == 0)
         fprintf(stderr, "No pages found in range %03X-%03X\n", opt_tv_start, opt_tv_end);
      else
         fprintf(stderr, "Failed to detect overview format on pages %03X-%03X\n", opt_tv_start, opt_tv_end);
   }
   return fmt;
}

/* ------------------------------------------------------------------------------
 * Parse date in an overview page
 * - similar to dates on description pages
 */
void ParseOvHeaderDate(int page, int sub, T_PG_DATE& pgdate)
{
   int reldate = -1;
   int mday = -1;
   int month = -1;
   int year = -1;

   vt_page& pgtext = PageText[page | (sub << 12)];
   int lang = PgLang[page];

   string wday_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_FULL);
   string wday_abbrv_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_ABBRV);
   string mname_match = GetDateNameRegExp(MonthNames, lang, DATE_NAME_ANY);
   string relday_match = GetDateNameRegExp(RelDateNames, lang, DATE_NAME_ANY);
   cmatch what;
   int prio = -1;

   for (int line = 1; line < pgdate.m_head_end; line++)
   {
      // [Mo.]13.04.[2006]
      // So,06.01.
      static regex expr1[8];
      static regex expr2[8];
      if (expr1[lang].empty()) {
         expr1[lang].assign(string("(^| |(") + wday_abbrv_match + ")(\\.|\\.,|,) ?)(\\d{1,2})\\.(\\d{1,2})\\.(\\d{2}|\\d{4})?([ ,:]|$)", regex::icase);
         expr2[lang].assign(string("(^| |(") + wday_match + ")(, ?| ))(\\d{1,2})\\.(\\d{1,2})\\.(\\d{2}|\\d{4})?([ ,:]|$)", regex::icase);
      }
      if (   regex_search(pgtext.m_line[line], what, expr1[lang])
          || regex_search(pgtext.m_line[line], what, expr2[lang]) )
      {
         if (CheckDate(atoi_substr(what[4]), atoi_substr(what[5]), atoi_substr(what[6]),
                       "", "", &mday, &month, &year)) {
            prio = 3;
         }
      }
      // 13.April [2006]
      static regex expr3[8];
      if (expr3[lang].empty()) {
         expr3[lang].assign(string("(^| )(\\d{1,2})\\. ?(") + mname_match + ")( (\\d{2}|\\d{4}))?([ ,:]|$)", regex::icase);
      }
      if (regex_search(pgtext.m_line[line], what, expr3[lang])) {
         if (CheckDate(atoi_substr(what[2]), -1, atoi_substr(what[5]),
                       "", string(what[3]), &mday, &month, &year)) {
            prio = 3;
         }
      }
      // Sunday 22 April (i.e. no dot after month)
      static regex expr4[8];
      static regex expr5[8];
      if (expr4[lang].empty()) {
         expr4[lang].assign(string("(^| )(") + wday_match + string(")(, ?| )(\\d{1,2})\\.? ?(") + mname_match + ")( (\\d{4}|\\d{2}))?( |,|;|$)", regex::icase);
         expr5[lang].assign(string("(^| )(") + wday_abbrv_match + string(")(\\.,? ?|, ?| )(\\d{1,2})\\.? ?(") + mname_match + ")( (\\d{4}|\\d{2}))?( |,|;|$)", regex::icase);
      }
      if (   regex_search(pgtext.m_line[line], what, expr4[lang])
          || regex_search(pgtext.m_line[line], what, expr5[lang])) {
         if (CheckDate(atoi_substr(what[4]), -1, atoi_substr(what[7]),
                       string(what[2]), string(what[5]), &mday, &month, &year)) {
            prio = 3;
         }
      }
      if (prio >= 3) continue;
      // "Do. 21-22 Uhr" (e.g. on VIVA)  --  TODO internationalize "Uhr"
      // "Do  21:00-22:00" (e.g. Tele-5)
      static regex expr6[8];
      if (expr6[lang].empty()) {
         expr6[lang].assign(string("(^| )((") + wday_abbrv_match + string(")\\.?|(") + wday_match + ")) *((\\d{1,2}(-| - )\\d{1,2}( +Uhr| ?h|   ))|(\\d{1,2}[\\.:]\\d{2}(-| - )(\\d{1,2}[\\.:]\\d{2})?))( |$)", regex::icase);
      }
      if (regex_search(pgtext.m_line[line], what, expr6[lang])) {
         string wday_name;
         if (what[2].matched)
            wday_name = string(what[2]);
         else
            wday_name = string(what[3]);
         int off = GetWeekDayOffset(wday_name);
         if (off >= 0)
            reldate = off;
         prio = 2;
      }
      if (prio >= 2) continue;
      // monday, tuesday, ... (non-abbreviated only)
      static regex expr7[8];
      if (expr7[lang].empty()) {
         expr7[lang].assign(string("(^| )(") + wday_match + ")([ ,:]|$)", regex::icase);
      }
      if (regex_search(pgtext.m_line[line], what, expr7[lang])) {
         int off = GetWeekDayOffset(string(what[2]));
         if (off >= 0)
            reldate = off;
         prio = 1;
      }
      if (prio >= 1) continue;
      // today, tomorrow, ...
      static regex expr8[8];
      if (expr8[lang].empty()) {
         expr8[lang].assign(string("(^| )(") + relday_match + ")([ ,:]|$)", regex::icase);
      }
      if (regex_search(pgtext.m_line[line], what, expr8[lang])) {
         string rel_name = string(what[2]);
         const T_DATE_NAME * p = MapDateName(rel_name.c_str(), RelDateNames);
         if (p != 0)
            reldate = p->idx;
         prio = 0;
      }
   }

   if ((mday == -1) && (reldate != -1)) {
      time_t whence = VbiCaptureTime + reldate*24*60*60;
      struct tm * ptm = localtime(&whence);
      mday = ptm->tm_mday;
      month = ptm->tm_mon + 1;
      year = ptm->tm_year + 1900;
   }
   else if ((year != -1) && (year < 100)) {
      struct tm * ptm = localtime(&VbiCaptureTime);
      int cur_year = ptm->tm_year + 1900;
      year += (cur_year - cur_year % 100);
   }

   // return results via the struct
   pgdate.m_mday = mday;
   pgdate.m_month = month;
   pgdate.m_year = year;
   pgdate.m_date_off = 0;
}

/* ------------------------------------------------------------------------------
 * Parse feature indicators in the overview table
 * - main task to extrace references to teletext pages with longer descriptions
 *   unfortunately these references come in a great varity of formats, so we
 *   must mostly rely on the teletext number (risking to strip off 3-digit
 *   numbers at the end of titles in rare cases)
 * - it's mandatory to remove features becaue otherwise they might show up in
 *   the middle of a wrapped title
 * - most feature strings are unique enough so that they can be identified
 *   by a simple pattern match (e.g. 16:9, UT)
 * - TODO: use color to identify features; make a first sweep to check if
 *   most unique features always have a special color (often yellow) and then
 *   remove all words in that color inside title strings

 * Examples:
 *
 * 06.05 # logo!                     >331
 *
 * 13.15  1315  Heilkraft aus der Wüste   
 *              (1/2) Geheimnisse der     
 *              Buschmänner 16:9 oo       
 *
 * TODO: some tags need to be localized (i.e. same as month names etc.)
 */
enum TV_FEAT_TYPE
{
   TV_FEAT_SUBTITLES,
   TV_FEAT_2CHAN,
   TV_FEAT_ASPECT_16_9,
   TV_FEAT_HDTV,
   TV_FEAT_BW,
   TV_FEAT_HD,
   TV_FEAT_DOLBY,
   TV_FEAT_MONO,
   TV_FEAT_OMU,
   TV_FEAT_STEREO,
   TV_FEAT_TIP,
   TV_FEAT_COUNT
};
struct TV_FEAT_STR
{
   const char         * p_name;
   TV_FEAT_TYPE         type;
};

const TV_FEAT_STR FeatToFlagMap[] =
{
   { "untertitel", TV_FEAT_SUBTITLES },
   { "ut", TV_FEAT_SUBTITLES },
   { "omu", TV_FEAT_OMU },
   { "sw", TV_FEAT_BW },
   { "s/w", TV_FEAT_BW },
   { "hd", TV_FEAT_HD },
   { "16:9", TV_FEAT_ASPECT_16_9 },
   { "hd", TV_FEAT_HDTV },
   { "°°", TV_FEAT_2CHAN }, // according to ARD page 395
   { "oo", TV_FEAT_STEREO },
   { "stereo", TV_FEAT_STEREO },
   { "ad", TV_FEAT_2CHAN }, // accoustic description
   { "hörfilm", TV_FEAT_2CHAN },
   { "hörfilm°°", TV_FEAT_2CHAN },
   { "hf", TV_FEAT_2CHAN },
   { "2k", TV_FEAT_2CHAN },
   { "2k-ton", TV_FEAT_2CHAN },
   { "dolby", TV_FEAT_DOLBY },
   { "surround", TV_FEAT_DOLBY },
   { "stereo", TV_FEAT_STEREO },
   { "mono", TV_FEAT_MONO },
   { "tipp", TV_FEAT_TIP },
   // ORF
   //{ "°°", TV_FEAT_STEREO }, // conflicts with ARD
   { "°*", TV_FEAT_2CHAN },
   { "ds", TV_FEAT_DOLBY },
   { "ss", TV_FEAT_DOLBY },
   { "dd", TV_FEAT_DOLBY },
   { "zs", TV_FEAT_2CHAN },
};
#define TV_FEAT_MAP_LEN (sizeof(FeatToFlagMap)/sizeof(FeatToFlagMap[0]))

void MapTrailingFeat(const char * feat, int len, TV_SLOT& slot, const string& title)
{
   if ((len >= 2) && (strncasecmp(feat, "ut", 2) == 0)) // TV_FEAT_SUBTITLES
   {
      if (opt_debug) printf("FEAT \"%s\" -> on TITLE %s\n", feat, title.c_str());
      slot.m_has_subtitles = true;
      return;
   }

   for (uint idx = 0; idx < TV_FEAT_MAP_LEN; idx++)
   {
      if (strncasecmp(feat, FeatToFlagMap[idx].p_name, len) == 0)
      {
         switch (FeatToFlagMap[idx].type)
         {
            case TV_FEAT_SUBTITLES:     slot.m_has_subtitles = true; break;
            case TV_FEAT_2CHAN:         slot.m_is_2chan = true; break;
            case TV_FEAT_ASPECT_16_9:   slot.m_is_aspect_16_9 = true; break;
            case TV_FEAT_HD:            slot.m_is_video_hd = true; break;
            case TV_FEAT_BW:            slot.m_is_video_bw = true; break;
            case TV_FEAT_DOLBY:         slot.m_is_dolby = true; break;
            case TV_FEAT_MONO:          slot.m_is_mono = true; break;
            case TV_FEAT_OMU:           slot.m_is_omu = true; break;
            case TV_FEAT_STEREO:        slot.m_is_stereo = true; break;
            case TV_FEAT_TIP:           slot.m_is_tip = true; break;
            default: assert(false); break;
         }
         if (opt_debug) printf("FEAT \"%s\" -> on TITLE %s\n", feat, title.c_str());
         return;
      }
   }
   if (opt_debug) printf("FEAT dropping \"%s\" on TITLE %s\n", feat, title.c_str());
}

// note: must correct $n below if () are added to pattern
#define FEAT_PAT_STR "UT(( auf | )?[1-8][0-9][0-9])?|" \
                     "[Uu]ntertitel|[Hh]örfilm(°°)?|HF|AD|" \
                     "s/?w|S/?W|tlw. s/w|oo|°°|°\\*|OmU|16:9|HD|" \
                     "2K|2K-Ton|[Mm]ono|[Ss]tereo|[Dd]olby|[Ss]urround|" \
                     "DS|SS|DD|ZS|" \
                     "Wh\\.?|Wdh\\.?|Whg\\.?|Tipp!?"

void ParseTrailingFeat(string& title, TV_SLOT& slot)
{
   string orig_title = regex_replace(title, regex("[\\x00-\\x1F\\x7F]"), " ");
   smatch whats;

   // teletext reference (there's at most one at the very right, so parse this first)
   static const regex expr1("([ \\x00-\\x07\\x1D]\\.+|\\.\\.+|\\.+>|>>?|--?>|>{1,4} +|\\.* +"
                            "|[\\.>]*[\\x00-\\x07\\x1D]+)"
                            "([1-8][0-9][0-9]) {0,3}$");
   if (regex_search(title, whats, expr1)) {
      string ttx_ref = string(whats[2].first, whats[2].second);

      const char * p = &(whats[2].first[0]);
      slot.m_ttx_ref = ((p[0] - 0x30)<<8) |
                       ((p[1] - 0x30)<<4) |
                       (p[2] - 0x30);
      if (opt_debug) printf("FEAT TTX ref \"%s\" on TITLE %s\n", ttx_ref.c_str(), orig_title.c_str());

      // warning: must be done last - invalidates "whats"
      title.erase(title.length() - whats[0].length());
   }

   // note: remove trailing space here, not in the loop to allow max. 1 space between features
   static const regex expr2("[ \\x00-\\x1F\\x7F]+$");
   title = regex_replace(title, expr2, "");

   // ARTE: (oo) (2K) (Mono)
   // SWR: "(16:9/UT)", "UT 150", "UT auf 150"
   static const regex expr3("^(.*?)[ \\x00-\\x07\\x1D]"
                            "\\(((" FEAT_PAT_STR ")(( ?/ ?|, ?| )(" FEAT_PAT_STR "))*)\\)$");
   static const regex expr4("^(.*?)(  +|[\\x00-\\x07\\x1D]+)((" FEAT_PAT_STR ")"
                            "((, ?|/ ?| |[,/]?[\\x00-\\x07\\x1D]+)(" FEAT_PAT_STR "))*)$");

   if (regex_search(title, whats, expr3)) {
      string feat_list = string(whats[2].first, whats[2].second);

      MapTrailingFeat(feat_list.c_str(), whats[3].length(), slot, orig_title);
      feat_list.erase(0, whats[3].length());

      // delete everything following the first sub-match
      title.erase(whats[1].length());

      if (feat_list.length() != 0)  {
         static const regex expr5("( ?/ ?|, ?| )(" FEAT_PAT_STR ")$");
         while (regex_search(feat_list, whats, expr5)) {
            MapTrailingFeat(&*whats[2].first, whats[2].length(), slot, orig_title);
            feat_list.erase(feat_list.length() - whats[0].length());
         } 
      }
      else {
         static const regex expr6("[ \\x00-\\x07\\x1D]*\\((" FEAT_PAT_STR ")\\)$");
         while (regex_search(title, whats, expr6)) {
            MapTrailingFeat(&*whats[1].first, whats[1].length(), slot, orig_title);
            title.erase(title.length() - whats[0].length());
         } 
      }
   }
   else if (regex_search(title, whats, expr4)) {
      string feat_list = string(whats[3]);

      MapTrailingFeat(feat_list.c_str(), whats[4].length(), slot, orig_title);
      feat_list.erase(0, whats[4].length());

      // delete everything following the first sub-match
      title.erase(whats[1].length());

      static const regex expr7("([,/][ \\x00-\\x07\\x1D]*|[ \\x00-\\x07\\x1D]+)(" FEAT_PAT_STR ")$");
      while (regex_search(feat_list, whats, expr7)) {
         MapTrailingFeat(&*whats[2].first, whats[2].length(), slot, orig_title);
         feat_list.erase(feat_list.length() - whats[0].length());
      } 
   } //else { print "NONE orig_title\n"; }

   // compress adjecent white-space & remove special chars
   static const regex expr8("[\\x00-\\x1F\\x7F ]+");
   title = regex_replace(title, expr8, " ");

   // remove leading and trailing space
   static const regex expr9("(^ +| +$)");
   title = regex_replace(title, expr9, "");
}

/* ------------------------------------------------------------------------------
 * Chop footer
 * - footers usually contain advertisments (or more precisely: references
 *   to teletext pages with ads) and can simply be discarded
 *
 *   Schule. Dazu Arbeitslosigkeit von    
 *   bis zu 30 Prozent. Wir berichten ü-  
 *   ber Menschen in Ostdeutschland.  318>
 *    Ab in die Sonne!................500 
 *
 *   Teilnahmebedingungen: Seite 881      
 *     Türkei-Urlaub jetzt buchen! ...504 
 */
int ParseFooter(int page, int sub, int head)
{
   const vt_page& pgtext = PageText[page | (sub << 12)];
   const vt_page& pgctrl = PageCtrl[page | (sub << 12)];
   cmatch what;
   int foot;

   for (foot = 23 ; foot > head; foot--) {
      // note missing lines are treated as empty lines
      const char * text = pgtext.m_line[foot];

      // stop at lines which look like separators
      static const regex expr1("^ *-{10,} *$");
      if (regex_search(text, what, expr1)) {
         foot -= 1;
         break;
      }
      static const regex expr2("\\S");
      if (!regex_search(text, what, expr2)) {
         static const regex expr3("[\\x0D\\x0F][^\\x0C]*[^ ]");
         string prev_ctrl = string(pgctrl.m_line[foot - 1], VT_PKG_RAW_LEN);
         smatch whats;
         if (!regex_search(prev_ctrl, whats, expr3)) {
            foot -= 1;
            break;
         }
         // ignore this empty line since there's double-height chars above
         continue;
      }
      // check for a teletext reference
      // TODO internationalize
      static const regex expr4("^ *(seite |page |<+ *)?[1-8][0-9][0-9]([^\\d]|$)", regex::icase);
      static const regex expr5("[^\\d][1-8][0-9][0-9]([\\.\\?!:,]|>+)? *$");
      //static const regex expr6("(^ *<|> *$)");
      if (   regex_search(text, what, expr4)
          || regex_search(text, what, expr5)) {
           //|| ((sub != 0) && regex_search(text, what, expr6)) )
      }
      else {
         break;
      }
   }
   // plausibility check (i.e. don't discard the entire page)
   if (foot < 23 - 5) {
      foot = 23;
   }
   return foot;
}

/* Try to identify footer lines by a different background color
 * - bg color is changed by first changing the fg color (codes 0x0-0x7),
 *   then switching copying fg over bg color (code 0x1D)
 */
int ParseFooterByColor(int page, int sub)
{
   int ColCount[8] = {0};
   int LineCol[24] = {0};
   smatch whats;

   // check which color is used for the main part of the page
   // - ignore top-most header and one footer line
   // - ignore missing lines (although they would display black (except for
   //   double-height) but we don't know if they're intentionally missing)
   int handle = page | (sub << 12);
   vt_page& pg_pkg = Pkg[handle];
   for (int line = 4; line <= 23; line++) {
      if (pg_pkg.m_defined & (1 << line)) {
         // get first non-blank char; skip non-color control chars
         static const regex expr1("^[ \\x00-\\x1F]*([\\x00-\\x07\\x10-\\x17])\\x1D");
         string text(pg_pkg.m_line[line], VT_PKG_RAW_LEN);
         if (regex_search(text, whats, expr1)) {
            unsigned char c = *whats[1].first;
            if (c <= 0x07) {
               ColCount[c] += 1;
            }
            LineCol[line] = c;
            // else: ignore mosaic
         }
         else {
            // background color unchanged
            ColCount[0] += 1;
            LineCol[line] = 0;
         }
      }
   }

   // search most used color
   int max_idx = 0;
   for (int col_idx = 0; col_idx < 8; col_idx++) {
      if (ColCount[col_idx] > ColCount[max_idx]) {
         max_idx = col_idx;
      }
   }

   // count how many consecutive lines have this color
   // FIXME allow also use of several different colors in the middle and different ones for footer
   int max_count = 0;
   int count = 0;
   for (int line = 4; line <= 23; line++) {
      if (LineCol[line] == max_idx) {
         count += 1;
         if (count > max_count)
            max_count = count;
      }
      else {
         count = 0;
      }
   }

   // TODO: merge with other footer function: require TTX ref in every skipped segment with the same bg color

   // reliable only if 8 consecutive lines without changes, else don't skip any footer lines
   int last_line = 23;
   if (max_count >= 8) {
      // skip footer until normal color is reached
      for (int line = 23; line >= 0; line--) {
         if (   (pg_pkg.m_defined & (1 << line))
             && (LineCol[line] == max_idx)) {
            last_line = line;
            break;
         }
      }
   }
   return last_line;
}

/* ------------------------------------------------------------------------------
 * Remove garbage from line end in overview title lines
 * - KiKa sometimes uses the complete page for the overview and includes
 *   the footer text at the right side on a line used for the overview,
 *   separated by using a different background color
 *   " 09.45 / Dragon (7)        Weiter   >>> " (line #23)
 * - note the cases with no text before the page ref is handled b the
 *   regular footer detection
 */
void RemoveTrailingPageFooter(string& text)
{
   smatch whats;

   // look for a page reference or ">>" at line end
   static const regex expr1("(.*[^[:alnum:]])([1-8][0-9][0-9]|>{1,4})[^\\x1D\\x21-\\xFF]*$");
   if (regex_search(text, whats, expr1)) {
      int ref_off = whats[1].length();
      // check if the background color is changed
      static const regex expr2("(.*)\\x1D[^\\x1D]+$");
      if (regex_search(text.substr(0, ref_off), whats, expr2)) {
         ref_off = whats[1].length();
         // determine the background color of the reference (i.e. last used FG color before 1D)
         int ref_col = 7;
         static const regex expr3("(.*)([\\x00-\\x07\\x10-\\x17])[^\\x00-\\x07\\x10-\\x17\\x1D]*$");
         if (regex_search(text.substr(0, ref_off), whats, expr3)) {
            ref_col = *whats[2].first;
         }
         //print "       REF OFF:$ref_off COL:$ref_col\n";
         // determine the background before the reference
         bool matched = false;
         int txt_off = ref_off;
         if (regex_search(text.substr(0, ref_off), whats, expr2)) {
            int tmp_off = whats[1].length();
            if (regex_search(text.substr(0, tmp_off), whats, expr3)) {
               int txt_col = *whats[2].first;
               //print "       TXTCOL:$txt_col\n";
               // check if there's any text with this color
               static const regex expr4("[\\x21-\\xff]");
               if (regex_search(text.substr(tmp_off, ref_off), whats, expr4)) {
                  matched = (txt_col != ref_col);
                  txt_off = tmp_off;
               }
            }
         }
         // check for text at the default BG color (unless ref. has default too)
         if (!matched && (ref_col != 7)) {
            static const regex expr5("[\\x21-\\xff]");
            matched = regex_search(text.substr(0, txt_off), whats, expr5);
            //if (matched) print "       DEFAULT TXTCOL:7\n";
         }
         if (matched) {
            if (opt_debug) printf("OV removing trailing page ref\n");
            for (uint idx = ref_off; idx < text.size(); idx++) {
               text[idx] = ' ';
            }
         }
      }
   }
}

/* ------------------------------------------------------------------------------
 * Convert a "traditional" VPS CNI into a 16-bit PDC channel ID
 * - first two digits are the PDC country code in hex (e.g. 1D,1A,24 for D,A,CH)
 * - 3rd digit is an "address area", indicating network code tables 1 to 4
 * - the final 3 digits are decimal and the index into the network table
 */
int ConvertVpsCni(const char * cni_str)
{
   int a, b, c;

   if (   (sscanf(cni_str, "%2x%1d%2d", &a, &b, &c) == 3)
       && (b >= 1) && (b <= 4)) {
      return (a<<8) | ((4 - b) << 6) | (c & 0x3F);
   }
   else {
      return -1;
   }
}

/* ------------------------------------------------------------------------------
 * Parse and remove VPS indicators
 * - format as defined in ETS 300 231, ch. 7.3.1.3
 * - matching text is replaced with blanks
 * - for performance reasons the caller should check if there's a magenta
 *   or conceal character in the line before calling this function
 * - TODO: KiKa special: "VPS 20.00" (magenta)
 */
void ParseVpsLabel(string& text, T_VPS_TIME& vps_data, bool is_desc)
{
   smatch whats;
   static const regex expr1("^(.*)([\\x05\\x18]+(VPS[\\x05\\x18 ]+)?"
               "(\\d{4})([\\x05\\x18 ]+[\\dA-Fs]*)*([\\x00-\\x04\\x06\\x07]|$))");
   static const regex expr2("^(.*)([\\x05\\x18]+([0-9A-F]{2}\\d{3})[\\x05\\x18 ]+"
               "(\\d{6})([\\x05\\x18 ]+[\\dA-Fs]*)*([\\x00-\\x04\\x06\\x07]|$))");
   static const regex expr3("^(.*)([\\x05\\x18]+(\\d{6})([\\x05\\x18 ]+[\\dA-Fs]*)*([\\x00-\\x04\\x06\\x07]|$))");
   static const regex expr4("^(.*)([\\x05\\x18]+(\\d{2}[.:]\\d{2}) oo *([\\x00-\\x04\\x06\\x07]|$))");
   static const regex expr5("^(.*)([\\x05\\x18][\\x05\\x18 ]*VPS *([\\x00-\\x04\\x06\\x07]|$))");

   // time
   // discard any concealed/magenta labels which follow
   if (regex_search(text, whats, expr1)) {
      vps_data.m_vps_time = string(whats[4].first, whats[4].second);
      vps_data.m_new_vps_time = true;
      // blank out the same area in the text-only string
      for (int idx = whats[1].length(); idx < whats[1].length() + whats[2].length(); idx++)
         text[idx] = ' ';
      if (opt_debug) printf("VPS time found: %s\n", vps_data.m_vps_time.c_str());
   }
   // CNI and date "1D102 120406 F9" (ignoring checksum)
   else if (regex_search(text, whats, expr2)) {
      string cni_str = string(whats[3].first, whats[3].second);
      vps_data.m_vps_date = string(whats[4].first, whats[4].second);
      vps_data.m_new_vps_date = true;
      for (int idx = whats[1].length(); idx < whats[1].length() + whats[2].length(); idx++)
         text[idx] = ' ';
      vps_data.m_vps_cni = ConvertVpsCni(cni_str.c_str());
      if (opt_debug) printf("VPS date and CNI: 0x%X, /%s/\n", vps_data.m_vps_cni, vps_data.m_vps_date.c_str());
   }
   // date
   else if (regex_search(text, whats, expr3)) {
      vps_data.m_vps_date = string(whats[3].first, whats[3].second);
      vps_data.m_new_vps_date = true;
      for (int idx = whats[1].length(); idx < whats[1].length() + whats[2].length(); idx++)
         text[idx] = ' ';
      if (opt_debug) printf("VPS date: 3\n");
   }
   // end time (RTL special - not standardized)
   else if (!is_desc && regex_search(text, whats, expr4)) {
      // detected by the OV parser without evaluating the conceal code
      //vps_data.m_page_end_time = string(whats[3].first, whats[3].second);
      //if (opt_debug) printf("VPS(pseudo) page end time: %s\n", vps_data.m_page_end_time);
   }
   // "VPS" marker string
   else if (regex_search(text, whats, expr5)) {
      for (int idx = whats[1].length(); idx < whats[1].length() + whats[2].length(); idx++)
         text[idx] = ' ';
   }
   else {
      if (opt_debug) printf("VPS label unrecognized in line \"%s\"\n", text.c_str());

      // replace all concealed text with blanks (may also be non-VPS related, e.g. HR3: "Un", "Ra" - who knows what this means to tell us)
      // FIXME text can also be concealed by setting fg := bg (e.g. on desc pages of MDR)
      static const regex expr6("^(.*)(\\x18[^\\x00-\\x07\\x10-\\x17]*)");
      if (regex_search(text, whats, expr6)) {
         for (int idx = whats[1].length(); idx < whats[1].length() + whats[2].length(); idx++)
            text[idx] = ' ';
      }
   }
}

/* ------------------------------------------------------------------------------
 * Retrieve programme entries from an overview page
 * - the layout has already been determined in advance, i.e. we assume that we
 *   have a tables with strict columns for times and titles; rows that don't
 *   have the expected format are skipped (normally only header & footer)
 */
void ParseOvList(vector<TV_SLOT>& Slots, int page, int sub, int foot_line,
                 const T_OV_FMT_STAT& pgfmt, T_PG_DATE& pgdate)
{
   bool is_tip = false;
   int hour = -1;
   int min = -1;
   string title;
   smatch whats;
   bool new_title = false;
   bool in_title = false;
   TV_SLOT slot;
   T_VPS_TIME vps_data;

   const vt_page& pgctrl = PageCtrl[page | (sub << 12)];
   vps_data.m_new_vps_date = false;

   for (int line = 1; line <= 23; line++) {
      // note: use text including control-characters, because the next 2 steps require these
      string ctrl(pgctrl.m_line[line], VT_PKG_RAW_LEN);

      if (line >= 23) {
         RemoveTrailingPageFooter(ctrl);
      }
      // extract and remove VPS labels
      // (labels not asigned here since we don't know yet if a new title starts in this line)
      vps_data.m_new_vps_time = false;
      static const regex expr1("[\\x05\\x18]+ *[^\\x00-\\x20]");
      if (regex_search(ctrl, whats, expr1)) {
         ParseVpsLabel(ctrl, vps_data, false);
      }

      // remove remaining control characters
      string text(ctrl, 0, VT_PKG_RAW_LEN);
      for (int idx = 0; idx < VT_PKG_RAW_LEN; idx++) {
         unsigned char c = text[idx];
         if ((c <= 0x1F) || (c == 0x7F))
            text[idx] = ' ';
      }

      new_title = false;
      if (pgfmt.vps_off >= 0) {
         // TODO: Phoenix wraps titles into 2nd line, appends subtitle with "-"
         // m#^ {0,2}(\d\d)[\.\:](\d\d)( {1,3}(\d{4}))? +#
         static const regex expr2("^ *([\\*!] +)?$");
         string str2(text, 0, pgfmt.time_off);
         if (regex_search(str2, whats, expr2)) {
            is_tip = whats[1].matched;
            // note: VPS time has already been extracted above, so the area is blank
            static const regex expr3("^(\\d\\d)[\\.:](\\d\\d) +$");
            string str3(text, pgfmt.time_off, pgfmt.title_off - pgfmt.time_off);
            if (regex_search(str3, whats, expr3)) {
               hour = atoi_substr(whats[1]);
               min = atoi_substr(whats[2]);
               title = string(ctrl, pgfmt.title_off, VT_PKG_RAW_LEN - pgfmt.title_off);
               new_title = true;
            }
         }
      }
      else {
         // m#^( {0,5}| {0,3}\! {1,3})(\d\d)[\.\:](\d\d) +#
         static const regex expr5("^ *([\\*!] +)?$");
         string str5(text, 0, pgfmt.time_off);
         if (regex_search(str5, whats, expr5)) {
            is_tip = whats[1].matched;
            static const regex expr6("^(\\d\\d)[\\.:](\\d\\d) +$");
            string str6(text, pgfmt.time_off, pgfmt.title_off - pgfmt.time_off);
            if (regex_search(str6, whats, expr6)) {
               hour = atoi_substr(whats[1]);
               min = atoi_substr(whats[2]);
               new_title = true;
               title = string(ctrl, pgfmt.title_off, VT_PKG_RAW_LEN - pgfmt.title_off);
            }
         }
      }

      if (new_title) {
         // remember end of page header for date parser
         if (pgdate.m_head_end < 0)
            pgdate.m_head_end = line;

         // push previous title
         if (slot.is_valid())
            Slots.push_back(slot);
         slot.clear();

         slot.m_hour = hour;
         slot.m_min = min;
         slot.m_is_tip = is_tip;
         if (vps_data.m_new_vps_time) {
            slot.m_vps_time = vps_data.m_vps_time;
         }
         if (vps_data.m_new_vps_date) {
            slot.m_vps_date = vps_data.m_vps_date;
            vps_data.m_new_vps_date = false;
         }
         slot.m_vps_cni = vps_data.m_vps_cni;

         ParseTrailingFeat(title, slot);
         if (opt_debug) printf("OV TITLE: \"%s\", %02d:%02d\n", title.c_str(), slot.m_hour, slot.m_min);

         // kika special: subtitle appended to title
         static const regex expr7("(.*\\(\\d+( *[\\&\\-] *\\d+)?\\))\\/ *(\\S.*)");
         if (regex_search(title, whats, expr7)) {
            slot.m_subtitle = string(whats[3].first, whats[3].second);
            title = string(whats[1].first, whats[1].second);  // invalidates "whats"
            slot.m_subtitle = regex_replace(slot.m_subtitle, regex(" {2,}"), " ");
         }
         slot.m_title = title;
         in_title = true;
         //printf("ADD  %02d.%02d.%d %02d:%02d %s\n", slot.m_mday, slot.m_month, slot.m_year, slot.m_hour, slot.m_min, slot.m_title.c_str());
      }
      else if (in_title) {

         // stop appending subtitles before page footer ads
         if (line > foot_line)
            break;

         if (vps_data.m_new_vps_time)
            slot.m_vps_time = vps_data.m_vps_time;

         // check if last line specifies and end time
         // (usually last line of page)
         // TODO internationalize "bis", "Uhr"
         static const regex expr8("^ *(\\(?bis |ab |\\- ?)(\\d{1,2})[\\.:](\\d{2})"
                                  "( Uhr| ?h)( |\\)|$)");
         static const regex expr9("^( *)(\\d\\d)[\\.:](\\d\\d) (oo)? *$");
         if (   regex_search(text, whats, expr8)   // ARD,SWR,BR-alpha
             || regex_search(text, whats, expr9) ) // arte, RTL
         {
            slot.m_end_hour = atoi_substr(whats[2]);
            slot.m_end_min = atoi_substr(whats[3]);
            new_title = true;
            if (opt_debug) printf("Overview end time: %02d:%02d\n", slot.m_end_hour, slot.m_end_min);
         }
         // check if we're still in a continuation of the previous title
         // time column must be empty (possible VPS labels were already removed above)
         else if (pgfmt.subt_off >= 0) {
            static const regex expr10("^[ \\x00-\\x07\\x10-\\x17]*$");
            static const regex expr11("^[ \\x00-\\x07\\x10-\\x17]*$");
            if (   !regex_search(text.substr(0, pgfmt.subt_off), whats, expr10)
                || regex_search(text.substr(pgfmt.subt_off), whats, expr11) )
            {
               new_title = true;
            }
         }

         if (new_title == false) {
            string subt = ctrl;
            ParseTrailingFeat(subt, slot);
            if (!subt.empty()) {
               // combine words separated by line end with "-"
               static const regex expr12("\\S-$");
               static const regex expr13("^[[:lower:]]");
               if (   (slot.m_subtitle.size() == 0)
                   && regex_search(slot.m_title, whats, expr12)
                   && regex_search(subt, whats, expr13) )
               {
                  if (slot.m_title[slot.m_title.size() - 1] == '-')
                     slot.m_title.erase(slot.m_title.end() - 1);
                  slot.m_title += subt;
               }
               else {
                  if (   !slot.m_subtitle.empty()
                      && regex_search(slot.m_subtitle, whats, expr12)
                      && regex_search(subt, whats, expr13) )
                  {
                     if (slot.m_subtitle[slot.m_subtitle.size() - 1] == '-')
                        slot.m_subtitle.erase(slot.m_subtitle.end() - 1);
                     slot.m_subtitle += subt;
                  }
                  else if (!slot.m_subtitle.empty()) {
                     slot.m_subtitle += " ";
                     slot.m_subtitle += subt;
                  }
                  else {
                     slot.m_subtitle = subt;
                  }
               }
            }
            if (vps_data.m_new_vps_date) {
               slot.m_vps_date = vps_data.m_vps_date;
               vps_data.m_new_vps_date = false;
            }
         }
         else {
            if (slot.is_valid())
               Slots.push_back(slot);
            slot.clear();
            in_title = false;
         }
      }
   }
   if (slot.is_valid())
      Slots.push_back(slot);
}

/* ------------------------------------------------------------------------------
 * Check if an overview page has exactly the same title list as the predecessor
 */
bool CheckRedundantSubpage(const vector<TV_SLOT>& Slots,
                           int prev_slot_idx, uint cur_slot_idx)
{
   bool result = false;

   if (Slots.size() - cur_slot_idx == cur_slot_idx - prev_slot_idx) {
      result = true;
      for (uint idx = 0; idx < cur_slot_idx - prev_slot_idx; idx++) {
         if (Slots[prev_slot_idx + idx].m_start_t
                != Slots[cur_slot_idx + idx].m_start_t) {

            // TODO compare title
            result = false;
            break;
         }
      }
   }
   return result;
}

/* ------------------------------------------------------------------------------
 * Determine stop times
 * - assuming that in overview tables the stop time is equal to the start of the
 *   following programme & that this also holds true inbetween adjacent pages
 * - if in doubt, leave it undefined (this is allowed in XMLTV)
 * - TODO: restart at non-adjacent text pages
 */
void CalculateStopTimes(vector<TV_SLOT>& Slots, uint start_idx)
{
   for (uint idx = start_idx; idx < Slots.size(); idx++)
   {
      TV_SLOT& slot = Slots[idx];

      if (slot.m_stop_t < 0) {
         if (slot.m_end_min >= 0) {
            // there was an end time in the overview or a description page -> use that
            int date_off = slot.m_date_off;

            // check for a day break between start and end
            if ( (slot.m_end_hour < slot.m_hour) ||
                 ( (slot.m_end_hour == slot.m_hour) &&
                   (slot.m_end_min < slot.m_min) ))
            {
               date_off += 1;
            }

            struct tm tm;
            memset(&tm, 0, sizeof(tm));

            tm.tm_hour = slot.m_end_hour;
            tm.tm_min  = slot.m_end_min;
            tm.tm_mday = slot.m_mday + date_off;
            tm.tm_mon  = slot.m_month - 1;
            tm.tm_year = slot.m_year - 1900;
            tm.tm_isdst = -1;

            slot.m_stop_t = mktime(&tm);
         }
         else if (idx + 1 < Slots.size())
         {
            // no end time: use start time of the following programme if less than 9h away
            const TV_SLOT& next = Slots[idx + 1];

            if ( (next.m_start_t > slot.m_start_t) &&
                 (next.m_start_t - slot.m_start_t < 9*60*60) )
            {
               slot.m_stop_t = next.m_start_t;
            }
         }
      }
   }
}

/* ------------------------------------------------------------------------------
 * Convert discrete start times into UNIX epoch format
 * - implies a conversion from local time zone into GMT
 */
time_t ConvertStartTime(const TV_SLOT& slot)
{
   struct tm tm;

   memset(&tm, 0, sizeof(tm));

   tm.tm_hour = slot.m_hour;
   tm.tm_min  = slot.m_min;
   tm.tm_mday = slot.m_mday + slot.m_date_off;
   tm.tm_mon  = slot.m_month - 1;
   tm.tm_year = slot.m_year - 1900;
   tm.tm_isdst = -1;

   return mktime(&tm);
}

/* ------------------------------------------------------------------------------
 * Calculate delta in days between the given programme slot and a discrete date
 * - works both on program "slot" and "pgdate" which have the same member names
 *   (since Perl doesn't have strict typechecking for structs)
 */
int CalculateDateDelta(const T_PG_DATE& slot1, const T_PG_DATE& slot2)
{
   struct tm tm1;
   struct tm tm2;

   memset(&tm1, 0, sizeof(tm1));
   memset(&tm2, 0, sizeof(tm2));

   tm1.tm_mday = slot1.m_mday + slot1.m_date_off;
   tm1.tm_mon  = slot1.m_month - 1;
   tm1.tm_year = slot1.m_year - 1900;
   tm1.tm_isdst = -1;

   tm2.tm_mday = slot2.m_mday + slot2.m_date_off;
   tm2.tm_mon  = slot2.m_month - 1;
   tm2.tm_year = slot2.m_year - 1900;
   tm2.tm_isdst = -1;

   time_t date1 = mktime(&tm1);
   time_t date2 = mktime(&tm2);

   // add 2 hours to allow for shorter days during daylight saving time change
   return (date2 + 2*60*60 - date1) / (24*60*60);
}

/* ------------------------------------------------------------------------------
 * Calculate number of page following the previous one
 * - basically that's a trivial increment by 1, however page numbers are
 *   actually hexedecinal numbers, where the numbers with non-decimal digits
 *   are skipped (i.e. not intended for display)
 * - hence to get from 9 to 0 in the 2nd and 3rd digit we have to add 7
 *   instead of just 1.
 */
int GetNextPageNumber(int page)
{
   int next;

   if ((page & 0xF) == 9) {
      next = page + (0x010 - 0x009);
   }
   else {
      next = page + 1;
   }
   // same skip for the 2nd digit, if there was a wrap in the 3rd digit
   if ((next & 0xF0) == 0x0A0) {
      next = next + (0x100 - 0x0A0);
   }
   return next;
}

/* ------------------------------------------------------------------------------
 * Check if two given teletext pages are adjacent
 * - both page numbers must have decimal digits only (i.e. match /[1-8][1-9][1-9]/)
 */
bool CheckIfPagesAdjacent(const T_PG_DATE& pg1, const T_PG_DATE& pg2)
{
   bool result = false;

   if ( (pg1.m_page == pg2.m_page) &&
        (pg1.m_sub_page + pg1.m_sub_page_skip + 1 == pg2.m_sub_page) ) {
      // next sub-page on same page
      result = true;
   }
   else {
      // check for jump from last sub-page of prev page to first of new page
      int next = GetNextPageNumber(pg1.m_page);

      if ( (next == pg2.m_page) &&
           (pg1.m_sub_page + pg1.m_sub_page_skip == PgSub[pg1.m_page]) &&
           (pg2.m_sub_page <= 1) ) {
         result = true;
      } 
   }
   return result;
}

/* ------------------------------------------------------------------------------
 * Determine LTO for a given time and date
 * - LTO depends on the local time zone and daylight saving time being in effect or not
 */
int DetermineLto(time_t whence)
{
   // LTO := Difference between GMT and local time
   struct tm * ptm = gmtime(&whence);

   // for this to work it's mandatory to pass is_dst=-1 to mktime
   ptm->tm_isdst = -1;

   time_t gmt = mktime(ptm);

   return (gmt != -1) ? (whence - gmt) : 0;
}

/* ------------------------------------------------------------------------------
 * Determine dates of programmes on an overview page
 * - TODO: arte: remove overlapping: the first one encloses multiple following programmes; possibly recognizable by the VP label 2500
 *  "\x1814.40\x05\x182500\x07\x07THEMA: DAS \"NEUE GROSSE   ",
 *  "              SPIEL\"                    ",
 *  "\x0714.40\x05\x051445\x02\x02USBEKISTAN - ABWEHR DER   ",
 *  "            \x02\x02WAHABITEN  (2K)           ",
 */
bool CalculateDates(T_PG_DATE& pgdate, const T_PG_DATE& prev_pgdate,
                    vector<TV_SLOT>& Slots, int prev_slot_idx, int cur_slot_idx)
{
   // note: caller's prev_pgdate must not be invalidated here, hence use a separate flag
   bool use_prev_pgdate = prev_pgdate.is_valid();
   bool result = false;

   int date_off = 0;
   if (use_prev_pgdate) {
      const TV_SLOT& prev_slot1 = Slots[prev_slot_idx];
      const TV_SLOT& prev_slot2 = Slots[cur_slot_idx - 1];
      // check if the page number of the previous page is adjacent
      // and as consistency check require that the prev page covers less than a day
      if (   (prev_slot2.m_start_t - prev_slot1.m_start_t < 22*60*60)
          && CheckIfPagesAdjacent(prev_pgdate, pgdate) ) {

         int prev_delta = 0;
         // check if there's a date on the current page
         // if yes, get the delta to the previous one in days (e.g. tomorrow - today = 1)
         // if not, silently fall back to the previous page's date and assume date delta zero
         if (pgdate.m_year != -1) {
            prev_delta = CalculateDateDelta(prev_pgdate, pgdate);
         }
         if (prev_delta == 0) {
            // check if our start date should be different from the one of the previous page
            // -> check if we're starting on a new date (smaller hour)
            // (note: comparing with the 1st slot of the prev. page, not the last!)
            if (Slots[cur_slot_idx].m_hour < prev_slot1.m_hour) {
               // TODO: check continuity to slot2: gap may have 6h max.
               // TODO: check end hour
               date_off = 1;
            }
            // else: same date as the last programme on the prev page
            // may have been a date change inside the prev. page but our header date may still be unchanged
            // historically this is only done up to 6 o'clock (i.e. 0:00 to 6:00 counts as the old day)
            //else if (Slots[cur_slot_idx].m_hour <= 6) {
            //   # check continuity
            //   if ( defined(prev_slot2.m_end_hour) ?
            //          ( (Slots[cur_slot_idx].m_hour >= prev_slot2.m_end_hour) &&
            //            (Slots[cur_slot_idx].m_hour - prev_slot2.m_end_hour <= 1) ) :
            //          ( (Slots[cur_slot_idx].m_hour >= prev_slot2.m_hour) &&
            //            (Slots[cur_slot_idx].m_hour - prev_slot2.m_hour <= 6) ) ) {
            //      # OK
            //   } else {
            //      # 
            //   }
            //}
            // TODO: check for date change within the previous page
         }
         else {
            if (opt_debug) printf("OV DATE: prev page %03X.%04X date cleared\n", prev_pgdate.m_page, prev_pgdate.m_sub_page);
            use_prev_pgdate = false;
         }
         // TODO: date may also be wrong by +1 (e.g. when starting at 23:55 with date for 00:00)
      }
      else {
         // not adjacent -> disregard the info
         if (opt_debug) printf("OV DATE: prev page %03X.%04X not adjacent - not used for date check\n", prev_pgdate.m_page, prev_pgdate.m_sub_page);
         use_prev_pgdate = false;
      }
   }

   int mday = -1;
   int month = -1;
   int year = -1;

   if (pgdate.m_year != -1) {
      mday = pgdate.m_mday;
      month = pgdate.m_month;
      year = pgdate.m_year;
      // store date offset in page meta data (to calculate delta of subsequent pages)
      pgdate.m_date_off = date_off;
      if (opt_debug) printf("OV DATE: using page header date %d.%d.%d, DELTA %d\n", mday, month, year, date_off);
   }
   else if (use_prev_pgdate) {
      // copy date from previous page
      pgdate.m_mday = mday = prev_pgdate.m_mday;
      pgdate.m_month = month = prev_pgdate.m_month;
      pgdate.m_year = year = prev_pgdate.m_year;
      // add date offset if a date change was detected
      date_off += prev_pgdate.m_date_off;
      pgdate.m_date_off = date_off;

      if (opt_debug) printf("OV DATE: using predecessor date %d.%d.%d, DELTA %d\n", mday, month, year, date_off);
   }

   // finally assign the date to all programmes on this page (with auto-increment at hour wraps)
   if (year != -1) {
      int prev_hour = -1;
      for (uint idx = cur_slot_idx; idx < Slots.size(); idx++) {
         TV_SLOT& slot = Slots[idx];

         // detect date change (hour wrap at midnight)
         if ((prev_hour != -1) && (prev_hour > slot.m_hour)) {
            date_off += 1;
         }
         prev_hour = slot.m_hour;

         slot.m_date_off  = date_off;
         slot.m_mday      = pgdate.m_mday;
         slot.m_month     = pgdate.m_month;
         slot.m_year      = pgdate.m_year;

         slot.m_start_t   = ConvertStartTime(slot);
      }
      result = true;
   }
   else {
      if (opt_debug) printf("OV missing date - discarding programmes\n");
   }
   return result;
}

/* ------------------------------------------------------------------------------
 * Parse programme overview table
 * - 1: retrieve programme list (i.e. start times and titles)
 * - 2: retrieve date from the page header
 * - 3: determine dates
 * - 4: determine stop times
 */
void ParseOv(int page, int sub, const T_OV_FMT_STAT& fmt,
             T_PG_DATE& pgdate, T_PG_DATE& prev_pgdate,
             int prev_slot_idx, vector<TV_SLOT>& Slots)
{
   if (opt_debug) printf("OVERVIEW PAGE %03X.%04X\n", page, sub);

   PageToLatin(page, sub);

   int foot = ParseFooterByColor(page, sub);
   uint start_idx = Slots.size();

   ParseOvList(Slots, page, sub, foot, fmt, pgdate);

   if (Slots.size() > start_idx) {
      ParseOvHeaderDate(page, sub, pgdate);

      if (CalculateDates(pgdate, prev_pgdate, Slots, prev_slot_idx, start_idx)) {

         // check if this subpage has any new content
         // (some networks use subpages just to display different ads)
         if (   (sub > 1) && prev_pgdate.is_valid()
             && (prev_pgdate.m_page == page)
             && (prev_pgdate.m_sub_page + prev_pgdate.m_sub_page_skip + 1 == sub)
             && CheckRedundantSubpage(Slots, prev_slot_idx, start_idx) )
         {
            // redundant -> discard content of this page
            if (opt_debug) printf("OV dropping redundant sub-page %03X.%d\n", page, sub);
            prev_pgdate.m_sub_page_skip += 1;
            Slots.erase(Slots.begin() + start_idx, Slots.end());
         }
         else
         {
            // guess missing stop time for last slot of the previous page
            if (   prev_pgdate.is_valid()
                && (start_idx > 0)
                && CheckIfPagesAdjacent(prev_pgdate, pgdate) )
            {
               start_idx -= 1;
            }

            // guess missing stop times for the current page
            CalculateStopTimes(Slots, start_idx);
         }
      }
      else {
         // failed to determine the date
         Slots.erase(Slots.begin() + start_idx, Slots.end());
      }
   }
}

/* ------------------------------------------------------------------------------
 * Retrieve programme data from an overview page
 * - 1: compare several overview pages to identify the layout
 * - 2: parse all overview pages, retrieving titles and ttx references
 */
vector<TV_SLOT> ParseAllOvPages()
{
   vector<TV_SLOT> Slots;

   T_OV_FMT_STAT fmt = DetectOvFormat();
   if (fmt.is_valid()) {
      T_PG_DATE pgdate;
      T_PG_DATE prev_pgdate;
      int prev_slot_idx = -1;

      for (map<int,int>::iterator p = PgCnt.begin(); p != PgCnt.end(); p++) {
         if ((p->first >= opt_tv_start) && (p->first <= opt_tv_end)) {
            int page = p->first;
            int max_sub = PgSub[page];
            int sub1, sub2;
            if (max_sub == 0) {
               sub1 = sub2 = 0;
            }
            else {
               sub1 = 1;
               sub2 = max_sub;
            }
            for (int sub = sub1; sub <= sub2; sub++) {
               uint new_prev_slot_idx = Slots.size();
               if (TtxSubPageDefined(page, sub)) {
                  pgdate.clear();
                  pgdate.m_page = page;
                  pgdate.m_sub_page = sub;
                  pgdate.m_sub_page_skip = 0;

                  ParseOv(page, sub, fmt, pgdate, prev_pgdate, prev_slot_idx, Slots);
               }

               if (Slots.size() > new_prev_slot_idx) {
                  prev_pgdate = pgdate;
                  prev_slot_idx = new_prev_slot_idx;
               }
               else if ( prev_pgdate.is_valid() &&
                         (prev_pgdate.m_sub_page + prev_pgdate.m_sub_page_skip < sub) ) {
                  prev_pgdate.clear();
                  prev_slot_idx = -1;
               }
            }
         }
      }
   }

   return Slots;
}

/* ------------------------------------------------------------------------------
 * Filter out expired programmes
 * - the stop time is relevant for expiry (and this really makes a difference if
 *   the expiry time is low (e.g. 6 hours) since there may be programmes exceeding
 *   it and we certainly shouldn't discard programmes which are still running
 * - resulting problem: we don't always have the stop time
 */
vector<TV_SLOT> FilterExpiredSlots(const vector<TV_SLOT>& Slots)
{
   time_t exp_thresh = time(NULL) - opt_expire * 60;
   vector<TV_SLOT> NewSlots;

   NewSlots.reserve(Slots.size());

   for (uint idx = 0; idx < Slots.size(); idx++)
   {
      if (   (   (Slots[idx].m_stop_t != -1)
              && (Slots[idx].m_stop_t >= exp_thresh))
          || (Slots[idx].m_start_t + 120*60 >= exp_thresh) )
      {
         NewSlots.push_back(Slots[idx]);
      }
      else {
        if (opt_debug) printf("EXPIRED new %ld-%ld < %ld '%s'\n", (long)Slots[idx].m_start_t,
                              (long)Slots[idx].m_stop_t, (long)exp_thresh, Slots[idx].m_title.c_str());
      }
   }
   return NewSlots;
}

/* ------------------------------------------------------------------------------
 * Convert a UNIX epoch timestamp into XMLTV format
 */
string GetXmlTimestamp(time_t whence)
{
   // get GMT in broken-down format
   struct tm * ptm = gmtime(&whence);

   char buf[100];
   strftime(buf, sizeof(buf), "%Y%m%d%H%M%S +0000", ptm);

   return string(buf);
}

string GetXmlVpsTimestamp(const TV_SLOT& slot)
{
   int lto = DetermineLto(slot.m_start_t);
   int m, d, y;
   char date_str[20];
   char tz_str[20];

   if (slot.m_vps_date.length() > 0) {
      // VPS data was specified -> reformat only
      if (sscanf(slot.m_vps_date.c_str(), "%2u%2u%2u", &d, &m, &y) != 3)
         assert(false);  // parser failure
      sprintf(date_str, "%04d%02d%02d", y + 2000, m, d);
   }
   else {
      // no VPS date given -> derive from real start time/date
      struct tm * ptm = localtime(&slot.m_start_t);
      sprintf(date_str, "%04d%02d%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
   }
   // get time zone (format +HHHMM)
   sprintf(tz_str, "00 %+03d%02d", lto / (60*60), abs(lto / 60) % 60);

   return string(date_str) + slot.m_vps_time + tz_str;
}

/* ------------------------------------------------------------------------------
 * Convert a text string for inclusion in XML as CDATA
 * - the result is not suitable for attributes since quotes are not escaped
 * - input must already have teletext control characters removed
 */
#if 0
class T_RESUB_XML
{
public:
   const string operator() (const smatch& whats) const {
      switch (*whats[0].first)
      {
         case '&': return string("&amp;");
         case '<': return string("&lt;");
         case '>': return string("&gt;");
         default: assert(false); return string("");
      }
   }
};
#endif
void Latin1ToXml(string& title)
{
   //static const regex expr1("[&<>]");
   //title = regex_replace(title, expr1, T_RESUB_XML());

   // note: & replacement must be first
   title = regex_replace(title, regex("&"), "&amp;");
   title = regex_replace(title, regex("<"), "&lt;");
   title = regex_replace(title, regex(">"), "&gt;");
}

void XmlToLatin1(string& title)
{
   title = regex_replace(title, regex("&gt;"), ">");
   title = regex_replace(title, regex("&lt;"), "<");
   title = regex_replace(title, regex("&amp;"), "&");
}

/* ------------------------------------------------------------------------------
 * Export programme data into XMLTV format
 */
void ExportTitle(FILE * fp, const TV_SLOT& slot, const string& ch_id)
{
   if ((slot.m_title.length() > 0) &&
       (slot.m_mday != -1) &&
       (slot.m_month != -1) &&
       (slot.m_year != -1) &&
       (slot.m_hour != -1) &&
       (slot.m_min != -1))
   {
      string start_str = GetXmlTimestamp(slot.m_start_t);
      string stop_str;
      string vps_str;
      if (slot.m_stop_t != -1) {
         stop_str = string(" stop=\"") + GetXmlTimestamp(slot.m_stop_t) + string("\"");
      }
      if (slot.m_vps_time.length() > 0) {
         vps_str = string(" pdc-start=\"") + GetXmlVpsTimestamp(slot) + string("\"");
      }
      string title = slot.m_title;
      // convert all-upper-case titles to lower-case
      // (unless only one consecutive letter, e.g. movie "K2" or "W.I.T.C.H.")
      // TODO move this into separate post-processing stage; make statistics for all titles first
      static const regex expr1("[[:lower:]]");
      static const regex expr2("[[:upper:]]{2}");
      smatch whats;

      if (   !regex_search(title, whats, expr1)
          && regex_search(title, whats, expr2)) {
         for (string::iterator p = title.begin(); p < title.end(); p++) {
            *p = tolower(*p);
         }
      }
      Latin1ToXml(title);

      fprintf(fp, "\n<programme start=\"%s\"%s%s channel=\"%s\">\n"
                  "\t<title>%s</title>\n",
                  start_str.c_str(), stop_str.c_str(), vps_str.c_str(), ch_id.c_str(),
                  title.c_str());

      if (slot.m_subtitle.length() > 0) {
         // repeat the title because often the subtitle is actually just the 2nd part
         // of an overlong title (there's no way to distinguish this from series subtitles etc.)
         // TODO: some channel distinguish title from subtitle by making title all-caps (e.g. TV5)
         string subtitle = slot.m_title + string(" ") +  slot.m_subtitle;
         Latin1ToXml(subtitle);
         fprintf(fp, "\t<sub-title>%s</sub-title>\n", subtitle.c_str());
      }
      if (slot.m_ttx_ref != -1) {
         if (slot.m_desc.length() > 0) {
            fprintf(fp, "\t<!-- TTX %03X -->\n", slot.m_ttx_ref);
            string desc = slot.m_desc;
            Latin1ToXml(desc);
            fprintf(fp, "\t<desc>%s</desc>\n", desc.c_str());
         }
         else {
            // page not captured or no matching date/title found
            fprintf(fp, "\t<desc>(teletext %03X)</desc>\n", slot.m_ttx_ref);
         }
      }
      // video
      if (   slot.m_is_video_bw
          || slot.m_is_aspect_16_9
          || slot.m_is_video_hd) {
         fprintf(fp, "\t<video>\n");
         if (slot.m_is_video_bw) {
            fprintf(fp, "\t\t<colour>no</colour>\n");
         }
         if (slot.m_is_aspect_16_9) {
            fprintf(fp, "\t\t<aspect>16:9</aspect>\n");
         }
         if (slot.m_is_video_hd) {
            fprintf(fp, "\t\t<quality>HDTV</quality>\n");
         }
         fprintf(fp, "\t</video>\n");
      }
      // audio
      if (slot.m_is_dolby) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>surround</stereo>\n\t</audio>\n");
      }
      else if (slot.m_is_stereo) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>stereo</stereo>\n\t</audio>\n");
      }
      else if (slot.m_is_mono) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>mono</stereo>\n\t</audio>\n");
      }
      else if (slot.m_is_2chan) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>bilingual</stereo>\n\t</audio>\n");
      }
      // subtitles
      if (slot.m_is_omu) {
         fprintf(fp, "\t<subtitles type=\"onscreen\"/>\n");
      }
      else if (slot.m_has_subtitles) {
         fprintf(fp, "\t<subtitles type=\"teletext\"/>\n");
      }
      // tip/highlight (ARD only)
      if (slot.m_is_tip) {
         fprintf(fp, "\t<star-rating>\n\t\t<value>1/1</value>\n\t</star-rating>\n");
      }
      fprintf(fp, "</programme>\n");
   }
   else {
      if (opt_debug) printf("SKIPPING PARTIAL %s %02d:%02d\n", slot.m_title.c_str(), slot.m_hour, slot.m_min);
   }
}

/* ------------------------------------------------------------------------------
 * Parse description pages
 *

 *ARD:
 *                          Mi 12.04.06 Das Erste 
 *                                                
 *         11.15 - 12.00 Uhr                      
 *         In aller Freundschaft           16:9/UT
 *         Ehen auf dem Prüfstand                 

 *ZDF:
 *  ZDFtext          ZDF.aspekte
 *  Programm  Freitag, 21.April   [no time!]

 *MDR:
 *Mittwoch, 12.04.2006, 14.30-15.30 Uhr  
 *                                        
 * LexiTV - Wissen für alle               
 * H - wie Hühner                        

 *3sat:
 *      Programm         Heute            
 *      Mittwoch, 12. April               
 *                         1D107 120406 3B
 *  07.00 - 07.30 Uhr            VPS 0700 
 *  nano Wh.                              

 * Date format:
 * ARD:   Fr 14.04.06 // 15.40 - 19.15 Uhr
 * ZDF:   Freitag, 14.April // 15.35 - 19.00 Uhr
 * Sat.1: Freitag 14.04. // 16:00-17:00
 * RTL:   Fr[.] 14.04. 13:30 Uhr
 * Pro7:  So 16.04 06:32-07:54 (note: date not on 2/2, only title rep.)
 * RTL2:  Freitag, 14.04.06; 12.05 Uhr (at bottom) (1/6+2/6 for movie)
 * VOX:   Fr 18:00 Uhr
 * MTV:   Fr 14.04 18:00-18:30 // Sa 15.04 18:00-18:30
 * VIVA:  Mo - Fr, 20:30 - 21:00 Uhr // Mo - Fr, 14:30 - 15:00 Uhr (also w/o date)
 *        Mo 21h; Mi 17h (Wh); So 15h
 *        Di, 18:30 - 19:00 h
 *        Sa. 15.4. 17:30h
 * arte:  Fr 14.04. 00.40 [VPS  0035] bis 20.40 (double-height)
 * 3sat:  Freitag, 14. April // 06.20 - 07.00 Uhr
 * CNN:   Sunday 23 April, 13:00 & 19:30 CET
 *        Saturday 22, Sunday 23 April
 *        daily at 11:00 CET
 * MDR:   Sonnabend, 15.04.06 // 00.00 - 02.55 Uhr
 * KiKa:  14. April 2006 // 06.40-07.05 Uhr
 * SWR:   14. April, 00.55 - 02.50 Uhr
 * WDR:   Freitag, 14.04.06   13.40-14.40
 * BR3:   Freitag, 14.04.06 // 13.30-15.05
 *        12.1., 19.45 
 * HR3:   Montag 8.45-9.15 Uhr
 *        Montag // 9.15-9.45 (i.e. 2-line format)
 * Kabl1: Fr 14.04 20:15-21:11
 * Tele5: Fr 19:15-20:15
 */

bool ParseDescDate(int page, int sub, TV_SLOT& slot)
{
   int lmday = -1;
   int lmonth = -1;
   int lyear = -1;
   int lhour = -1;
   int lmin = -1;
   int lend_hour = -1;
   int lend_min = -1;
   bool check_time = false;

   const vt_page& pgtext = PageText[page | (sub << 12)];
   int lang = PgLang[page];
   string wday_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_FULL);
   string wday_abbrv_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_ABBRV);
   string mname_match = GetDateNameRegExp(MonthNames, lang, DATE_NAME_ANY);
   cmatch what;

   // search date and time
   for (int line = 1; line <= 23; line++) {
      const char * text = pgtext.m_line[line];
      bool new_date = false;

      {
         // [Fr] 14.04.[06] (year not optional for single-digit day/month)
         static const regex expr1("(^| )(\\d{1,2})\\.(\\d{1,2})\\.(\\d{4}|\\d{2})?( |,|;|:|$)");
         static const regex expr2("(^| )(\\d{1,2})\\.(\\d)\\.(\\d{4}|\\d{2})( |,|;|:|$)");
         static const regex expr3("(^| )(\\d{1,2})\\.(\\d)\\.?(\\d{4}|\\d{2})?(,|;| ?-)? +\\d{1,2}[\\.:]\\d{2}(h| |,|;|:|$)");
         if (   regex_search(text, what, expr1)
             || regex_search(text, what, expr2)
             || regex_search(text, what, expr3)) {
            if (CheckDate(atoi_substr(what[2]), atoi_substr(what[3]), atoi_substr(what[4]),
                          "", "", &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // Fr 14.04 (i.e. no dot after month)
         // here's a risk to match on a time, so we must require a weekday name
         static regex expr4[8];
         static regex expr5[8];
         if (expr4[lang].empty()) {
            expr4[lang].assign(string("(^| )(") + wday_match + ")(, ?| | - )"
                               "(\\d{1,2})\\.(\\d{1,2})( |\\.|,|;|$)", regex::icase);
            expr5[lang].assign(string("(^| )(") + wday_abbrv_match + ")(\\.,? ?|, ?| - | )"
                               "(\\d{1,2})\\.(\\d{1,2})( |\\.|,|;|:|$)", regex::icase);
         }
         if (   regex_search(text, what, expr4[lang])
             || regex_search(text, what, expr5[lang])) {
            if (CheckDate(atoi_substr(what[4]), atoi_substr(what[5]), -1,
                          string(what[2]), "", &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // 14.[ ]April [2006]
         static regex expr6[8];
         if (expr6[lang].empty()) {
            expr6[lang].assign(string("(^| )(\\d{1,2})\\.? ?(") + mname_match +
                               ")( (\\d{4}|\\d{2}))?( |,|;|:|$)", regex::icase);
         }
         if (regex_search(text, what, expr6[lang])) {
            if (CheckDate(atoi_substr(what[2]), -1, atoi_substr(what[5]),
                          "", string(what[3]), &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // Sunday 22 April (i.e. no dot after day)
         static regex expr7[8];
         static regex expr8[8];
         if (expr7[lang].empty()) {
            expr7[lang].assign(string("(^| )(") + wday_match +
                               string(")(, ?| - | )(\\d{1,2})\\.? ?(") + mname_match +
                               ")( (\\d{4}|\\d{2}))?( |,|;|:|$)", regex::icase);
            expr8[lang].assign(string("(^| )(") + wday_abbrv_match +
                               string(")(\\.,? ?|, ?| ?- ?| )(\\d{1,2})\\.? ?(") + mname_match +
                               ")( (\\d{4}|\\d{2}))?( |,|;|:|$)", regex::icase);
         }
         if (   regex_search(text, what, expr7[lang])
             || regex_search(text, what, expr8[lang])) {
            if (CheckDate(atoi_substr(what[4]), -1, atoi_substr(what[7]),
                          string(what[2]), string(what[5]), &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // TODO: "So, 23." (n-tv)

         // Fr[,] 18:00 [-19:00] [Uhr|h]
         // TODO parse time (i.e. allow omission of "Uhr")
         static regex expr10[8];
         static regex expr11[8];
         if (expr10[lang].empty()) {
            expr10[lang].assign(string("(^| )(") + wday_match +
                                ")(, ?| - | )(\\d{1,2}[\\.:]\\d{2}"
                                "((-| - )\\d{1,2}[\\.:]\\d{2})?(h| |,|;|:|$))", regex::icase);
            expr11[lang].assign(string("(^| )(") + wday_abbrv_match +
                                ")(\\.,? ?|, ?| - | )(\\d{1,2}[\\.:]\\d{2}"
                                "((-| - )\\d{1,2}[\\.:]\\d{2})?(h| |,|;|:|$))", regex::icase);
         }
         if (   regex_search(text, what, expr10[lang])
             || regex_search(text, what, expr11[lang])) {
            if (CheckDate(-1, -1, -1, string(what[2]), "", &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // TODO: 21h (i.e. no minute value: TV5)

         // TODO: make exact match between VPS date and time from overview page
         // TODO: allow garbage before or after label; check reveal and magenta codes (ETS 300 231 ch. 7.3)
         // VPS label "1D102 120406 F9"
         static const regex expr12("^ +[0-9A-F]{2}\\d{3} (\\d{2})(\\d{2})(\\d{2}) [0-9A-F]{2} *$");
         if (regex_search(text, what, expr12)) {
            lmday = atoi_substr(what[1]);
            lmonth = atoi_substr(what[2]);
            lyear = atoi_substr(what[3]) + 2000;
            goto DATE_FOUND;
         }
      }
      DATE_FOUND:

      // TODO: time should be optional if only one subpage
      // 15.40 [VPS 1540] - 19.15 [Uhr|h]
      static const regex expr12("(^| )(\\d{1,2})[\\.:](\\d{1,2})( +VPS +(\\d{4}))?(-| - | bis )(\\d{1,2})[\\.:](\\d{1,2})(h| |,|;|:|$)");
      static const regex expr13("(^| )(\\d{2})h(\\d{2})( +VPS +(\\d{4}))?(-| - )(\\d{2})h(\\d{2})( |,|;|:|$)");
      // 15.40 (Uhr|h)
      static const regex expr14("(^| )(\\d{1,2})[\\.:](\\d{1,2})( |,|;|:|$)");
      static const regex expr15("(^| )(\\d{1,2})[\\.:](\\d{1,2}) *$");
      static const regex expr16("(^| )(\\d{1,2})[\\.:](\\d{1,2})( ?h| Uhr)( |,|;|:|$)");
      static const regex expr17("(^| )(\\d{1,2})h(\\d{2})( |,|;|:|$)");
      check_time = false;
      if (   regex_search(text, what, expr12)
          || regex_search(text, what, expr13)) {
         int hour = atoi_substr(what[2]);
         int min = atoi_substr(what[3]);
         int end_hour = atoi_substr(what[7]);
         int end_min = atoi_substr(what[8]);
         // int vps = atoi_substr(what[5]);
         if (opt_debug) printf("DESC TIME %02d.%02d - %02d.%02d\n", hour, min, end_hour, end_min);
         if ((hour < 24) && (min < 60) &&
             (end_hour < 24) && (end_min < 60)) {
            lhour = hour;
            lmin = min;
            lend_hour = end_hour;
            lend_min = end_min;
            check_time = true;
         }
      }
      // 15.40 (Uhr|h)
      else if (   (new_date && regex_search(text, what, expr14))
               || regex_search(text, what, expr15)
               || regex_search(text, what, expr16)
               || regex_search(text, what, expr17)) {
         int hour = atoi_substr(what[2]);
         int min = atoi_substr(what[3]);
         if (opt_debug) printf("DESC TIME %02d.%02d\n", hour, min);
         if ((hour < 24) && (min < 60)) {
            lhour = hour;
            lmin = min;
            check_time = true;
         }
      }

      if (check_time && (lyear != -1)) {
         // allow slight mismatch of <5min (for ORF2 or TV5: list says 17:03, desc says 17:05)
         struct tm tm;
         memset(&tm, 0, sizeof(struct tm));
         tm.tm_min = lmin;
         tm.tm_hour = lhour;
         tm.tm_mday = lmday;
         tm.tm_mon = lmonth - 1;
         tm.tm_year = lyear - 1900;
         tm.tm_isdst = -1;
         time_t start_t = mktime(&tm);
         if ((start_t != -1) && (abs(start_t - slot.m_start_t) < 5*60)) {
            // match found
            if ((lend_hour != -1) && (slot.m_end_hour != -1)) {
               // add end time to the slot data
               // TODO: also add VPS time
               // XXX FIXME: these are never used because stop_t is already calculated!
               slot.m_end_hour = lend_hour;
               slot.m_end_min = lend_min;
            }
            return true;
         } else {
            if (opt_debug) printf("MISMATCH: %s %s\n", GetXmlTimestamp(start_t).c_str(), GetXmlTimestamp(slot.m_start_t).c_str());
            lend_hour = -1;
            if (new_date) {
               // date on same line as time: invalidate both upon mismatch
               lyear = -1;
            }
         }
      }
   }
   return false;
}

int ParseDescTitle(int page, int sub, TV_SLOT& slot)
{
   const vt_page& pgtext = PageText[page | (sub << 12)];

   regex expr1(string("^ *\\Q") + slot.m_title + "\\E");
   smatch whats;

   // remove and compress whitespace
   string l1 = pgtext.m_line[1];
   l1 = regex_replace(l1, regex("(^ +| +$)"), "");
   l1 = regex_replace(l1, regex("  +"), " ");

   for (int line = 1; line <= 23/2; line++) {
      string l2 = pgtext.m_line[line + 1];
      l2 = regex_replace(l2, regex("(^ +| +$)"), "");
      l2 = regex_replace(l2, regex("  +"), " ");

      //string l = l1 + string(" ") + l2;
      //TODO: join lines if l1 ends with "-" ?
      //TODO: correct title/subtitle separation of overview

      // TODO: one of the compared title strings may be all uppercase (SWR)

      if (regex_search(l1, whats, expr1)) {
         // Done: discard everything above
         // TODO: back up if there's text in the previous line with the same indentation
         return line;
      }
      l1 = l2;
   }
   //print "NOT FOUND title\n";
   return 1;
}

/* Reformat tables in "cast" format into plain lists. Note this function
 * must be called before removing control characters so that columns are
 * still aligned. Example:
 *
 * Dr.Hermann Seidel .. Heinz Rühmann
 * Vera Bork .......... Wera Frydtberg
 * Freddy Blei ........ Gert Fröbe
 * OR
 * Regie..............Ute Wieland
 * Produktion.........Ralph Schwingel,
 *                    Stefan Schubert
 */
bool DescFormatCastTable(vector<string>& Lines)
{
   smatch whats;
   bool result = false;

   // step #1: build statistic about lines with ".." (including space before and after)
   map<int,int> Tabs;
   int tab_max = -1;
   for (uint idx = 0; idx < Lines.size(); idx++) {
      static const regex expr1("^((.*?)( ?)(\\.\\.+)( ?))");
      if (regex_search(Lines[idx], whats, expr1)) {
         int rec = whats[1].length() |
                   (whats[3].length() << 8) |
                   (whats[5].length() << 16);
         Tabs[rec]++;
         if ((tab_max == -1) || (Tabs[tab_max] < Tabs[rec]))
            tab_max = rec;
      }
   }

   // minimum is two lines looking like table rows
   if ((tab_max != -1) && (Tabs[tab_max] >= 2)) {
      int off  = tab_max & 0xFF;
      int spc1 = (tab_max >> 8) & 0xFF;
      int spc2 = tab_max >> 16;
      if (opt_debug) printf("DESC reformat table into list: %d rows, FMT:%d,%d %d\n", Tabs[tab_max], off, spc1, spc2);

      // step #2: find all lines with dots ending at the right column and right amount of spaces
      int last_row = -1;
      int row = 0;
      for (uint idx = 0; idx < Lines.size(); idx++) {
         static const regex expr2("^(.*?)( ?)(\\.+)( ?)$");
         static const regex expr5("^ +[^ ]$");

         if (   regex_search(Lines[idx].substr(0, off), whats, expr2)
             && ((!whats[2].length() && !spc1) || (whats[2].length() == spc1))
             && ((!whats[4].length() && !spc2) || (whats[4].length() == spc2)) ) {
            string tab1 = string(whats[1]);
            string tab2 = Lines[idx].substr(off);
            static const regex expr3("^[^ \\.]");
            if (regex_search(tab2, whats, expr3)) {
               // match -> replace dots with colon
               tab1 = regex_replace(tab1, regex(" *:$"), "");
               tab2 = regex_replace(tab2, regex(",? +$"), "");
               Lines[idx] = tab1 + string(": ") + tab2 + string(",");
               last_row = row;
            }
            else if (last_row > 0) {
               // end of table (non-empty line) -> terminate list with semicolon
               Lines[last_row] = regex_replace(Lines[last_row], regex(",$"), ";");
               last_row = -1;
            }
         }
         else if (   (last_row != -1)
                  && regex_search(Lines[idx].substr(0, off + 1), whats, expr5)) {
            // right-side table column continues (left side empty)
            Lines[last_row] = regex_replace(Lines[last_row], regex(",$"), "");
            string tab2 = regex_replace(Lines[idx].substr(off), regex(",? +$"), "");
            Lines[idx] = tab2 + ",";
            last_row = row;
         }
         else if (last_row > 0) {
            // end of table: if paragraph break remove comma; else terminate with semicolon
            static const regex expr4("[^ ]");
            if (!regex_search(Lines[idx], whats, expr4)) {
               Lines[last_row] = regex_replace(Lines[last_row], regex(",$"), "");
            } else {
               Lines[last_row] = regex_replace(Lines[last_row], regex(",$"), ";");
            }
            last_row = -1;
         }
         row++;
      }
      if (last_row > 0) {
         Lines[last_row] = regex_replace(Lines[last_row], regex(",$"), "");
      }
      result = true;
   }
   return result;
}

string ParseDescContent(int page, int sub, int head, int foot)
{
   T_VPS_TIME vps_data;
   smatch whats;
   vector<string> Lines;

   const vt_page& pgctrl = PageCtrl[page | (sub << 12)];
   bool is_nl = 0;
   string desc;

   for (int idx = head; idx <= foot; idx++) {
      string ctrl(pgctrl.m_line[idx], VT_PKG_RAW_LEN);

      // TODO parse features behind date, title or subtitle

      // extract and remove VPS labels and all concealed text
      static const regex expr1("[\\x05\\x18]");
      if (regex_search(ctrl, whats, expr1)) {
         ParseVpsLabel(ctrl, vps_data, true);
      }
      static const regex expr2("[\\x00-\\x1F\\x7F]");
      ctrl = regex_replace(ctrl, expr2, " ");

      // remove VPS time and date labels
      static const regex expr3("^ +[0-9A-F]{2}\\d{3} (\\d{2})(\\d{2})(\\d{2}) [0-9A-F]{2} *$");
      static const regex expr4(" +VPS \\d{4} *$");
      ctrl = regex_replace(ctrl, expr3, "");
      ctrl = regex_replace(ctrl, expr4, "");

      Lines.push_back(ctrl);
   }

   DescFormatCastTable(Lines);

   for (uint idx = 0; idx < Lines.size(); idx++) {
      // TODO: replace 0x7f (i.e. square) at line start with "-"
      // line consisting only of "-": treat as paragraph break
      string line = Lines[idx];
      static const regex expr5("^ *[\\-_\\+]{20,} *$");
      line = regex_replace(line, regex(expr5), "");

      static const regex expr6("[^ ]");
      if (regex_search(line, whats, expr6)) {
         static const regex expr8("(^ +| +$)");
         line = regex_replace(line, expr8, "");

         static const regex expr9("  +");
         line = regex_replace(line, expr9, " ");

         if (is_nl)
            desc += "\n";
         static const regex expr12("\\S-$");
         static const regex expr13("^[[:lower:]]");
         if (   regex_search(desc, whats, expr12)
             && regex_search(line, whats, expr13) )
         {
            if (desc[desc.size() - 1] == '-')
               desc.erase(desc.end() - 1);
            desc += line;
         }
         else if ((desc.length() > 0) && !is_nl) {
            desc += string(" ") + line;
         }
         else {
            desc += line;
         }
         is_nl = false;
      } else {
         // empty line: paragraph break
         if (desc.length() > 0) {
            is_nl = true;
         }
      }
   }
   return desc;
}

/* TODO: check for all referenced text pages where no match was found if they
 *       describe a yet unknown programme (e.g. next instance of a weekly series)
 */
string ParseDescPage(TV_SLOT& slot)
{
   string desc;
   bool found = false;
   int page = slot.m_ttx_ref;

   if (opt_debug) {
      char buf[100];
      strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", localtime(&slot.m_start_t));
      printf("DESC parse page %03X: search match for %s \"%s\"\n", page, buf, slot.m_title.c_str());
   }

   if (PgCnt.find(page) != PgCnt.end()) {
      int sub1;
      int sub2;
      if (PgSub[page] == 0) {
         sub1 = sub2 = 0;
      } else {
         sub1 = 1;
         sub2 = PgSub[page];
      }
      for (int sub = sub1; sub <= sub2; sub++) {
         if (TtxSubPageDefined(page, sub)) {
            PageToLatin(page, sub);
            // TODO: multiple sub-pages may belong to same title, but have no date
            //       caution: multiple pages may also have the same title, but describe different instances of a series

            // TODO: bottom of desc pages may contain a 2nd date/time for repeats (e.g. SAT.1: "Whg. Fr 05.05. 05:10-05:35") - note the different BG color!

            if (ParseDescDate(page, sub, slot)) {
               int head = ParseDescTitle(page, sub, slot);

               int foot = ParseFooterByColor(page, sub);
               int foot2 = ParseFooter(page, sub, head);
               foot = (foot2 < foot) ? foot2 : foot;
               if (opt_debug) printf("DESC page %03X.%04X match found: lines %d-%d\n", page, sub, head, foot);

               if (found)
                  desc += "\n";
               desc += ParseDescContent(page, sub, head, foot);

               found = true;
            } else {
               if (opt_debug) printf("DESC page %03X.%04X no match found\n", page, sub);
               if (found)
                  break;
            }
         }
      }
   } else {
      if (opt_debug) printf("DESC page %03X not found\n", page);
   }
   return desc;
}

/* ------------------------------------------------------------------------------
 * Determine channel name and ID from teletext header packets
 * - remove clock, date and page number: rest should be channel name
 */
string ParseChannelName()
{
   map<string,int> ChName;
   string wday_match;
   string wday_abbrv_match;
   string mname_match;
   int lang = -1;
   regex expr3[8];
   regex expr4[8];

   for (map<int,int>::iterator p = PgCnt.begin(); p != PgCnt.end(); p++) {
      int page = p->first;
      if ( (((page>>4)&0xF)<=9) && ((page&0xF)<=9) ) {
         int sub1, sub2;
         if (PgSub[page] == 0) {
            sub1 = sub2 = 0;
         }
         else {
            sub1 = 1;
            sub2 = PgSub[page];
         }
         for (int sub = sub1; sub <= sub2; sub++) {
            if (Pkg[page|(sub<<12)].m_defined & (1 << 0)) {
               if (PgLang[page] != lang) {
                  lang = PgLang[page];
                  wday_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_FULL);
                  wday_abbrv_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_ABBRV);
                  mname_match = GetDateNameRegExp(MonthNames, lang, DATE_NAME_ANY);
               }

               vt_pkg_txt ctrl;
               memcpy(ctrl, Pkg[page|(sub<<12)].m_line[0], VT_PKG_RAW_LEN + 1);
               memset(ctrl, ' ', 8);
               vt_pkg_txt text;
               TtxToLatin1(ctrl, text, lang);
               // remove remaining control characters
               for (int idx = 0; idx < VT_PKG_RAW_LEN; idx++) {
                  unsigned char c = text[idx];
                  if ((c <= 0x1F) || (c == 0x7F))
                     text[idx] = ' ';
               }
               char pgn[30];
               sprintf(pgn, "(^| )%03X( |$)", page);
               regex expr1(pgn);
               smatch whats;
               string hd(text + 8, VT_PKG_RAW_LEN);
               // remove page number and time (both are required)
               if (regex_search(hd, whats, expr1)) {
                  hd.replace(whats.position(), whats[0].length(), 1, ' ');
                  static regex expr2("^(.*)( \\d{2}[\\.: ]?\\d{2}([\\.: ]\\d{2}) *)");
                  if (regex_search(hd, whats, expr2)) {
                     hd.erase(whats[1].length());
                     // remove date: "Sam.12.Jan" OR "12 Sa Jan" (VOX)
                     if (expr3[lang].empty()) {
                        expr3[lang].assign(string("(((") + wday_abbrv_match + string(")|(") + wday_match +
                                           string("))(\\, ?|\\. ?|  ?\\-  ?|  ?)?)?\\d{1,2}(\\.\\d{1,2}|[ \\.](") +
                                           mname_match + "))(\\.|[ \\.]\\d{2,4}\\.?)? *$", regex::icase);
                        expr4[lang].assign(string("\\d{1,2}(\\, ?|\\. ?|  ?\\-  ?|  ?)(((") +
                                           wday_abbrv_match + string(")|(") +
                                           wday_match +
                                           string ("))(\\, ?|\\. ?|  ?\\-  ?|  ?)?)?(\\.\\d{1,2}|[ \\.](") +
                                           mname_match + "))(\\.|[ \\.]\\d{2,4}\\.?)? *$",
                                           regex::icase);
                     }
                     if (regex_search(hd, whats, expr3[lang])) {
                        hd.erase(whats.position(), whats[0].length());
                     }
                     else if (regex_search(hd, whats, expr4[lang])) {
                        hd.erase(whats.position(), whats[0].length());
                     }

                     // remove and compress whitespace
                     hd = regex_replace(hd, regex("(^ +| +$)"), "");
                     hd = regex_replace(hd, regex("  +"), " ");

                     // remove possible "text" postfix
                     hd = regex_replace(hd, regex("[ \\.\\-]?text$", regex::icase), "");
                     hd = regex_replace(hd, regex("[ \\.\\-]?Text .*"), "");

                     if (ChName.find(hd) != ChName.end()) {
                        ChName[hd] += 1;
                        if (ChName[hd] >= 100)
                           break;
                     }
                     else {
                        ChName[hd] = 1;
                     }
                  }
               }
            }
         }
      }
   }
   string name;
   if (!ChName.empty()) {
      // search the most frequently seen CNI value 
      int max_cnt = -1;
      for (map<string,int>::iterator p = ChName.begin(); p != ChName.end(); p++) {
         if (p->second > max_cnt) {
            name = p->first;
            max_cnt = p->second;
         }
      }
   }
   return name;
}

/* ------------------------------------------------------------------------------
 * Determine channel ID from CNI
 */
struct T_CNI_TO_ID_MAP
{
   uint16_t cni;
   const char * const p_name;
};
const T_CNI_TO_ID_MAP Cni2ChannelId[] =
{
   { 0x1DCA, "1.br-online.de" },
   { 0x4801, "1.omroep.nl" },
   { 0x1AC1, "1.orf.at" },
   { 0x3901, "1.rai.it" },
   { 0x24C1, "1.sfdrs.ch" },
   { 0x24C2, "1.tsr.ch" },
   { 0x4802, "2.omroep.nl" },
   { 0x1AC2, "2.orf.at" },
   { 0x3902, "2.rai.it" },
   { 0x24C7, "2.sfdrs.ch" },
   { 0x24C8, "2.tsr.ch" },
   { 0x1DCB, "3.br-online.de" },
   { 0x4803, "3.omroep.nl" },
   { 0x1AC3, "3.orf.at" },
   { 0x3903, "3.rai.it" },
   { 0x1DC7, "3sat.de" },
   { 0x1D44, "alpha.br-online.de" },
   { 0x1DC1, "ard.de" },
   { 0x1D85, "arte-tv.com" },
   { 0x2C7F, "bbc1.bbc.co.uk" },
   { 0x2C40, "bbc2.bbc.co.uk" },
   { 0x2C57, "bbcworld.com" },
   { 0x1DE1, "bw.swr.de" },
   { 0x2C11, "channel4.com" },
   { 0x1546, "cnn.com" },
   { 0x1D76, "das-vierte.de" },
   { 0x5BF2, "discoveryeurope.com" },
   { 0x1D8D, "dsf.com" },
   { 0x2FE1, "euronews.net" },
   { 0x1D91, "eurosport.de" },
   { 0x1DD1, "hamburg1.de" },
   { 0x1DCF, "hr-online.de" },
   { 0x1D92, "kabel1.de" },
   { 0x1DC9, "kika.de" },
   { 0x3203, "la1.rtbf.be" },
   { 0x3204, "la2.rtbf.be" },
   { 0x2F06, "m6.fr" },
   { 0x1DFE, "mdr.de" },
   { 0x1D73, "mtv.de" },
   { 0x1D8C, "n-tv.de" },
   { 0x1D7A, "n24.de" },
   { 0x2C31, "nbc.com" },
   { 0x1DD4, "ndr.de" },
   { 0x1DBA, "neunlive.de" },
   { 0x1D7C, "onyx.tv" },
   { 0x1D82, "orb.de" },
   { 0x1DC8, "phoenix.de" },
   { 0x2487, "prosieben.ch" },
   { 0x1D94, "prosieben.de" },
   { 0x1DDC, "rbb-online.de" },
   { 0x1DE4, "rp.swr.de" },
   { 0x1DAB, "rtl.de" },
   { 0x1D8F, "rtl2.de" },
   { 0x1DB9, "sat1.de" },
   { 0x2C0D, "sky-news.sky.com" },
   { 0x2C0E, "sky-one.sky.com" },
   { 0x1DE2, "sr.swr.de" },
   { 0x1D8A, "superrtl.de" },
   { 0x1DE0, "swr.de" },
   { 0x1D78, "tele5.de" },
   { 0x9001, "trt.net.tr" },
   { 0x1601, "tv1.vrt.be" },
   { 0x2FE5, "tv5.org" },
   { 0x1D88, "viva.tv" },
   { 0x1D89, "viva2.de" },
   { 0x1D8E, "vox.de" },
   { 0x1DE6, "wdr.de" },
   { 0x1DC2, "zdf.de" },
   { 0, 0 }
};
const uint16_t NiToPdcCni[] =
{
   // Austria
   0x4301, 0x1AC1,
   0x4302, 0x1AC2,
   // Belgium
   0x0404, 0x1604,
   0x3201, 0x1601,
   0x3202, 0x1602,
   0x3205, 0x1605,
   0x3206, 0x1606,
   // Croatia
   // Czech Republic
   0x4201, 0x32C1,
   0x4202, 0x32C2,
   0x4203, 0x32C3,
   0x4211, 0x32D1,
   0x4212, 0x32D2,
   0x4221, 0x32E1,
   0x4222, 0x32E2,
   0x4231, 0x32F1,
   0x4232, 0x32F2,
   // Denmark
   0x4502, 0x2902,
   0x4503, 0x2904,
   0x49CF, 0x2903,
   0x7392, 0x2901,
   // Finland
   0x3581, 0x2601,
   0x3582, 0x2602,
   0x358F, 0x260F,
   // France
   0x330A, 0x2F0A,
   0x3311, 0x2F11,
   0x3312, 0x2F12,
   0x3320, 0x2F20,
   0x3321, 0x2F21,
   0x3322, 0x2F22,
   0x33C1, 0x2FC1,
   0x33C2, 0x2FC2,
   0x33C3, 0x2FC3,
   0x33C4, 0x2FC4,
   0x33C5, 0x2FC5,
   0x33C6, 0x2FC6,
   0x33C7, 0x2FC7,
   0x33C8, 0x2FC8,
   0x33C9, 0x2FC9,
   0x33CA, 0x2FCA,
   0x33CB, 0x2FCB,
   0x33CC, 0x2FCC,
   0x33F1, 0x2F01,
   0x33F2, 0x2F02,
   0x33F3, 0x2F03,
   0x33F4, 0x2F04,
   0x33F5, 0x2F05,
   0x33F6, 0x2F06,
   0xF101, 0x2FE2,
   0xF500, 0x2FE5,
   0xFE01, 0x2FE1,
   // Germany
   0x4901, 0x1DC1,
   0x4902, 0x1DC2,
   0x490A, 0x1D85,
   0x490C, 0x1D8E,
   0x4915, 0x1DCF,
   0x4918, 0x1DC8,
   0x4941, 0x1D41,
   0x4942, 0x1D42,
   0x4943, 0x1D43,
   0x4944, 0x1D44,
   0x4982, 0x1D82,
   0x49BD, 0x1D77,
   0x49BE, 0x1D7B,
   0x49BF, 0x1D7F,
   0x49C7, 0x1DC7,
   0x49C9, 0x1DC9,
   0x49CB, 0x1DCB,
   0x49D4, 0x1DD4,
   0x49D9, 0x1DD9,
   0x49DC, 0x1DDC,
   0x49DF, 0x1DDF,
   0x49E1, 0x1DE1,
   0x49E4, 0x1DE4,
   0x49E6, 0x1DE6,
   0x49FE, 0x1DFE,
   0x49FF, 0x1DCE,
   0x5C49, 0x1D7D,
   // Greece
   0x3001, 0x2101,
   0x3002, 0x2102,
   0x3003, 0x2103,
   // Hungary
   // Iceland
   // Ireland
   0x3531, 0x4201,
   0x3532, 0x4202,
   0x3533, 0x4203,
   // Italy
   0x3911, 0x1511,
   0x3913, 0x1513,
   0x3914, 0x1514,
   0x3915, 0x1515,
   0x3916, 0x1516,
   0x3917, 0x1517,
   0x3918, 0x1518,
   0x3919, 0x1519,
   0x3942, 0x1542,
   0x3943, 0x1543,
   0x3944, 0x1544,
   0x3945, 0x1545,
   0x3946, 0x1546,
   0x3947, 0x1547,
   0x3948, 0x1548,
   0x3949, 0x1549,
   0x3960, 0x1560,
   0x3968, 0x1568,
   0x3990, 0x1590,
   0x3991, 0x1591,
   0x3992, 0x1592,
   0x3993, 0x1593,
   0x3994, 0x1594,
   0x3996, 0x1596,
   0x39A0, 0x15A0,
   0x39A1, 0x15A1,
   0x39A2, 0x15A2,
   0x39A3, 0x15A3,
   0x39A4, 0x15A4,
   0x39A5, 0x15A5,
   0x39A6, 0x15A6,
   0x39B0, 0x15B0,
   0x39B2, 0x15B2,
   0x39B3, 0x15B3,
   0x39B4, 0x15B4,
   0x39B5, 0x15B5,
   0x39B6, 0x15B6,
   0x39B7, 0x15B7,
   0x39B9, 0x15B9,
   0x39C7, 0x15C7,
   // Luxembourg
   // Netherlands
   0x3101, 0x4801,
   0x3102, 0x4802,
   0x3103, 0x4803,
   0x3104, 0x4804,
   0x3105, 0x4805,
   0x3106, 0x4806,
   0x3120, 0x4820,
   0x3122, 0x4822,
   // Norway
   // Poland
   // Portugal
   // San Marino
   // Slovakia
   0x42A1, 0x35A1,
   0x42A2, 0x35A2,
   0x42A3, 0x35A3,
   0x42A4, 0x35A4,
   0x42A5, 0x35A5,
   0x42A6, 0x35A6,
   // Slovenia
   // Spain
   // Sweden
   0x4601, 0x4E01,
   0x4602, 0x4E02,
   0x4640, 0x4E40,
   // Switzerland
   0x4101, 0x24C1,
   0x4102, 0x24C2,
   0x4103, 0x24C3,
   0x4107, 0x24C7,
   0x4108, 0x24C8,
   0x4109, 0x24C9,
   0x410A, 0x24CA,
   0x4121, 0x2421,
   // Turkey
   0x9001, 0x4301,
   0x9002, 0x4302,
   0x9003, 0x4303,
   0x9004, 0x4304,
   0x9005, 0x4305,
   0x9006, 0x4306,
   // UK
   0x01F2, 0x5BF1,
   0x10E4, 0x2C34,
   0x1609, 0x2C09,
   0x1984, 0x2C04,
   0x200A, 0x2C0A,
   0x25D0, 0x2C30,
   0x28EB, 0x2C2B,
   0x2F27, 0x2C37,
   0x37E5, 0x2C25,
   0x3F39, 0x2C39,
   0x4401, 0x5BFA,
   0x4402, 0x2C01,
   0x4403, 0x2C3C,
   0x4404, 0x5BF0,
   0x4405, 0x5BEF,
   0x4406, 0x5BF7,
   0x4407, 0x5BF2,
   0x4408, 0x5BF3,
   0x4409, 0x5BF8,
   0x4440, 0x2C40,
   0x4441, 0x2C41,
   0x4442, 0x2C42,
   0x4444, 0x2C44,
   0x4457, 0x2C57,
   0x4468, 0x2C68,
   0x4469, 0x2C69,
   0x447B, 0x2C7B,
   0x447D, 0x2C7D,
   0x447E, 0x2C7E,
   0x447F, 0x2C7F,
   0x44D1, 0x5BCC,
   0x4D54, 0x2C14,
   0x4D58, 0x2C20,
   0x4D59, 0x2C21,
   0x4D5A, 0x5BF4,
   0x4D5B, 0x5BF5,
   0x5AAF, 0x2C3F,
   0x82DD, 0x2C1D,
   0x82E1, 0x2C05,
   0x833B, 0x2C3D,
   0x884B, 0x2C0B,
   0x8E71, 0x2C31,
   0x8E72, 0x2C35,
   0x9602, 0x2C02,
   0xA2FE, 0x2C3E,
   0xA82C, 0x2C2C,
   0xADD8, 0x2C18,
   0xADDC, 0x5BD2,
   0xB4C7, 0x2C07,
   0xB7F7, 0x2C27,
   0xC47B, 0x2C3B,
   0xC8DE, 0x2C1E,
   0xF33A, 0x2C3A,
   0xF9D2, 0x2C12,
   0xFA2C, 0x2C2D,
   0xFA6F, 0x2C2F,
   0xFB9C, 0x2C1C,
   0xFCD1, 0x2C11,
   0xFCE4, 0x2C24,
   0xFCF3, 0x2C13,
   0xFCF4, 0x5BF6,
   0xFCF5, 0x2C15,
   0xFCF6, 0x5BF9,
   0xFCF7, 0x2C17,
   0xFCF8, 0x2C08,
   0xFCF9, 0x2C19,
   0xFCFA, 0x2C1A,
   0xFCFB, 0x2C1B,
   0xFCFC, 0x2C0C,
   0xFCFD, 0x2C0D,
   0xFCFE, 0x2C0E,
   0xFCFF, 0x2C0F,
   // Ukraine
   // USA
   0, 0
};

string ParseChannelId()
{
   // search the most frequently seen CNI value 
   uint16_t cni = 0;
   int max_cnt = -1;
   for (map<int,int>::iterator p = PkgCni.begin(); p != PkgCni.end(); p++) {
      if (p->second > max_cnt) {
         cni = p->first;
         max_cnt = p->second;
      }
   }

   if (cni != 0) {
      for (int idx = 0; NiToPdcCni[idx] != 0; idx += 2) {
         if (NiToPdcCni[idx] == cni) {
            cni = NiToPdcCni[idx + 1];
            break;
         }
      }
      int pdc_cni = cni;
      if ((cni >> 8) == 0x0D) {
         pdc_cni |= 0x1000;
      } else if ((cni >> 8) == 0x0A) {
         pdc_cni |= 0x1000;
      } else if ((cni >> 8) == 0x04) {
         pdc_cni |= 0x2000;
      }
      for (int idx = 0; Cni2ChannelId[idx].cni != 0; idx++) {
         if (Cni2ChannelId[idx].cni == pdc_cni) {
            return string(Cni2ChannelId[idx].p_name);
         }
      }

      // not found - derive pseudo-ID from CNI value
      char buf[20];
      sprintf(buf, "CNI%04X", cni);
      return string(buf);
   }
   else {
      return string("");
   }
}

/* ------------------------------------------------------------------------------
 * Parse an XMLTV timestamp (DTD 0.5)
 * - we expect local timezone only (since this is what the ttx grabber generates)
 */
time_t ParseTimestamp(const char * ts)
{
   int year, mon, mday, hour, min, sec;

   // format "YYYYMMDDhhmmss ZZzzz"
   if (sscanf(ts, "%4d%2d%2d%2d%2d%2d +0000",
              &year, &mon, &mday, &hour, &min, &sec) == 6)
   {
      struct tm tm_obj = {0};

      tm_obj.tm_min = min;
      tm_obj.tm_hour = hour;
      tm_obj.tm_mday = mday;
      tm_obj.tm_mon = mon - 1;
      tm_obj.tm_year = year - 1900;
      tm_obj.tm_isdst = -1;

      /*
       * Hack to get mktime() to take an operand in GMT instead of localtime:
       * Calculate the offset from GMT to adjust the argument given to mktime().
       */
      time_t now = time(NULL);
#ifndef WIN32
      tm_obj.tm_sec = localtime(&now)->tm_gmtoff;
#else
      struct tm * pTm = localtime( &now );
      tm_obj.tm_sec = 60*60 * pTm->tm_isdst - timezone;
#endif

      return mktime( &tm_obj );
   }
   else {
      return -1;
   }
}

string GetXmltvTitle(const string& xml)
{
   smatch whats;
   string title;

   static const regex expr1("<title>(.*)</title>");
   if (regex_search(xml, whats, expr1)) {
      title = string(whats[1]);
      XmlToLatin1(title);
   }
   return title;
}

time_t GetXmltvStopTime(const string& xml, time_t old_ts)
{
   smatch whats;
   static const regex expr1("stop=\"([^\"]*)\"");

   if (regex_search(xml, whats, expr1)) {
      return ParseTimestamp(&*whats[1].first);
   }
   else {
      return old_ts;
   }
}

class TV_SLOT_cmp_start
{
public:
  int operator() (const TV_SLOT * a, const TV_SLOT * b) const {
     return a->m_start_t < b->m_start_t;
  }
};


/* ------------------------------------------------------------------------------
 * Read an XMLTV input file
 * - note this is NOT a full XML parser (not by far)
 *   will only work with XMLTV files generated by this grabber
 */
void ImportXmltvFile(const char * fname,
                     map<string,string>& MergeProg,
                     map<string,string>& MergeChn)
{
   map<string,int> ChnProgDef;
   enum T_IMPORT_STATE {
      STATE_NULL,
      STATE_DONE,
      STATE_TV,
      STATE_CHN,
      STATE_PROG
   } state = STATE_NULL;
   string tag_data;
   string chn_id;
   time_t start_t = -1;
   time_t exp_thresh = time(NULL) - opt_expire * 60;
   cmatch what;

   FILE * fp = fopen(fname, "r");
   if (fp != NULL)
   {
      char buf[1024];
      while (fgets(buf, sizeof(buf), fp) != 0) {
         // handling XML header
         if (state == STATE_NULL) {
            static const regex expr1("^\\s*<(\\?xml|!)[^>]*>\\s*$", regex::icase);
            static const regex expr2("^\\s*<tv([^>\"=]+(=\"[^\"]*\")?)*>\\s*$", regex::icase);
            static const regex expr3("\\S");

            if (regex_search(buf, what, expr1)) {
               // swallow DTD header
            }
            else if (regex_search(buf, what, expr2)) {
               // <tv> top-level tag found
               state = STATE_TV;
            }
            else if (regex_search(buf, what, expr3)) {
               fprintf(stderr, "Unexpected line '%s' in %s\n", buf, fname);
            }
         }
         else if (state == STATE_DONE) {
            fprintf(stderr, "Unexpected line '%s' following </tv> in %s\n", buf, fname);
         }
         // handling main section in-between <tv> </tv>
         else if (state == STATE_TV) {
            static const regex expr1("^\\s*</tv>\\s*$", regex::icase);
            static const regex expr2("^\\s*<channel", regex::icase);
            static const regex expr3("^\\s*<programme", regex::icase);
            static const regex expr4("\\S");
            if (regex_search(buf, what, expr1)) {
               state = STATE_DONE;
            }
            else if (regex_search(buf, what, expr2)) {
               static const regex expr5("id=\"([^\"]*)\"", regex::icase);
               if (regex_search(buf, what, expr5)) {
                  chn_id = string(what[1]);
               } else {
                  fprintf(stderr, "Missing 'id' attribute in '%s' in %s\n", buf, fname);
               }
               tag_data = buf;
               state = STATE_CHN;
            }
            else if (regex_search(buf, what, expr3)) {
               static const regex expr6("start=\"([^\"]*)\".*channel=\"([^\"]*)\"", regex::icase);
               if (regex_search(buf, what, expr6)) {
                  chn_id = string(what[2]);
                  start_t = ParseTimestamp(&*what[1].first);
                  if (MergeChn.find(chn_id) == MergeChn.end()) {
                     fprintf(stderr, "Unknown channel %s in merge input\n", chn_id.c_str());
                     exit(2);
                  }
               } else {
                  fprintf(stderr, "Warning: Missing 'start' and 'channel' attributes "
                          "in '%s' in %s\n", buf, fname);
                  chn_id.clear();
               }
               tag_data = buf;
               state = STATE_PROG;
            }
            else if (regex_search(buf, what, expr4)) {
               fprintf(stderr, "Warning: Unexpected tag '%s' in %s ignored "
                       "(expecting <channel> or <programme>)\n", buf, fname);
            }
         }
         // handling channel data
         else if (state == STATE_CHN) {
            tag_data += buf;
            static const regex expr7("^\\s*</channel>\\s*$", regex::icase);
            if (regex_search(buf, what, expr7)) {
               if (opt_debug) printf("XMLTV import channel '%s'\n", chn_id.c_str());
               MergeChn[chn_id] = tag_data;
               state = STATE_TV;
            }
         }
         // handling programme data
         else if (state == STATE_PROG) {
            tag_data += buf;
            static const regex expr8("^\\s*</programme>\\s*$", regex::icase);
            if (regex_search(buf, what, expr8)) {
               if (   ((start_t >= exp_thresh) || opt_verify)
                   && (chn_id.length() > 0)) {
                  char buf[20];
                  sprintf(buf, "%ld;", (long)start_t);
                  MergeProg[string(buf) + chn_id] = tag_data;
                  if (opt_debug) printf("XMLTV import programme '%s' start:%ld %s\n",
                                        GetXmltvTitle(tag_data).c_str(), (long)start_t, chn_id.c_str());
                  // remember that there's at least one programme for this channel
                  ChnProgDef[chn_id] = 1;
               }
               state = STATE_TV;
            }
         }
      }
      fclose(fp);

      // remove empty channels
      for (map<string,string>::iterator p = MergeChn.begin(); p != MergeChn.end(); ) {
         if (ChnProgDef.find(p->first) == ChnProgDef.end()) {
            if (opt_debug) printf("XMLTV input: dropping empty CHANNEL ID %s\n", p->first.c_str());
            MergeChn.erase(p++);
         } else {
            p++;
         }
      }
   }
   else {
      fprintf(stderr, "cannot open merge input file '%s': %s\n", fname, strerror(errno));
   }
}

/* ------------------------------------------------------------------------------
 * Merge description and other meta-data from old source, if missing in new source
 * - called before programmes in the old source are discarded
 */
void MergeSlotDesc(TV_SLOT * p_slot, const string& old_xml,
                   time_t new_start, time_t new_stop,
                   time_t old_start, time_t old_stop)
{
   if ((old_start == new_start) && (old_stop == new_stop))
   {
      string old_title = GetXmltvTitle(old_xml);

      // FIXME allow for parity errors (& use redundancy to correct them)
      if (p_slot->m_title == old_title)
      {
         static const regex expr1("^\\(teletext [1-8][0-9][0-9]\\)$");
         smatch whats;

         if (   (p_slot->m_desc.length() == 0)
             || regex_search(p_slot->m_desc, whats, expr1) )
         {
            string old_desc;
            static const regex expr2("<desc>(.*)</desc>");

            if (regex_search(old_xml, whats, expr2))
            {
               old_desc = string(whats[1]);
               XmlToLatin1(old_desc);
            }

            if (   (old_desc.length() > 0)
                && !regex_search(old_desc, whats, expr1))
            {
               // copy description
               p_slot->m_desc = old_desc;
            }
         }
      }
   }
}

/* ------------------------------------------------------------------------------
 * Merge old and new programmes
 */
int MergeNextSlot( list<TV_SLOT*>& NewSlotList,
                   list<time_t>& OldSlotList,
                   const map<time_t,string>& OldProgHash )
{
   TV_SLOT * p_slot = 0;
   const string * p_xml = 0;
   time_t new_start = -1;
   time_t new_stop = -1;
   time_t old_start = -1;
   time_t old_stop = -1;

   // get the first (oldest) programme start/stop times from both sources
   if (!NewSlotList.empty()) {
      p_slot = NewSlotList.front();
      new_start = p_slot->m_start_t;
      new_stop = p_slot->m_stop_t;
      if (new_stop == -1)  // FIXME
         new_stop = new_start + 1;

      // remove overlapping (or repeated) programmes in the new data
      list<TV_SLOT*>::iterator it_next_slot = NewSlotList.begin();
      ++it_next_slot;
      while (   (it_next_slot != NewSlotList.end())
             && ((*it_next_slot)->m_start_t < new_stop)) {
         if (opt_debug) printf("MERGE DISCARD NEW %ld '%.30s' ovl %ld..%ld\n",
                               (*it_next_slot)->m_start_t, (*it_next_slot)->m_title.c_str(),
                               (long)new_start, (long)new_stop);
         NewSlotList.erase(it_next_slot++);
      }
   }
   if (!OldSlotList.empty()) {
      p_xml = &OldProgHash.find(OldSlotList.front())->second;
      old_start = OldSlotList.front();
      old_stop = GetXmltvStopTime(*p_xml, old_start);
   }

   if ((new_start != -1) && (old_start != -1)) {
      if (opt_debug) printf("MERGE CMP %s -- %.30s\n", p_slot->m_title.c_str(), GetXmltvTitle(*p_xml).c_str());
      // discard old programmes which overlap the next new one
      // TODO: merge description from old to new if time and title are identical and new has no description
      while ((old_start < new_stop) && (old_stop > new_start)) {
         if (opt_debug) printf("MERGE DISCARD OLD %ld...%ld  ovl %ld..%ld\n",
                               (long)old_start, (long)old_stop, (long)new_start, (long)new_stop);
         MergeSlotDesc(p_slot, *p_xml, new_start, new_stop, old_start, old_stop);
         OldSlotList.pop_front();

         if (!OldSlotList.empty()) {
            p_xml = &OldProgHash.find(OldSlotList.front())->second;
            old_start = OldSlotList.front();
            old_stop = GetXmltvStopTime(*p_xml, old_start);
            if (opt_debug) printf("MERGE CMP %s -- %.30s\n",
                                  p_slot->m_title.c_str(), GetXmltvTitle(*p_xml).c_str());
         } else {
            old_start = -1;
            old_stop = -1;
            break;
         }
      }
      if ((old_start == -1) || (new_start <= old_start)) {
         // new programme starts earlier -> choose data from new source
         // discard old programmes which are overlapped the next new one
         while (!OldSlotList.empty() && (OldSlotList.front() < new_stop)) {
            if (opt_debug) printf("MERGE DISCARD2 OLD %ld ovl STOP %ld\n",
                                  (long)OldSlotList.front(), (long)new_stop);
            MergeSlotDesc(p_slot, *p_xml, new_start, new_stop, old_start, old_stop);
            OldSlotList.pop_front();
         }
         if (opt_debug) printf("MERGE CHOOSE NEW\n");
         return 1; // new
      } else {
         // choose data from old source
         if (opt_debug) printf("MERGE CHOOSE OLD\n");
         return 2; // old
      }
   }
   // special cases: only one source available anymore
   else if (new_start != -1) {
      return 1; // new
   }
   else if (old_start != -1) {
      return 2; // old
   }
   else {
      return 0; // error
   }
}

/* ------------------------------------------------------------------------------
 * Write grabbed data to XMLTV
 * - merge with old data, if available
 */
void ExportXmltv(const string& ch_id, string ch_name,
                 vector<TV_SLOT>& NewSlots,
                 map<string,string>& MergeProg,
                 map<string,string>& MergeChn)
{
   FILE * fp;

   if (opt_outfile != 0) {
      fp = fopen(opt_outfile, "w");
      if (fp == NULL) {
         fprintf(stderr, "Failed to create %s: %s\n", opt_outfile, strerror(errno));
         exit(1);
      }
   }
   else {
      fp = stdout;
   }

   fprintf(fp, "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n"
               "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n");
   if (opt_verify) {
      fprintf(fp, "<tv>\n");
   }
   else {
      fprintf(fp, "<tv generator-info-name=\"%s\" generator-info-url=\"%s\" "
                  "source-info-name=\"teletext\">\n",
                  version, home_url);
   }

   // print channel table
   map<string,string>::iterator p_chn = MergeChn.find(ch_id);
   if (MergeChn.find(ch_id) == MergeChn.end()) {
      fprintf(fp, "<channel id=\"%s\">\n"
                  "\t<display-name>%s</display-name>\n"
                  "</channel>\n",
                  ch_id.c_str(), ch_name.c_str());
   }
   for (map<string,string>::iterator p = MergeChn.begin(); p != MergeChn.end(); p++) {
      if (p->first == ch_id) {
         fprintf(fp, "<channel id=\"%s\">\n"
                     "\t<display-name>%s</display-name>\n"
                     "</channel>\n", ch_id.c_str(), ch_name.c_str());
      }
      else {
         fprintf(fp, "%s", p->second.c_str());
      }
   }

   if (MergeChn.find(ch_id) != MergeChn.end()) {
      // extract respective channel's data from merge input
      map<time_t,string> OldProgHash;
      for (map<string,string>::iterator p = MergeProg.begin(); p != MergeProg.end(); ) {
         long start_ts;
         int slen;
         if (   (sscanf(p->first.c_str(), "%ld;%n", &start_ts, &slen) >= 1)
             && (p->first.compare(slen, p->first.length() - slen, ch_id) == 0) ) {
            OldProgHash[start_ts] = p->second;
            MergeProg.erase(p++);
         }
         else {
            p++;
         }
      }
      // sort new programmes by start time
      list<TV_SLOT*> NewSlotList;
      for (uint idx = 0; idx < NewSlots.size(); idx++)
         NewSlotList.push_back(&NewSlots[idx]);
      NewSlotList.sort(TV_SLOT_cmp_start());

      // map holding old programmes is already sorted, as start time is used as key
      list<time_t> OldSlotList;
      for (map<time_t,string>::iterator p = OldProgHash.begin(); p != OldProgHash.end(); p++)
         OldSlotList.push_back(p->first);

      // combine both sources (i.e. merge them)
      while (!NewSlotList.empty() || !OldSlotList.empty()) {
         switch (MergeNextSlot(NewSlotList, OldSlotList, OldProgHash)) {
            case 1:
               assert(!NewSlotList.empty());
               ExportTitle(fp, *NewSlotList.front(), ch_id);
               NewSlotList.pop_front();
               break;
            case 2:
               assert(!OldSlotList.empty());
               fprintf(fp, "\n%s", OldProgHash[OldSlotList.front()].c_str());
               OldSlotList.pop_front();
               break;
            default:
               break;
         }
      }
   }
   else {
      // no merge required -> simply print all new
      for (uint idx = 0; idx < NewSlots.size(); idx++) {
         ExportTitle(fp, NewSlots[idx], ch_id);
      }
   }

   // append data for all remaining old channels unchanged
   for (map<string,string>::iterator p = MergeProg.begin(); p != MergeProg.end(); p++) {
      fprintf(fp, "\n%s", p->second.c_str());
   }

   fprintf(fp, "</tv>\n");
   fclose(fp);
}

/* ------------------------------------------------------------------------------
 * Main
 */
int main( int argc, char *argv[])
{
   // enable locale so that case conversions work outside of ASCII too
   setlocale(LC_CTYPE, "");

   // parse command line arguments
   ParseArgv(argc, argv);

   // read teletext data into memory
   if (!opt_verify) {
      ReadVbi();
   } else {
      ImportRawDump();
   }

   if (opt_dump == 1) {
      // debug only: dump teletext data into file
      DumpTextPages();
   }
   else if (opt_dump == 2) {
      DumpRawTeletext();
   }
   else if (opt_dump == 3) {
      // dump already done while capturing
   }
   else {
      // parse and export programme data
      // grab new XML data from teletext
      vector<TV_SLOT> NewSlots = ParseAllOvPages();

      // retrieve descriptions from references teletext pages
      for (uint idx = 0; idx < NewSlots.size(); idx++) {
         if (NewSlots[idx].m_ttx_ref != -1) {
            NewSlots[idx].m_desc = ParseDescPage(NewSlots[idx]);
         }
      }

      // remove programmes beyond the expiration threshold
      if (!opt_verify) {
         NewSlots = FilterExpiredSlots(NewSlots);
      }

      // make sure to never write an empty file
      if (!NewSlots.empty()) {

         // get channel name from teletext header packets
         string ch_name = *opt_chname ? string(opt_chname) : ParseChannelName();

         string ch_id = *opt_chid ? string(opt_chid) : ParseChannelId();

         if (ch_name.length() == 0) {
            ch_name = ch_id;
         }
         if (ch_name.length() == 0) {
            ch_name = "???";
         }
         if (ch_id.length() == 0) {
            ch_id = ch_name;
         }

         // read and merge old data from XMLTV file
         map<string,string> MergeProg;
         map<string,string> MergeChn;
         if (opt_mergefile != 0) {
            ImportXmltvFile(opt_mergefile, MergeProg, MergeChn);
         }

         ExportXmltv(ch_id, ch_name, NewSlots, MergeProg, MergeChn);
         exit(0);
      }
      else {
         // return error code to signal abormal termination
         exit(100);
      }
   }
}

