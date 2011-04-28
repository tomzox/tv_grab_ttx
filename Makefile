#
# This program captures teletext from a VBI device, scrapes TV programme
# schedules and descriptions from captured pages and exports them in
# XMLTV format (DTD version 0.5)
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Copyright 2006-2011 by Tom Zoerner (tomzo at users.sf.net)
#
# $Id: Makefile,v 1.2 2011/01/09 18:41:34 tom Exp $
#

all: build_dir tv_grab_ttx tv_grab_ttx.1

MODS = ttx_main ttx_scrape ttx_ov_fmt ttx_feat ttx_pg_ref ttx_date \
       ttx_xmltv ttx_db ttx_acq ttx_util

CFLAGS  = -Wall -pipe -O3 -ggdb -MMD -DUSE_LIBZVBI -Isrc
LDFLAGS =

# use self-compiled version of libzvbi with debug symbols
#LDFLAGS = -L/home/tom/work/zvbi/cvs/vbi/src/.libs -Wl,-rpath=/home/tom/work/zvbi/cvs/vbi/src/.libs

SRC_DIR = src
BUILD_DIR = obj
OBJS = $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(MODS)))

LIBS = -lzvbi -lboost_regex

#CFLAGS += -ftest-coverage -fprofile-arcs
#LDFLAGS += -ftest-coverage -fprofile-arcs
#CFLAGS += -pg -O1
#LDFLAGS += -pg

.PHONY: build_dir
build_dir:
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cc
	g++ $(CFLAGS) -c -o $@ $<

tv_grab_ttx: $(OBJS)
	@rm -f $@
	g++ $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

tv_grab_ttx.1: tv_grab_ttx.pod
	pod2man -date " " -center "Teletext EPG grabber" -section "1" \
	        -release "tv_grab_ttx (C) 2006-2011 Th. Zoerner" \
	        tv_grab_ttx.pod > tv_grab_ttx.1; \

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f tv_grab_ttx tv_grab_ttx.1 *.o core core.* vgcore.*

# include automatically generated dependency list
-include $(BUILD_DIR)/*.d
