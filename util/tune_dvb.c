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
 * Copyright 2020-2021 by T. Zoerner (tomzo at users.sf.net)
 *
 * ----------------------------------------------------------------------------
 *
 * This program tunes the channel named on the command line and keeps the
 * frontend device open until the process is killed or its STDIN is
 * closed. Another application can capture teletext on the given channel
 * concurrently.
 *
 * Parameters for tuning are looked up in a channels.conf file, which can
 * be either in VDR-1.7.4 format or in MPlayer / xine / kaffeine / ...
 * format (e.g. as created by tool "w_scan").
 *
 * Usage: tune_dvb [OPTIONS] <channel_name>
 *
 * Options:
 *
 * -c <path>: Specifies path and file name where to find channels.conf.
 * The file format is auto-detected.
 *
 * -p: In this mode the program just parses the channels.conf file for
 * determining the teletext PID of the given channel, prints it and
 * exits. This option is needed by the teletext grabber, as this util
 * can only tune the front-end, but the grabber needs to open the demux
 * device and then configure if with the PID returned by this option.
 *
 * -P: Similar -p, but returns teletext PID and name for all channels
 * sharing the same transponder/frequency then exits. In the output,
 * information of channels is separated by newline; each line contains
 * the channel's PID and name, separated by TAB.
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
#include <ctype.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <libzvbi.h>
#include <linux/dvb/frontend.h>

#include "scan_pat_pmt.h"

#define CHANNELS_CONF_NAME "channels.conf"
#define PATH_DEV_FRONTEND "/dev/dvb/adapter0/frontend0"
#define PATH_DEV_DEMUX "/dev/dvb/adapter0/demux0"

typedef enum
{
  TUNER_NORM_DVB_C,
  TUNER_NORM_DVB_S,
  TUNER_NORM_DVB_S2,
  TUNER_NORM_DVB_T,
  TUNER_NORM_DVB_T2,
  TUNER_NORM_COUNT
} TUNER_NORM;

// Set of parameters for tuning
typedef struct
{
   TUNER_NORM  norm;
   long        freq;
   long        symbolRate;
   int         codeRate;    // FEC
   int         codeRateLp;  // DVB-T only
   int         inversion;
   int         modulation;  // QAM
   int         bandwidth;   // DVB-T only
   int         transMode;   // DVB-T only
   int         guardBand;   // DVB-T only
   int         hierarchy;   // DVB-T only
   int         serviceId;
   int         ttxPid;
} T_DVB_FREQ;

// ----------------------------------------------------------------------------
// Helper function for parsing text lines of channels.conf
// - Given "max_cnt" should be +1 abouve max expected too catch excessive fields
// - Replaces all ':' in the given string with zero bytes and fills the given
//   array with pointers to the thus generated sub-strings
// - Newline char is stripped from the last field
//
static uint ParseSplitStringColon( char * sbuf, char ** fields, uint max_cnt )
{
   char * sep_ptr = sbuf;
   int field_idx;

   fields[0] = sbuf;
   for (field_idx = 1; field_idx < max_cnt; ++field_idx)
   {
      sep_ptr = strchr(sep_ptr, ':');
      if (sep_ptr == NULL)
         break;
      *(sep_ptr++) = 0;
      fields[field_idx] = sep_ptr;
   }

   // trim trailing newline char from last field
   char *p1 = fields[field_idx - 1];
   char *p2 = p1 + strlen(p1) - 1;
   while ((p2 >= p1) && isspace(*p2))
      *(p2--) = 0;

   // detect excessive fields above max_cnt
   if (strchr(fields[field_idx - 1], ':') != NULL)
      field_idx += 1;

   return field_idx;
}

