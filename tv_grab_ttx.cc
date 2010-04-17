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
 * $Id: tv_grab_ttx.cc,v 1.22 2010/04/17 20:46:31 tom Exp $
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
class TTX_PG_HANDLE
{
public:
   TTX_PG_HANDLE(uint page, uint sub) : m_handle((page << 16) | (sub & 0x3f7f)) {}
   bool operator< (TTX_PG_HANDLE v) const { return m_handle < v.m_handle; }
   bool operator== (TTX_PG_HANDLE v) const { return m_handle == v.m_handle; }
   bool operator!= (TTX_PG_HANDLE v) const { return m_handle != v.m_handle; }
   uint page() const { return (m_handle >> 16); }
   uint sub() const { return (m_handle & 0x3f7f); }
private:
   uint m_handle;
};

class TTX_DB_PAGE
{
public:
   TTX_DB_PAGE(uint page, uint sub, uint ctrl, time_t ts);
   void add_raw_pkg(uint idx, const uint8_t * p_data);
   void inc_acq_cnt(time_t ts);
   void erase_page_c4();
   const string& get_ctrl(uint line) const {
      assert(line < TTX_TEXT_LINE_CNT);
      if (!m_text_valid)
         page_to_latin1();
      return m_ctrl[line];
   }
   const string& get_text(uint line) const {
      assert(line < TTX_TEXT_LINE_CNT);
      if (!m_text_valid)
         page_to_latin1();
      return m_text[line];
   }
   int get_lang() const { return m_lang; }
   int get_acq_rep() const { return m_acq_rep_cnt; }
   time_t get_timestamp() const { return m_timestamp; }
   void dump_page_as_text(FILE * fp);
   void dump_page_as_raw(FILE * fp, int last_sub);

   typedef uint8_t TTX_RAW_PKG[VT_PKG_RAW_LEN];
   static const uint TTX_TEXT_LINE_CNT = 24;
   static const uint TTX_RAW_PKG_CNT = 30;

private:
   void page_to_latin1() const;
   void line_to_latin1(uint line, string& out) const;

   TTX_RAW_PKG  m_raw_pkg[TTX_RAW_PKG_CNT];
   uint32_t     m_raw_pkg_valid;
   int          m_acq_rep_cnt;
   int          m_lang;
   int          m_page;
   int          m_sub;
   time_t       m_timestamp;
   
   mutable bool   m_text_valid;
   mutable string m_ctrl[TTX_TEXT_LINE_CNT];
   mutable string m_text[TTX_TEXT_LINE_CNT];
};

TTX_DB_PAGE::TTX_DB_PAGE(uint page, uint sub, uint ctrl, time_t ts)
{
   m_page = page;
   m_sub = sub;
   m_raw_pkg_valid = 0;
   m_acq_rep_cnt = 1;
   m_text_valid = false;
   m_timestamp = ts;

   // store G0 char set (bits must be reversed: C12,C13,C14)
   int ctl2 = ctrl >> 16;
   m_lang = ((ctl2 >> 7) & 1) | ((ctl2 >> 5) & 2) | ((ctl2 >> 3) & 4);
}

void TTX_DB_PAGE::add_raw_pkg(uint idx, const uint8_t * p_data)
{
   assert(idx < TTX_RAW_PKG_CNT);

   if (idx == 0) {
      memset(m_raw_pkg[idx], ' ', 8);
      memcpy(m_raw_pkg[idx] + 8, p_data + 8, VT_PKG_RAW_LEN - 8);
   }
   else {
      memcpy(m_raw_pkg[idx], p_data, VT_PKG_RAW_LEN);
   }
   m_raw_pkg_valid |= 1 << idx;
}

void TTX_DB_PAGE::inc_acq_cnt(time_t ts)
{
   m_acq_rep_cnt += 1;
   m_timestamp = ts;
}

void TTX_DB_PAGE::erase_page_c4()
{
   m_raw_pkg_valid = 1;
   m_text_valid = false;
}

void TTX_DB_PAGE::page_to_latin1() const // modifies mutable
{
   assert(!m_text_valid); // checked by caller for efficiency

   for (uint idx = 0; idx < TTX_TEXT_LINE_CNT; idx++) {

      if (m_raw_pkg_valid & (1 << idx)) {
         m_ctrl[idx].assign(VT_PKG_RAW_LEN, ' ');
         line_to_latin1(idx, m_ctrl[idx]);

         m_text[idx].assign(VT_PKG_RAW_LEN, ' ');
         for (int col = 0; col < VT_PKG_RAW_LEN; col++) {
            unsigned char c = m_ctrl[idx][col];
            if ((c < 0x1F) || (c == 0x7F))
               m_text[idx][col] = ' ';
            else
               m_text[idx][col] = c;
         }
      }
      else {
         m_ctrl[idx].assign(VT_PKG_RAW_LEN, ' ');
         m_text[idx].assign(VT_PKG_RAW_LEN, ' ');
      }
   }

   m_text_valid = true;
}

class TTX_DB_BTT
{
public:
   TTX_DB_BTT() : m_have_btt(false), m_have_mpt(false), m_have_ait(false) {}
   void add_btt_pkg(uint page, uint idx, const uint8_t * p_data);
   int get_last_sub(uint page) const;
   bool is_valid() const { return m_have_btt; }
   bool is_top_page(uint page) const;
   void dump_btt_as_text(FILE * fp);
private:
   struct BTT_AIT_ELEM {
      uint8_t      hd_text[13];
      uint16_t     page;
   };
private:
   bool         m_have_btt;
   bool         m_have_mpt;
   bool         m_have_ait;
   uint8_t      m_pg_func[900 - 100];  ///< Page function - see 9.4.2.1
   uint8_t      m_sub_cnt[900 - 100];
   uint16_t     m_link[3 * 5];
   BTT_AIT_ELEM m_ait[44];
};

bool TTX_DB_BTT::is_top_page(uint page) const
{
   return    (page == 0x1F0)
          || (m_have_btt && (m_link[0] == page))
          || (m_have_btt && (m_link[1] == page));
}

void TTX_DB_BTT::add_btt_pkg(uint page, uint pkg, const uint8_t * p_data)
{
   if (page == 0x1F0)
   {
      if (!m_have_btt) {
         memset(m_pg_func, 0xFF, sizeof(m_pg_func));
         memset(m_link, 0xFF, sizeof(m_link));
         m_have_btt = true;
      }
      if ((pkg >= 1) && (pkg <= 20)) {
         int off = (pkg - 1) * 40;
         for (int idx = 0; idx < 40; idx++) {
            int v = vbi_unham8(p_data[idx]);
            if (v >= 0) {
               m_pg_func[off + idx] = v;
            }
         }
      }
      else if ((pkg >= 21) && (pkg <= 23)) {
         int off = (pkg - 21) * 5;
         for (int idx = 0; idx < 5; idx++) {
            m_link[off + idx] = (vbi_unham8(p_data[idx * 2*4 + 0]) << 8) |
                                (vbi_unham8(p_data[idx * 2*4 + 1]) << 4) |
                                (vbi_unham8(p_data[idx * 2*4 + 2]) << 0);
            // next 5 bytes are unused
         }
      }
   }
   else if (m_have_btt && (m_link[0] == page)) {
      if (!m_have_mpt) {
         memset(m_sub_cnt, 0, sizeof(m_sub_cnt));
         m_have_mpt = true;
      }
      if ((pkg >= 1) && (pkg <= 20)) {
         int off = (pkg - 1) * 40;
         for (int idx = 0; idx < 40; idx++) {
            int v = vbi_unham8(p_data[idx]);
            if (v >= 0) {
               m_sub_cnt[off + idx] = v;
            }
         }
      }
   }
   else if (m_have_btt && (m_link[1] == page)) {
      if (!m_have_ait) {
         memset(m_ait, 0, sizeof(m_ait));
         m_have_ait = true;
      }
      if ((pkg >= 1) && (pkg <= 22)) {
         int off = 2 * (pkg - 1);
         // note terminating 0 is written once during init
         memcpy(m_ait[off + 0].hd_text, p_data + 8, 12); // TODO char set conversion
         memcpy(m_ait[off + 1].hd_text, p_data + 20 + 8, 12);

         m_ait[off + 0].page = (vbi_unham8(p_data[0 + 0]) << 8) |
                               (vbi_unham8(p_data[0 + 1]) << 4) |
                               (vbi_unham8(p_data[0 + 2]) << 0);
         m_ait[off + 1].page = (vbi_unham8(p_data[20 + 0]) << 8) |
                               (vbi_unham8(p_data[20 + 1]) << 4) |
                               (vbi_unham8(p_data[20 + 2]) << 0);
      }
   }
}

int TTX_DB_BTT::get_last_sub(uint page) const
{
   if (m_have_mpt) {
      // array is indexed by decimal page number (compression)
      int d0 = (page >> 8);
      int d1 = (page >> 4) & 0x0F;
      int d2 = page & 0x0F;
      if ((d1 < 10) && (d2 < 10)) {
         assert((d0 >= 1) && (d0 <= 8));
         int idx = d0 * 100 + d1 * 10 + d2 - 100;
         if (m_sub_cnt[idx] == 0)
            return -1;
         else if (m_sub_cnt[idx] == 1)
            return 0;
         else
            return m_sub_cnt[idx];
      }
   }
   return -1;
}

void TTX_DB_BTT::dump_btt_as_text(FILE * fp)
{
   if (m_have_ait) {
      fprintf(fp, "PAGE BTT-AIT\n");
      for (int idx = 0; idx < 44; idx++) {
         fprintf(fp, " %03X %.12s\n", m_ait[idx].page, m_ait[idx].hd_text);
      }
   }
}

class TTX_DB
{
public:
   ~TTX_DB();
   typedef map<TTX_PG_HANDLE, TTX_DB_PAGE*>::iterator iterator;
   typedef map<TTX_PG_HANDLE, TTX_DB_PAGE*>::const_iterator const_iterator;

   bool sub_page_exists(uint page, uint sub) const;
   const TTX_DB_PAGE* get_sub_page(uint page, uint sub) const;
   const_iterator begin() const { return m_db.begin(); }
   const_iterator end() const { return m_db.end(); }
   const_iterator first_sub_page(uint page) const;
   const_iterator& next_sub_page(uint page, const_iterator& p) const;
   int last_sub_page_no(uint page) const;

   TTX_DB_PAGE* add_page(uint page, uint sub, uint ctrl, const uint8_t * p_data, time_t ts);
   void add_page_data(uint page, uint sub, uint idx, const uint8_t * p_data);
   void erase_page_c4(int page, int sub);
   void dump_db_as_text(FILE * fp);
   void dump_db_as_raw(FILE * fp);
   double get_acq_rep_stats();
   bool page_acceptable(uint page) const;
private:
   map<TTX_PG_HANDLE, TTX_DB_PAGE*> m_db;
   TTX_DB_BTT m_btt;
};

TTX_DB::~TTX_DB()
{
   for (iterator p = m_db.begin(); p != m_db.end(); p++) {
      delete p->second;
   }
}

bool TTX_DB::sub_page_exists(uint page, uint sub) const
{
   return m_db.find(TTX_PG_HANDLE(page, sub)) != m_db.end();
}

const TTX_DB_PAGE* TTX_DB::get_sub_page(uint page, uint sub) const
{
   const_iterator p = m_db.find(TTX_PG_HANDLE(page, sub));
   return (p != m_db.end()) ? p->second : 0;
}

TTX_DB::const_iterator TTX_DB::first_sub_page(uint page) const
{
   const_iterator p = m_db.lower_bound(TTX_PG_HANDLE(page, 0));
   return (p->first.page() == page) ? p : end();
}

TTX_DB::const_iterator& TTX_DB::next_sub_page(uint page, const_iterator& p) const
{
   ++p;
   if (p->first.page() > page)
      p = end();
   return p;
}

int TTX_DB::last_sub_page_no(uint page) const
{
   int last_sub = m_btt.get_last_sub(page);
   if (last_sub == -1) {
      const_iterator p = m_db.lower_bound(TTX_PG_HANDLE(page + 1, 0));
      last_sub = -1;
      if ((p != m_db.begin()) && (m_db.size() > 0)) {
         --p;
         if (p->first.page() == page)
            last_sub = p->first.sub();
      }
   }
   return last_sub;
}

// Decides if the page is acceptable for addition to the database.
bool TTX_DB::page_acceptable(uint page) const
{
   // decimal page (human readable) or TOP table
   return (   (((page & 0x0F) <= 9) && (((page >> 4) & 0x0f) <= 9))
           || m_btt.is_top_page(page));
}

TTX_DB_PAGE* TTX_DB::add_page(uint page, uint sub, uint ctrl, const uint8_t * p_data, time_t ts)
{
   TTX_PG_HANDLE handle(page, sub);

   iterator p = m_db.lower_bound(handle);
   if ((p == m_db.end()) || (p->first != handle)) {
      TTX_DB_PAGE * p_pg = new TTX_DB_PAGE(page, sub, ctrl, ts);
      p = m_db.insert(p, make_pair(handle, p_pg));
   }
   else {
      p->second->inc_acq_cnt(ts);
   }
   p->second->add_raw_pkg(0, p_data);
   return p->second;
}

void TTX_DB::add_page_data(uint page, uint sub, uint idx, const uint8_t * p_data)
{
   if (m_btt.is_top_page(page)) {
      // forward the data to the BTT
      m_btt.add_btt_pkg(page, idx, p_data);
   }
   else {
      TTX_PG_HANDLE handle(page, sub);

      iterator p = m_db.find(handle);
      if (p != m_db.end()) {
         p->second->add_raw_pkg(idx, p_data);
      }
      else {
         //if (opt_debug) printf("ERROR: page:%d sub:%d not found for adding pkg:%d\n", page, sub, idx);
      }
   }
}

/* Erase the page with the given number from memory: used to handle the "erase"
 * control bit in the TTX header. Since the page is added again right after we
 * only invalidate the page contents, but keep the page.
 */
void TTX_DB::erase_page_c4(int page, int sub)
{
   TTX_PG_HANDLE handle(page, sub);

   iterator p = m_db.find(handle);
   if (p != m_db.end()) {
      p->second->erase_page_c4();
   }
}

double TTX_DB::get_acq_rep_stats()
{
   int page_cnt = 0;
   int page_rep = 0;
   uint prev_page = 0xFFFu;

   for (const_iterator p = m_db.begin(); p != m_db.end(); p++) {
      if (p->first.page() != prev_page)
         page_cnt += 1;
      prev_page = p->first.page();

      page_rep += p->second->get_acq_rep();
   }
   return (page_cnt > 0) ? ((double)page_rep / page_cnt) : 0.0;
}

class TTX_CHN_ID
{
public:
   typedef map<int,int>::iterator iterator;
   typedef map<int,int>::const_iterator const_iterator;

   void add_cni(uint cni);
   void dump_as_raw(FILE * fp);
   string get_ch_id();
private:
   struct T_CNI_TO_ID_MAP
   {
      uint16_t cni;
      const char * const p_name;
   };
   map<int,int> m_cnis;

