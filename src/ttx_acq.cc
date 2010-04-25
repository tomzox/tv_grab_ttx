/*
 * Teletext EPG grabber: VBI data acquisition control
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
 * $Id: ttx_acq.cc,v 1.1 2010/04/25 14:18:10 tom Exp $
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

#include <string>

#include "libzvbi.h"
#include "boost/regex.h"
#include "boost/regex.hpp"

using namespace std;
using namespace boost;

#include "ttx_db.h"
#include "ttx_util.h"
#include "ttx_acq.h"

class TTX_ACQ_PKG
{
   typedef uint8_t vt_pkg_raw[VT_PKG_RAW_LEN];
public:
   uint16_t     m_page_no;
   uint16_t     m_ctrl_lo;
   uint8_t      m_ctrl_hi;
   uint8_t      m_pkg;
   vt_pkg_raw   m_data;
private:
   // constructor for page header packet
   void set_pg_header(int page, int ctrl, const uint8_t * data) {
      m_page_no = page;
      m_ctrl_lo = ctrl & 0xFFFFU;
      m_ctrl_hi = ctrl >> 16;
      m_pkg = 0;
      memcpy(m_data, data, VT_PKG_RAW_LEN);
   };
   // constructor for page body packets
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
public:
   bool feed_ttx_pkg(uint8_t * data);
   bool feed_vps_pkg(const uint8_t * data);
   bool dump_raw_pkg(int fd);
   bool read_raw_pkg(int fd);
};

/* ------------------------------------------------------------------------- */

class TTX_ACQ_SRC
{
public:
   virtual ~TTX_ACQ_SRC() {};
   virtual bool read_pkg(TTX_ACQ_PKG& ret_buf, time_t time_limit, time_t *ts) = 0;
};

class TTX_ACQ_ZVBI : public TTX_ACQ_SRC
{
public:
   TTX_ACQ_ZVBI(const char * p_dev, int dvb_pid);
   virtual bool read_pkg(TTX_ACQ_PKG& ret_buf, time_t time_limit, time_t *ts);
   virtual ~TTX_ACQ_ZVBI();
private:
   void device_read();

   vbi_capture * mp_cap;
   vbi_capture_buffer * mp_buf;
   int m_line_count;
   int m_line_idx;
   time_t m_timestamp;
};

class TTX_ACQ_FILE : public TTX_ACQ_SRC
{
public:
   TTX_ACQ_FILE(const char * p_file);
   virtual ~TTX_ACQ_FILE();
   virtual bool read_pkg(TTX_ACQ_PKG& ret_buf, time_t time_limit, time_t *ts);
private:
   int m_fd;
   time_t m_timestamp;
};

/* ------------------------------------------------------------------------------
 * Decoding of teletext packets (esp. hamming decoding)
 * - for page header (packet 0) the page number is derived
 * - for teletext packets (packet 1..25) the payload data is just copied
 * - for PDC and NI (packet 30) the CNI is derived
 * - returns a list with 5 elements: page/16, ctrl/16, ctrl/8, pkg/8, data/40*8
 */
bool TTX_ACQ_PKG::feed_ttx_pkg(uint8_t * data)
{
   int mpag = vbi_unham16p(data);
   int mag = mpag & 7;
   int y = (mpag & 0xf8) >> 3;
   bool result = false;

   if (y == 0) {
      // teletext page header (packet #0)
      int page = (mag << 8) | vbi_unham16p(data + 2);
      int ctrl = (vbi_unham16p(data + 4)) |
                 (vbi_unham16p(data + 6) << 8) |
                 (vbi_unham16p(data + 8) << 16);
      // drop filler packets
      if ((page & 0xFF) != 0xFF) {
         vbi_unpar(data + 2, 40);
         set_pg_header(page, ctrl, data + 2);
         result = true;
      }
   }
   else if (y<=25) {
      // regular teletext packet (lines 1-25)
      vbi_unpar(data + 2, 40);
      set_pg_row(mag, y, data + 2);
      result = true;
   }
   else if (y == 30) {
      int dc = (vbi_unham16p(data + 2) & 0x0F) >> 1;
      if (dc == 0) {
         // packet 8/30/1
         int cni = vbi_rev16p(data + 2+7);
         if ((cni != 0) && (cni != 0xffff)) {
            set_cni(mag, 30, cni);
            result = true;
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
            set_cni(mag, 30, cni);
            result = true;
         }
      }
   }
   return result;
}