// ----------------------------------------------------------------------------
// Sub-function for parsing the VDR options field
//
static bool ParseVdrChannelParams( const char * field_par, T_DVB_FREQ * freq_par )
{
   const char * field_start = field_par;
   const char * field_end = field_par + strlen(field_par);
   bool result = TRUE;

   while (field_par < field_end)
   {
      char op = *(field_par++);
      char * ends;
      long val;

      switch (op)
      {
         // specifiers that are followed by a decimal value
         case 'I':
         case 'C':
         case 'D':
         case 'M':
         case 'B':
         case 'T':
         case 'G':
         case 'Y':
         case 'S':
            val = strtol(field_par, &ends, 10);
            if (ends != field_par)
            {
               field_par = ends;

               if (op == 'M')
               {
                  switch (val)
                  {
                     case 2:  freq_par->modulation = QPSK; break;
                     case 5:  freq_par->modulation = PSK_8; break;
                     case 6:  freq_par->modulation = APSK_16; break;
                     case 7:  freq_par->modulation = APSK_32; break;
                     case 10: freq_par->modulation = VSB_8; break;
                     case 11: freq_par->modulation = VSB_16; break;
                     case 12: freq_par->modulation = DQPSK; break;

                     case 16:  freq_par->modulation = QAM_16; break;
                     case 32:  freq_par->modulation = QAM_32; break;
                     case 64:  freq_par->modulation = QAM_64; break;
                     case 128:  freq_par->modulation = QAM_128; break;
                     case 256:  freq_par->modulation = QAM_256; break;
                     case 998:  // 998 acc. to w_scan
                     case 999:  freq_par->modulation = QAM_AUTO; break;
                     default:
                        fprintf(stderr, "WARNING: invalid modulation value %ld in VDR: %s", val, field_start);
                        result = FALSE;
                  }
               }
               else if (op == 'B')
               {
                  switch (val)
                  {
                     case 8:  freq_par->bandwidth = BANDWIDTH_8_MHZ; break;
                     case 7:  freq_par->bandwidth = BANDWIDTH_7_MHZ; break;
                     case 6:  freq_par->bandwidth = BANDWIDTH_6_MHZ; break;
                     case 5:  freq_par->bandwidth = BANDWIDTH_5_MHZ; break;
                     case 10:  freq_par->bandwidth = BANDWIDTH_10_MHZ; break;
                     case 1712: freq_par->bandwidth = BANDWIDTH_1_712_MHZ; break;
                     case 999:  freq_par->bandwidth = BANDWIDTH_AUTO; break;
                     default:
                        fprintf(stderr, "WARNING: invalid bandwidth value %ld in VDR: %s", val, field_start);
                        result = FALSE;
                  }
               }
               else if (op == 'T')
               {
                  switch (val)
                  {
                     case 2:   freq_par->transMode = TRANSMISSION_MODE_2K; break;
                     case 8:   freq_par->transMode = TRANSMISSION_MODE_8K; break;
                     case 4:   freq_par->transMode = TRANSMISSION_MODE_4K; break;
                     case 1:   freq_par->transMode = TRANSMISSION_MODE_1K; break;
                     case 16:  freq_par->transMode = TRANSMISSION_MODE_16K; break;
                     case 32:  freq_par->transMode = TRANSMISSION_MODE_32K; break;
                     case 999: freq_par->transMode = TRANSMISSION_MODE_AUTO; break;
                     default:
                        fprintf(stderr, "WARNING: invalid transmission mode value %ld in VDR: %s", val, field_start);
                        result = FALSE;
                  }
               }
               else if (op == 'I')
               {
                  switch (val)
                  {
                     case 0:   freq_par->inversion = INVERSION_OFF; break;
                     case 1:   freq_par->inversion = INVERSION_ON; break;
                     case 999: freq_par->inversion = INVERSION_AUTO; break;
                     default:
                        fprintf(stderr, "WARNING: invalid inversion value %ld in VDR: %s", val, field_start);
                        result = FALSE;
                  }
               }
               else if ((op == 'C') || (op == 'C'))
               {
                  int conv = 0;
                  switch (val)
                  {
                     case 0: conv = FEC_NONE; break;
                     case 12: conv = FEC_1_2; break;
                     case 23: conv = FEC_2_3; break;
                     case 25: conv = FEC_2_5; break;
                     case 34: conv = FEC_3_4; break;
                     case 35: conv = FEC_3_5; break;
                     case 45: conv = FEC_4_5; break;
                     case 56: conv = FEC_5_6; break;
                     case 67: conv = FEC_6_7; break;
                     case 78: conv = FEC_7_8; break;
                     case 89: conv = FEC_8_9; break;
                     case 910: conv = FEC_9_10; break;
                     case 999: conv = FEC_AUTO; break;
                     default:
                        fprintf(stderr, "WARNING: invalid code rate value %ld in VDR: %s", val, field_start);
                        result = FALSE;
                  }
                  if (op == 'C')
                     freq_par->codeRate = conv;
                  else
                     freq_par->codeRateLp = conv;
               }
               else if (op == 'G')
               {
                  switch (val)
                  {
                     case 32:    freq_par->guardBand = GUARD_INTERVAL_1_32; break;
                     case 16:    freq_par->guardBand = GUARD_INTERVAL_1_16; break;
                     case 8:     freq_par->guardBand = GUARD_INTERVAL_1_8; break;
                     case 4:     freq_par->guardBand = GUARD_INTERVAL_1_4; break;
                     case 128:   freq_par->guardBand = GUARD_INTERVAL_1_128; break;
                     case 19128: freq_par->guardBand = GUARD_INTERVAL_19_128; break;
                     case 19256: freq_par->guardBand = GUARD_INTERVAL_19_256; break;
                     case 999:   freq_par->guardBand = GUARD_INTERVAL_AUTO; break;
                     default:
                        fprintf(stderr, "WARNING: invalid guard band value %ld in VDR: %s", val, field_start);
                        result = FALSE;
                  }
               }
               else if (op == 'Y')
               {
                  switch (val)
                  {
                     case 0:    freq_par->hierarchy = HIERARCHY_NONE; break;
                     case 1:    freq_par->hierarchy = HIERARCHY_1; break;
                     case 2:    freq_par->hierarchy = HIERARCHY_2; break;
                     case 4:    freq_par->hierarchy = HIERARCHY_4; break;
                     case 999:   freq_par->hierarchy = HIERARCHY_AUTO; break;
                     default:
                        fprintf(stderr, "WARNING: invalid guard band value %ld in VDR: %s", val, field_start);
                        result = FALSE;
                  }
               }
               else if (op == 'S')
               {
                  switch (val)
                  {
                     case 0:   // DVB-S or DVB-T
                        break;
                     case 1:   // DVB-S2 or DVB-T2
                        if (freq_par->norm == TUNER_NORM_DVB_T)
                           freq_par->norm = TUNER_NORM_DVB_T2;
                        else if (freq_par->norm == TUNER_NORM_DVB_S)
                           freq_par->norm = TUNER_NORM_DVB_S2;
                        break;
                     default:
                        fprintf(stderr, "WARNING: invalid DVB-S generation %ld in VDR: %s", val, field_start);
                        result = FALSE;
                  }
               }
            }
            else
            {
               fprintf(stderr, "WARNING: missing value for param '%c' in VDR: %s", op, field_start);
               result = FALSE;
            }
            break;

         // parameters that are not followed by value
         case 'H':
         case 'V':
         case 'R':
         case 'L':
            // POLARIZATION_HORIZONTAL
            // POLARIZATION_VERTICAL
            // POLARIZATION_CIRCULAR_LEFT
            // POLARIZATION_CIRCULAR_RIGHT
            break;

         // ignore whitespace
         case ' ':
         case '\t':
            break;
         default:
            fprintf(stderr, "WARNING: invalid param '%c' in VDR: %s", op, field_start);
            result = FALSE;
            break;
      }
   }
   return result;
}