   static const T_CNI_TO_ID_MAP Cni2ChannelId[];
   static const uint16_t NiToPdcCni[];
};

void TTX_CHN_ID::add_cni(uint cni)
{
   iterator p = m_cnis.lower_bound(cni);
   if ((p == m_cnis.end()) || (p->first != int(cni))) {
      p = m_cnis.insert(p, make_pair<int,int>(cni, 1));
   }
   else {
      ++p->second;
   }
}

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

class TV_FEAT
{
public:
   TV_FEAT() {
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
   };
   void set_tip(bool v) { if (v) m_is_tip = true; };
private:
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
   static const TV_FEAT_STR FeatToFlagMap[];

   void MapTrailingFeat(const char * feat, int len, const string& title);
public:
   void ParseTrailingFeat(string& title);
   static void RemoveTrailingFeat(string& title);
public:
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
};

class TV_SLOT
{
public:
   TV_SLOT(int ttx_ref, time_t start_ts, time_t stop_ts,
           const string& vps_time, const string& vps_date)
      : m_start_t(start_ts)
      , m_stop_t(stop_ts)
      , m_vps_time(vps_time)
      , m_vps_date(vps_date)
      , m_ttx_ref(ttx_ref) {
   }
public:
   time_t       m_start_t;
   time_t       m_stop_t;
   string       m_vps_time;
   string       m_vps_date;

   string       m_title;
   string       m_subtitle;
   string       m_desc;
   TV_FEAT      m_feat;
   int          m_ttx_ref;
};

class T_PG_DATE
{
public:
   T_PG_DATE() {
      m_mday = -1;
      m_month = -1;
      m_year = -1;
      m_date_off = 0;
   };
public:
   int          m_mday;
   int          m_month;
   int          m_year;
   int          m_date_off;
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

class T_TRAIL_REF_FMT;
class T_OV_LINE_FMT;
class OV_SLOT;

class OV_PAGE
{
public:
   OV_PAGE(int page, int sub);
   ~OV_PAGE();
   bool parse_slots(int foot_line, const T_OV_LINE_FMT& pgfmt);
   bool parse_ov_date();
   bool check_redundant_subpage(OV_PAGE * prev);
   void calc_stop_times(const OV_PAGE * next);
   bool is_adjacent(const OV_PAGE * prev) const;
   bool calc_date_off(const OV_PAGE * prev);
   bool check_start_times();
   void calculate_start_times();
   void extract_ttx_ref(const T_TRAIL_REF_FMT& fmt);
   void extract_tv(list<TV_SLOT*>& tv_slots);
   static T_TRAIL_REF_FMT detect_ov_ttx_ref_fmt(const vector<OV_PAGE*>& ov_pages);
private:
   bool parse_end_time(const string& text, const string& ctrl, int& hour, int& min);
private:
   const int    m_page;
   const int    m_sub;
   T_PG_DATE    m_date;
   int          m_sub_page_skip;
   int          m_head;
   int          m_foot;
   vector<OV_SLOT*> m_slots;
};

class OV_SLOT
{
public:
   OV_SLOT(int hour, int min, bool is_tip);
   ~OV_SLOT();
private:
   friend class OV_PAGE;
   time_t       m_start_t;
   time_t       m_stop_t;
   int          m_hour;
   int          m_min;
   int          m_end_hour;
   int          m_end_min;
   string       m_vps_time;
   string       m_vps_date;
   int          m_ttx_ref;
   bool         m_is_tip;
   vector<string> m_title;
public:
   void add_title(string ctrl);
   void parse_ttx_ref(const T_TRAIL_REF_FMT& fmt);
   void detect_ttx_ref_fmt(vector<T_TRAIL_REF_FMT>& fmt_list);
   void parse_feature_flags(TV_SLOT * p_slot);
   void parse_title(TV_SLOT * p_slot);
   time_t ConvertStartTime(const T_PG_DATE * pgdate, int date_off) const;
   bool is_same_prog(const OV_SLOT& v) const;
};

class XMLTV
{
public:
   void ImportXmltvFile(const char * fname);
   void ExportXmltv(list<TV_SLOT*>& NewSlots);
   void SetChannelName(const char * user_chname, const char * user_chid);
private:
   map<string,string> m_merge_prog;
   map<string,string> m_merge_chn;
   string m_ch_name;
   string m_ch_id;
};

// global data
TTX_DB ttx_db;
TTX_CHN_ID ttx_chn_id;

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

// forward declarations
void ParseDescPage(TV_SLOT * slot, const T_PG_DATE * pg_date);


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

template<class IT>
int atox_substr(const IT& first, const IT& second)
{
   IT p = first;
   int val = 0;
   while (p != second) {
      char c = *(p++);
      if ((c >= '0') && (c <= '9'))
         val = (val * 16) + (c - '0');
      else
         val = (val * 16) + ((c - 'A') & ~0x20) + 10;
   }
   return val;
}

template<class MATCH>
int atox_substr(const MATCH& match)
{
   return match.matched ? atox_substr(match.first, match.second) : -1;
}

template<class IT>
void str_blank(const IT& first, const IT& second)
{
   IT p = first;
   while (p != second) {
      *(p++) = ' ';
   }
}

template<class MATCH>
void str_blank(const MATCH& match)
{
   if (match.matched) {
      str_blank(match.first, match.second);
   }
}



/* Turn the entire given string into lower-case.
 */
void str_tolower(string& str)
{
   for (string::iterator p = str.begin(); p < str.end(); p++) {
      *p = tolower(*p);
   }
}

/* Replace all TTX control characters with whitespace. Note this is used after
 * G0 to Latin-1 conversion, so there may be 8-bit chars.
 */
void str_repl_ctrl(string& str)
{
   for (int idx = 0; idx < VT_PKG_RAW_LEN; idx++) {
      unsigned char c = str[idx];
      if ((c <= 0x1F) || (c == 0x7F))
         str[idx] = ' ';
   }
}

/* Append the second sring to the first one, with exactly one blank
 * in-between. Exception: if the first string ends in "-" and the
 * second starts lower-case, remove the "-" and append w/o blank.
 */
bool str_concat_title(string& title, const string& str2, bool if_cont_only)
{
   bool result = false;

   // count whitespace at the end of the first string
   string::reverse_iterator p_end = title.rbegin();
   uint del1 = 0;
   while (   (p_end != title.rend())
          && (uint8_t(*p_end) <= ' ') )
   {
      ++p_end;
      ++del1;
   }

   // count whitespace at the start of the second string
   string::const_iterator p_start = str2.begin();
   while (   (p_start != str2.end())
          && (uint8_t(*p_start) <= ' ') )
   {
      ++p_start;
   }

   // remove whitespace and "-" at the end of the first string
   if (   (title.length() > 2 + del1)
       && (title[title.length() - del1 - 1] == '-')
       && (title[title.length() - del1 - 2] != ' ')
       && (p_start != str2.end())
       && islower(*p_start) )
   {
      // remove (replace) the "-" and
      // append (replace) the second string, skipping whitespace
      title.replace(title.end() - del1 - 1, title.end(),
                    p_start, str2.end());
      result = true;
   }
   else if (!if_cont_only)
   {
      if (del1 > 0)
         title.erase(title.end() - del1);

      if ((title.length() > 0) && (p_start != str2.end()))
         title.append(1, ' ');

      // append the second string, skipping whitespace
      title.append(p_start, str2.end());
   }
   return result;
}

/* Check if there's a word boundary in the given string beginning
 * at the character with the given position (i.e. the character before
 * the given position is the last alpha-numeric character.)
 */
bool str_is_left_word_boundary(const string& str, uint pos)
{
   return (pos == 0) || !isalnum(str[pos - 1]);
}

/* Check if there's a word boundary in the given string before the
 * character with the given position.
 */
bool str_is_right_word_boundary(const string& str, uint pos)
{
   return (pos >= str.length()) || !isalnum(str[pos]);
}

/* Search for the string "word" inside of "str". The match must be
 * located at the beginning or end, or the adjacent characters must be
 * non-alphanumeric (i.e. word boundaries)
 */
string::size_type str_find_word(const string& str, const string& word)
{
   string::size_type pos = 0;
   do {
      pos = str.find(word, pos);

      if (pos != string::npos) {
         if (   str_is_left_word_boundary(str, pos)
             && str_is_right_word_boundary(str, pos + word.length()) ) {
            return pos;
         }
      } else {
         break;
      }
   } while (++pos + word.length() < str.length());

   return string::npos;
}

/* Count and return the number of blank (or control) characters at the beginnine
 * of the given string.
 */
uint str_get_indent(const string& str)
{
   uint off;

   for (off = 0; off < str.length(); off++) {
      uint8_t c = str[off];
      if (c > ' ')  // not ctrl char or space
         break;
   }
   return off;
}

/* Remove white-space from the beginning and end of the given string.
 */
void str_chomp(string& str)
{
   if (str.length() > 0) {
      string::iterator end = str.begin() + str.length() - 1;
      while ((str.begin() != end) && (uint8_t(*end) <= ' ')) {
         end--;
      }
      str.erase(end + 1, str.end());
   }

   string::iterator begin = str.begin();
   while ((begin != str.end()) && (uint8_t(*begin) <= ' ')) {
      begin++;
   }
   str.erase(str.begin(), begin);
}

// return the current foreground color at the end of the given string
template<class IT>
int str_fg_col(const IT& first, const IT& second)
{
   int fg = 7;

   for (IT p = first; p != second; p++) {
      unsigned char c = *p;
      if (c <= 7) {
         fg = (unsigned int) c;
      }
   }
   return fg;
}

template<class MATCH>
int str_fg_col(const MATCH& match)
{
   return str_fg_col(match.first, match.second);
}

// return TRUE if text is concealed at the end of the given string
template<class IT>
bool str_is_concealed(const IT& first, const IT& second)
{
   bool is_concealed = false;

   for (IT p = first; p != second; p++) {
      unsigned char c = *p;
      if (c == '\x18') {
         is_concealed = true;
      }
      else if (c <= '\x07') {
         is_concealed = false;
      }
   }
   return is_concealed;
}

template<class MATCH>
bool str_is_concealed(const MATCH& match)
{
   return str_is_concealed(match.first, match.second);
}

// return the current background color at the end of the given string
template<class IT>
int str_bg_col(const IT& first, const IT& second)
{
   int fg = 7;
   int bg = 0;

   for (IT p = first; p != second; p++) {
      unsigned char c = *p;
      if (c <= 7) {
         fg = (unsigned int) c;
      }
      else if ((c >= 0x10) && (c <= 0x17)) {
         fg = (unsigned int) c - 0x10;
      }
      else if (c == 0x1D) {
         bg = fg;
      }
   }
   return bg;
}

template<class MATCH>
int str_bg_col(const MATCH& match)
{
   return str_bg_col(match.first, match.second);
}

/* ------------------------------------------------------------------------------
 * TODO
 */

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

   if (opt_dvbpid >= 0) {
      mp_cap = vbi_capture_dvb_new2(p_dev, opt_dvbpid, &err, opt_debug);
   }
   else {
      unsigned srv = VBI_SLICED_VPS |
                     VBI_SLICED_TELETEXT_B |
                     VBI_SLICED_TELETEXT_B_525;
      mp_cap = vbi_capture_v4l2_new(const_cast<char*>(p_dev), 6, &srv, 0, &err, opt_debug);
   }