/* ------------------------------------------------------------------------------
 * Decoding of VPS packets
 * - returns a list of 5 elements (see TTX decoder), but data contains only 16-bit CNI
 */
bool TTX_ACQ_PKG::feed_vps_pkg(const uint8_t * data)
{
   bool result = false;
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
      set_cni(0, 32, cni);
      result = true;
   }
   return result;
}

/* ------------------------------------------------------------------------------
 * Write a single TTX packet or CNI to STDOUT in a packed binary format
 */
bool TTX_ACQ_PKG::dump_raw_pkg(int fd)
{
   const uint8_t * p_data = reinterpret_cast<const uint8_t*>(this);
   int len = sizeof(*this);
   ssize_t wstat;

   do {
      wstat = write(fd, p_data, len);
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

bool TTX_ACQ_PKG::read_raw_pkg(int fd)
{
   size_t line_off = 0;
   ssize_t rstat;

   do {
      rstat = read(fd, reinterpret_cast<uint8_t*>(this) + line_off,
                       sizeof(*this) - line_off);
      if (rstat > 0) {
         line_off += rstat;
      }
      else if (rstat == 0) {
         if (line_off > 0)
            fprintf(stderr, "Warning: short read from input file (last record is truncated)\n");
         break;
      }
   } while (   ((rstat == -1) && (errno == EINTR))
            || (line_off < sizeof(*this)) );

   if (rstat < 0) {
      fprintf(stderr, "read error from input file: %s\n", strerror(errno));
   }

   return (line_off == sizeof(*this));
}

/* ------------------------------------------------------------------------------
 */
TTX_ACQ_FILE::TTX_ACQ_FILE(const char * p_file)
{
   if (*p_file == 0) {
      m_fd = dup(0);
      m_timestamp = time(NULL);
   }
   else {
      m_fd = open(p_file, O_RDONLY);
      if (m_fd < 0) {
         fprintf(stderr, "Failed to open %s: %s\n", p_file, strerror(errno));
         exit(1);
      }
      struct stat st;
      if (fstat(m_fd, &st) != 0) {
         fprintf(stderr, "Failed to fstat opened %s: %s\n", p_file, strerror(errno));
         exit(1);
      }
      m_timestamp = st.st_mtime;
   }
}

TTX_ACQ_FILE::~TTX_ACQ_FILE()
{
   close(m_fd);
}

bool TTX_ACQ_FILE::read_pkg(TTX_ACQ_PKG& ret_buf, time_t time_limit, time_t *ts)
{
   *ts = m_timestamp;
   return ret_buf.read_raw_pkg(m_fd);
}

/* ------------------------------------------------------------------------------
 * Open the VBI device for capturing, using the ZVBI library
 */
TTX_ACQ_ZVBI::TTX_ACQ_ZVBI(const char * p_dev, int dvb_pid)
{
   char * err = 0;

   if (dvb_pid >= 0) {
      mp_cap = vbi_capture_dvb_new2(p_dev, dvb_pid, &err, opt_debug);
   }
   else {
      unsigned srv = VBI_SLICED_VPS |
                     VBI_SLICED_TELETEXT_B |
                     VBI_SLICED_TELETEXT_B_525;
      mp_cap = vbi_capture_v4l2_new(const_cast<char*>(p_dev), 6, &srv, 0, &err, opt_debug);
   }

   if (mp_cap == 0) {
      fprintf(stderr, "Failed to open capture device %s: %s\n", p_dev, err);
      exit(1);
   }
   mp_buf = 0;
   m_line_count = 0;
   m_line_idx = 0;
   m_timestamp = 0;
}

TTX_ACQ_ZVBI::~TTX_ACQ_ZVBI()
{
   vbi_capture_delete(mp_cap);
}

/* ------------------------------------------------------------------------------
 * Read one frame's worth of teletext and VPS packets
 * - blocks until the device delivers the next packet
 */
void TTX_ACQ_ZVBI::device_read()
{
   int fd_ttx = vbi_capture_fd(mp_cap);
   fd_set rd_fd;
   FD_ZERO(&rd_fd);
   FD_SET(fd_ttx, &rd_fd);

   select(fd_ttx + 1, &rd_fd, NULL, NULL, NULL);

   vbi_capture_buffer * buf = NULL;
   struct timeval to;
   to.tv_sec = 0;
   to.tv_usec = 0;
   int ret = vbi_capture_pull_sliced(mp_cap, &buf, &to);
   if (ret > 0) {
      mp_buf = buf;
      m_line_count = buf->size / sizeof(vbi_sliced);
      m_line_idx = 0;
   }
   else if (ret < 0) {
      fprintf(stderr, "capture device error: %s\n", strerror(errno));
      exit(1);
   }
   m_timestamp = time(NULL);
}

bool TTX_ACQ_ZVBI::read_pkg(TTX_ACQ_PKG& ret_buf, time_t time_limit, time_t *ts)
{
   bool result = false;

   do {
      if ((time_limit > 0) && (time(NULL) > time_limit)) {
         // capture duration limit reached -> terminate loop
         break;
      }
      if (m_line_idx >= m_line_count) {
         device_read();
      }

      if (m_line_idx < m_line_count) {
         vbi_sliced * p_line = reinterpret_cast<vbi_sliced*>(mp_buf->data) + m_line_idx;
         ++m_line_idx;

         if (p_line->id & (VBI_SLICED_TELETEXT_B |
                           VBI_SLICED_TELETEXT_B_525)) {
            result = ret_buf.feed_ttx_pkg(p_line->data);
         }
         else if (p_line->id & VBI_SLICED_VPS) {
            result = ret_buf.feed_vps_pkg(p_line->data);
         }
      }
   } while (!result);

   *ts = m_timestamp;
   return result;
}

class TTX_ACQ
{
public:
   TTX_ACQ();
   ~TTX_ACQ();
   void add_pkg(const TTX_ACQ_PKG * p_pkg_buf, time_t ts);
private:
   int m_cur_page[8];
   int m_cur_sub[8];
   int m_prev_pkg[8];
   int m_prev_mag;
   bool m_is_mag_serial;
};

TTX_ACQ::TTX_ACQ()
{
   for (int idx = 0; idx < 8; idx++) {
      m_cur_page[idx] = -1;
      m_cur_sub[idx] = 0;
      m_prev_pkg[idx] = 0;
   }

   m_prev_mag = 0;
   m_is_mag_serial = false;
}

TTX_ACQ::~TTX_ACQ()
{
}

void TTX_ACQ::add_pkg(const TTX_ACQ_PKG * p_pkg_buf, time_t ts)
{
   int page = p_pkg_buf->m_page_no;
   if (page < 0x100)
      page += 0x800;
   int mag = (p_pkg_buf->m_page_no >> 8) & 7;
   int sub = p_pkg_buf->m_ctrl_lo & 0x3f7f;
   int pkg = p_pkg_buf->m_pkg;
   const uint8_t * p_data = p_pkg_buf->m_data;
   //printf("%03X.%04X, %2d %.40s\n", page, sub, pkg, p_data);

   if (m_is_mag_serial && (mag != m_prev_mag) && (pkg <= 29)) {
      m_cur_page[m_prev_mag] = -1;
   }

   if (pkg == 0) {
      // erase page control bit (C4) - ignored, some channels abuse this
      //if ((p_pkg_buf->m_ctrl_lo & 0x80) && (p_pkg_buf->m_ctrl_hi & 0x02)) {  // C4 & C8
      //   ttx_db.erase_page_c4(page, sub);
      //}
      // inhibit display control bit (C10)
      //if (p_pkg_buf->m_ctrl_hi & 0x08) {
      //   m_cur_page[mag] = -1;
      //   return;
      //}
      // Suppress header (C7)
      if (p_pkg_buf->m_ctrl_hi & 0x01) {
         const char * blank_line = "                                        "; // 40 spaces
         p_data = reinterpret_cast<const uint8_t*>(blank_line);
      }
      // drop all non-decimal pages
      if (!ttx_db.page_acceptable(page)) {
         m_cur_page[mag] = -1;
         return;
      }
      // magazine serial mode (C11: 1=serial 0=parallel)
      m_is_mag_serial = ((p_pkg_buf->m_ctrl_hi & 0x10) != 0);
      if ((page != m_cur_page[mag]) || (sub != m_cur_sub[mag])) {
         ttx_db.add_page(page, sub,
                         p_pkg_buf->m_ctrl_lo | (p_pkg_buf->m_ctrl_hi << 16),
                         p_data, ts);
      }
      else {
         ttx_db.add_page_data(page, sub, 0, p_data);
      }

      m_cur_page[mag] = page;
      m_cur_sub[mag] = sub;
      m_prev_pkg[mag] = 0;
      m_prev_mag = mag;
   }
   else if (pkg <= 27) {
      page = m_cur_page[mag];
      sub = m_cur_sub[mag];
      if (page != -1) {
         // page body packets are supposed to be in order
         if (pkg < m_prev_pkg[mag]) {
            page = m_cur_page[mag] = -1;
         }
         else {
            if (pkg < 26)
               m_prev_pkg[mag] = pkg;
            ttx_db.add_page_data(page, sub, pkg, p_data);
         }
      }
   }
}

/* ------------------------------------------------------------------------------
 * Read VBI data from a device or file
 * - when reading from a file, each record has a small header plus 40 bytes payload
 * - TODO: use MIP to find TV overview pages
 */
void ReadVbi(const char * p_infile, const char * p_dumpfile,
             const char * p_dev_name, int dvb_pid,
             bool do_verbose, bool do_dump, int cap_duration)
{
   TTX_ACQ_SRC * acq_src = 0;
   TTX_ACQ_PKG pkg_buf;
   TTX_ACQ acq;
   int last_pg = -1;
   int intv = 0;

   if (p_infile != 0) {
      // read VBI input data from a stream or file
      acq_src = new TTX_ACQ_FILE(p_infile);
   }
   else {
      // capture input data from a VBI device
      acq_src = new TTX_ACQ_ZVBI(p_dev_name, dvb_pid);

      if (p_dumpfile != 0) {
         close(1);
         if (open(p_dumpfile, O_WRONLY|O_CREAT|O_EXCL, 0666) < 0) {
            fprintf(stderr, "Failed to create %s: %s\n", p_dumpfile, strerror(errno));
            exit(1);
         }
      }
   }
   time_t time_limit = (cap_duration > 0) ? (time(NULL) + cap_duration) : -1;

   while (1) {
      time_t timestamp;
      if (!acq_src->read_pkg(pkg_buf, time_limit, &timestamp))
         break;

      if (do_dump) {
         pkg_buf.dump_raw_pkg(1);
      }

      if (do_verbose && (pkg_buf.m_pkg == 0)) {
         int page = pkg_buf.m_page_no;
         if (page < 0x100)
            page += 0x800;
         int spg = (page << 16) | (pkg_buf.m_ctrl_lo & 0x3f7f);
         if (spg != last_pg) {
            fprintf(stderr, "%03X.%04X\n", page, pkg_buf.m_ctrl_lo & 0x3f7f);
            last_pg = spg;
         }
      }

      // check if every page (NOT sub-page) was received N times in average
      // (only do the check every 50th page to save CPU)
      if ((cap_duration == 0) && (pkg_buf.m_pkg == 0)) {
         intv += 1;
         if (intv >= 50) {
            if (ttx_db.get_acq_rep_stats() >= 10.0)
               break;
            intv = 0;
         }
      }

      if (pkg_buf.m_pkg < 30) {
         acq.add_pkg(&pkg_buf, timestamp);
      }
      else if ((pkg_buf.m_pkg == 30) || (pkg_buf.m_pkg == 32)) {
         uint16_t cni;
         memcpy(&cni, pkg_buf.m_data, sizeof(cni));
         ttx_chn_id.add_cni(cni);
      }
   }
   delete acq_src;
}

/* ------------------------------------------------------------------------------
 * Dump all loaded teletext pages as plain text
 * - teletext control characters and mosaic is replaced by space
 * - used for -dump option, intended for debugging only
 */
void DumpTextPages(const char * p_name)
{
   if (p_name != 0) {
      int fd = open(p_name, O_WRONLY|O_CREAT|O_EXCL, 0666);
      if (fd < 0) {
         fprintf(stderr, "Failed to create %s: %s\n", p_name, strerror(errno));
         exit(1);
      }
      FILE * fp = fdopen(fd, "w");

      ttx_db.dump_db_as_text(fp);
      fclose(fp);
   }
   else {
      ttx_db.dump_db_as_text(stdout);
   }
}

void TTX_DB::dump_db_as_text(FILE * fp)
{
   for (iterator p = m_db.begin(); p != m_db.end(); p++) {
      p->second->dump_page_as_text(fp);
   }

   ttx_db.m_btt.dump_btt_as_text(fp);
}

void TTX_DB_PAGE::dump_page_as_text(FILE * fp)
{
   fprintf(fp, "PAGE %03X.%04X\n", m_page, m_sub);

   for (uint idx = 1; idx < TTX_TEXT_LINE_CNT; idx++) {
      fprintf(fp, "%.40s\n", get_text(idx).c_str());
   }
   fprintf(fp, "\n");
}

/* ------------------------------------------------------------------------------
 * Dump all loaded teletext data as Perl script
 * - the generated script can be loaded with the -verify option
 */
void DumpRawTeletext(const char * p_name, int pg_start, int pg_end)
{
   FILE * fp = stdout;

   if (p_name != 0) {
      FILE * fp = fopen(p_name, "w");
      if (fp == NULL) {
         fprintf(stderr, "Failed to create %s: %s\n", p_name, strerror(errno));
         exit(1);
      }
   }

   fprintf(fp, "#!/usr/bin/perl -w\n");

   // return TRUE to allow to "require" the file
   ttx_db.dump_db_as_raw(fp, pg_start, pg_end);

   ttx_chn_id.dump_as_raw(fp);

   // return TRUE to allow to "require" the file in Perl
   fprintf(fp, "1;\n");

   if (fp != stdout) {
      fclose(fp);
   }
}

void TTX_CHN_ID::dump_as_raw(FILE * fp)
{
   for (const_iterator p = m_cnis.begin(); p != m_cnis.end(); p++) {
      fprintf(fp, "$PkgCni{0x%X} = %d;\n", p->first, p->second);
   }
}

void TTX_DB::dump_db_as_raw(FILE * fp, int pg_start, int pg_end)
{
   // acq start time (for backwards compatibility with Perl version only)
   const_iterator first = m_db.begin();
   time_t acq_ts = (first != m_db.end()) ? first->second->get_timestamp() : 0;
   fprintf(fp, "$VbiCaptureTime = %ld;\n", (long)acq_ts);

   for (iterator p = m_db.begin(); p != m_db.end(); p++) {
      int page = p->first.page();
      if ((page >= pg_start) && (page <= pg_end)) {
         int last_sub = ttx_db.last_sub_page_no(page);

         p->second->dump_page_as_raw(fp, last_sub);
      }
   }
}

void TTX_DB_PAGE::dump_page_as_raw(FILE * fp, int last_sub)
{
   fprintf(fp, "$PgCnt{0x%03X} = %d;\n", m_page, m_acq_rep_cnt);
   fprintf(fp, "$PgSub{0x%03X} = %d;\n", m_page, last_sub);
   fprintf(fp, "$PgLang{0x%03X} = %d;\n", m_page, m_lang);
   fprintf(fp, "$PgTime{0x%03X} = %ld;\n", m_page, (long)m_timestamp);

   fprintf(fp, "$Pkg{0x%03X|(0x%04X<<12)} =\n[\n", m_page, m_sub);

   for (uint idx = 0; idx < TTX_RAW_PKG_CNT; idx++) {
      if (m_raw_pkg_valid & (1 << idx)) {
         fprintf(fp, "  \"");
         for (uint cidx = 0; cidx < VT_PKG_RAW_LEN; cidx++) {
            // quote binary characters
            unsigned char c = m_raw_pkg[idx][cidx];
            if ((c < 0x20) || (c == 0x7F)) {
               fprintf(fp, "\\x%02X", c);
            }
            // backwards compatibility: quote C and Perl special characters
            else if (   (c == '@')
                     || (c == '$')
                     || (c == '%')
                     || (c == '"')
                     || (c == '\\') ) {
               fputc('\\', fp);
               fputc(c, fp);
            }
            else {
               fputc(c, fp);
            }
         }
         fprintf(fp, "\",\n");
      }
      else {
         fprintf(fp, "  undef,\n");
      }
   }
   fprintf(fp, "];\n");
}

/* ------------------------------------------------------------------------------
 * Import a data file generated by DumpRawTeletext
 */
void ImportRawDump(const char * p_name)
{
   FILE * fp;

   if ((p_name == 0) || (*p_name == 0)) {
      fp = stdin;
   } else {
      fp = fopen(p_name, "r");
      if (fp == NULL) {
         fprintf(stderr, "Failed to open %s: %s\n", p_name, strerror(errno));
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
   static const regex expr6b("\\$PgTime\\{0x([0-9A-Za-z]+)\\}\\s*=\\s*(\\d+);\\s*");
   static const regex expr7("\\$Pkg\\{0x([0-9A-Za-z]+)\\|\\(0x([0-9A-Za-z]+)<<12\\)\\}\\s*=\\s*");
   static const regex expr8("\\[\\s*");
   static const regex expr9("\\s*undef,\\s*");
   static const regex expr10("\\s*\"(.*)\",\\s*");
   static const regex expr11("\\];\\s*");
   static const regex expr12("1;\\s*");
   static const regex expr13("(#.*\\s*|\\s*)");

   int file_line_no = 0;
   int page = -1;
   uint sub = 0;
   uint pkg_idx = 0;
   uint lang = 0;
   uint pg_cnt = 0;
   TTX_DB_PAGE::TTX_RAW_PKG pg_data[TTX_DB_PAGE::TTX_RAW_PKG_CNT];
   uint32_t pg_data_valid = 0;
   time_t timestamp = 0;

   char buf[256];
   while (fgets(buf, sizeof(buf), fp) != 0) {
      file_line_no ++;
      if (regex_match(buf, what, expr1)) {
      }
      else if (regex_match(buf, what, expr2)) {
         timestamp = atol(string(what[1]).c_str());
      }
      else if (regex_match(buf, what, expr3)) {
         int cni = atox_substr(what[1]);
         int cnt = atoi_substr(what[2]);
         for (int idx = 0; idx < cnt; idx++) {
            ttx_chn_id.add_cni(cni);
         }
      }
      else if (regex_match(buf, what, expr4)) {
         int lpage = atox_substr(what[1]);
         assert((page == -1) || (page == lpage));
         page = lpage;
         pg_cnt = atoi_substr(what[2]);
      }
      else if (regex_match(buf, what, expr5)) {
         int lpage = atox_substr(what[1]);
         assert((page == -1) || (page == lpage));
         page = lpage;
         sub = atoi_substr(what[2]);
      }
      else if (regex_match(buf, what, expr6)) {
         int lpage = atox_substr(what[1]);
         assert((page == -1) || (page == lpage));
         page = lpage;
         lang = atoi_substr(what[2]);
      }
      else if (regex_match(buf, what, expr6b)) {
         int lpage = atox_substr(what[1]);
         assert((page == -1) || (page == lpage));
         page = lpage;
         timestamp = atol(string(what[2]).c_str());
      }
      else if (regex_match(buf, what, expr7)) {
         int lpage = atox_substr(what[1]);
         assert((page == -1) || (page == lpage));
         page = lpage;
         sub = atox_substr(what[2]);
         pg_data_valid = 0;
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
         assert((page != -1) && (pkg_idx < TTX_DB_PAGE::TTX_RAW_PKG_CNT));
         const char * p = &what[1].first[0];
         int idx = 0;
         while ((*p != 0) && (idx < VT_PKG_RAW_LEN)) {
            int val, val_len;
            if ((*p == '\\') && (p[1] == 'x') &&
                (sscanf(p + 2, "%2x%n", &val, &val_len) >= 1) && (val_len == 2)) {
               pg_data[pkg_idx][idx++] = val;
               p += 4;
            } else if (*p == '\\') {
               pg_data[pkg_idx][idx++] = p[1];
               p += 2;
            } else {
               pg_data[pkg_idx][idx++] = *p;
               p += 1;
            }
         }
         pg_data_valid |= (1 << pkg_idx);
         pkg_idx += 1;
      }
      else if (regex_match(buf, what, expr11)) {
         assert(page != -1);
         int ctrl = sub | ((lang & 1) << (16+7)) | ((lang & 2) << (16+5)) | ((lang & 4) << (16+3));
         TTX_DB_PAGE * pgtext = ttx_db.add_page(page, sub, ctrl, pg_data[0], timestamp);
         for (uint idx = 1; idx < pkg_idx; idx++) {
            if (pg_data_valid & (1 << idx))
               ttx_db.add_page_data(page, sub, idx, pg_data[idx]);
         }
         for (uint idx = 1; idx < pg_cnt; idx++) {
            pgtext->inc_acq_cnt(timestamp);
         }
         page = -1;
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

