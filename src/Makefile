# prez Makefile
# 
# Copyright(C) 2014 Sureshkumar Nedunchezhian. All rights reserved.
#
# Copyright (C) 2009 Salvatore Sanfilippo <antirez at gmail dot com>
# This file is released under the BSD license, see the COPYING file
#
# The Makefile composes the final FINAL_CFLAGS and FINAL_LDFLAGS using
# what is needed for prez plus the standard CFLAGS and LDFLAGS passed.
# However when building the dependencies (Jemalloc ...)
# CFLAGS and LDFLAGS are propagated to the dependencies, so to pass
# flags only to be used when compiling / linking prez itself PREZ_CFLAGS
# and PREZ_LDFLAGS are used instead (this is the case of 'make gcov').
#
# Dependencies are stored in the Makefile.dep file. To rebuild this file
# Just use 'make dep', but this is only needed by developers.

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
OPTIMIZATION?=-O2
DEPENDENCY_TARGETS=

# Default settings
STD=-std=c99 -pedantic
WARN=-Wall
OPT=$(OPTIMIZATION)

PREFIX?=/usr/local
INSTALL_BIN=$(PREFIX)/bin
INSTALL=install

# Default allocator
ifeq ($(uname_S),Linux)
	MALLOC=jemalloc
else
	MALLOC=libc
endif

# Backwards compatibility for selecting an allocator
ifeq ($(USE_TCMALLOC),yes)
	MALLOC=tcmalloc
endif

ifeq ($(USE_TCMALLOC_MINIMAL),yes)
	MALLOC=tcmalloc_minimal
endif

ifeq ($(USE_JEMALLOC),yes)
	MALLOC=jemalloc
endif

# Override default settings if possible
-include .make-settings

FINAL_CFLAGS=$(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS) $(PREZ_CFLAGS)
FINAL_LDFLAGS=$(LDFLAGS) $(PREZ_LDFLAGS) $(DEBUG)
FINAL_LIBS=-lm
DEBUG=-g -ggdb

ifeq ($(uname_S),SunOS)
	# SunOS
	INSTALL=cp -pf
	FINAL_CFLAGS+= -D__EXTENSIONS__ -D_XPG6
	FINAL_LIBS+= -ldl -lnsl -lsocket -lpthread
else
ifeq ($(uname_S),Darwin)
	# Darwin (nothing to do)
else
	# All the other OSes (notably Linux)
	FINAL_LDFLAGS+= -rdynamic
	FINAL_LIBS+= -pthread
endif
endif

# Include paths to dependencies
FINAL_CFLAGS+=

ifeq ($(MALLOC),tcmalloc)
	FINAL_CFLAGS+= -DUSE_TCMALLOC
	FINAL_LIBS+= -ltcmalloc
endif

ifeq ($(MALLOC),tcmalloc_minimal)
	FINAL_CFLAGS+= -DUSE_TCMALLOC
	FINAL_LIBS+= -ltcmalloc_minimal
endif

ifeq ($(MALLOC),jemalloc)
	DEPENDENCY_TARGETS+= jemalloc
	FINAL_CFLAGS+= -DUSE_JEMALLOC -I../deps/jemalloc/include
	FINAL_LIBS+= ../deps/jemalloc/lib/libjemalloc.a -ldl
endif

PREZ_CC=$(QUIET_CC)$(CC) $(FINAL_CFLAGS)
PREZ_LD=$(QUIET_LINK)$(CC) $(FINAL_LDFLAGS)
PREZ_INSTALL=$(QUIET_INSTALL)$(INSTALL)

CCCOLOR="\033[34m"
LINKCOLOR="\033[34;1m"
SRCCOLOR="\033[33m"
BINCOLOR="\033[37;1m"
MAKECOLOR="\033[32;1m"
ENDCOLOR="\033[0m"