   if (mp_cap == 0) {
      fprintf(stderr, "Failed to open capture device %s: %s\n", opt_device, err);
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
      fprintf(stderr, "capture device error: %s: %s\n", strerror(errno), opt_device);
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
void ReadVbi()
{
   TTX_ACQ_SRC * acq_src = 0;
   TTX_ACQ_PKG pkg_buf;
   TTX_ACQ acq;
   int last_pg = -1;
   int intv = 0;

   if (opt_infile != 0) {
      // read VBI input data from a stream or file
      acq_src = new TTX_ACQ_FILE(opt_infile);
   }
   else {
      // capture input data from a VBI device
      acq_src = new TTX_ACQ_ZVBI(opt_device, opt_dvbpid);

      if (opt_outfile != 0) {
         close(1);
         if (open(opt_outfile, O_WRONLY|O_CREAT|O_EXCL, 0666) < 0) {
            fprintf(stderr, "Failed to create %s: %s\n", opt_outfile, strerror(errno));
            exit(1);
         }
      }
   }
   time_t time_limit = (opt_duration > 0) ? (time(NULL) + opt_duration) : -1;

   while (1) {
      time_t timestamp;
      if (!acq_src->read_pkg(pkg_buf, time_limit, &timestamp))
         break;

      if (opt_dump == 3) {
         pkg_buf.dump_raw_pkg(1);
      }

      if (opt_verbose && (pkg_buf.m_pkg == 0)) {
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
      if ((opt_duration == 0) && (pkg_buf.m_pkg == 0)) {
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
void DumpTextPages()
{
   if (opt_outfile != 0) {
      int fd = open(opt_outfile, O_WRONLY|O_CREAT|O_EXCL, 0666);
      if (fd < 0) {
         fprintf(stderr, "Failed to create %s: %s\n", opt_outfile, strerror(errno));
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
void DumpRawTeletext()
{
   FILE * fp = stdout;

   if (opt_outfile != 0) {
      FILE * fp = fopen(opt_outfile, "w");
      if (fp == NULL) {
         fprintf(stderr, "Failed to create %s: %s\n", opt_outfile, strerror(errno));
         exit(1);
      }
   }

   fprintf(fp, "#!/usr/bin/perl -w\n");

   // return TRUE to allow to "require" the file
   ttx_db.dump_db_as_raw(fp);

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

void TTX_DB::dump_db_as_raw(FILE * fp)
{
   // acq start time (for backwards compatibility with Perl version only)
   const_iterator first = m_db.begin();
   time_t acq_ts = (first != m_db.end()) ? first->second->get_timestamp() : 0;
   fprintf(fp, "$VbiCaptureTime = %ld;\n", (long)acq_ts);

   for (iterator p = m_db.begin(); p != m_db.end(); p++) {
      int page = p->first.page();
      if ((page >= opt_tv_start) && (page <= opt_tv_end)) {
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
void TTX_DB_PAGE::line_to_latin1(uint line, string& out) const
{
   bool is_g1 = false;

   for (int idx = 0; idx < VT_PKG_RAW_LEN; idx++) {
      uint8_t val = m_raw_pkg[line][idx];

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
         assert((size_t)m_lang < sizeof(NatOptMaps)/sizeof(NatOptMaps[0]));
         val = NatOptMaps[m_lang][NationalOptionsMatrix[val]];
      }
      out[idx] = val;
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

int GetWeekDayOffset(string wday_name, time_t timestamp)
{
   int reldate = -1;

   if (wday_name.length() > 0) {
      const T_DATE_NAME * p = MapDateName(wday_name.c_str(), WDayNames);
      if (p != 0) {
         int wday_idx = p->idx;
         struct tm * ptm = localtime(&timestamp);
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
               time_t timestamp,
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
      int reldate = GetWeekDayOffset(wday_name, timestamp);
      if (reldate < 0)
         return false;

      time_t whence = timestamp + reldate*24*60*60;
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
         struct tm * ptm = localtime(&timestamp);
         year = ptm->tm_year + 1900;
      }
      else if (year < 100) {
         struct tm * ptm = localtime(&timestamp);
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
class T_OV_LINE_FMT
{
public:
   T_OV_LINE_FMT() : m_time_off(-1), m_vps_off(-1), m_title_off(-1), m_subt_off(-1) {}
   bool operator==(const T_OV_LINE_FMT& b) const {
      return (   (m_time_off == b.m_time_off)
              && (m_vps_off == b.m_vps_off)
              && (m_title_off == b.m_title_off)
              && (m_separator == b.m_separator));
   }
   bool operator<(const T_OV_LINE_FMT& b) const {
      return    (m_time_off < b.m_time_off)
             || (   (m_time_off == b.m_time_off)
                 && (   (m_vps_off < b.m_vps_off)
                     || (   (m_vps_off == b.m_vps_off)
                         && (   (m_title_off < b.m_title_off)
                             || (   (m_title_off == b.m_title_off)
                                 && (m_separator < b.m_separator) )))));
   }
   const char * print_key() const;
   bool detect_line_fmt(const string& text, const string& text2);
   bool parse_title_line(const string& text, int& hour, int& min, bool &is_tip) const;
   bool parse_subtitle(const string& text) const;
   string extract_title(const string& text) const;
   string extract_subtitle(const string& text) const;
   bool is_valid() const { return m_time_off >= 0; }
   int get_subt_off() const { return m_subt_off; }
   void set_subt_off(const T_OV_LINE_FMT& v) { m_subt_off = v.m_subt_off; }
   static T_OV_LINE_FMT select_ov_fmt(vector<T_OV_LINE_FMT>& fmt_list);
private:
   int m_time_off;    ///< Offset to HH:MM or HH.MM
   int m_vps_off;     ///< Offset to HHMM (concealed VPS)
   int m_title_off;   ///< Offset to title
   int m_subt_off;    ///< Offset to 2nd title line
   char m_separator;  ///< HH:MM separator character
};

const char * T_OV_LINE_FMT::print_key() const
{
   static char buf[100];
   sprintf(buf, "time:%d,vps:%d,title:%d,title2:%d,MMHH-sep:'%c'",
           m_time_off, m_vps_off, m_title_off, m_subt_off, m_separator);
   return buf;
}

bool T_OV_LINE_FMT::parse_title_line(const string& text, int& hour, int& min, bool &is_tip) const
{
   match_results<string::const_iterator> whati;
   bool result = false;

   if (m_vps_off >= 0) {
      // TODO: Phoenix wraps titles into 2nd line, appends subtitle with "-"
      // m#^ {0,2}(\d\d)[\.\:](\d\d)( {1,3}(\d{4}))? +#
      static const regex expr2("^ *([\\*!] +)?$");
      if (regex_search(text.begin(), text.begin() + m_time_off, whati, expr2)) {
         is_tip = whati[1].matched;
         // note: VPS time has already been extracted above, so the area is blank
         static const regex expr3("^(2[0-3]|[01][0-9])([\\.:])([0-5][0-9]) +[^ ]$");
         if (   regex_search(text.begin() + m_time_off,
                             text.begin() + m_title_off + 1,
                             whati, expr3)
             && (whati[2].first[0] == m_separator) ) {
            hour = atoi_substr(whati[1]);
            min = atoi_substr(whati[3]);
            result = true;
         }
      }
   }
   else {
      // m#^( {0,5}| {0,3}\! {1,3})(\d\d)[\.\:](\d\d) +#
      static const regex expr5("^ *([\\*!] +)?$");
      if (regex_search(text.begin(), text.begin() + m_time_off, whati, expr5)) {
         is_tip = whati[1].matched;
         static const regex expr6("^(2[0-3]|[01][0-9])([\\.:])([0-5][0-9]) +[^ ]$");
         if (   regex_search(text.begin() + m_time_off,
                             text.begin() + m_title_off + 1,
                             whati, expr6)
             && (whati[2].first[0] == m_separator) ) {
            hour = atoi_substr(whati[1]);
            min = atoi_substr(whati[3]);
            result = true;
         }
      }
   }
   return result;
}

bool T_OV_LINE_FMT::parse_subtitle(const string& text) const
{
   bool result = false;

   if (m_subt_off >= 0) {
      static const regex expr10("^[ \\x00-\\x07\\x10-\\x17]*$");
      static const regex expr11("^[ \\x00-\\x07\\x10-\\x17]*$");
      match_results<string::const_iterator> whati;

      if (   !regex_search(text.begin(), text.begin() + m_subt_off, whati, expr10)
          || regex_search(text.begin() + m_subt_off, text.end(), whati, expr11) )
      {
      }
      else {
         result = true;
      }
   }
   return result;
}

string T_OV_LINE_FMT::extract_title(const string& text) const
{
   return text.substr(m_title_off);
}

string T_OV_LINE_FMT::extract_subtitle(const string& text) const
{
   return text.substr(m_subt_off);
}

bool T_OV_LINE_FMT::detect_line_fmt(const string& text, const string& text2)
{
   smatch whats;

   // look for a line containing a start time (hour 0-23 : minute 0-59)
   // TODO allow start-stop times "10:00-11:00"?
   static const regex expr1("^( *| *! +)([01][0-9]|2[0-3])([\\.:])([0-5][0-9]) +");
   if (regex_search(text, whats, expr1)) {
      int off = whats[0].length();

      m_time_off = whats[1].length();
      m_vps_off = -1;
      m_title_off = off;
      m_subt_off = -1;
      m_separator = whats[3].first[0];
      // TODO require that times are increasing (within the same format)
      //m_mod = atoi_substr(whats[2]) * 60 + atoi_substr(whats[4]);

      // look for a VPS label on the same line after the human readable start time
      // TODO VPS must be magenta or concealed
      static const regex expr2("^([0-2][0-9][0-5][0-9] +)");
      if (regex_search(text.begin() + off, text.end(), whats, expr2)) {
         m_vps_off = off;
         m_title_off = off + whats[1].length();
      }
      else {
         m_vps_off = -1;
         m_title_off = off;
      }

      // measure the indentation of the following line, if starting with a letter (2nd title line)
      static const regex expr3("^( *| *([01][0-9]|2[0-3])[0-5][0-9] +)[[:alpha:]]");
      static const regex expr4("^( *)[[:alpha:]]");
      if ( (m_vps_off == -1)
           ? regex_search(text2, whats, expr3)
           : regex_search(text2, whats, expr4) )
      {
         m_subt_off = whats[1].second - whats[1].first;
      }

      //if (opt_debug) printf("FMT: %s\n", print_key());
      return true;
   }
   return false;
}

T_OV_LINE_FMT T_OV_LINE_FMT::select_ov_fmt(vector<T_OV_LINE_FMT>& fmt_list)
{
   if (!fmt_list.empty()) {
      map<T_OV_LINE_FMT,int> fmt_stats;
      int max_cnt = 0;
      int max_idx = -1;

      // search the most used format (ignoring "subt_off")
      for (uint idx = 0; idx < fmt_list.size(); idx++) {
         map<T_OV_LINE_FMT,int>::iterator p = fmt_stats.lower_bound(fmt_list[idx]);
         if ((p == fmt_stats.end()) || (fmt_list[idx] < p->first)) {
            p = fmt_stats.insert(p, make_pair(fmt_list[idx], 1));
         }
         else {
            p->second += 1;
         }
         if (p->second > max_cnt) {
            max_cnt = fmt_stats[fmt_list[idx]];
            max_idx = idx;
         }
      }
      T_OV_LINE_FMT& fmt = fmt_list[max_idx];

      // search the most used "subt_off" among the most used format
      map<int,int> fmt_subt;
      max_cnt = 0;
      for (uint idx = 0; idx < fmt_list.size(); idx++) {
         if (   (fmt_list[idx] == fmt)
             && (fmt_list[idx].get_subt_off() != -1))
         {
            int subt_off = fmt_list[idx].get_subt_off();
            map<int,int>::iterator p = fmt_subt.lower_bound(subt_off);
            if ((p == fmt_subt.end()) || (subt_off < p->first)) {
               p = fmt_subt.insert(p, make_pair(subt_off, 1));
            }
            else {
               p->second += 1;
            }
            if (p->second > max_cnt) {
               max_cnt = p->second;
               fmt.set_subt_off( fmt_list[idx] );
            }
         }
      }

      if (opt_debug) printf("auto-detected overview format: %s\n", fmt.print_key());
      return fmt;
   }
   else {
      return T_OV_LINE_FMT();
   }
}

/* ------------------------------------------------------------------------------
 *  Determine layout of programme overview pages
 *  (to allow stricter parsing)
 *  - TODO: detect color used to mark features (e.g. yellow on ARD) (WDR: ttx ref same color as title)
 *  - TODO: detect color used to distinguish title from subtitle/category (WDR,Tele5)
 */
T_OV_LINE_FMT DetectOvFormat()
{
   vector<T_OV_LINE_FMT> fmt_list;
   T_OV_LINE_FMT fmt;

   // look at the first 5 pages (default start at page 301)
   int cnt = 0;
   for (TTX_DB::const_iterator p = ttx_db.begin(); p != ttx_db.end(); p++)
   {
      int page = p->first.page();
      int sub = p->first.sub();
      if ((page >= opt_tv_start) && (page <= opt_tv_end)) {
         const TTX_DB_PAGE * pgtext = ttx_db.get_sub_page(page, sub);

         for (int line = 5; line <= 21; line++) {
            const string& text = pgtext->get_text(line);
            const string& text2 = pgtext->get_text(line + 1);

            if (fmt.detect_line_fmt(text, text2)) {
               fmt_list.push_back(fmt);
            }
         }

         if (++cnt > 5)
            break;
      }
   }

   if (fmt_list.empty()) {
      if (cnt == 0)
         fprintf(stderr, "No pages found in range %03X-%03X\n", opt_tv_start, opt_tv_end);
      else
         fprintf(stderr, "Failed to detect overview format on pages %03X-%03X\n", opt_tv_start, opt_tv_end);
   }

   return T_OV_LINE_FMT::select_ov_fmt(fmt_list);
}

OV_PAGE::OV_PAGE(int page, int sub)
   : m_page(page)
   , m_sub(sub)
   , m_sub_page_skip(0)
   , m_head(-1)
   , m_foot(23)
{
}

OV_PAGE::~OV_PAGE()
{
   for (uint idx = 0; idx < m_slots.size(); idx++) {
      delete m_slots[idx];
   }
}

/* ------------------------------------------------------------------------------
 * Parse date in an overview page
 * - similar to dates on description pages
 */
bool OV_PAGE::parse_ov_date()
{
   int reldate = -1;
   int mday = -1;
   int month = -1;
   int year = -1;

   const TTX_DB_PAGE * pgtext = ttx_db.get_sub_page(m_page, m_sub);
   int lang = pgtext->get_lang();

   string wday_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_FULL);
   string wday_abbrv_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_ABBRV);
   string mname_match = GetDateNameRegExp(MonthNames, lang, DATE_NAME_ANY);
   string relday_match = GetDateNameRegExp(RelDateNames, lang, DATE_NAME_ANY);
   smatch whats;
   int prio = -1;

   for (int line = 1; line < m_head; line++)
   {
      const string& text = pgtext->get_text(line);

      // [Mo.]13.04.[2006]
      // So,06.01.
      static regex expr1[8];
      static regex expr2[8];
      if (expr1[lang].empty()) {
         expr1[lang].assign(string("(^| |(") + wday_abbrv_match + ")(\\.|\\.,|,) ?)(\\d{1,2})\\.(\\d{1,2})\\.(\\d{2}|\\d{4})?([ ,:]|$)", regex::icase);
         expr2[lang].assign(string("(^| |(") + wday_match + ")(, ?| ))(\\d{1,2})\\.(\\d{1,2})\\.(\\d{2}|\\d{4})?([ ,:]|$)", regex::icase);
      }
      if (   regex_search(text, whats, expr1[lang])
          || regex_search(text, whats, expr2[lang]) )
      {
         if (CheckDate(atoi_substr(whats[4]), atoi_substr(whats[5]), atoi_substr(whats[6]),
                       "", "", pgtext->get_timestamp(), &mday, &month, &year)) {
            prio = 3;
         }
      }
      // 13.April [2006]
      static regex expr3[8];
      if (expr3[lang].empty()) {
         expr3[lang].assign(string("(^| )(\\d{1,2})\\. ?(") + mname_match + ")( (\\d{2}|\\d{4}))?([ ,:]|$)", regex::icase);
      }
      if (regex_search(text, whats, expr3[lang])) {
         if (CheckDate(atoi_substr(whats[2]), -1, atoi_substr(whats[5]),
                       "", string(whats[3]), pgtext->get_timestamp(),
                       &mday, &month, &year)) {
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
      if (   regex_search(text, whats, expr4[lang])
          || regex_search(text, whats, expr5[lang])) {
         if (CheckDate(atoi_substr(whats[4]), -1, atoi_substr(whats[7]),
                       string(whats[2]), string(whats[5]), pgtext->get_timestamp(),
                       &mday, &month, &year)) {
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
      if (regex_search(text, whats, expr6[lang])) {
         string wday_name;
         if (whats[2].matched)
            wday_name.assign(whats[2]);
         else
            wday_name.assign(whats[3]);
         int off = GetWeekDayOffset(wday_name, pgtext->get_timestamp());
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
      if (regex_search(text, whats, expr7[lang])) {
         int off = GetWeekDayOffset(string(whats[2]), pgtext->get_timestamp());
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
      if (regex_search(text, whats, expr8[lang])) {
         string rel_name = string(whats[2]);
         const T_DATE_NAME * p = MapDateName(rel_name.c_str(), RelDateNames);
         if (p != 0)
            reldate = p->idx;
         prio = 0;
      }
   }

   if ((mday == -1) && (reldate != -1)) {
      time_t whence = pgtext->get_timestamp() + reldate*24*60*60;
      struct tm * ptm = localtime(&whence);
      mday = ptm->tm_mday;
      month = ptm->tm_mon + 1;
      year = ptm->tm_year + 1900;
   }
   else if ((year != -1) && (year < 100)) {
      time_t whence = pgtext->get_timestamp();
      struct tm * ptm = localtime(&whence);
      int cur_year = ptm->tm_year + 1900;
      year += (cur_year - cur_year % 100);
   }
   else if (year == -1) {
      if (opt_debug) printf("OV DATE %03X.%04X: no match\n", m_page, m_sub);
   }

   m_date.m_mday = mday;
   m_date.m_month = month;
   m_date.m_year = year;
   m_date.m_date_off = 0;

   return (year != -1);
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

const TV_FEAT::TV_FEAT_STR TV_FEAT::FeatToFlagMap[] =
{
   { "untertitel", TV_FEAT_SUBTITLES },
   { "ut", TV_FEAT_SUBTITLES },
   { "omu", TV_FEAT_OMU },
   { "sw", TV_FEAT_BW },
   { "s/w", TV_FEAT_BW },
   { "hd", TV_FEAT_HD },
   { "breitbild", TV_FEAT_ASPECT_16_9 },
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

void TV_FEAT::MapTrailingFeat(const char * feat, int len, const string& title)
{
   if ((len >= 2) && (strncasecmp(feat, "ut", 2) == 0)) // TV_FEAT_SUBTITLES
   {
      if (opt_debug) printf("FEAT \"%s\" -> on TITLE %s\n", feat, title.c_str());
      m_has_subtitles = true;
      return;
   }

   for (uint idx = 0; idx < TV_FEAT_MAP_LEN; idx++)
   {
      if (strncasecmp(feat, FeatToFlagMap[idx].p_name, len) == 0)
      {
         switch (FeatToFlagMap[idx].type)
         {
            case TV_FEAT_SUBTITLES:     m_has_subtitles = true; break;
            case TV_FEAT_2CHAN:         m_is_2chan = true; break;
            case TV_FEAT_ASPECT_16_9:   m_is_aspect_16_9 = true; break;
            case TV_FEAT_HD:            m_is_video_hd = true; break;
            case TV_FEAT_BW:            m_is_video_bw = true; break;
            case TV_FEAT_DOLBY:         m_is_dolby = true; break;
            case TV_FEAT_MONO:          m_is_mono = true; break;
            case TV_FEAT_OMU:           m_is_omu = true; break;
            case TV_FEAT_STEREO:        m_is_stereo = true; break;
            case TV_FEAT_TIP:           m_is_tip = true; break;
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
                     "s/?w|S/?W|tlw. s/w|oo|°°|°\\*|OmU|16:9|HD|[Bb]reitbild|" \
                     "2K|2K-Ton|[Mm]ono|[Ss]tereo|[Dd]olby|[Ss]urround|" \
                     "DS|SS|DD|ZS|" \
                     "Wh\\.?|Wdh\\.?|Whg\\.?|Tipp!?"

void TV_FEAT::RemoveTrailingFeat(string& title)
{
   static const regex expr3("\\(((" FEAT_PAT_STR ")(( ?/ ?|, ?| )(" FEAT_PAT_STR "))*)\\)$");
   static const regex expr4("((" FEAT_PAT_STR ")"
                            "((, ?|/ ?| |[,/]?[\\x00-\\x07\\x1D]+)(" FEAT_PAT_STR "))*)$");
   smatch whats;

   if (   regex_search(title, whats, expr3)
       || regex_search(title, whats, expr4) ) {
      title.erase(whats.position());
   }
}

void TV_FEAT::ParseTrailingFeat(string& title)
{
   string orig_title = regex_replace(title, regex("[\\x00-\\x1F\\x7F]"), " ");
   smatch whats;
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

      MapTrailingFeat(feat_list.c_str(), whats[3].length(), orig_title);
      feat_list.erase(0, whats[3].length());

      // delete everything following the first sub-match
      title.erase(whats[1].length());

      if (feat_list.length() != 0)  {
         static const regex expr5("( ?/ ?|, ?| )(" FEAT_PAT_STR ")$");
         while (regex_search(feat_list, whats, expr5)) {
            MapTrailingFeat(&whats[2].first[0], whats[2].length(), orig_title);
            feat_list.erase(feat_list.length() - whats[0].length());
         } 
      }
      else {
         static const regex expr6("[ \\x00-\\x07\\x1D]*\\((" FEAT_PAT_STR ")\\)$");
         while (regex_search(title, whats, expr6)) {
            MapTrailingFeat(&whats[1].first[0], whats[1].length(), orig_title);
            title.erase(title.length() - whats[0].length());
         } 
      }
   }
   else if (regex_search(title, whats, expr4)) {
      string feat_list = string(whats[3]);

      MapTrailingFeat(feat_list.c_str(), whats[4].length(), orig_title);
      feat_list.erase(0, whats[4].length());

      // delete everything following the first sub-match
      title.erase(whats[1].length());

      static const regex expr7("([,/][ \\x00-\\x07\\x1D]*|[ \\x00-\\x07\\x1D]+)(" FEAT_PAT_STR ")$");
      while (regex_search(feat_list, whats, expr7)) {
         MapTrailingFeat(&whats[2].first[0], whats[2].length(), orig_title);
         feat_list.erase(feat_list.length() - whats[0].length());
      } 
   } //else { print "NONE orig_title\n"; }

   // compress adjecent white-space & remove special chars
   static const regex expr8("[\\x00-\\x1F\\x7F ]+");
   title = regex_replace(title, expr8, " ");

   // remove leading and trailing space
   static const regex expr10("^ +");
   if (regex_search(title, whats, expr10)) {
      title.erase(0, whats[0].length());
   }
   static const regex expr11(" +$");
   if (regex_search(title, whats, expr11)) {
      title.erase(whats.position());
   }
}

/* ------------------------------------------------------------------------------
 * Detect position and leading garbage for description page references
 * - The main goal is to find the exact position relative to the line end
 * - When that is fixed, we can be flexible about leading separators, i.e.
 *   swallow when present, else require a single blank only.
 *
 * ARD:  "Die Sendung mit der\x03UT\x06... 313""
 * ZDF:  "ZDF SPORTreportage ...\x06321"
 * SAT1: "Navy CIS: Das Boot        >353 "
 * RTL:  "Die Camper..............>389 "
 * RTL2: "X-Factor: Das Unfassbare...328 "
 * Kab1: "X-Men\x03                       >375"
 */
class T_TRAIL_REF_FMT
{
public:
   T_TRAIL_REF_FMT() : m_spc_trail(-1) {}
   bool operator<(const T_TRAIL_REF_FMT& b) const {
      return    (m_ch1 < b.m_ch1)
             || (   (m_ch1 == b.m_ch1)
                 && (   (m_ch2 < b.m_ch2)
                     || (   (m_ch2 == b.m_ch2)
                         && (   (m_spc_lead < b.m_spc_lead)
                             || (   (m_spc_lead == b.m_spc_lead)
                                 && (m_spc_trail < b.m_spc_trail) )))));
   }
   const char * print_key() const;
   bool detect_ref_fmt(const string& text);
   bool parse_trailing_ttx_ref(string& text, int& ttx_ref) const;
   bool is_valid() const { return m_spc_trail >= 0; }
   static T_TRAIL_REF_FMT select_ttx_ref_fmt(const vector<T_TRAIL_REF_FMT>& fmt_list);
private:
   void init_expr() const;
public:
   char m_ch1;          ///< Leading separator (e.g. "..... 314"), or zero
   char m_ch2;          ///< Second terminator char (e.g. "...>314"), or zero
   int m_spc_lead;      ///< Number of leading spaces
   int m_spc_trail;     ///< Number of spaces before line end

   mutable regex m_expr;  ///< Regex used for extracting the match
   mutable int m_subexp_idx;  ///< Index of TTX page sub-expression in match result
};

const char * T_TRAIL_REF_FMT::print_key() const
{
   static char buf[100];
   sprintf(buf, "ch1:%c,ch2:%c,spc1:%d,spc2:%d",
           (m_ch1 > 0)?m_ch1:'X', (m_ch2 > 0)?m_ch2:'X', m_spc_lead, m_spc_trail);
   return buf;
}

bool T_TRAIL_REF_FMT::detect_ref_fmt(const string& text)
{
   smatch whats;

   static const regex expr1("((\\.+|>+)(>{1,4})?)?([ \\x00-\\x07\\x1D]{0,2})"
                            "[1-8][0-9][0-9]([ \\x00-\\x07\\x1D]{0,3})$");
   if (   regex_search(text, whats, expr1)
       && (whats[1].matched || (whats[4].length() > 0)) )
   {
      if (whats[1].matched) {
         m_ch1 = whats[2].first[0];
         m_ch2 = whats[3].matched ? whats[3].first[0] : 0;
         m_spc_lead = whats[4].length();
      }
      else {
         m_ch1 = 0;
         m_ch2 = 0;
         m_spc_lead = 1;
      }
      m_spc_trail = whats[5].length();

      //if (opt_debug) printf("FMT: %s\n", print_key());
      return true;
   }
   return false;
}

void T_TRAIL_REF_FMT::init_expr() const
{
   ostringstream re;
   if (m_ch1 > 0) {
      m_subexp_idx = 2;
      re << "(\\Q" << string(1, m_ch1) << "\\E*";
      if (m_ch2 > 0) {
         re << "\\Q" + string(1, m_ch2) << "\\E{0,4}";
      }
      re << "[ \\x00-\\x07\\x1D]{" << m_spc_lead << "}|[ \\x00-\\x07\\x1D]+)";
   }
   else {
      re << "[ \\x00-\\x07\\x1D]+";
      m_subexp_idx = 1;
   }
   re << "([1-8][0-9][0-9])[ \\x00-\\x07\\x1D]{" << m_spc_trail << "}$";

   //if (opt_debug) printf("TTX REF expr '%s'\n", re.str().c_str());

   m_expr.assign(re.str());
}

bool T_TRAIL_REF_FMT::parse_trailing_ttx_ref(string& title, int& ttx_ref) const
{
   smatch whats;

   if (is_valid()) {
      if (m_expr.empty()) {
         init_expr();
      }
      if (regex_search(title, whats, m_expr)) {
         string::const_iterator p = whats[m_subexp_idx].first;

         ttx_ref = ((p[0] - '0')<<8) |
                   ((p[1] - '0')<<4) |
                   (p[2] - '0');

         if (opt_debug) printf("TTX_REF %03X on title '%s'\n", ttx_ref, title.c_str());

         // warning: must be done last - invalidates "whats"
         title.erase(title.length() - whats[0].length());
         return true;
      }
   }
   return false;
}

T_TRAIL_REF_FMT T_TRAIL_REF_FMT::select_ttx_ref_fmt(const vector<T_TRAIL_REF_FMT>& fmt_list)
{
   map<T_TRAIL_REF_FMT,int> fmt_stats;
   int max_cnt = 0;
   int max_idx = -1;

   if (!fmt_list.empty()) {
      for (uint idx = 0; idx < fmt_list.size(); idx++) {
         // count the number of occurrences of the same format in the list
         map<T_TRAIL_REF_FMT,int>::iterator p = fmt_stats.lower_bound(fmt_list[idx]);
         if ((p == fmt_stats.end()) || (fmt_list[idx] < p->first)) {
            p = fmt_stats.insert(p, make_pair(fmt_list[idx], 1));
         }
         else {
            p->second += 1;
         }
         // track the most frequently used format
         if (p->second > max_cnt) {
            max_cnt = fmt_stats[fmt_list[idx]];
            max_idx = idx;
         }
      }
      if (opt_debug) printf("auto-detected TTX reference format: %s\n", fmt_list[max_idx].print_key());

      return fmt_list[max_idx];
   }
   else {
      if (opt_debug) printf("no TTX references found for format auto-detection\n");
      return T_TRAIL_REF_FMT();
   }
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
   const TTX_DB_PAGE * pgtext = ttx_db.get_sub_page(page, sub);
   smatch whats;
   int foot;

   for (foot = 23 ; foot >= head; foot--) {
      // note missing lines are treated as empty lines
      const string& text = pgtext->get_text(foot);

      // stop at lines which look like separators
      static const regex expr1("^ *-{10,} *$");
      if (regex_search(text, whats, expr1)) {
         foot -= 1;
         break;
      }
      static const regex expr2("\\S");
      if (!regex_search(text, whats, expr2)) {
         static const regex expr3("[\\x0D\\x0F][^\\x0C]*[^ ]");
         if (!regex_search(pgtext->get_ctrl(foot - 1), whats, expr3)) {
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
      if (   regex_search(text, whats, expr4)
          || regex_search(text, whats, expr5)) {
           //|| ((sub != 0) && regex_search(text, whats, expr6)) )
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
   const TTX_DB_PAGE * pgtext = ttx_db.get_sub_page(page, sub);
   for (int line = 4; line <= 23; line++) {
      // get first non-blank char; skip non-color control chars
      static const regex expr1("^[^\\x21-\\x7F]*\\x1D");
      static const regex expr2("[\\x21-\\x7F]");
      static const regex expr3("[\\x0D\\x0F][^\\x0C]*[^ ]");

      if (regex_search(pgtext->get_ctrl(line), whats, expr1)) {
         int bg = str_bg_col(whats[0]);
         ColCount[bg] += 1;
         LineCol[line] = bg;
      }
      else if (   !regex_search(pgtext->get_ctrl(line), whats, expr2)
               && regex_search(pgtext->get_ctrl(line - 1), whats, expr3)) {
         // ignore this empty line since there's double-height chars above
         int bg = LineCol[line - 1];
         ColCount[bg] += 1;
         LineCol[line] = bg;
      }
      else {
         // background color unchanged
         ColCount[0] += 1;
         LineCol[line] = 0;
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

   // ignore last line if completely empty (e.g. ARTE: in-between double-hirhgt footer and FLOF)
   int last_line = 23;
   static const regex expr4("[\\x21-\\xff]");
   if (!regex_search(pgtext->get_ctrl(last_line), whats, expr4)) {
      --last_line;
   }

   // reliable only if 8 consecutive lines without changes, else don't skip any footer lines
   if (max_count >= 8) {
      // skip footer until normal color is reached
      for (int line = last_line; line >= 0; line--) {
         if (LineCol[line] == max_idx) {
            last_line = line;
            break;
         }
      }
   }
   //if (opt_debug) printf("FOOTER max-col:%d count:%d continuous:%d last_line:%d\n",
   //                      max_idx, ColCount[max_idx], max_count, last_line);

   return last_line;
}

/* ------------------------------------------------------------------------------
 * Remove garbage from line end in overview title lines
 * - KiKa sometimes uses the complete page for the overview and includes
 *   the footer text at the right side on a line used for the overview,
 *   separated by using a different background color
 *   " 09.45 / Dragon (7)        Weiter   >>> " (line #23)
 * - The same occurs on 3sat description pages:
 *   "\\x06Lorraine Nancy.\\x03      Mitwirkende >>>  "
 *   TODO: however with foreground colour intsead of BG
 * - note the cases with no text before the page ref is handled b the
 *   regular footer detection
 */
void RemoveTrailingPageFooter(string& text)
{
   match_results<string::iterator> whati;

   // look for a page reference or ">>" at line end
   static const regex expr1("^(.*[^[:alnum:]])([1-8][0-9][0-9]|>{1,4})[^\\x1D\\x21-\\xFF]*$");
   if (regex_search(text.begin(), text.end(), whati, expr1)) {
      int ref_off = whati[1].length();
      // check if the background color is changed
      static const regex expr2("^(.*)\\x1D[^\\x1D]+$");
      if (regex_search(text.begin(), text.begin() + ref_off, whati, expr2)) {
         ref_off = whati[1].length();
         // determine the background color of the reference (i.e. last used FG color before 1D)
         int ref_col = 7;
         static const regex expr3("^(.*)([\\x00-\\x07\\x10-\\x17])[^\\x00-\\x07\\x10-\\x17\\x1D]*$");
         if (regex_search(text.begin(), text.begin() + ref_off, whati, expr3)) {
            ref_col = text[whati.position(2)];
         }
         //print "       REF OFF:$ref_off COL:$ref_col\n";
         // determine the background before the reference
         bool matched = false;
         int txt_off = ref_off;
         if (regex_search(text.begin(), text.begin() + ref_off, whati, expr2)) {
            int tmp_off = whati[1].length();
            if (regex_search(text.begin(), text.begin() + tmp_off, whati, expr3)) {
               int txt_col = text[whati.position(2)];
               //print "       TXTCOL:$txt_col\n";
               // check if there's any text with this color
               static const regex expr4("[\\x21-\\xff]");
               if (regex_search(text.begin() + tmp_off, text.begin() + ref_off, whati, expr4)) {
                  matched = (txt_col != ref_col);
                  txt_off = tmp_off;
               }
            }
         }
         // check for text at the default BG color (unless ref. has default too)
         if (!matched && (ref_col != 7)) {
            static const regex expr5("[\\x21-\\xff]");
            matched = regex_search(text.begin(), text.begin() + txt_off, whati, expr5);
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
template<class IT>
int ConvertVpsCni(const IT& first, const IT& second)
{
   int a, b, c;

   if (   (first + (2+1+2) == second)
       && (sscanf(&first[0], "%2x%1d%2d", &a, &b, &c) == 3)
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
 * - TODO: KiKa special: "VPS 20.00" (magenta)
 */
void ParseVpsLabel(string& text, const string& text_pred, T_VPS_TIME& vps_data, bool is_desc)
{
   match_results<string::iterator> whati;
   string::iterator starti = text.begin();

   // for performance reasons check first if there's a magenta or conceal char
   static const regex expr0("([\\x05\\x18]+[\\x05\\x18 ]*)([^\\x00-\\x20])");
   while (regex_search(starti, text.end(), whati, expr0)) {
      bool is_concealed = str_is_concealed(whati[1]);
      starti = whati[2].first;

      static const regex expr1("^(VPS[\\x05\\x18 ]+)?"
                               "(\\d{4})([\\x05\\x18 ]+[\\dA-Fs]*)*([\\x00-\\x04\\x06\\x07]| *$)");
      static const regex expr1b("^VPS[\\x05\\x18 ]+" // obligatory "VPS "
                               "(\\d{2})[.:](\\d{2})([\\x00-\\x04\\x06\\x07]| *$)");
      static const regex expr1c("^((\\d{2})[.:](\\d{2}))([\\x00-\\x04\\x06\\x07]| *$)"); // obligatory VPS in preceding line
      static const regex expr1c2("[\\x00-\\x07\\x018 ]*VPS[\\x00-\\x07\\x018 ]*");
      static const regex expr2("^([0-9A-F]{2}\\d{3})[\\x05\\x18 ]+"
                               "(\\d{6})([\\x05\\x18 ]+[\\dA-Fs]*)*([\\x00-\\x04\\x06\\x07]| *$)");
      static const regex expr3("^(\\d{6})([\\x05\\x18 ]+[\\dA-Fs]*)*([\\x00-\\x04\\x06\\x07]| *$)");
      //static const regex expr4("^(\\d{2}[.:]\\d{2}) oo *([\\x00-\\x04\\x06\\x07]| *$)");
      static const regex expr5("^VPS *([\\x00-\\x04\\x06\\x07]| *$)");

      // time
      // discard any concealed/magenta labels which follow
      if (regex_search(starti, text.end(), whati, expr1)) {
         vps_data.m_vps_time.assign(whati[2].first, whati[2].second);
         vps_data.m_new_vps_time = true;
         // blank out the same area in the text-only string
         str_blank(whati[0]);
         if (opt_debug) printf("VPS time found: %s\n", vps_data.m_vps_time.c_str());
      }
      // time with ":" separator - only allowed after "VPS" prefix
      // discard any concealed/magenta labels which follow
      else if (regex_search(starti, text.end(), whati, expr1b)) {
         vps_data.m_vps_time.assign(whati[1].first, whati[1].second);
         vps_data.m_vps_time += string(whati[2].first, whati[2].second);
         vps_data.m_new_vps_time = true;
         // blank out the same area in the text-only string
         str_blank(whati[0]);
         if (opt_debug) printf("VPS time found: %s\n", vps_data.m_vps_time.c_str());
      }
      // FIXME should accept this only inside the start time column
      else if (!is_desc && regex_search(starti, text.end(), whati, expr1c)) {
         match_results<string::const_iterator> whatic; // whati remains valid
         string str_tmp = string(text_pred.begin() + whati.position(1),
                                 text_pred.begin() + whati.position(1) + whati[1].length());
         if (regex_match(text_pred.begin() + whati.position(1),
                         text_pred.begin() + whati.position(1) + whati[1].length(),
                         whatic, expr1c2)) {
            vps_data.m_vps_time.assign(whati[2].first, whati[2].second);
            vps_data.m_vps_time += string(whati[3].first, whati[3].second);
            vps_data.m_new_vps_time = true;
            // blank out the same area in the text-only string
            str_blank(whati[0]);
            if (opt_debug) printf("VPS time found: %s\n", vps_data.m_vps_time.c_str());
         }
      }
      // CNI and date "1D102 120406 F9" (ignoring checksum)
      else if (regex_search(starti, text.end(), whati, expr2)) {
         vps_data.m_vps_date.assign(whati[2].first, whati[2].second);
         vps_data.m_new_vps_date = true;
         str_blank(whati[0]);
         vps_data.m_vps_cni = ConvertVpsCni(whati[1].first, whati[1].second);
         if (opt_debug) printf("VPS date and CNI: 0x%X, /%s/\n", vps_data.m_vps_cni, vps_data.m_vps_date.c_str());
      }
      // date
      else if (regex_search(starti, text.end(), whati, expr3)) {
         vps_data.m_vps_date.assign(whati[1].first, whati[1].second);
         vps_data.m_new_vps_date = true;
         str_blank(whati[0]);
         if (opt_debug) printf("VPS date: /%s/\n", vps_data.m_vps_date.c_str());
      }
      // end time (RTL special - not standardized)
      //else if (!is_desc && regex_search(starti, text.end(), whati, expr4)) {
         // detected by the OV parser without evaluating the conceal code
         //vps_data.m_page_end_time = string(whati[1].first, whati[1].second);
         //if (opt_debug) printf("VPS(pseudo) page end time: %s\n", vps_data.m_page_end_time);
      //}
      // "VPS" marker string
      else if (regex_search(starti, text.end(), whati, expr5)) {
         str_blank(whati[0]);
      }
      else if (is_concealed) {
         if (opt_debug) printf("VPS label unrecognized in line \"%s\"\n", text.c_str());

         // replace all concealed text with blanks (may also be non-VPS related, e.g. HR3: "Un", "Ra" - who knows what this means to tell us)
         // FIXME text can also be concealed by setting fg := bg (e.g. on desc pages of MDR)
         static const regex expr6("^[^\\x00-\\x07\\x10-\\x17]*");
         if (regex_search(starti, text.end(), whati, expr6)) {
            str_blank(whati[0]);
         }
         if (opt_debug) printf("VPS label unrecognized in line \"%s\"\n", text.c_str());
      }
   }
}

/* TODO  SWR:
 *  21.58  2158  Baden-Württemberg Wetter
 *          VPS   (bis 22.00 Uhr)
 * TODO ARD
 *  13.00  ARD-Mittagsmagazin ....... 312
 *         mit Tagesschau
 *    VPS  bis 14.00 Uhr
 */
bool OV_PAGE::parse_end_time(const string& text, const string& ctrl, int& hour, int& min)
{
   smatch whats;
   bool result = false;

   // check if last line specifies and end time
   // (usually last line of page)
   // TODO internationalize "bis", "Uhr"
   static const regex expr8("^ *(\\(?bis |ab |\\- ?)(\\d{1,2})[\\.:](\\d{2})"
                            "( Uhr| ?h)( |\\)|$)");
   // FIXME should acceppt this only after title_off
   static const regex expr9("^([\\x00-\\x07\\x18 ]*)(\\d\\d)[\\.:](\\d\\d)([\\x00-\\x07\\x18 ]+oo)?[\\x00-\\x07\\x18 ]*$");
   if (   regex_search(text, whats, expr8)   // ARD,SWR,BR-alpha
       || (   regex_search(ctrl, whats, expr9)  // arte, RTL
           && (str_fg_col(ctrl.begin(), ctrl.begin() + whats.position(2)) != 5)) ) // KiKa VPS label across 2 lines
   {
      hour = atoi_substr(whats[2]);
      min = atoi_substr(whats[3]);

      if (opt_debug) printf("Overview end time: %02d:%02d\n", hour, min);
      result = true;
   }
   return result;
}

OV_SLOT::OV_SLOT(int hour, int min, bool is_tip)
{
   m_hour = hour;
   m_min = min;
   m_is_tip = is_tip;
   m_start_t = -1;
   m_stop_t = -1;
   m_end_hour = -1;
   m_end_min = -1;
   m_ttx_ref = -1;
}

OV_SLOT::~OV_SLOT()
{
}

void OV_SLOT::add_title(string subt)
{
   m_title.push_back(subt);
}

void OV_SLOT::parse_ttx_ref(const T_TRAIL_REF_FMT& fmt)
{
   for (uint idx = 0; idx < m_title.size(); idx++) {
      if (fmt.parse_trailing_ttx_ref(m_title[idx], m_ttx_ref))
         break;
   }
}

void OV_SLOT::detect_ttx_ref_fmt(vector<T_TRAIL_REF_FMT>& fmt_list)
{
   for (uint idx = 0; idx < m_title.size(); idx++) {
      T_TRAIL_REF_FMT fmt;
      if (fmt.detect_ref_fmt(m_title[idx])) {
         fmt_list.push_back(fmt);
      }
   }
}


void OV_SLOT::parse_feature_flags(TV_SLOT * p_slot)
{
   for (uint idx = 0; idx < m_title.size(); idx++) {
      if (idx != 0)
         m_title[idx].insert(0, 2, ' '); //FIXME!! HACK

      p_slot->m_feat.ParseTrailingFeat(m_title[idx]);
   }
}

void OV_SLOT::parse_title(TV_SLOT * p_slot)
{
#if 0 // obsolete?
   // kika special: subtitle appended to title
   static const regex expr7("(.*\\(\\d+( ?[\\&\\-\\+] ?\\d+)*\\))/ *(\\S.*)");
   smatch whats;
   if (regex_search(subt, whats, expr7)) {
      m_title.push_back(string(whats[1]));
      m_title.push_back(string(whats[3]));
      str_chomp(m_title[1]);
   }
   else {
      m_title.push_back(subt);
      str_chomp(m_title[0]);
   }
#endif

   // combine title with next line only if finding "-"
   uint first_subt = 1;
   p_slot->m_title = m_title[0];
   while (   (m_title.size() > first_subt)
          && (str_concat_title(p_slot->m_title, m_title[first_subt], true)) ) {
      ++first_subt;
   }

   // rest of lines: combine words separated by line end with "-"
   for (uint idx = first_subt; idx < m_title.size(); idx++) {
      str_concat_title(p_slot->m_subtitle, m_title[idx], false);
   }
}

/* ------------------------------------------------------------------------------
 * Retrieve programme entries from an overview page
 * - the layout has already been determined in advance, i.e. we assume that we
 *   have a tables with strict columns for times and titles; rows that don't
 *   have the expected format are skipped (normally only header & footer)
 */
bool OV_PAGE::parse_slots(int foot_line, const T_OV_LINE_FMT& pgfmt)
{
   T_VPS_TIME vps_data;
   OV_SLOT * ov_slot = 0;

   const TTX_DB_PAGE * pgctrl = ttx_db.get_sub_page(m_page, m_sub);
   vps_data.m_new_vps_date = false;

   for (int line = 1; line <= 23; line++) {
      // note: use text including control-characters, because the next 2 steps require these
      string ctrl = pgctrl->get_ctrl(line);

      // extract and remove VPS labels
      // (labels not asigned here since we don't know yet if a new title starts in this line)
      vps_data.m_new_vps_time = false;
      ParseVpsLabel(ctrl, pgctrl->get_ctrl(line - 1), vps_data, false);

      // remove remaining control characters
      string text = ctrl;
      str_repl_ctrl(text);

      bool is_tip = false;
      int hour = -1;
      int min = -1;

      if ( pgfmt.parse_title_line(text, hour, min, is_tip) ) {
         // remember end of page header for date parser
         if (m_head < 0)
            m_head = line;

         if (opt_debug) printf("OV TITLE: \"%s\", %02d:%02d\n", pgfmt.extract_title(text).c_str(), hour, min);

         ov_slot = new OV_SLOT(hour, min, is_tip);
         m_slots.push_back(ov_slot);

         ov_slot->add_title(pgfmt.extract_title(ctrl));

         if (vps_data.m_new_vps_time) {
            ov_slot->m_vps_time = vps_data.m_vps_time;
         }
         if (vps_data.m_new_vps_date) {
            ov_slot->m_vps_date = vps_data.m_vps_date;
            vps_data.m_new_vps_date = false;
         }
         //ov_slot->m_vps_cni = vps_data.m_vps_cni; // currently unused

         //printf("ADD  %02d.%02d.%d %02d:%02d %s\n", ov_slot->m_mday, ov_slot->m_month, ov_slot->m_year, ov_slot->m_hour, ov_slot->m_min, ov_slot->m_title.c_str());
      }
      else if (ov_slot != 0) {
         // stop appending subtitles before page footer ads
         if (line > foot_line)
            break;

         if (vps_data.m_new_vps_time)
            ov_slot->m_vps_time = vps_data.m_vps_time;

         //FIXME normally only following the last slot on the page
         if (parse_end_time(text, ctrl, ov_slot->m_end_hour, ov_slot->m_end_min)) {
            // end of this slot
            ov_slot = 0;
         }
         // check if we're still in a continuation of the previous title
         // time column must be empty (possible VPS labels were already removed above)
         else if (pgfmt.parse_subtitle(text)) {
            ov_slot->add_title(pgfmt.extract_subtitle(ctrl));

            if (vps_data.m_new_vps_date) {
               ov_slot->m_vps_date = vps_data.m_vps_date;
               vps_data.m_new_vps_date = false;
            }
         }
         else {
            ov_slot = 0;
         }
      }
   }
   return m_slots.size() > 0;
}

bool OV_SLOT::is_same_prog(const OV_SLOT& v) const
{
   return (v.m_hour == m_hour) &&
          (v.m_min == m_min);
}

/* ------------------------------------------------------------------------------
 * Check if an overview page has exactly the same title list as the predecessor
 * (some networks use subpages just to display different ads)
 */
bool OV_PAGE::check_redundant_subpage(OV_PAGE * prev)
{
   bool result = false;

   if (   (m_page == prev->m_page)
       && (m_sub > 1)
       && (m_sub == prev->m_sub + prev->m_sub_page_skip + 1) )
   {
      // FIXME check for overlap in a more general way (allow for missing line on one page? but then we'd need to merge)
      if (prev->m_slots.size() == m_slots.size()) {
         result = true;
         for (uint idx = 0; idx < m_slots.size(); idx++) {
            if (!m_slots[idx]->is_same_prog(*prev->m_slots[idx])) {
               result = false;
               break;
            }
         }
         if (result) {
            if (opt_debug) printf("OV_PAGE 0x%3X.%d dropped: redundant to sub-page %d\n", m_page, m_sub, prev->m_sub);
            prev->m_sub_page_skip += 1;
         }
      }
   }
   return result;
}

/* ------------------------------------------------------------------------------
 * Convert discrete start times into UNIX epoch format
 * - implies a conversion from local time zone into GMT
 */
time_t OV_SLOT::ConvertStartTime(const T_PG_DATE * pgdate, int date_off) const
{
   struct tm tm;

   memset(&tm, 0, sizeof(tm));

   tm.tm_hour = m_hour;
   tm.tm_min  = m_min;

   tm.tm_mday = pgdate->m_mday + pgdate->m_date_off + date_off;
   tm.tm_mon  = pgdate->m_month - 1;
   tm.tm_year = pgdate->m_year - 1900;

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
bool CheckIfPagesAdjacent(int page1, int sub1, int sub_skip, int page2, int sub2)
{
   bool result = false;

   if ( (page1 == page2) &&
        (sub1 + sub_skip + 1 == sub2) ) {
      // next sub-page on same page
      result = true;
   }
   else {
      // check for jump from last sub-page of prev page to first of new page
      int next = GetNextPageNumber(page1);

      int last_sub = ttx_db.last_sub_page_no(page1);

      if ( (next == page2) &&
           (sub1 + sub_skip == last_sub) &&
           (sub2 <= 1) ) {
         result = true;
      } 
   }
   return result;
}

bool OV_PAGE::is_adjacent(const OV_PAGE * prev) const
{
   return CheckIfPagesAdjacent(prev->m_page, prev->m_sub, prev->m_sub_page_skip, m_page, m_sub);
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
bool OV_PAGE::calc_date_off(const OV_PAGE * prev)
{
   bool result = false;

   int date_off = 0;
   if ((prev != 0) && (prev->m_slots.size() > 0)) {
      const OV_SLOT * prev_slot1 = prev->m_slots[0];
      //FIXME const OV_SLOT * prev_slot2 = prev->m_slots[prev->m_slots.size() - 1];
      // check if the page number of the previous page is adjacent
      // and as consistency check require that the prev page covers less than a day
      if (   /* (prev_slot2->m_start_t - prev_slot1->m_start_t < 22*60*60) // TODO/FIXME
          &&*/ is_adjacent(prev))
      {
         int prev_delta = 0;
         // check if there's a date on the current page
         // if yes, get the delta to the previous one in days (e.g. tomorrow - today = 1)
         // if not, silently fall back to the previous page's date and assume date delta zero
         if (m_date.m_year != -1) {
            prev_delta = CalculateDateDelta(prev->m_date, m_date);
         }
         if (prev_delta == 0) {
            // check if our start date should be different from the one of the previous page
            // -> check if we're starting on a new date (smaller hour)
            // (note: comparing with the 1st slot of the prev. page, not the last!)
            if (m_slots[0]->m_hour < prev_slot1->m_hour) {
               // TODO: check continuity to slot2: gap may have 6h max.
               // TODO: check end hour
               date_off = 1;
            }
         }
         else {
            if (opt_debug) printf("OV DATE %03X.%d: prev page %03X.%04X date cleared\n",
                                  m_page, m_sub, prev->m_page, prev->m_sub);
            prev = 0;
         }
         // TODO: date may also be wrong by +1 (e.g. when starting at 23:55 with date for 00:00)
      }
      else {
         // not adjacent -> disregard the info
         if (opt_debug) printf("OV DATE %03X.%d: prev page %03X.%04X not adjacent - not used for date check\n",
                               m_page, m_sub, prev->m_page, prev->m_sub);
         prev = 0;
      }
   }

   if (m_date.m_year != -1) {
      // store date offset in page meta data (to calculate delta of subsequent pages)
      m_date.m_date_off = date_off;
      if (opt_debug) printf("OV DATE %03X.%d: using page header date %d.%d.%d, DELTA %d\n",
                            m_page, m_sub, m_date.m_mday, m_date.m_month, m_date.m_year, m_date.m_date_off);
      result = true;
   }
   else if (prev != 0) {
      // copy date from previous page
      m_date.m_mday = prev->m_date.m_mday;
      m_date.m_month = prev->m_date.m_month;
      m_date.m_year = prev->m_date.m_year;
      // add date offset if a date change was detected
      m_date.m_date_off = prev->m_date.m_date_off + date_off;

      if (opt_debug) printf("OV DATE: using predecessor date %d.%d.%d, DELTA %d\n", m_date.m_mday, m_date.m_month, m_date.m_year, m_date.m_date_off);
      result = true;
   }
   else {
      if (opt_debug) printf("OV %03X.%d missing date - discarding programmes\n", m_page, m_sub);
   }
   return result;
}

/* ------------------------------------------------------------------------------
 * Calculate exact start time for each title: Based on page date and HH::MM
 * - date may wrap inside of the page
 */
void OV_PAGE::calculate_start_times()
{
   assert (m_date.m_year != -1);

   int date_off = 0;
   int prev_hour = -1;

   for (uint idx = 0; idx < m_slots.size(); idx++) {
      OV_SLOT * slot = m_slots[idx];

      // detect date change (hour wrap at midnight)
      if ((prev_hour != -1) && (prev_hour > slot->m_hour)) {
         date_off += 1;
      }
      prev_hour = slot->m_hour;

      slot->m_start_t = slot->ConvertStartTime(&m_date, date_off);
   }
}

/* ------------------------------------------------------------------------------
 * Check plausibility of times on a page
 * - there are sometimes pages with times in reverse order (e.g. with ratings)
 *   these cause extreme overlaps as all titles will cover 22 to 23 hours
 */
bool OV_PAGE::check_start_times()
{
   int date_wraps = 0;
   int prev_hour = -1;
   int prev_min = -1;

   for (uint idx = 0; idx < m_slots.size(); idx++) {
      OV_SLOT * slot = m_slots[idx];

      // detect date change (hour wrap at midnight)
      if (   (prev_hour != -1)
          && (   (prev_hour > slot->m_hour)
              || (   (prev_hour == slot->m_hour)
                  && (prev_min > slot->m_min) )))  // allow ==
      {
         ++date_wraps;
      }
      prev_hour = slot->m_hour;
      prev_min = slot->m_min;
   }
   bool result = date_wraps < 2;

   if (opt_debug && !result) printf("DROP PAGE %03X.%d: %d date wraps\n", m_page, m_sub, date_wraps);
   return result;
}

/* ------------------------------------------------------------------------------
 * Determine stop times
 * - assuming that in overview tables the stop time is equal to the start of the
 *   following programme & that this also holds true inbetween adjacent pages
 * - if in doubt, leave it undefined (this is allowed in XMLTV)
 * - TODO: restart at non-adjacent text pages
 */
void OV_PAGE::calc_stop_times(const OV_PAGE * next)
{
   for (uint idx = 0; idx < m_slots.size(); idx++)
   {
      OV_SLOT * slot = m_slots[idx];

      if (slot->m_end_min >= 0) {
         // there was an end time in the overview or a description page -> use that
         struct tm tm = *localtime(&slot->m_start_t);

         tm.tm_min = slot->m_end_min;
         tm.tm_hour = slot->m_end_hour;

         // check for a day break between start and end
         if ( (slot->m_end_hour < slot->m_hour) ||
              ( (slot->m_end_hour == slot->m_hour) &&
                (slot->m_end_min < slot->m_min) )) {
            tm.tm_mday += 1; // possible wrap done by mktime()
         }
         slot->m_stop_t = mktime(&tm);

         if (opt_debug) printf("OV_SLOT %02d:%02d use end time %02d:%02d - %s",
                               slot->m_hour, slot->m_min, slot->m_end_hour, slot->m_end_min, ctime(&slot->m_stop_t));
      }
      else if (idx + 1 < m_slots.size()) {
         OV_SLOT * next_slot = m_slots[idx + 1];

         if (next_slot->m_start_t != slot->m_start_t) {
            slot->m_stop_t = next_slot->m_start_t;
         }

         if (opt_debug) printf("OV_SLOT %02d:%02d ends at next start time %s",
                               slot->m_hour, slot->m_min, ctime(&next_slot->m_start_t));
      }
      else if (   (next != 0)
               && (next->m_slots.size() > 0)
               && next->is_adjacent(this) )
      {
         OV_SLOT * next_slot = next->m_slots[0];

         // no end time: use start time of the following programme if less than 9h away
         if (   (next_slot->m_start_t > slot->m_start_t)
             && (next_slot->m_start_t - slot->m_start_t < 9*60*60) )
         {
            slot->m_stop_t = next_slot->m_start_t;
         }
         if (opt_debug) printf("OV_SLOT %02d:%02d ends at next page %03X.%d start time %s",
                               slot->m_hour, slot->m_min, next->m_page, next->m_sub,
                               ctime(&next_slot->m_start_t));
      }
   }
}

/* ------------------------------------------------------------------------------
 * Detect position and leading garbage for description page references
 */
T_TRAIL_REF_FMT OV_PAGE::detect_ov_ttx_ref_fmt(const vector<OV_PAGE*>& ov_pages)
{
   vector<T_TRAIL_REF_FMT> fmt_list;

   // parse all slot titles for TTX reference format
   for (uint pg_idx = 0; pg_idx < ov_pages.size(); pg_idx++) {
      OV_PAGE * ov_page = ov_pages[pg_idx];
      for (uint slot_idx = 0; slot_idx < ov_page->m_slots.size(); slot_idx++) {
         OV_SLOT * slot = ov_page->m_slots[slot_idx];
         slot->detect_ttx_ref_fmt(fmt_list);
      }
   }
   return T_TRAIL_REF_FMT::select_ttx_ref_fmt(fmt_list);
}

void OV_PAGE::extract_ttx_ref(const T_TRAIL_REF_FMT& fmt)
{
   for (uint idx = 0; idx < m_slots.size(); idx++) {
      OV_SLOT * slot = m_slots[idx];
      slot->parse_ttx_ref(fmt);
   }
}

void OV_PAGE::extract_tv(list<TV_SLOT*>& tv_slots)
{
   if (m_slots.size() > 0) {
      RemoveTrailingPageFooter(m_slots.back()->m_title.back());
   }

   for (uint idx = 0; idx < m_slots.size(); idx++) {
      OV_SLOT * slot = m_slots[idx];

      TV_SLOT * p_tv = new TV_SLOT(slot->m_ttx_ref, slot->m_start_t, slot->m_stop_t,
                                   slot->m_vps_time, slot->m_vps_date);

      slot->parse_feature_flags(p_tv);
      p_tv->m_feat.set_tip(slot->m_is_tip);

      slot->parse_title(p_tv);

      if (slot->m_ttx_ref != -1) {
         ParseDescPage(p_tv, &m_date);
      }

      tv_slots.push_back(p_tv);
   }
}

/* ------------------------------------------------------------------------------
 * Retrieve programme data from an overview page
 * - 1: compare several overview pages to identify the layout
 * - 2: parse all overview pages, retrieving titles and ttx references
 *   + a: retrieve programme list (i.e. start times and titles)
 *   + b: retrieve date from the page header
 *   + c: determine dates
 *   + d: determine stop times
 */
list<TV_SLOT*> ParseAllOvPages()
{
   list<TV_SLOT*> tv_slots;

   T_OV_LINE_FMT fmt = DetectOvFormat();
   if (fmt.is_valid()) {
      vector<OV_PAGE*> ov_pages;

      for (TTX_DB::const_iterator p = ttx_db.begin(); p != ttx_db.end(); p++)
      {
         int page = p->first.page();
         int sub = p->first.sub();

         if ((page >= opt_tv_start) && (page <= opt_tv_end)) {
            if (opt_debug) printf("OVERVIEW PAGE %03X.%04X\n", page, sub);

            OV_PAGE * ov_page = new OV_PAGE(page, sub);

            int foot = ParseFooterByColor(page, sub);

            if (ov_page->parse_slots(foot, fmt)) {
               if (ov_page->check_start_times()) {
                  ov_page->parse_ov_date();

                  if (   (ov_pages.size() == 0)
                      || !ov_page->check_redundant_subpage(ov_pages.back())) {

                     ov_pages.push_back(ov_page);
                     ov_page = 0;
                  }
               }
            }
            delete ov_page;
         }
      }

      for (uint idx = 0; idx < ov_pages.size(); ) {
         if (ov_pages[idx]->calc_date_off((idx > 0) ? ov_pages[idx - 1] : 0)) {

            ov_pages[idx]->calculate_start_times();
            idx++;
         }
         else {
            delete ov_pages[idx];
            ov_pages.erase(ov_pages.begin() + idx);
         }
      }

      // guess missing stop times for the current page
      // (requires start times for the next page)
      for (uint idx = 0; idx < ov_pages.size(); idx++) {
         OV_PAGE * next = (idx + 1 < ov_pages.size()) ? ov_pages[idx + 1] : 0;
         ov_pages[idx]->calc_stop_times(next);
      }

      // retrieve TTX page references
      T_TRAIL_REF_FMT ttx_ref_fmt = OV_PAGE::detect_ov_ttx_ref_fmt(ov_pages);
      for (uint idx = 0; idx < ov_pages.size(); idx++) {
         ov_pages[idx]->extract_ttx_ref(ttx_ref_fmt);
      }

      // retrieve descriptions from references teletext pages
      for (uint idx = 0; idx < ov_pages.size(); idx++) {
         ov_pages[idx]->extract_tv(tv_slots);

         delete ov_pages[idx];
      }
   }

   return tv_slots;
}

/* ------------------------------------------------------------------------------
 * Filter out expired programmes
 * - the stop time is relevant for expiry (and this really makes a difference if
 *   the expiry time is low (e.g. 6 hours) since there may be programmes exceeding
 *   it and we certainly shouldn't discard programmes which are still running
 * - resulting problem: we don't always have the stop time
 */
void FilterExpiredSlots(list<TV_SLOT*>& Slots)
{
   time_t exp_thresh = time(NULL) - opt_expire * 60;

   if (!Slots.empty()) {
      for (list<TV_SLOT*>::iterator p = Slots.begin(); p != Slots.end(); ) {
         if (   (   ((*p)->m_stop_t != -1)
                 && ((*p)->m_stop_t >= exp_thresh))
             || ((*p)->m_start_t + 120*60 >= exp_thresh) )
         {
            ++p;
         }
         else {
            if (opt_debug) printf("EXPIRED new %ld-%ld < %ld '%s'\n", (long)(*p)->m_start_t,
                                  (long)(*p)->m_stop_t, (long)exp_thresh, (*p)->m_title.c_str());
            delete *p;
            Slots.erase(p++);
         }
      }
      if (Slots.empty()) {
         fprintf(stderr, "Warning: all newly acquired programmes are already expired\n");
      }
   }
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
   assert(slot.m_title.length() > 0);
   assert(slot.m_start_t != -1);

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
         str_tolower(title);
      }
      Latin1ToXml(title);

      //TODO fprintf(fp, "\n<!-- TTX %03X.%04d %02d:%02d %02d.%02d.%04d -->\n", ...);

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
      if (   slot.m_feat.m_is_video_bw
          || slot.m_feat.m_is_aspect_16_9
          || slot.m_feat.m_is_video_hd) {
         fprintf(fp, "\t<video>\n");
         if (slot.m_feat.m_is_video_bw) {
            fprintf(fp, "\t\t<colour>no</colour>\n");
         }
         if (slot.m_feat.m_is_aspect_16_9) {
            fprintf(fp, "\t\t<aspect>16:9</aspect>\n");
         }
         if (slot.m_feat.m_is_video_hd) {
            fprintf(fp, "\t\t<quality>HDTV</quality>\n");
         }
         fprintf(fp, "\t</video>\n");
      }
      // audio
      if (slot.m_feat.m_is_dolby) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>surround</stereo>\n\t</audio>\n");
      }
      else if (slot.m_feat.m_is_stereo) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>stereo</stereo>\n\t</audio>\n");
      }
      else if (slot.m_feat.m_is_mono) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>mono</stereo>\n\t</audio>\n");
      }
      else if (slot.m_feat.m_is_2chan) {
         fprintf(fp, "\t<audio>\n\t\t<stereo>bilingual</stereo>\n\t</audio>\n");
      }
      // subtitles
      if (slot.m_feat.m_is_omu) {
         fprintf(fp, "\t<subtitles type=\"onscreen\"/>\n");
      }
      else if (slot.m_feat.m_has_subtitles) {
         fprintf(fp, "\t<subtitles type=\"teletext\"/>\n");
      }
      // tip/highlight (ARD only)
      if (slot.m_feat.m_is_tip) {
         fprintf(fp, "\t<star-rating>\n\t\t<value>1/1</value>\n\t</star-rating>\n");
      }
      fprintf(fp, "</programme>\n");
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

bool ParseDescDate(int page, int sub, TV_SLOT * slot, int date_off)
{
   int lmday = -1;
   int lmonth = -1;
   int lyear = -1;
   int lhour = -1;
   int lmin = -1;
   int lend_hour = -1;
   int lend_min = -1;
   bool check_time = false;

   const TTX_DB_PAGE * pgtext = ttx_db.get_sub_page(page, sub);
   int lang = pgtext->get_lang();
   string wday_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_FULL);
   string wday_abbrv_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_ABBRV);
   string mname_match = GetDateNameRegExp(MonthNames, lang, DATE_NAME_ANY);
   smatch whats;

   // search date and time
   for (int line = 1; line <= 23; line++) {
      const string& text = pgtext->get_text(line);
      bool new_date = false;

      {
         // [Fr] 14.04.[06] (year not optional for single-digit day/month)
         static const regex expr1("(^| )(\\d{1,2})\\.(\\d{1,2})\\.(\\d{4}|\\d{2})?( |,|;|:|$)");
         static const regex expr2("(^| )(\\d{1,2})\\.(\\d)\\.(\\d{4}|\\d{2})( |,|;|:|$)");
         static const regex expr3("(^| )(\\d{1,2})\\.(\\d)\\.?(\\d{4}|\\d{2})?(,|;| ?-)? +\\d{1,2}[\\.:]\\d{2}(h| |,|;|:|$)");
         if (   regex_search(text, whats, expr1)
             || regex_search(text, whats, expr2)
             || regex_search(text, whats, expr3)) {
            if (CheckDate(atoi_substr(whats[2]), atoi_substr(whats[3]), atoi_substr(whats[4]),
                          "", "", pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
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
         if (   regex_search(text, whats, expr4[lang])
             || regex_search(text, whats, expr5[lang])) {
            if (CheckDate(atoi_substr(whats[4]), atoi_substr(whats[5]), -1,
                          string(whats[2]), "", pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
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
         if (regex_search(text, whats, expr6[lang])) {
            if (CheckDate(atoi_substr(whats[2]), -1, atoi_substr(whats[5]),
                          "", string(whats[3]), pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
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
         if (   regex_search(text, whats, expr7[lang])
             || regex_search(text, whats, expr8[lang])) {
            if (CheckDate(atoi_substr(whats[4]), -1, atoi_substr(whats[7]),
                          string(whats[2]), string(whats[5]), pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
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
         if (   regex_search(text, whats, expr10[lang])
             || regex_search(text, whats, expr11[lang])) {
            if (CheckDate(-1, -1, -1, string(whats[2]), "", pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // " ... Sonntag" (HR3) (time sometimes directly below, but not always)
         static regex expr20[8];
         if (expr20[lang].empty()) {
            expr20[lang].assign(string("(^| )(") + wday_match + ") *$", regex::icase);
         }
         if (!check_time && regex_search(text, whats, expr20[lang])) {
            if (CheckDate(-1, -1, -1, string(whats[2]), "", pgtext->get_timestamp(),
                          &lmday, &lmonth, &lyear)) {
               new_date = true;
               goto DATE_FOUND;
            }
         }
         // TODO: 21h (i.e. no minute value: TV5)

         // TODO: make exact match between VPS date and time from overview page
         // TODO: allow garbage before or after label; check reveal and magenta codes (ETS 300 231 ch. 7.3)
         // VPS label "1D102 120406 F9"
         static const regex expr21("^ +[0-9A-F]{2}\\d{3} (\\d{2})(\\d{2})(\\d{2}) [0-9A-F]{2} *$");
         if (regex_search(text, whats, expr21)) {
            lmday = atoi_substr(whats[1]);
            lmonth = atoi_substr(whats[2]);
            lyear = atoi_substr(whats[3]) + 2000;
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
      if (   regex_search(text, whats, expr12)
          || regex_search(text, whats, expr13)) {
         int hour = atoi_substr(whats[2]);
         int min = atoi_substr(whats[3]);
         int end_hour = atoi_substr(whats[7]);
         int end_min = atoi_substr(whats[8]);
         // int vps = atoi_substr(whats[5]);
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
      else if (   (new_date && regex_search(text, whats, expr14))
               || regex_search(text, whats, expr15)
               || regex_search(text, whats, expr16)
               || regex_search(text, whats, expr17)) {
         int hour = atoi_substr(whats[2]);
         int min = atoi_substr(whats[3]);
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
         if ((start_t != -1) && (abs(start_t - slot->m_start_t) < 5*60)) {
            // match found
#if 0 //TODO
            if ((lend_hour != -1) && (slot->m_end_hour != -1)) {
               // add end time to the slot data
               // TODO: also add VPS time
               // XXX FIXME: these are never used because stop_t is already calculated!
               slot->m_end_hour = lend_hour;
               slot->m_end_min = lend_min;
            }
#endif
            return true;
         }
         else {
            if (date_off != 0) {
               // try again with compensation by date offset which was found on overview page (usually in time range 0:00 - 6:00)
               tm.tm_mday += date_off;
               start_t = mktime(&tm);
            }
            if ((start_t != -1) && (abs(start_t - slot->m_start_t) < 5*60)) {
               // match found
               return true;
            }
            else {
               if (opt_debug) printf("MISMATCH[date_off:%d]: %s %s\n", date_off,
                                     GetXmlTimestamp(start_t).c_str(),
                                     GetXmlTimestamp(slot->m_start_t).c_str());
               lend_hour = -1;
               if (new_date) {
                  // date on same line as time: invalidate both upon mismatch
                  lyear = -1;
               }
            }
         }
      }
   }
   return false;
}

/* Search for the line which contains the title string and return the
 * line number. Additionally, extract feature attributes from the lines
 * holding the title. The title may span across two consecutive lines,
 * which are then concatenated in the same way as for overview pages.
 * The comparison is done case-insensitive as some networks write the
 * titles in all upper-case.
 */
int ParseDescTitle(int page, int sub, TV_SLOT * slot)
{
   const TTX_DB_PAGE * pgctrl = ttx_db.get_sub_page(page, sub);
   string prev;

   string title_lc = slot->m_title;
   str_tolower(title_lc);

   for (int idx = 1; idx <= 23/2; idx++) {
      string text_lc = pgctrl->get_ctrl(idx);
      str_tolower(text_lc);

      uint off = str_get_indent(text_lc);

      //if (str_find_word(text_lc, title_lc) != string::npos)
      if (   (title_lc.length() <= text_lc.length() - off)
          && (text_lc.compare(off, title_lc.length(), title_lc) == 0) ) {
         // TODO: back up if there's text in the previous line with the same indentation

         string tmp = pgctrl->get_ctrl(idx);
         slot->m_feat.ParseTrailingFeat(tmp);

         // correct title/sub-title split
         if (!slot->m_subtitle.empty()) {
            // check if the title line on the desc page contains 1st + 2nd title lines from overview
            // Overview:                     Description page:
            // Licht aus in Erichs           Licht aus in Erichs Lampenladen
            // Lampenladen
            string subtitle = slot->m_title + string(" ") +  slot->m_subtitle;
            if (   (tmp.length() >= subtitle.length())
                && str_is_right_word_boundary(tmp, subtitle.length())
                && (tmp.compare(0, subtitle.length(), subtitle) == 0) ) {
               if (opt_debug) printf("DESC title override \"%s\"\n", subtitle.c_str());
               slot->m_title = subtitle;
               slot->m_subtitle.clear();
            }
            else if (tmp.length() - off > slot->m_title.length()) {
               string next = pgctrl->get_ctrl(idx + 1);
               if (str_get_indent(next) == off) {
                  str_concat_title(tmp, next, false);
                  str_chomp(tmp);
                  TV_FEAT::RemoveTrailingFeat(tmp);
                  // Overview:                     Description page:
                  // Battlefield Earth - Kampf     Battlefield Earth - Kampf um die
                  // um die Erde                   Erde
                  // Science Fiction USA 2000      Science Fiction USA 2000
                  // FIXME instead of min() use "2nd line of title on overview page" as limit
                  uint len = min(tmp.length(), subtitle.length());
                  if (   str_is_right_word_boundary(tmp, len)
                      && str_is_right_word_boundary(subtitle, len)
                      && (tmp.compare(0, len, subtitle, 0, len) == 0) ) {
                     if (opt_debug) printf("DESC title override \"%s\"\n", subtitle.c_str());
                     slot->m_title.assign(subtitle, 0, len);
                     slot->m_subtitle.assign(subtitle, len, subtitle.length() - len);
                     str_chomp(slot->m_subtitle);
                     slot->m_feat.ParseTrailingFeat(next);
                  }
               }
            }
         }

         return idx;
      }
      else if (!prev.empty()) {
         TV_FEAT::RemoveTrailingFeat(prev);
         str_concat_title(prev, pgctrl->get_ctrl(idx), false);
         str_tolower(prev);
         //if (str_find_word(prev, title_lc) != string::npos)  // sub-string
         if (   (title_lc.length() <= prev.length())
             && (prev.compare(0, title_lc.length(), title_lc) == 0) ) {
            string tmp = string(pgctrl->get_ctrl(idx - 1));
            slot->m_feat.ParseTrailingFeat(tmp);

            tmp = pgctrl->get_ctrl(idx);
            slot->m_feat.ParseTrailingFeat(tmp);

            //TODO: check if title/sub-title split can be corrected (see above)

            return idx - 1;
         }
      }
      prev.assign(pgctrl->get_ctrl(idx), off, VT_PKG_RAW_LEN - off);
   }
   //print "NOT FOUND title\n";
   return 1;
}

/* Compare two text pages line-by-line until a difference is found.
 * Thus identical lines on sub-pages can be excluded from descriptions.
 */
int CorrelateDescTitles(int page, int sub1, int sub2, int head)
{
   const TTX_DB_PAGE * pgctrl1 = ttx_db.get_sub_page(page, sub1);
   const TTX_DB_PAGE * pgctrl2 = ttx_db.get_sub_page(page, sub2);

   for (uint line = head; line < TTX_DB_PAGE::TTX_TEXT_LINE_CNT; line++) {
      const string& p1 = pgctrl1->get_ctrl(line);
      const string& p2 = pgctrl2->get_ctrl(line);
      uint cnt = 0;
      for (uint col = 0; col < VT_PKG_RAW_LEN; col++) {
         if (p1[col] == p2[col])
            cnt++;
      }
      // stop if lines are not similar enough
      if (cnt < VT_PKG_RAW_LEN - VT_PKG_RAW_LEN/10) {
         return line;
      }
   }
   return TTX_DB_PAGE::TTX_TEXT_LINE_CNT;
}

/* Replace page index markers in description pages with space; the marker
 * is expected at the end of one of the top lines of the page only
 * (e.g. "...     2/2 "). Note currently not handled: providers
 * might mark a page with "1/1" when sub-pages differ by ads only.
 */
void DescRemoveSubPageIdx(vector<string>& Lines, int sub)
{
   smatch whats;

   for (uint row = 0; (row < Lines.size()) && (row < 6); row++) {
      static const regex expr1(" (\\d+)/(\\d+) {0,2}$");
      if (regex_search(Lines[row], whats, expr1)) {
         int sub_idx = atoi_substr(whats[1]);
         int sub_cnt = atoi_substr(whats[2]);
         if ((sub == 0)
               ? ((sub_idx == 1) && (sub_cnt == 1))
               : ((sub_idx == sub) && (sub_cnt > 1)) )
         {
            Lines[row].replace(whats.position(), whats[0].length(), whats[0].length(), ' ');
            break;
         }
      }
   }
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
   uint tab_max = 0;
   for (uint row = 0; row < Lines.size(); row++) {
      static const regex expr1("^( *)((.*?)( ?)\\.\\.+( ?))[^ ].*?[^ ]( *)$");
      if (regex_search(Lines[row], whats, expr1)) {
         // left-aligned (ignore spacing on the right)
         uint rec = whats[1].length() |
                    ((whats[1].length() + whats[2].length()) << 6)|
                    (whats[4].length() << 12) |
                    (whats[5].length() << 18) |
                    (0x3F << 24);
         Tabs[rec]++;
         if ((tab_max == 0) || (Tabs[tab_max] < Tabs[rec]))
            tab_max = rec;

         // right-aligned (ignore spacing on the left)
         rec = whats[1].length() |
               (0x3F << 6) |
               (whats[4].length() << 12) |
               (whats[5].length() << 18) |
               (whats[6].length() << 24);
         Tabs[rec]++;
         if ((tab_max == 0) || (Tabs[tab_max] < Tabs[rec]))
            tab_max = rec;
      }
   }

   // minimum is two lines looking like table rows
   if ((tab_max != 0) && (Tabs[tab_max] >= 2)) {
      const uint spc0 = tab_max & 0x3F;
      const uint off  = (tab_max >> 6) & 0x3F;
      const uint spc1 = (tab_max >> 12) & 0x3F;
      const uint spc2 = (tab_max >> 18) & 0x3F;
      const uint spc3 = tab_max >> 24;

      regex expr2;
      if (spc3 == 0x3F) {
         expr2.assign(string("^") + string(spc0, ' ') +
                      string("([^ ].*?)") + string(spc1, ' ') +
                      string("(\\.*)") + string(spc2, ' ') + "[^ \\.]$");
      } else {
         expr2.assign(string("^(.*?)") + string(spc1, ' ') +
                      string("(\\.+)") + string(spc2, ' ') +
                      string("([^ \\.].*?)") + string(spc3, ' ') + "$");
      }
      // Special handling for lines with overlong names on left or right side:
      // only for lines inside of table; accept anything which looks like a separator
      static const regex expr3("^(.*?)\\.\\.+ ?[^ \\.]");
      static const regex expr4("^(.*?[^ ]) \\. [^ \\.]");  // must not match "Mr. X"

      if (opt_debug) printf("DESC reformat table into list: %d rows, FMT:%d,%d %d,%d EXPR:%s\n",
                            Tabs[tab_max], off, spc1, spc2, spc3, expr2.str().c_str());

      // step #2: find all lines with dots ending at the right column and right amount of spaces
      int last_row = -1;
      for (uint row = 0; row < Lines.size(); row++) {
         if (spc3 == 0x3F) {
            //
            // 2nd column is left-aligned
            //
            if (   (   regex_search(Lines[row].substr(0, off + 1), whats, expr2)
                    && ((last_row != -1) || (whats[2].length() > 0)) )
                || ((last_row != -1) && regex_search(Lines[row], whats, expr3))
                || ((last_row != -1) && regex_search(Lines[row], whats, expr4)) ) {
               string tab1 = Lines[row].substr(spc0, whats[1].length());
               string tab2 = Lines[row].substr(whats[0].length() - 1); // for expr3 or 4, else it's fixed to "off"
               // match -> replace dots with colon
               tab1 = regex_replace(tab1, regex("[ :]*$"), "");
               tab2 = regex_replace(tab2, regex("[ ,]*$"), "");
               Lines[row] = tab1 + string(": ") + tab2 + string(",");
               last_row = row;
            }
            else if (last_row != -1) {
               static const regex expr5("^ +[^ ]$");
               if (regex_search(Lines[row].substr(0, off + 1), whats, expr5)) {
                  // right-side table column continues (left side empty)
                  Lines[last_row] = regex_replace(Lines[last_row], regex(",$"), "");
                  string tab2 = regex_replace(Lines[row].substr(off), regex(",? +$"), "");
                  Lines[row] = tab2 + ",";
                  last_row = row;
               }
            }
         } else {
            //
            // 2nd column is right-aligned
            //
            if (regex_search(Lines[row], whats, expr2)) {
               string tab1 = string(whats[1]);
               string tab2 = Lines[row].substr(whats.position(3));
               // match -> replace dots with colon
               tab1 = regex_replace(tab1, regex(" *:$"), "");
               tab2 = regex_replace(tab2, regex(",? +$"), "");
               Lines[row] = tab1 + string(": ") + tab2 + string(",");
               last_row = row;
            }
         }

         if ((last_row != -1) && (last_row < (int)row)) {
            // end of table: terminate list
            Lines[last_row] = regex_replace(Lines[last_row], regex(",$"), ".");
            last_row = -1;
         }
      }
      if (last_row != -1) {
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

   const TTX_DB_PAGE * pgctrl = ttx_db.get_sub_page(page, sub);
   bool is_nl = 0;
   string desc;

   for (int idx = head; idx <= foot; idx++) {
      string ctrl = pgctrl->get_ctrl(idx);

      // TODO parse features behind date, title or subtitle

      // extract and remove VPS labels and all concealed text
      ParseVpsLabel(ctrl, pgctrl->get_ctrl(idx - 1), vps_data, true);

      static const regex expr2("[\\x00-\\x1F\\x7F]");
      ctrl = regex_replace(ctrl, expr2, " ");

      // remove VPS time and date labels
      static const regex expr3("^ +[0-9A-F]{2}\\d{3} (\\d{2})(\\d{2})(\\d{2}) [0-9A-F]{2} *$");
      static const regex expr4(" +VPS \\d{4} *$");
      ctrl = regex_replace(ctrl, expr3, "");
      ctrl = regex_replace(ctrl, expr4, "");

      Lines.push_back(ctrl);
   }

   DescRemoveSubPageIdx(Lines, sub);
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
            desc.erase(desc.end() - 1);
            desc += line;
         }
         else if ((desc.length() > 0) && !is_nl) {
            desc += " ";
            desc += line;
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
   // TODO: also cut off ">314" et.al. (but not ref. to non-TV pages)
   static const regex expr14("(>>+ *| +)$");
   if (regex_search(desc, whats, expr14)) {
      desc.erase(whats.position());
   }
   return desc;
}

/* TODO: check for all referenced text pages where no match was found if they
 *       describe a yet unknown programme (e.g. next instance of a weekly series)
 */
void ParseDescPage(TV_SLOT * slot, const T_PG_DATE * pg_date)
{
   bool found = false;
   int first_sub = -1;
   int page = slot->m_ttx_ref;

   if (opt_debug) {
      char buf[100];
      strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", localtime(&slot->m_start_t));
      printf("DESC parse page %03X: search match for %s \"%s\"\n", page, buf, slot->m_title.c_str());
   }

   for (TTX_DB::const_iterator p = ttx_db.first_sub_page(page);
           p != ttx_db.end();
              ttx_db.next_sub_page(page, p))
   {
      int sub = p->first.sub();

      // TODO: multiple sub-pages may belong to same title, but have no date
      //       caution: multiple pages may also have the same title, but describe different instances of a series

      // TODO: bottom of desc pages may contain a 2nd date/time for repeats (e.g. SAT.1: "Whg. Fr 05.05. 05:10-05:35") - note the different BG color!

      if (ParseDescDate(page, sub, slot, pg_date->m_date_off)) {
         int head = ParseDescTitle(page, sub, slot);
         if (first_sub >= 0)
            head = CorrelateDescTitles(page, sub, first_sub, head);
         else
            first_sub = sub;

         int foot = ParseFooterByColor(page, sub);
         int foot2 = ParseFooter(page, sub, head);
         foot = (foot2 < foot) ? foot2 : foot;
         if (opt_debug) printf("DESC page %03X.%04X match found: lines %d-%d\n", page, sub, head, foot);

         if (foot > head) {
            if (!slot->m_desc.empty())
               slot->m_desc += "\n";
            slot->m_desc += ParseDescContent(page, sub, head, foot);

            slot->m_ttx_ref = page;
         }
         found = true;
      }
      else {
         if (opt_debug) printf("DESC page %03X.%04X no match found\n", page, sub);
         if (found)
            break;
      }
   }
   if (!found && opt_debug) printf("DESC page %03X not found\n", page);
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
   smatch whats;
   int lang = -1;
   regex expr3[8];
   regex expr4[8];

   for (TTX_DB::const_iterator p = ttx_db.begin(); p != ttx_db.end(); p++) {
      int page = p->first.page();
      if ( (((page>>4)&0xF) <= 9) && ((page&0xF) <= 9) ) {
         if (p->second->get_lang() != lang) {
            lang = p->second->get_lang();
            wday_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_FULL);
            wday_abbrv_match = GetDateNameRegExp(WDayNames, lang, DATE_NAME_ABBRV);
            mname_match = GetDateNameRegExp(MonthNames, lang, DATE_NAME_ANY);
         }

         char pgn[20];
         sprintf(pgn, "%03X", page);
         string hd = p->second->get_text(0);
         // remove page number and time (both are required)
         string::size_type pgn_pos = str_find_word(hd, pgn);
         if (pgn_pos != string::npos) {
            hd.replace(pgn_pos, 3, 3, ' ');

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
const TTX_CHN_ID::T_CNI_TO_ID_MAP TTX_CHN_ID::Cni2ChannelId[] =
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

const uint16_t TTX_CHN_ID::NiToPdcCni[] =
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

string TTX_CHN_ID::get_ch_id()
{
   // search the most frequently seen CNI value 
   uint16_t cni = 0;
   int max_cnt = -1;
   for (const_iterator p = m_cnis.begin(); p != m_cnis.end(); p++) {
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

void XMLTV::SetChannelName(const char * user_chname, const char * user_chid)
{
   // get channel name from teletext header packets
   m_ch_name = *opt_chname ? string(opt_chname) : ParseChannelName();

   m_ch_id = *opt_chid ? string(opt_chid) : ttx_chn_id.get_ch_id();

   if (m_ch_name.length() == 0) {
      m_ch_name = m_ch_id;
   }
   if (m_ch_name.length() == 0) {
      m_ch_name = "???";
   }
   if (m_ch_id.length() == 0) {
      m_ch_id = m_ch_name;
   }
}

/* ------------------------------------------------------------------------------
 * Parse an XMLTV timestamp (DTD 0.5)
 * - we expect local timezone only (since this is what the ttx grabber generates)
 */
time_t ParseXmltvTimestamp(const char * ts)
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
#if defined(_BSD_SOURCE)
      time_t ts = timegm( &tm_obj );
#else
      time_t ts = mktime( &tm_obj );
      //ts += tm_obj.tm_gmtoff;  // glibc extension (_BSD_SOURCE)
      ts += 60*60 * tm_obj.tm_isdst - timezone;
#endif

      return ts;
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
      title.assign(whats[1]);
      XmlToLatin1(title);
   }
   return title;
}

time_t GetXmltvStopTime(const string& xml, time_t old_ts)
{
   smatch whats;
   static const regex expr1("stop=\"([^\"]*)\"");

   if (regex_search(xml, whats, expr1)) {
      return ParseXmltvTimestamp(&whats[1].first[0]);
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
void XMLTV::ImportXmltvFile(const char * fname)
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
                  chn_id.assign(what[1]);
               } else {
                  fprintf(stderr, "Missing 'id' attribute in '%s' in %s\n", buf, fname);
               }
               tag_data = buf;
               state = STATE_CHN;
            }
            else if (regex_search(buf, what, expr3)) {
               static const regex expr6("start=\"([^\"]*)\".*channel=\"([^\"]*)\"", regex::icase);
               if (regex_search(buf, what, expr6)) {
                  chn_id.assign(what[2]);
                  start_t = ParseXmltvTimestamp(&what[1].first[0]);
                  if (m_merge_chn.find(chn_id) == m_merge_chn.end()) {
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
               m_merge_chn[chn_id] = tag_data;
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
                  m_merge_prog[string(buf) + chn_id] = tag_data;
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
      for (map<string,string>::iterator p = m_merge_chn.begin(); p != m_merge_chn.end(); ) {
         if (ChnProgDef.find(p->first) == ChnProgDef.end()) {
            if (opt_debug) printf("XMLTV input: dropping empty CHANNEL ID %s\n", p->first.c_str());
            m_merge_chn.erase(p++);
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
               old_desc.assign(whats[1]);
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
         delete *it_next_slot;
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
         if (opt_debug) printf("MERGE CHOOSE NEW %ld..%ld\n", new_start, new_stop);
         return 1; // new
      } else {
         // choose data from old source
         if (opt_debug) printf("MERGE CHOOSE OLD %ld..%ld\n", old_start, old_stop);
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
void XMLTV::ExportXmltv(list<TV_SLOT*>& NewSlots)
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
   map<string,string>::iterator p_chn = m_merge_chn.find(m_ch_id);
   if (m_merge_chn.find(m_ch_id) == m_merge_chn.end()) {
      fprintf(fp, "<channel id=\"%s\">\n"
                  "\t<display-name>%s</display-name>\n"
                  "</channel>\n",
                  m_ch_id.c_str(), m_ch_name.c_str());
   }
   for (map<string,string>::iterator p = m_merge_chn.begin(); p != m_merge_chn.end(); p++) {
      if (p->first == m_ch_id) {
         fprintf(fp, "<channel id=\"%s\">\n"
                     "\t<display-name>%s</display-name>\n"
                     "</channel>\n", m_ch_id.c_str(), m_ch_name.c_str());
      }
      else {
         fprintf(fp, "%s", p->second.c_str());
      }
   }

   if (m_merge_chn.find(m_ch_id) != m_merge_chn.end()) {
      // extract respective channel's data from merge input
      map<time_t,string> OldProgHash;
      for (map<string,string>::iterator p = m_merge_prog.begin(); p != m_merge_prog.end(); ) {
         long start_ts;
         int slen;
         if (   (sscanf(p->first.c_str(), "%ld;%n", &start_ts, &slen) >= 1)
             && (p->first.compare(slen, p->first.length() - slen, m_ch_id) == 0) ) {
            OldProgHash[start_ts] = p->second;
            m_merge_prog.erase(p++);
         }
         else {
            p++;
         }
      }
      // sort new programmes by start time
      NewSlots.sort(TV_SLOT_cmp_start());

      // map holding old programmes is already sorted, as start time is used as key
      list<time_t> OldSlotList;
      for (map<time_t,string>::iterator p = OldProgHash.begin(); p != OldProgHash.end(); p++)
         OldSlotList.push_back(p->first);

      // combine both sources (i.e. merge them)
      while (!NewSlots.empty() || !OldSlotList.empty()) {
         switch (MergeNextSlot(NewSlots, OldSlotList, OldProgHash)) {
            case 1:
               assert(!NewSlots.empty());
               ExportTitle(fp, *NewSlots.front(), m_ch_id);
               delete NewSlots.front();
               NewSlots.pop_front();
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
      //for (list<TV_SLOT*>::iterator p = NewSlots.begin(); p != NewSlots.end(); p++)
      //   ExportTitle(fp, *(*p), m_ch_id);
      while (!NewSlots.empty()) {
         ExportTitle(fp, *NewSlots.front(), m_ch_id);

         delete NewSlots.front();
         NewSlots.pop_front();
      }
   }

   // append data for all remaining old channels unchanged
   for (map<string,string>::iterator p = m_merge_prog.begin(); p != m_merge_prog.end(); p++) {
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
      // debug only: dump teletext pages into file (text only)
      DumpTextPages();
   }
   else if (opt_dump == 2) {
      // dump teletext pages into file, including all control chars
      DumpRawTeletext();
   }
   else if (opt_dump == 3) {
      // dump each VBI packet: already done while capturing
   }
   else {
      // parse and export programme data
      // grab new XML data from teletext
      list<TV_SLOT*> NewSlots = ParseAllOvPages();

      // remove programmes beyond the expiration threshold
      if (!opt_verify) {
         FilterExpiredSlots(NewSlots);
      }

      // make sure to never write an empty file
      if (!NewSlots.empty()) {
         XMLTV xmltv;

         xmltv.SetChannelName(opt_chname, opt_chid);

         // read and merge old data from XMLTV file
         if (opt_mergefile != 0) {
            xmltv.ImportXmltvFile(opt_mergefile);
         }

         xmltv.ExportXmltv(NewSlots);
      }
      else {
         // return error code to signal abormal termination
         exit(100);
      }
   }
}

