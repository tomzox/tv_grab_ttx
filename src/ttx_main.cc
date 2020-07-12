/*
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
 * Copyright 2006-2011,2020 by T. Zoerner (tomzo at users.sf.net)
 */

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <sstream>
#include <string>
#include <regex>

using namespace std;

#include "ttx_db.h"
#include "ttx_acq.h"
#include "ttx_xmltv.h"
#include "ttx_scrape.h"

const char version[] = "Teletext EPG grabber, v2.1";
const char copyright[] = "Copyright 2006-2011,2020 T. Zoerner";
const char home_url[] = "https://github.com/tomzox/tv_grab_ttx";

// command line options for capturing from device
int opt_duration = 0;
const char * opt_device = "/dev/dvb/adapter0/demux0";
int opt_dvbpid = -1;
int opt_verbose = 0;
int opt_debug = 0;

// command line options for grabbing
const char * opt_infile = 0;
const char * opt_outfile = 0;
const char * opt_mergefile = 0;
const char * opt_chname = 0;
const char * opt_chid = 0;
int opt_dump = 0;
bool opt_verify = false;
int opt_tv_start = 0x301;
int opt_tv_end = 0x399;
int opt_expire = 120;

/* ------------------------------------------------------------------------------
 * Command line argument parsing
 */
void ParseArgv(int argc, char *argv[])
{
   const char usage[] =
               "Usage: [OPTIONS] [<file>]\n"
               "  -device <path>\t: VBI device used for input (when no input file given)\n"
               "  -dvbpid <PID>\t\t: Use DVB stream with given PID\n"
               "  -duration <secs>\t: Capture duration in seconds\n"
               "  -page <NNN>-<MMM>\t: Page range for TV schedules\n"
               "  -chn_name <name>\t: display name for XMLTV \"<channel>\" tag\n"
               "  -chn_id <name>\t: channel ID for XMLTV \"<channel>\" tag\n"
               "  -outfile <path>\t: Redirect XMLTV or dump output into the given file\n"
               "  -merge <path>\t\t: Merge output with programmes from an XML file\n"
               "  -expire <N>\t\t: Omit programmes which ended at least N minutes ago\n"
               "  -dumpvbi\t\t: Continuously dump captured VBI data; no grabbing\n"
               "  -dumptext\t\t: Dump teletext pages as clear text; no grabbing\n"
               "  -dumpraw <NNN>-<MMM>\t: Dump teletext packets in given range; no grabbing\n"
               "  -verbose\t\t: Print teletext page numbers during capturing\n"
               "  -pgstat\t\t: Print teletext page statistics during capturing\n"
               "  -verify\t\t: Enable grabber verification mode\n"
               "  -debug\t\t: Emit debug messages during grabbing and device access\n"
               "  -version\t\t: Print version number only, then exit\n"
               "  -help\t\t\t: Print this text, then exit\n"
               "  file or \"-\"\t\t: Read VBI input from file or stdin instead of device\n";
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
      else if (strcmp(argv[idx], "-page") == 0) {
         if (idx + 1 >= argc) {
            fprintf(stderr, "%s: missing argument for -page\n%s", argv[0], usage);
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
      // -dumptext: print all teletext pages, then exit (for debugging only)
      else if (strcmp(argv[idx], "-dumptext") == 0) {
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
               fprintf(stderr, "-dumpraw: not a valid start page number: %03X (range 100-899)\n", opt_tv_start);
               exit(1);
            }
            if ((opt_tv_end < 0x100) || (opt_tv_end > 0x899)) {
               fprintf(stderr, "-dumpraw: not a valid end page number: %03X (range 100-899)\n", opt_tv_end);
               exit(1);
            }
            if (opt_tv_end < opt_tv_start) {
               fprintf(stderr, "-dumpraw: start page (%03X) must be <= end page (%03X)\n", opt_tv_start, opt_tv_end);
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
      // -pgstat: print page statistics during capturing
      else if (strcmp(argv[idx], "-pgstat") == 0) {
         opt_verbose = 2;
         idx += 1;
      }
      else if (   (strcmp(argv[idx], "-version") == 0)
               || (strcmp(argv[idx], "-v") == 0) ) {
         fprintf(stderr, "%s\n%s\n\n"
                         "   This is free software with ABSOLUTELY NO WARRANTY.\n"
                         "   Licensed under the GNU Public License version 3 or later.\n"
                         "   For details see <http://www.gnu.org/licenses/>\n",
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
         // no leading "-": no option and not stdin: input file name
         opt_infile = argv[idx];
         if (idx + 1 < argc) {
            fprintf(stderr, "%s: no more than one input file expected.\n"
                            "Error after '%s'\n%s",
                            argv[0], argv[idx], usage);
            exit(1);
         }
         struct stat st;
         if ((stat(opt_infile, &st) == 0) && S_ISCHR(st.st_mode)) {
            // character device - treat same as -dev
            opt_device = opt_infile;
            opt_infile = 0;
         }
         break;
      }
   }
}

/* ------------------------------------------------------------------------------
 * Main
 */
int main( int argc, char *argv[] )
{
   int result = 0;

   // enable locale so that case conversions work outside of ASCII too
   setlocale(LC_CTYPE, "");

   // parse command line arguments
   ParseArgv(argc, argv);

   TTX_DB ttx_db;

   // read teletext data into memory from file or device
   if (   (opt_infile == 0)
       || !ImportRawDump(&ttx_db, opt_infile) ) {

      ReadVbi(&ttx_db, opt_infile, opt_outfile, opt_device, opt_dvbpid,
              opt_verbose, (opt_dump == 3), opt_duration);
   }

   if (opt_dump == 1) {
      // debug only: dump teletext pages into file (text only)
      DumpTextPages(&ttx_db, opt_outfile);
   }
   else if (opt_dump == 2) {
      // dump teletext pages into file, including all control chars
      DumpRawTeletext(&ttx_db, opt_outfile, opt_tv_start, opt_tv_end);
   }
   else if (opt_dump == 3) {
      // dump each VBI packet: already done while capturing
   }
   else {
      // parse and export programme data
      // grab new XML data from teletext
      vector<OV_PAGE*> ov_pages = ParseAllOvPages(&ttx_db.page_db, opt_tv_start, opt_tv_end);

      ParseAllContent(ov_pages);

      // retrieve descriptions from references teletext pages
      list<TV_SLOT> NewSlots = OV_PAGE::get_ov_slots(ov_pages);

      // remove programmes beyond the expiration threshold
      if (!opt_verify) {
         FilterExpiredSlots(NewSlots, opt_expire);
      }

      // make sure to never write an empty file
      if (!NewSlots.empty()) {
         XMLTV xmltv;

         xmltv.SetChannelName(&ttx_db, opt_chname, opt_chid);

         if (!opt_verify) {
            xmltv.SetExpireTime(opt_expire);
         }

         // read and merge old data from XMLTV file
         if (opt_mergefile != 0) {
            xmltv.ImportXmltvFile(opt_mergefile);
         }

         xmltv.ExportXmltv(NewSlots, opt_outfile,
                           opt_verify ? 0 : version,
                           opt_verify ? 0 : home_url);
      }
      else {
         // return error code to signal abormal termination
         result = 100;
      }

      for (unsigned idx = 0; idx < ov_pages.size(); idx++) {
         delete ov_pages[idx];
      }
   }
   return result;
}

