/*
 * Teletext EPG grabber: Feature keyword parser
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
 * $Id: ttx_feat.h,v 1.1 2010/04/25 14:18:10 tom Exp $
 */
#if !defined (__TTX_FEAT_H)
#define __TTX_FEAT_H

class TV_FEAT
{
public:
   TV_FEAT();
   void set_tip(bool v) { if (v) m_is_tip = true; };
   void ParseTrailingFeat(string& title);
   static void RemoveTrailingFeat(string& title);

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

#endif // __TTX_FEAT_H