// ----------------------------------------------------------------------------
// Conversion helper functions for Mplayer configuration
//
#if 0
static int ParseMplayerPolarization( const char * pName, char ** pError )
{
   if (strncmp(pName, "h") == 0)
      return POLARIZATION_HORIZONTAL;
   if (strncmp(pName, "v") == 0)
      return POLARIZATION_VERTICAL;
   if (strncmp(pName, "l") == 0)
      return POLARIZATION_CIRCULAR_LEFT;
   if (strncmp(pName, "r") == 0)
      return POLARIZATION_CIRCULAR_RIGHT;
   *pError = "invalid POLARIZATION";
   return 0;
}
#endif

static int ParseMplayerInversion( const char * pName, char ** pError )
{
   if (strcmp(pName, "INVERSION_AUTO") == 0)
       return INVERSION_AUTO;
   if (strcmp(pName, "INVERSION_OFF") == 0)
       return INVERSION_OFF;
   if (strcmp(pName, "INVERSION_ON") == 0)
       return INVERSION_ON;
   *pError = "invalid INVERSION";
   return 0;
}

static int ParseMplayerCoderate( const char * pName, char ** pError )
{
   if (strcmp(pName, "FEC_AUTO") == 0)
      return FEC_AUTO;
   if (strcmp(pName, "FEC_NONE") == 0)
       return FEC_NONE;
   if (strcmp(pName, "FEC_2_5") == 0)
       return FEC_2_5;
   if (strcmp(pName, "FEC_1_2") == 0)
       return FEC_1_2;
   if (strcmp(pName, "FEC_3_5") == 0)
       return FEC_3_5;
   if (strcmp(pName, "FEC_2_3") == 0)
       return FEC_2_3;
   if (strcmp(pName, "FEC_3_4") == 0)
       return FEC_3_4;
   if (strcmp(pName, "FEC_4_5") == 0)
       return FEC_4_5;
   if (strcmp(pName, "FEC_5_6") == 0)
       return FEC_5_6;
   if (strcmp(pName, "FEC_6_7") == 0)
       return FEC_6_7;
   if (strcmp(pName, "FEC_7_8") == 0)
       return FEC_7_8;
   if (strcmp(pName, "FEC_8_9") == 0)
       return FEC_8_9;
   if (strcmp(pName, "FEC_9_10") == 0)
       return FEC_9_10;
   *pError = "invalid FEC code rate";
   return 0;
}