ifndef V
QUIET_CC = @printf '    %b %b\n' $(CCCOLOR)CC$(ENDCOLOR) $(SRCCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_LINK = @printf '    %b %b\n' $(LINKCOLOR)LINK$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR) 1>&2;
QUIET_INSTALL = @printf '    %b %b\n' $(LINKCOLOR)INSTALL$(ENDCOLOR) $(BINCOLOR)$@$(ENDCOLOR) 1>&2;
endif

PREZ_SERVER_NAME=prez-server
PREZ_SERVER_OBJ=adlist.o ae.o anet.o dict.o prez.o sds.o zmalloc.o lzf_c.o lzf_d.o pqsort.o zipmap.o sha1.o ziplist.o util.o config.o debug.o endianconv.o crc64.o networking.o object.o cluster.o log.o

all: $(PREZ_SERVER_NAME)
	@echo ""

.PHONY: all

# Deps (use make dep to generate this)
include Makefile.dep

dep:
	$(PREZ_CC) -MM *.c > Makefile.dep

.PHONY: dep

persist-settings: distclean
	echo STD=$(STD) >> .make-settings
	echo WARN=$(WARN) >> .make-settings
	echo OPT=$(OPT) >> .make-settings
	echo MALLOC=$(MALLOC) >> .make-settings
	echo CFLAGS=$(CFLAGS) >> .make-settings
	echo LDFLAGS=$(LDFLAGS) >> .make-settings
	echo PREZ_CFLAGS=$(PREZ_CFLAGS) >> .make-settings
	echo PREZ_LDFLAGS=$(PREZ_LDFLAGS) >> .make-settings
	echo PREV_FINAL_CFLAGS=$(FINAL_CFLAGS) >> .make-settings
	echo PREV_FINAL_LDFLAGS=$(FINAL_LDFLAGS) >> .make-settings
	-(cd ../deps && $(MAKE) $(DEPENDENCY_TARGETS))

.PHONY: persist-settings

# Prerequisites target
.make-prerequisites:
	@touch $@

# Clean everything, persist settings and build dependencies if anything changed
ifneq ($(strip $(PREV_FINAL_CFLAGS)), $(strip $(FINAL_CFLAGS)))
.make-prerequisites: persist-settings
endif

ifneq ($(strip $(PREV_FINAL_LDFLAGS)), $(strip $(FINAL_LDFLAGS)))
.make-prerequisites: persist-settings
endif

# prez-server
$(PREZ_SERVER_NAME): $(PREZ_SERVER_OBJ)
	$(PREZ_LD) -o $@ $^ $(FINAL_LIBS)

# Because the jemalloc.h header is generated as a part of the jemalloc build,
# building it should complete before building any other object. Instead of
# depending on a single artifact, build all dependencies first.
%.o: %.c .make-prerequisites
	$(PREZ_CC) -c $<

clean:
	rm -rf $(PREZ_SERVER_NAME) *.o *.gcda *.gcno *.gcov prez.info lcov-html

.PHONY: clean

distclean: clean
	-(cd ../deps && $(MAKE) distclean)
	-(rm -f .make-*)

.PHONY: distclean

lcov:
	$(MAKE) gcov
	@(set -e; cd ..; ./runtest --clients 1)
	@geninfo -o prez.info .
	@genhtml --legend -o lcov-html prez.info

.PHONY: lcov

32bit:
	@echo ""
	@echo "WARNING: if it fails under Linux you probably need to install libc6-dev-i386"
	@echo ""
	$(MAKE) CFLAGS="-m32" LDFLAGS="-m32"

gcov:
	$(MAKE) PREZ_CFLAGS="-fprofile-arcs -ftest-coverage -DCOVERAGE_TEST" PREZ_LDFLAGS="-fprofile-arcs -ftest-coverage"

noopt:
	$(MAKE) OPTIMIZATION="-O0"

valgrind:
	$(MAKE) OPTIMIZATION="-O0" MALLOC="libc"

# #Fixme: Support this later
#src/help.h:
#    @../utils/generate-command-help.rb > help.h

install: all
	@mkdir -p $(INSTALL_BIN)
	$(PREZ_INSTALL) $(PREZ_SERVER_NAME) $(INSTALL_BIN)
