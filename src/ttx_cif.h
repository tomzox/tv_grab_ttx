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
 * $Id: ttx_cif.h,v 1.2 2010/05/06 17:57:53 tom Exp $
 */
#if !defined (__TTX_IF_H)
#define __TTX_IF_H

#if defined (__cplusplus)
extern "C" {
#endif

void ttx_db_init( void );
void ttx_db_add_cni(unsigned cni);
bool ttx_db_add_pkg( int page, int ctrl, int pkgno, const uint8_t * p_data, time_t ts );
int ttx_db_parse( int pg_start, int pg_end, int expire_min,
                  const char * p_xml_in, const char * p_xml_out,
                  const char * p_ch_name, const char * p_ch_id );
void ttx_db_dump(const char * p_name, int pg_start, int pg_end);

#if defined (__cplusplus)
}
#endif

#endif // __TTX_IF_H