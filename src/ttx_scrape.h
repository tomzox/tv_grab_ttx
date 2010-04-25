/*
 * Teletext EPG grabber: EPG data scraping main class
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
 * $Id: ttx_scrape.h,v 1.1 2010/04/25 14:18:10 tom Exp $
 */
#if !defined (__TTX_SCRAPE_H)
#define __TTX_SCRAPE_H

#include "ttx_feat.h"
#include "ttx_pg_ref.h"
#include "ttx_ov_fmt.h"
#include "ttx_date.h"

class T_VPS_TIME
{
public:
   string       m_vps_time;
   bool         m_new_vps_time;
   string       m_vps_date;
   bool         m_new_vps_date;
   int          m_vps_cni;
};

class OV_SLOT;
class TV_SLOT;

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
   void extract_tv();
   static list<TV_SLOT> get_ov_slots(vector<OV_PAGE*> ov_pages);
   static T_TRAIL_REF_FMT detect_ov_ttx_ref_fmt(const vector<OV_PAGE*>& ov_pages);
private:
   friend class TV_SLOT;
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
   friend class TV_SLOT;
   time_t       m_start_t;
   time_t       m_stop_t;
   int          m_hour;
   int          m_min;
   int          m_end_hour;
   int          m_end_min;
   int          m_date_wrap;
   string       m_vps_time;
   string       m_vps_date;
   int          m_ttx_ref;
   bool         m_is_tip;
   vector<string> m_ov_title;

   string       m_ext_title;
   string       m_ext_subtitle;
   string       m_ext_desc;
   TV_FEAT      m_ext_feat;
public:
   void add_title(string ctrl);
   void parse_ttx_ref(const T_TRAIL_REF_FMT& fmt);
   void detect_ttx_ref_fmt(vector<T_TRAIL_REF_FMT>& fmt_list);
   void parse_feature_flags();
   void parse_ov_title();
   void parse_desc_page(const T_PG_DATE * pg_date);
   int parse_desc_title(int page, int sub);
   void merge_desc(const string& desc);
   time_t convert_start_t(const T_PG_DATE * pgdate, int date_off) const;
   bool is_same_prog(const OV_SLOT& v) const;
};

class TV_SLOT
{
public:
   TV_SLOT(OV_PAGE* ov_page, int slot_idx)
      : mp_ov_page(ov_page)
      , m_slot_idx(slot_idx) {
   }
   time_t get_start_t() const { return mp_ov_page->m_slots[m_slot_idx]->m_start_t; }
   time_t get_stop_t() const { return mp_ov_page->m_slots[m_slot_idx]->m_stop_t; }
   const string& get_title() const { return mp_ov_page->m_slots[m_slot_idx]->m_ext_title; }
   const string& get_subtitle() const { return mp_ov_page->m_slots[m_slot_idx]->m_ext_subtitle; }
   const string& get_desc() const { return mp_ov_page->m_slots[m_slot_idx]->m_ext_desc; }
   const string& get_vps_time() const { return mp_ov_page->m_slots[m_slot_idx]->m_vps_time; }
   const string& get_vps_date() const { return mp_ov_page->m_slots[m_slot_idx]->m_vps_date; }
   const TV_FEAT& get_feat() const { return mp_ov_page->m_slots[m_slot_idx]->m_ext_feat; }
   int get_ttx_ref() const { return mp_ov_page->m_slots[m_slot_idx]->m_ttx_ref; }
   void merge_desc(const string& desc) { mp_ov_page->m_slots[m_slot_idx]->merge_desc(desc); }
private:
   OV_PAGE *    mp_ov_page;
   int          m_slot_idx;
};

vector<OV_PAGE*> ParseAllOvPages(int ov_start, int ov_end);
void FilterExpiredSlots(list<TV_SLOT>& Slots, int expire_min);

#endif // __TTX_SCRAPE_H