static int ParseMplayerModulation( const char * pName, char ** pError )
{
   if (strcmp(pName, "QAM_AUTO") == 0)
       return QAM_AUTO;
   if (strcmp(pName, "QPSK") == 0)
       return QPSK;
   if (strcmp(pName, "QAM_16") == 0)
       return QAM_16;
   if (strcmp(pName, "QAM_32") == 0)
       return QAM_32;
   if (strcmp(pName, "QAM_64") == 0)
       return QAM_64;
   if (strcmp(pName, "QAM_128") == 0)
       return QAM_128;
   if (strcmp(pName, "QAM_256") == 0)
       return QAM_256;
   if (strcmp(pName, "VSB_8") == 0)
       return VSB_8;
   if (strcmp(pName, "VSB_16") == 0)
       return VSB_16;
   if (strcmp(pName, "PSK_8") == 0)
       return PSK_8;
   if (strcmp(pName, "APSK_16") == 0)
       return APSK_16;
   if (strcmp(pName, "APSK_32") == 0)
       return APSK_32;
   if (strcmp(pName, "DQPSK") == 0)
       return DQPSK;
   if (strcmp(pName, "QAM_4_NR") == 0)
       return QAM_4_NR;
   *pError = "invalid modulation";
   return 0;
}

static int ParseMplayerBandwidth( const char * pName, char ** pError )
{
   if (strcmp(pName, "BANDWIDTH_AUTO") == 0)
      return BANDWIDTH_AUTO;
   if (strcmp(pName, "BANDWIDTH_8_MHZ") == 0)
       return 8000000;
   if (strcmp(pName, "BANDWIDTH_7_MHZ") == 0)
       return 7000000;
   if (strcmp(pName, "BANDWIDTH_6_MHZ") == 0)
       return 6000000;
   if (strcmp(pName, "BANDWIDTH_5_MHZ") == 0)
       return 5000000;
   if (strcmp(pName, "BANDWIDTH_10_MHZ") == 0)
       return 10000000;
   if (strcmp(pName, "BANDWIDTH_1_712_MHZ") == 0)
       return 1712000;
   *pError = "invalid bandwidth";
   return 0;
}

static int ParseMplayerTransmissionMode( const char * pName, char ** pError )
{
   if (strcmp(pName, "TRANSMISSION_MODE_AUTO") == 0)
      return TRANSMISSION_MODE_AUTO;
   if (strcmp(pName, "TRANSMISSION_MODE_1K") == 0)
       return TRANSMISSION_MODE_1K;
   if (strcmp(pName, "TRANSMISSION_MODE_2K") == 0)
       return TRANSMISSION_MODE_2K;
   if (strcmp(pName, "TRANSMISSION_MODE_4K") == 0)
       return TRANSMISSION_MODE_4K;
   if (strcmp(pName, "TRANSMISSION_MODE_8K") == 0)
       return TRANSMISSION_MODE_8K;
   if (strcmp(pName, "TRANSMISSION_MODE_16K") == 0)
       return TRANSMISSION_MODE_16K;
   if (strcmp(pName, "TRANSMISSION_MODE_32K") == 0)
       return TRANSMISSION_MODE_32K;
   if (strcmp(pName, "TRANSMISSION_MODE_C1") == 0)
       return TRANSMISSION_MODE_C1;
   if (strcmp(pName, "TRANSMISSION_MODE_C3780") == 0)
       return TRANSMISSION_MODE_C3780;
   *pError = "invalid transmission mode";
   return 0;
}

