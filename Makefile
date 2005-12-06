# Makefile for pcmciautils
#
# Copyright (C) 2005      Dominik Brodowski <linux@dominikbrodowski.net>
#
# Based largely on the Makefile for udev by:
#
# Copyright (C) 2003,2004 Greg Kroah-Hartman <greg@kroah.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
#

# Set the following to `true' to log the debug
# and make a unstripped, unoptimized  binary.
# Leave this set to `false' for production use.
DEBUG = false

PCCARDCTL =			pccardctl
PCMCIA_CHECK_BROKEN_CIS =	pcmcia-check-broken-cis
PCMCIA_MODALIAS =		pcmcia-modalias
PCMCIA_SOCKET_STARTUP =		pcmcia-socket-startup

VERSION =	002
#INSTALL_DIR =	/usr/local/sbin
RELEASE_NAME =	pcmciautils-$(VERSION)

#DESTDIR =

KERNEL_DIR = /lib/modules/${shell uname -r}/build

# override this to make udev look in a different location for it's config files
prefix =
exec_prefix =	${prefix}
etcdir =	${prefix}/etc
sbindir =	${exec_prefix}/sbin
srcdir = .

INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL}
INSTALL_DATA  = ${INSTALL} -m 644
INSTALL_SCRIPT = ${INSTALL_PROGRAM}

# place to put our hotplug scripts nodes
hotplugdir =	${prefix}/etc/hotplug

# place where PCMICIA config is put to
pcmciaconfdir =	${prefix}${etcdir}/pcmcia

# make the build silent. Set this to something else to make it noisy again.
V=false

# set up PWD so that older versions of make will work with our build.
PWD = $(shell pwd)

# If you are running a cross compiler, you may want to set this
# to something more interesting, like "arm-linux-".  If you want
# to compile vs uClibc, that can be done here as well.
CROSS = #/usr/i386-linux-uclibc/usr/bin/i386-uclibc-
CC = $(CROSS)gcc
LD = $(CROSS)gcc
AR = $(CROSS)ar
STRIP = $(CROSS)strip
RANLIB = $(CROSS)ranlib
HOSTCC = gcc

export CROSS CC AR STRIP RANLIB CFLAGS LDFLAGS LIB_OBJS ARCH_LIB_OBJS CRT0

# code taken from uClibc to determine the current arch
ARCH := ${shell $(CC) -dumpmachine | sed -e s'/-.*//' -e 's/i.86/i386/' -e 's/sparc.*/sparc/' \
	-e 's/arm.*/arm/g' -e 's/m68k.*/m68k/' -e 's/powerpc/ppc/g'}

# code taken from uClibc to determine the gcc include dir
GCCINCDIR := ${shell LC_ALL=C $(CC) -print-search-dirs | sed -ne "s/install: \(.*\)/\1include/gp"}

# code taken from uClibc to determine the libgcc.a filename
GCC_LIB := $(shell $(CC) -print-libgcc-file-name )

# use '-Os' optimization if available, else use -O2
OPTIMIZATION := ${shell if $(CC) -Os -S -o /dev/null -xc /dev/null >/dev/null 2>&1; \
		then echo "-Os"; else echo "-O2" ; fi}

# check if compiler option is supported
cc-supports = ${shell if $(CC) ${1} -S -o /dev/null -xc /dev/null > /dev/null 2>&1; then echo "$(1)"; fi;}

WARNINGS := -Wall -fno-builtin -Wchar-subscripts -Wpointer-arith -Wsign-compare
WARNINGS += $(call cc-supports,-Wno-pointer-sign)
WARNINGS += $(call cc-supports,-Wdeclaration-after-statement)
WARNINGS += -Wshadow

CFLAGS := -pipe

HEADERS = \
	src/cistpl.h	\
	src/startup.h	\
	src/yacc_config.h 


OBJS = \
	src/lex_config.l		\
	src/pccardctl.c			\
	src/pcmcia-check-broken-cis.c	\
	src/pcmcia-modalias.c		\
	src/read-cis.c			\
	src/startup.c			\
	src/startup.h			\
	src/yacc_config.h		\
	src/yacc_config.y

CFLAGS +=	-I$(PWD)/src

#LIBC =
CFLAGS += $(WARNINGS) -I$(GCCINCDIR)
LIB_OBJS = -lc -lsysfs
#LDFLAGS =

ifeq ($(strip $(V)),false)
	QUIET=@$(PWD)/build/ccdv
	HOST_PROGS=build/ccdv
else
	QUIET=
	HOST_PROGS=
endif

# if DEBUG is enabled, then we do not strip or optimize
ifeq ($(strip $(DEBUG)),true)
	CFLAGS  += -O1 -g -DDEBUG -D_GNU_SOURCE
	LDFLAGS += -Wl,-warn-common
	STRIPCMD = /bin/true -Since_we_are_debugging
else
	CFLAGS  += $(OPTIMIZATION) -fomit-frame-pointer -D_GNU_SOURCE
	LDFLAGS += -s -Wl,-warn-common
	STRIPCMD = $(STRIP) -s --remove-section=.note --remove-section=.comment
endif
                                                

