/*
 * Teletext EPG grabber: C external interfaces
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
 * $Id: ttx_cif.cc,v 1.2 2010/05/06 17:57:53 tom Exp $
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <assert.h>

#include <string>
#include <map>
#include <vector>

#include <boost/regex.h>
#include <boost/regex.hpp>

using namespace std;
using namespace boost;

#include "epgctl/epgversion.h"

#include "ttx_db.h"
#include "ttx_scrape.h"
#include "ttx_xmltv.h"
#include "ttx_cif.h"

void ttx_db_init( void )
{
   ttx_db.flush();
   ttx_chn_id.flush();
}

void ttx_db_add_cni(unsigned cni)
{
   ttx_chn_id.add_cni(cni);
}

bool ttx_db_add_pkg( int page, int ctrl, int pkgno, const uint8_t * p_data, time_t ts )
{
   static int cur_page = -1;
   static int cur_sub = -1;

   if (page < 0x100)
      page += 0x800;

   if (ttx_db.page_acceptable(page))
   {
      if (pkgno == 0)
      {
         cur_page = page;
         cur_sub = ctrl & 0x3F7F;
         ttx_db.add_page(cur_page, cur_sub, ctrl, p_data, ts);
      }
      else
      {
         ttx_db.add_page_data(cur_page, cur_sub, pkgno, p_data);
      }
   }
}

int ttx_db_parse( int pg_start, int pg_end, int expire_min,
                  const char * p_xml_in, const char * p_xml_out,
                  const char * p_ch_name, const char * p_ch_id )
{
   int result = 0;

   // parse and export programme data
   // grab new XML data from teletext
   vector<OV_PAGE*> ov_pages = ParseAllOvPages(pg_start, pg_end);

   // retrieve descriptions from references teletext pages
   list<TV_SLOT> NewSlots = OV_PAGE::get_ov_slots(ov_pages);

   // remove programmes beyond the expiration threshold
   FilterExpiredSlots(NewSlots, expire_min);

   // make sure to never write an empty file
   if (!NewSlots.empty()) {
      XMLTV xmltv;

      xmltv.SetChannelName(p_ch_name, p_ch_id);

      xmltv.SetExpireTime(expire_min);

      // read and merge old data from XMLTV file
      if (p_xml_in != 0) {
         xmltv.ImportXmltvFile(p_xml_in);
      }

      xmltv.ExportXmltv(NewSlots, p_xml_out,
                        "nxtvepg/" EPG_VERSION_STR, NXTVEPG_URL);
   }
   else {
      // return error code to signal abormal termination
      result = 100;
   }

   for (unsigned idx = 0; idx < ov_pages.size(); idx++) {
      delete ov_pages[idx];
   }

   return result;
}

void ttx_db_dump(const char * p_name, int pg_start, int pg_end)
{
   DumpRawTeletext(p_name, pg_start, pg_end);
}