static int ParseMplayerGuardInterval( const char * pName, char ** pError )
{
   if (strcmp(pName, "GUARD_INTERVAL_AUTO") == 0)
      return GUARD_INTERVAL_AUTO;
   if (strcmp(pName, "GUARD_INTERVAL_1_32") == 0)
       return GUARD_INTERVAL_1_32;
   if (strcmp(pName, "GUARD_INTERVAL_1_16") == 0)
       return GUARD_INTERVAL_1_16;
   if (strcmp(pName, "GUARD_INTERVAL_1_8") == 0)
       return GUARD_INTERVAL_1_8;
   if (strcmp(pName, "GUARD_INTERVAL_1_4") == 0)
       return GUARD_INTERVAL_1_4;
   if (strcmp(pName, "GUARD_INTERVAL_1_128") == 0)
       return GUARD_INTERVAL_1_128;
   if (strcmp(pName, "GUARD_INTERVAL_19_128") == 0)
       return GUARD_INTERVAL_19_128;
   if (strcmp(pName, "GUARD_INTERVAL_19_256") == 0)
       return GUARD_INTERVAL_19_256;
   if (strcmp(pName, "GUARD_INTERVAL_PN420") == 0)
       return GUARD_INTERVAL_PN420;
   if (strcmp(pName, "GUARD_INTERVAL_PN595") == 0)
       return GUARD_INTERVAL_PN595;
   if (strcmp(pName, "GUARD_INTERVAL_PN945") == 0)
       return GUARD_INTERVAL_PN945;
   *pError = "invalid guard interval";
   return 0;
}

static int ParseMplayerHierarchy( const char * pName, char ** pError )
{
   if (strcmp(pName, "HIERARCHY_AUTO") == 0)
      return HIERARCHY_AUTO;
   if (strcmp(pName, "HIERARCHY_NONE") == 0)
       return HIERARCHY_NONE;
   if (strcmp(pName, "HIERARCHY_1") == 0)
       return HIERARCHY_1;
   if (strcmp(pName, "HIERARCHY_2") == 0)
       return HIERARCHY_2;
   if (strcmp(pName, "HIERARCHY_4") == 0)
       return HIERARCHY_4;
   *pError = "invalid hierarchy";
   return 0;
}

static int ParseAtoi( const char * pName, char ** pError )
{
   char * pEnd;
   long longVal = strtol(pName, &pEnd, 0);
   long intVal = longVal;
   if ((pEnd != pName) && (*pEnd == 0) && (intVal == longVal))
      return intVal;
   *pError = "expected numerical value";
   return 0;
}

static long ParseAtol( const char * pName, char ** pError )
{
   char * pEnd;
   long longVal = strtol(pName, &pEnd, 0);
   if ((pEnd != pName) && (*pEnd == 0))
      return longVal;
   *pError = "expected numerical value";
   return 0;
}

// ----------------------------------------------------------------------------
// Extract all channels from mplayer "channels.conf" table
//
// - VDR:
//   - Format: http://www.vdr-wiki.de/wiki/index.php/Channels.conf
//   - List is a colon-separated table with one line per channel; each line contains:
//     name:freq:param:signal source:symbol rate:VPID:APID:TPID:CAID:SID:NID:TID:RID
//
// - Mplayer:
//   - Format differs between DVB-C, DVB-S, DVB-T2: different number of columns
//   - See dump_mplayer.c in https://www.gen2vdr.de/wirbel/w_scan/archiv.html
//
//   DVB-C:  NAME:FREQUENCY:INVERSION:SYMBOL_RATE:FEC:MODULATION:PID_VIDEO:PID_AUDIO:SERVICE_ID
//   DVB-S:  NAME:FREQUENCY:POLARIZATION:ROTOR_POS:SYMBOL_RATE:PID_VIDEO:PID_AUDIO:SERVICE_ID
//   DVB-T2: NAME:FREQUENCY:INVERSION:BANDWIDTH:SYMBOL_RATE:SYMBOL_RATE_LP:MODULATION:TRANSMISSION:GUARD_BAND:HIEARCHY:PID_VIDEO:PID_AUDIO:SERVICE_ID
//
//   Examples:
//   DVB-C:  name:610000000:INVERSION_AUTO:6900000:FEC_NONE:QAM_64:1511:1512:50122
//   DVB-S:  name:11727:h:0:27500:109:209:9
//   DVB-T2: name:474000000:INVERSION_AUTO:BANDWIDTH_8_MHZ:FEC_AUTO:FEC_AUTO:QAM_256:TRANSMISSION_MODE_32K:GUARD_INTERVAL_1_128:HIERARCHY_AUTO:6601:6602:17540
//
typedef enum
{
   CCNF_VDR_NAME,
   CCNF_VDR_FREQUENCY,
   CCNF_VDR_PARAM,
   CCNF_VDR_SIG_SRC,
   CCNF_VDR_SYMBOL_RATE,
   CCNF_VDR_PID_VIDEO,
   CCNF_VDR_PID_AUDIO,
   CCNF_VDR_PID_TTX,
   CCNF_VDR_PID_CA,
   CCNF_VDR_SERVICE_ID,
   CCNF_VDR_NID,
   CCNF_VDR_TID,
   CCNF_VDR_RID,
   CCNF_VDR_COUNT
} T_FIELDS_VDR;