all: ccdv $(PCCARDCTL) $(PCMCIA_CHECK_BROKEN_CIS) $(PCMCIA_MODALIAS) $(PCMCIA_SOCKET_STARTUP)

ccdv:
	@echo "Building ccdv"
	@$(HOSTCC) -O1 build/ccdv.c -o build/ccdv

.c.o:
	$(QUIET) $(CC) $(CFLAGS) -c -o $@ $<
	
$(PCCARDCTL): $(LIBC) src/$(PCCARDCTL).o $(OBJS) $(HEADERS)
	$(QUIET) $(LD) $(LDFLAGS) -o $@ $(CRT0) src/$(PCCARDCTL).o $(LIB_OBJS) $(ARCH_LIB_OBJS)
	$(QUIET) $(STRIPCMD) $@

$(PCMCIA_CHECK_BROKEN_CIS): $(LIBC) src/$(PCMCIA_CHECK_BROKEN_CIS).o src/read-cis.o $(OBJS) $(HEADERS)
	$(QUIET) $(LD) $(LDFLAGS) -o $@ $(CRT0) src/$(PCMCIA_CHECK_BROKEN_CIS).o src/read-cis.o $(LIB_OBJS) $(ARCH_LIB_OBJS)
	$(QUIET) $(STRIPCMD) $@

$(PCMCIA_MODALIAS): $(LIBC) src/$(PCMCIA_MODALIAS).o $(OBJS) $(HEADERS)
	$(QUIET) $(LD) $(LDFLAGS) -o $@ $(CRT0) src/$(PCMCIA_MODALIAS).o $(LIB_OBJS) $(ARCH_LIB_OBJS)
	$(QUIET) $(STRIPCMD) $@

$(PCMCIA_SOCKET_STARTUP): $(LIBC) src/startup.o src/yacc_config.o src/lex_config.o $(OBJS) $(HEADERS)
	$(QUIET) $(LD) $(LDFLAGS) -o $@ $(CRT0) src/startup.o src/yacc_config.o src/lex_config.o $(LIB_OBJS) $(ARCH_LIB_OBJS)
	$(QUIET) $(STRIPCMD) $@

yacc_config.o lex_config.o: %.o: %.c
	$(CC) -c -MD -O -pipe $(CPPFLAGS) $<

clean:
	-find . \( -not -type d \) -and \( -name '*~' -o -name '*.[oas]' \) -type f -print \
	 | xargs rm -f 
	-rm -f $(PCCARDCTL) $(PCMCIA_CHECK_BROKEN_CIS) $(PCMCIA_MODALIAS) $(PCMCIA_SOCKET_STARTUP)
	-rm -f src/yacc_config.c src/yacc_config.d src/lex_config.c src/lex_config.d
	-rm -f build/ccdv

install-hotplug:
	$(INSTALL) -d $(DESTDIR)$(hotplugdir)
	$(INSTALL_PROGRAM) -D hotplug/pcmcia.agent $(DESTDIR)$(hotplugdir)/pcmcia.agent
	$(INSTALL_PROGRAM) -D hotplug/pcmcia.rc $(DESTDIR)$(hotplugdir)/pcmcia.rc
	$(INSTALL_PROGRAM) -D hotplug/pcmcia_socket.agent $(DESTDIR)$(hotplugdir)/pcmcia_socket.agent
	$(INSTALL_PROGRAM) -D hotplug/pcmcia_socket.rc $(DESTDIR)$(hotplugdir)/pcmcia_socket.rc

uninstall-hotplug:
	- rm -f $(hotplugdir)/pcmcia.agent $(hotplugdir)/pcmcia.rc
	- rm -f $(hotplugdir)/pcmcia_socket.agent $(hotplugdir)/pcmcia_socket.rc

install-tools:
	$(INSTALL) -d $(DESTDIR)$(sbindir)
	$(INSTALL_PROGRAM) -D $(PCMCIA_MODALIAS) $(DESTDIR)$(sbindir)/$(PCMCIA_MODALIAS)
	$(INSTALL_PROGRAM) -D $(PCMCIA_SOCKET_STARTUP) $(DESTDIR)$(sbindir)/$(PCMCIA_SOCKET_STARTUP)
	$(INSTALL_PROGRAM) -D $(PCCARDCTL) $(DESTDIR)$(sbindir)/$(PCCARDCTL)
	$(INSTALL_PROGRAM) -D $(PCMCIA_CHECK_BROKEN_CIS) $(DESTDIR)$(sbindir)/$(PCMCIA_CHECK_BROKEN_CIS)

uninstall-tools:
	- rm -f $(sbindir)/$(PCMCIA_MODALIAS)
	- rm -f $(sbindir)/$(PCMCIA_SOCKET_STARTUP)
	- rm -f $(sbindir)/$(PCCARDCTL)
	- rm -f $(sbindir)/$(PCMCIA_CHECK_BROKEN_CIS)

install-config:
	$(INSTALL) -d $(DESTDIR)$(pcmciaconfdir)
	$(INSTALL_DATA)  -D config/config.opts $(DESTDIR)$(pcmciaconfdir)/config.opts

uninstall-config:
#	- rm -f $(pcmciaconfdir)/config.opts
	
install: install-tools install-hotplug install-config

uninstall: uninstall-tools uninstall-hotplug uninstall-config
