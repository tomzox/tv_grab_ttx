/*
 * Helper program for tuning into a DVB channel during teletext acquisition
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
 * Copyright 2020 by T. Zoerner (tomzo at users.sf.net)
 *
 * ----------------------------------------------------------------------------
 *
 * This program tunes the channel named on the command line and keeps the
 * frontend device open until the process is killed or its STDIN is
 * closed. Another application can capture teletext on the given channel
 * concurrently.
 *
 * Parameters for tuning are looked up in a channels.conf file, which has
 * to be in VDR-1.7.4 format (e.g. generated by tools dvbscan or
 * dvbv5-scan in packages dvb-tools and dvb-apps respectively). Note the
 * reason for not supporting other formats is that they usually only
 * include audio/video PIDs, but not the teletext PID required here.
 *
 * Usage: tune_dvb [OPTIONS] <channel_name>
 *
 * Options:
 *
 * -c <path>: Specifies path and file name where to find channels.conf
 *
 * -p: In this mode the program just parses the channels.conf file for
 * determining the teletext PID of the given channel, prints it and
 * exists. This option is needed, as the program only tunes the front-end,
 * but does not open the demux device, thus the user of this program needs
 * the PID.
 *
 * -P: Similar -p, but returns teletext PID and name (separated by TAB)
 * for all channels sharing the same transponder/frequency, then exits.
 *
 * -q: When tuning fails, query and print current front-end parameters.
 * Usually tuning fails when the device is already opened by another
 * application, so this options allows to snoop which parameters were
 * configured.
 *
 * -v: Capture teletext and print page headers: After tuning the channel,
 * the program starts capturing teletext and prints the header line of
 * each received page. This is intended for checking teletext reception on
 * that channel.
 *
 * -x: With this option the channel is tuned, but the program does not
 * wait afterwards, which usually means the tuner is shut off. This is
 * useful only for testing if the channel name is known and parameters
 * are supported.
 *
 * ----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <libzvbi.h>
#include <linux/dvb/frontend.h>

#define CHANNELS_CONF_NAME "channels.conf"
#define PATH_DEV_FRONTEND "/dev/dvb/adapter0/frontend0"
#define PATH_DEV_DEMUX "/dev/dvb/adapter0/demux0"

// Set of parameters for tuning
typedef struct
{
   long freq;
   int modu;
   long symr;
   int tpid;
} T_DVB_FREQ;

// ----------------------------------------------------------------------------
// Parse an entry in the channels.conf table
// - Format: http://www.vdr-wiki.de/wiki/index.php/Channels.conf
// - List is a colon-separated table with one line per channel; each line contains:
//   name:freq:param:signal source:symbol rate:VPID:APID:TPID:CAID:SID:NID:TID:RID

static bool
ParseVdrChannelEntry( char * sbuf, T_DVB_FREQ * freq_par )
{
   char * sep_ptr;
   const int field_cnt = 13;
   char * fields[field_cnt];
   int field_idx;
   bool result = FALSE;

   sep_ptr = sbuf;
   fields[0] = sbuf;
   for (field_idx = 1; field_idx < field_cnt; ++field_idx)
   {
      sep_ptr = strchr(sep_ptr, ':');
      if (sep_ptr == NULL)
         break;
      *(sep_ptr++) = 0;
      fields[field_idx] = sep_ptr;
   }
   // TODO last field: strip NL

   if (field_idx == field_cnt)
   {
      char * bouquet = strchr(fields[0], ';'); // strip bouquet name
      if (bouquet != NULL)
         *bouquet = 0;

      freq_par->freq = atol(fields[1]);
      while (freq_par->freq <= 1000000)
         freq_par->freq *= 1000;
      freq_par->symr = atol(fields[4]) * 1000;
      freq_par->tpid = atol(fields[7]);  // implicitly ignore ";" and following
      freq_par->modu = QAM_AUTO;
      result = TRUE;

      char * par = fields[2];
      char * end = par + strlen(par);
      while (par < end)
      {
         char op = *(par++);
         char * ends;
         long val;

         switch (op)
         {
            case 'I':
            case 'C':
            case 'D':
            case 'M':
            case 'B':
            case 'T':
            case 'G':
            case 'Y':
               if (par < end)
               {
                  val = strtol(par, &ends, 10);
                  par = ends;
                  if (op == 'M')
                  {
                     switch (val)
                     {
                        case 0:  freq_par->modu = QPSK; break;
                        case 16:  freq_par->modu = QAM_16; break;
                        case 32:  freq_par->modu = QAM_32; break;
                        case 64:  freq_par->modu = QAM_64; break;
                        case 128:  freq_par->modu = QAM_128; break;
                        case 256:  freq_par->modu = QAM_256; break;
                        default:
                           fprintf(stderr, "WARNING: invalid Modulation value %ld in %s: %s\n", val, CHANNELS_CONF_NAME, sbuf);
                     }
                  }
                  else if (op == 'B')
                  {
                     // TODO bandwidth DVB-T
                  }
                  else if (op == 'I')
                  {
                     // TODO inversion bandwidth DVB-T, DVB-C
                  }
               }
               else
               {
                  fprintf(stderr, "WARNING: missing value for param '%c' in %s: %s\n", op, CHANNELS_CONF_NAME, sbuf);
                  result = FALSE;
               }
               break;

            // not followed by value
            case 'H':
            case 'V':
            case 'R':
            case 'L':
               break;
            // ignore whitespace
            case ' ':
            case '\t':
               break;
            default:
               fprintf(stderr, "WARNING: invalid param '%c' in %s: %s\n", op, CHANNELS_CONF_NAME, sbuf);
               result = FALSE;
               break;
         }
      }
   }
   else
   {
      fprintf(stderr, "WARNING: malformed entry in %s: %s\n", CHANNELS_CONF_NAME, sbuf);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Search VDR "channels.conf" table for the parameters of a given channel name
// - returns parameters required for tuning & the teletext PID

static bool
GetVdrChannelConf( const char * conf_path, const char * chn_name, T_DVB_FREQ * freq_par )
{
   char sbuf[2048];
   FILE * fp;
   int cmp_len = strlen(chn_name);
   bool result = FALSE;

   fp = fopen(conf_path, "r");
   if (fp != NULL)
   {
      while (feof(fp) == FALSE)
      {
         sbuf[0] = '\0';

         if (fgets(sbuf, 2048-1, fp) != NULL)
         {
            if ((strncmp(sbuf, chn_name, cmp_len) == 0) &&
                ((sbuf[cmp_len] == ':') || (sbuf[cmp_len] == ';')))
            {
               result = ParseVdrChannelEntry(sbuf, freq_par);
               break;
            }
         }
         else
            break;
      }
      fclose(fp);
   }
   else
   {  // file open failed -> warn the user
      fprintf(stderr, "Opening %s: %s\n", conf_path, strerror(errno));
      exit(1);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Search VDR "channels.conf" table for the next entry with a given frequency
// - function is supposed to be called repeatedly until it returns FALSE
// - when result is TRUE, channel name and teletext PID are returned in ptr params

static bool
FilterVdrChannelFreq( const char * conf_path, long freq, const char ** pp_name, int * p_tpid )
{
   static FILE * fp = NULL;
   static char sbuf[2048];
   bool result = FALSE;

   if (fp == NULL)
      fp = fopen(conf_path, "r");

   if (fp != NULL)
   {
      while (feof(fp) == FALSE)
      {
         sbuf[0] = '\0';

         if (fgets(sbuf, 2048-1, fp) != NULL)
         {
            T_DVB_FREQ freq_par;
            if (ParseVdrChannelEntry(sbuf, &freq_par) && (freq_par.freq == freq))
            {
               *pp_name = sbuf;
               *p_tpid = freq_par.tpid;
               result = TRUE;
               break;
            }
         }
         else
         {
            fclose(fp);
            break;
         }
      }
      // keep fp open
   }
   else
   {  // file open failed -> warn the user
      fprintf(stderr, "Opening %s: %s\n", conf_path, strerror(errno));
      exit(1);
   }
   return result;
}

// ----------------------------------------------------------------------------
// Tune the front-end to the given parameters
// - the function opens, but does NOT close the device

int tune(const T_DVB_FREQ * freq_par)
{
   int result = FALSE;
   int fd = open(PATH_DEV_FRONTEND, O_RDWR);
   if (fd != -1)
   {
      struct dtv_property props[] =
      {
         {
            .cmd = DTV_FREQUENCY,
            //.u = { .data = 338000000 } // S: kHz, C+T: Hz
            .u = { .data = freq_par->freq } // S: kHz, C+T: Hz
         },
         {
            .cmd = DTV_MODULATION,
            //.u = { .data = QAM_256 }
            .u = { .data = freq_par->modu }
         },
         {
            .cmd = DTV_SYMBOL_RATE,
            //.u = { .data = 6900000 }
            .u = { .data = freq_par->symr }
         },
#if 0
         {
            .cmd = DTV_BANDWIDTH_HZ,   // DVB-T only
            .u = { .data = freq_par->bandwidth }
         },
         {
            .cmd = DTV_INNER_FEC,
            .u = { .data = freq_par->fec }
         },
         {
            .cmd = DTV_INVERSION,
            .u = { .data = freq_par->inversion }
         },
#endif
         {
            .cmd = DTV_TUNE,
            .u = { .data = 0 }
         },
      };
      struct dtv_properties prop =
      {
         .num = sizeof(props) / sizeof(props[0]),
         .props = props,
      };
      if (ioctl(fd, FE_SET_PROPERTY, &prop) == 0)
         result = TRUE;
      else
         perror("ioctl FE_SET_PROPERTY");

      //if (close(fd) != 0)
      //   perror("close frontend");
   }
   else
      fprintf(stderr, "open frontend %s: %s\n", PATH_DEV_FRONTEND, strerror(errno));

   return result;
}

// ----------------------------------------------------------------------------
// Query current frequency et.al. parameters from frontend device

int query(void)
{
   int result = FALSE;
   int fd = open("/dev/dvb/adapter0/frontend0", O_RDONLY);
   if (fd != -1)
   {
      // DTV_BANDWIDTH_HZ   // DVB-T only
      // DTV_INVERSION
      // DTV_INNER_FEC

      struct dtv_property props[] =
      {
         {
            .cmd = DTV_FREQUENCY,
            .u = { .data = 0 } // S: kHz, C+T: Hz
         },
         {
            .cmd = DTV_MODULATION,
            .u = { .data = 0 }
         },
         {
            .cmd = DTV_SYMBOL_RATE,
            .u = { .data = 0 }
         },
         {
            .cmd = DTV_INVERSION,
            .u = { .data = 0 }
         },
         {
            .cmd = DTV_INNER_FEC,
            .u = { .data = 0 }
         },
      };
      struct dtv_properties prop =
      {
         .num = sizeof(props) / sizeof(props[0]),
         .props = props,
      };
      if (ioctl(fd, FE_GET_PROPERTY, &prop) == 0)
      {
         printf("Current front-end parameters:\n");
         for (int idx = 0; idx < sizeof(props) / sizeof(props[0]); ++idx)
         {
            const char * prop_name = "";
            switch (props[idx].cmd)
            {
               case DTV_FREQUENCY: prop_name = "frequency"; break;
               case DTV_MODULATION: prop_name = "modulation"; break;
               case DTV_SYMBOL_RATE: prop_name = "symbol-rate"; break;
               case DTV_INVERSION: prop_name = "inversion"; break;
               case DTV_INNER_FEC: prop_name = "inner-FEC"; break;
            }
            printf("[%d] %s: %d\n", props[idx].cmd, prop_name, props[idx].u.data);
         }
         result = TRUE;
      }
      else
         perror("ioctl FE_GET_PROPERTY");

      if (close(fd) != 0)
         perror("close frontend");
   }
   else
      perror("open frontend");

   return result;
}

// ----------------------------------------------------------------------------
// Capture & decode incoming teletext using libzvbi
// - print raw headers of all received pages

void pg_handler(vbi_event *event, void *user_data)
{
   if (event->type == VBI_EVENT_TTX_PAGE)
   {
      char header[40+1];

      if ((event->ev.ttx_page.raw_header != NULL) && vbi_is_bcd(event->ev.ttx_page.pgno))
      {
         for (int idx = 0; idx < 40; ++idx)
         {
            int c = vbi_unpar8(event->ev.ttx_page.raw_header[idx]);
            if (c >= 0x20)  // implies not -1, i.e. no parity error
               header[idx] = c;
            else
               header[idx] = ' ';
         }
      }
      else
      {
         for (int idx = 0; idx < 40; ++idx)
            header[idx] = ' ';
      }
      header[40] = 0;
      printf("%03X.%-4X %s\n", event->ev.ttx_page.pgno, event->ev.ttx_page.subno, header);
   }
}

void ScanTtx(const char * name, int tpid)
{
   char * pErrStr = NULL;
   vbi_capture * pZvbiCapt =
      vbi_capture_dvb_new2(PATH_DEV_DEMUX, tpid, &pErrStr, TRUE);

   if (pZvbiCapt != NULL)
   {
      vbi_sliced * pZvbiData = malloc(256 * sizeof(vbi_sliced));

      vbi_decoder * vtdec = vbi_decoder_new();
      vbi_event_handler_register(vtdec, VBI_EVENT_TTX_PAGE, pg_handler, (void*)name);

      while (TRUE)
      {
         struct timeval timeout;
         double timestamp;
         int  lineCount;
         int  res;

         timeout.tv_sec = 2;
         timeout.tv_usec = 0;
         res = vbi_capture_read_sliced(pZvbiCapt, pZvbiData, &lineCount, &timestamp, &timeout);
         if (res > 0)
         {
            //fprintf(stderr, "read %d lines, timestamp %.2f\n", lineCount, timestamp);
            vbi_decode(vtdec, pZvbiData, lineCount, timestamp);
         }
         else if (res == 0)
         {
            printf("channel %s: no reception\n", name);
         }
         else
         {
            fprintf(stderr, "channel %s: device error: %s\n", name, strerror(errno));
            break;
         }
      }

      vbi_decoder_delete(vtdec);
      vbi_capture_delete(pZvbiCapt);
      free(pZvbiData);
   }
   else
   {
      fprintf(stderr, "Failed to open or initialize %s: %s\n", PATH_DEV_DEMUX,
              (pErrStr != NULL) ? pErrStr : "unknown cause");
      exit(1);
   }
}

// ----------------------------------------------------------------------------

const char * opt_usage =
   "Usage: %s [-c channels.conf] [-t] [-i] [-q] <channel>\n"
   "Options:\n"
   "-c <PATH>:  Path to channel table (VDR format)\n"
   "-p       :  Print teletext PID and exit\n"
   "-P       :  Print all teletext PIDs & names sharing transponder\n"
   "-q       :  Query and print current frequency\n"
   "-v       :  Capture teletext and print page headers\n"
   "-x       :  Exit immediately after tuning\n";

int main(int argc, char** argv)
{
   bool opt_tid_only = FALSE;
   bool opt_tid_list = FALSE;
   bool opt_query = FALSE;
   bool opt_nowait = FALSE;
   bool opt_verbose = FALSE;
   const char * opt_chanels_conf = CHANNELS_CONF_NAME;
   int opt;

   while ((opt = getopt(argc, argv, "c:pPqvx")) != -1)
   {
      switch (opt)
      {
         case 'c':
            opt_chanels_conf = optarg;
            break;
         case 'P':
            opt_tid_list = TRUE;
            opt_tid_only = TRUE;
            break;
         case 'p':
            opt_tid_only = TRUE;
            break;
         case 'q':
            opt_query = TRUE;
            break;
         case 'v':
            opt_verbose = TRUE;
            break;
         case 'x':
            opt_nowait = TRUE;
            break;
         case -1:  // done
            break;
         case '?': // error
         default:
            fprintf(stderr, opt_usage, argv[0]);
            exit(1);
      }
   }
   if (optind + 1 != argc)
   {
      fprintf(stderr, opt_usage, argv[0]);
      exit(1);
   }

   T_DVB_FREQ freq_par;
   if (!GetVdrChannelConf(opt_chanels_conf, argv[optind], &freq_par))
   {
      fprintf(stderr, "Failed to get channel config for %s\n", argv[optind]);
      exit(1);
   }
   if (opt_tid_only)
   {
      if (opt_tid_list)
      {
         const char * chn_name;
         int tpid;
         while (FilterVdrChannelFreq(opt_chanels_conf, freq_par.freq, &chn_name, &tpid))
         {
            if (tpid != 0)
            {
               printf("%d\t%s\n", tpid, chn_name);
            }
         }
      }
      else
      {
         printf("%d\n", freq_par.tpid);
      }
      exit(0);
   }

   if (!tune(&freq_par))
   {
      // tuning failed: query & print current parameters
      if (opt_query)
         query();

      exit(1);
   }

   if (opt_verbose)
   {
      ScanTtx(argv[optind], freq_par.tpid);
   }
   else if (opt_nowait == FALSE)
   {
      // wait until STDIN closed or receiving a signal
      while (1)
      {
         char buf[100];
         if (read(0, buf, sizeof(buf)) <= 0)
            break;
      }
   }

   return 0;
}