static bool
ParseChannelEntry( char * sbuf, T_DVB_FREQ * freq_par )
{
   char * fields[20];  // MAX(8, 7, 13, 13)
   bool result = FALSE;
   char * error = NULL;

   memset(freq_par, 0, sizeof(*freq_par));

   uint fieldCnt = ParseSplitStringColon(sbuf, fields, 20);

   if ( (fieldCnt == 13) &&
        (strncmp(fields[2], "INVERSION", 9) != 0) )  // MPlayer DVB-T2
   {
      // strip bouquet name from network name
      char * p1 = strchr(fields[CCNF_VDR_NAME], ';');
      if (p1 != NULL)
         *p1 = 0;

      if (strcmp(fields[CCNF_VDR_SIG_SRC], "C") == 0)
         freq_par->norm = TUNER_NORM_DVB_C;
      else if (strcmp(fields[CCNF_VDR_SIG_SRC], "T") == 0)
         freq_par->norm = TUNER_NORM_DVB_T;
      else
         freq_par->norm = TUNER_NORM_DVB_S;

      freq_par->freq = atol(fields[CCNF_VDR_FREQUENCY]);
      while (freq_par->freq <= 1000000)
         freq_par->freq *= 1000;

      freq_par->ttxPid     = atol(fields[CCNF_VDR_PID_TTX]);  // implicitly ignore ";" and following
      freq_par->serviceId  = atol(fields[CCNF_VDR_SYMBOL_RATE]);

      freq_par->symbolRate = atol(fields[CCNF_VDR_SYMBOL_RATE]) * 1000;
      freq_par->modulation = QAM_AUTO;
      freq_par->inversion  = INVERSION_AUTO;
      freq_par->codeRate   = FEC_AUTO;
      freq_par->codeRateLp = FEC_AUTO;
      freq_par->bandwidth  = BANDWIDTH_AUTO;
      freq_par->transMode  = TRANSMISSION_MODE_AUTO;
      freq_par->guardBand  = GUARD_INTERVAL_AUTO;
      freq_par->hierarchy  = HIERARCHY_AUTO;

      // skip radio channels etc.
      if (atol(fields[CCNF_VDR_PID_VIDEO]) != 0)
      {
         result = ParseVdrChannelParams(fields[CCNF_VDR_PARAM], freq_par);
      }
   }
   else if (fieldCnt == 9)
   {
      // skip radio channels etc.
      if (atol(fields[6]) != 0)  // PID_VIDEO
      {
         // DVB-C:  NAME:FREQUENCY:INVERSION:SYMBOL_RATE:FEC:MODULATION:PID_VIDEO:PID_AUDIO:SERVICE_ID
         freq_par->norm       = TUNER_NORM_DVB_C;
         freq_par->freq       = ParseAtol(fields[1], &error);
         freq_par->inversion  = ParseMplayerInversion(fields[2], &error);
         freq_par->symbolRate = ParseAtoi(fields[3], &error);
         freq_par->codeRate   = ParseMplayerCoderate(fields[4], &error);
         freq_par->modulation = ParseMplayerModulation(fields[5], &error);
         freq_par->serviceId  = ParseAtoi(fields[8], &error);

         if (error == NULL)
            result = TRUE;
         else
            fprintf(stderr, "WARNING: Error parsing DVB-C config: %s: %s\n", error, sbuf);
      }
   }
   else if (fieldCnt == 8)
   {
      // skip radio channels etc.
      if (atol(fields[5]) != 0)  // PID_VIDEO
      {
         // DVB-S:  NAME:FREQUENCY:POLARIZATION:ROTOR_POS:SYMBOL_RATE:PID_VIDEO:PID_AUDIO:SERVICE_ID
         freq_par->norm       = TUNER_NORM_DVB_S;
         freq_par->freq       = ParseAtol(fields[1], &error);
         //freq_par->polarization = ParseMplayerPolarization(fields[2], &error);
         //freq_par->rotorPos = ParseWintvCfg_Atoi(fields[3], &error);
         freq_par->symbolRate = ParseAtoi(fields[4], &error);
         freq_par->serviceId  = ParseAtoi(fields[7], &error);
         freq_par->inversion  = INVERSION_AUTO;
         freq_par->modulation = QAM_AUTO;
         freq_par->codeRate   = FEC_AUTO;

         if (error == NULL)
            result = TRUE;
         else
            fprintf(stderr, "WARNING: Error parsing DVB-S config: %s: %s\n", error, sbuf);
      }
   }
   else if (fieldCnt == 13)
   {
      // skip radio channels etc.
      if (atol(fields[10]) != 0)  // PID_VIDEO
      {
         // DVB-T2: NAME:FREQUENCY:INVERSION:BANDWIDTH:SYMBOL_RATE:SYMBOL_RATE_LP:MODULATION:TRANSMISSION:GUARD_BAND:HIEARCHY:PID_VIDEO:PID_AUDIO:SERVICE_ID
         freq_par->norm       = TUNER_NORM_DVB_T2;
         freq_par->freq       = ParseAtol(fields[1], &error);
         freq_par->inversion  = ParseMplayerInversion(fields[2], &error);
         freq_par->bandwidth  = ParseMplayerBandwidth(fields[3], &error);
         freq_par->codeRate   = ParseAtoi(fields[4], &error);
         freq_par->codeRateLp = ParseAtoi(fields[5], &error);
         freq_par->modulation = ParseMplayerModulation(fields[6], &error);
         freq_par->transMode  = ParseMplayerTransmissionMode(fields[7], &error);
         freq_par->guardBand  = ParseMplayerGuardInterval(fields[8], &error);
         freq_par->hierarchy  = ParseMplayerHierarchy(fields[9], &error);
         freq_par->serviceId  = ParseAtoi(fields[12], &error);

         if (error == NULL)
            result = TRUE;
         else
            fprintf(stderr, "WARNING: Error parsing DVB-T config: %s: %s\n", error, sbuf);
      }
   }
   else
   {
      fprintf(stderr, "WARNING: Malformed mplayer channels.conf entry skipped (unexpected field count): %s\n", sbuf);
   }
   return result;
}


