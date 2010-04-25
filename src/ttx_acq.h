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
 * $Id: ttx_acq.h,v 1.1 2010/04/25 14:18:10 tom Exp $
 */
#if !defined (__TTX_ACQ_H)
#define __TTX_ACQ_H

void ReadVbi(const char * p_infile, const char * p_dumpfile,
             const char * p_dev_name, int dvb_pid,
             bool do_verbose, bool do_dump, int cap_duration);
void ImportRawDump(const char * p_name);
void DumpTextPages(const char * p_name);
void DumpRawTeletext(const char * p_name, int pg_start, int pg_end);

#endif // __TTX_ACQ_H
