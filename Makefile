#
# Makefile for compilation on Linux or BSD
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
# Copyright 2006-2011,2020-2021 by T. Zoerner (tomzo at users.sf.net)
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

LIBS = -lzvbi

#CFLAGS += -ftest-coverage -fprofile-arcs
#LDFLAGS += -ftest-coverage -fprofile-arcs
#CFLAGS += -pg -O1
#LDFLAGS += -pg

ROOT    =
prefix  = /usr/local
exec_prefix = ${prefix}
bindir  = $(ROOT)${exec_prefix}/bin
mandir  = $(ROOT)${prefix}/man/man1

.PHONY: test
test: all
	./verify.sh

.PHONY: install
install: all
	test -d $(bindir) || install -d $(bindir)
	test -d $(mandir) || install -d $(mandir)
	install -c -m 0755 tv_grab_ttx $(bindir)
	install -c -m 0644 tv_grab_ttx.1 $(mandir)

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
	        -release "tv_grab_ttx (C) 2006-2011,2020-2021 T. Zoerner" \
	        tv_grab_ttx.pod > tv_grab_ttx.1; \

tune_dvb: util/tune_dvb.c util/scan_pat_pmt.c util/scan_descriptors.h
	gcc -Wall -O -g $(LDFLAGS) -o $@ util/tune_dvb.c util/scan_pat_pmt.c $(LIBS)

.PHONY: ctags
ctags:
	ctags -R src

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f tv_grab_ttx tune_dvb tv_grab_ttx.1 tv_grab_ttx.exe
	rm -f tags core core.* vgcore.*
	rm -rf deb

# include automatically generated dependency list
-include $(BUILD_DIR)/*.d