// ----------------------------------------------------------------------------
// Search VDR "channels.conf" table for the parameters of a given channel name
// - returns parameters required for tuning & the teletext PID

static bool
GetChannelConf( const char * conf_path, const char * chn_name, T_DVB_FREQ * freq_par )
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
               result = ParseChannelEntry(sbuf, freq_par);
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
FilterChannelFreq( const char * conf_path, long freq, const char ** pp_name, int * p_srv_id, int * p_tpid )
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
            if (ParseChannelEntry(sbuf, &freq_par) && (freq_par.freq == freq))
            {
               *pp_name = strdup(sbuf);
               *p_srv_id = freq_par.serviceId;
               *p_tpid = freq_par.ttxPid;
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

int tune(const T_DVB_FREQ * pFreqPar)
{
   int result = FALSE;
   int fd = open(PATH_DEV_FRONTEND, O_RDWR);
   if (fd != -1)
   {
      struct dtv_property props[13];
      int propCnt = 0;
#define VBI_DVB_ADD_CMD(CMD, DATA)  do{ \
         props[propCnt].cmd = CMD; \
         props[propCnt].u.data = DATA; \
         propCnt += 1; }while(0)
      memset(props, 0, sizeof(props));

      if ( (pFreqPar->norm == TUNER_NORM_DVB_T) ||
           (pFreqPar->norm == TUNER_NORM_DVB_T2))
      {
         if (pFreqPar->norm == TUNER_NORM_DVB_T)
            VBI_DVB_ADD_CMD( DTV_DELIVERY_SYSTEM, SYS_DVBT);
         else
            VBI_DVB_ADD_CMD( DTV_DELIVERY_SYSTEM, SYS_DVBT2);

         VBI_DVB_ADD_CMD( DTV_FREQUENCY, pFreqPar->freq);
         VBI_DVB_ADD_CMD( DTV_INVERSION, pFreqPar->inversion);
         VBI_DVB_ADD_CMD( DTV_BANDWIDTH_HZ, pFreqPar->bandwidth);
         VBI_DVB_ADD_CMD( DTV_CODE_RATE_HP, pFreqPar->codeRate);
         VBI_DVB_ADD_CMD( DTV_CODE_RATE_LP, pFreqPar->codeRateLp);
         VBI_DVB_ADD_CMD( DTV_MODULATION, pFreqPar->modulation);
         VBI_DVB_ADD_CMD( DTV_TRANSMISSION_MODE, pFreqPar->transMode);
         VBI_DVB_ADD_CMD( DTV_GUARD_INTERVAL, pFreqPar->guardBand);
         VBI_DVB_ADD_CMD( DTV_HIERARCHY, pFreqPar->hierarchy);
      }
      else  // DVB-C or DVB-S: note for DVB-S some params are always AUTO
      {
         if (pFreqPar->norm == TUNER_NORM_DVB_S)
            VBI_DVB_ADD_CMD( DTV_DELIVERY_SYSTEM, SYS_DVBS);
         else if (pFreqPar->norm == TUNER_NORM_DVB_S2)
            VBI_DVB_ADD_CMD( DTV_DELIVERY_SYSTEM, SYS_DVBS2);
         else
            VBI_DVB_ADD_CMD( DTV_DELIVERY_SYSTEM, SYS_DVBC_ANNEX_C);

         VBI_DVB_ADD_CMD( DTV_FREQUENCY, pFreqPar->freq);
         VBI_DVB_ADD_CMD( DTV_INVERSION, pFreqPar->inversion);
         VBI_DVB_ADD_CMD( DTV_MODULATION, pFreqPar->modulation);
         VBI_DVB_ADD_CMD( DTV_SYMBOL_RATE, pFreqPar->symbolRate);
         VBI_DVB_ADD_CMD( DTV_INNER_FEC, pFreqPar->codeRate);
      }
      VBI_DVB_ADD_CMD( DTV_TUNE, 0);
      assert(propCnt < sizeof(props)/sizeof(props[0]));

      struct dtv_properties prop =
      {
         .num = propCnt,
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
   if (!GetChannelConf(opt_chanels_conf, argv[optind], &freq_par))
   {
      fprintf(stderr, "Failed to get channel config for %s\n", argv[optind]);
      exit(1);
   }

   scan_services_t srvList[32];
   srvList[0].service_id = freq_par.serviceId;
   srvList[0].ttx_pid = freq_par.ttxPid;
   uint srvCnt = 1;

   if (opt_tid_only)
   {
      if (opt_tid_list)
      {
         const char * chn_name[32];
         int srvId;
         int tpid;
         bool needPatPmt = (freq_par.ttxPid == 0);
         chn_name[0] = argv[optind];

         while ((srvCnt < 32) &&
                FilterChannelFreq(opt_chanels_conf, freq_par.freq, &chn_name[srvCnt], &srvId, &tpid))
         {
            if (srvId != srvList[0].service_id)
            {
               srvList[srvCnt].service_id = srvId;
               srvList[srvCnt].ttx_pid = tpid;
               srvCnt += 1;
               needPatPmt |= (tpid == 0);
            }
         }
         if (needPatPmt)
         {
            if (opt_verbose)
               fprintf(stderr, "Tuning for scanning PAT/PMT for teletext PIDs\n");

            if (tune(&freq_par))
               scan_pat_pmt(PATH_DEV_DEMUX, srvList, srvCnt);
         }
         if (srvList[0].ttx_pid == 0)
         {
            fprintf(stderr, "%s: ERROR: no teletext PID for %s\n", argv[0], chn_name[0]);
            exit(1);
         }
         for (int idx = 0; idx < srvCnt; ++idx)
         {
            if (srvList[idx].ttx_pid != 0)
               printf("%d\t%s\n", srvList[idx].ttx_pid, chn_name[idx]);
         }
      }
      else
      {
         if (srvList[0].ttx_pid == 0)
         {
            if (tune(&freq_par))
            {
               scan_pat_pmt(PATH_DEV_DEMUX, srvList, 1);

               if (srvList[0].ttx_pid == 0)
               {
                  fprintf(stderr, "%s: ERROR: no teletext PID for %s\n", argv[0], argv[optind]);
                  exit(1);
               }
            }
            else
               exit(1);
         }
         printf("%d\n", srvList[0].ttx_pid);
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

   if (srvList[0].ttx_pid == 0)
   {
      scan_pat_pmt(PATH_DEV_DEMUX, srvList, 1);

      if (srvList[0].ttx_pid == 0)
      {
         fprintf(stderr, "%s: ERROR: no teletext PID for %s\n", argv[0], argv[optind]);
         exit(1);
      }
      freq_par.ttxPid = srvList[0].ttx_pid;
   }

   if (opt_verbose)
   {
      ScanTtx(argv[optind], freq_par.ttxPid);
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
