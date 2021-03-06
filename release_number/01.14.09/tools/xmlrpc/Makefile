ifeq ($(SRCDIR),)
  updir = $(shell echo $(dir $(1)) | sed 's/.$$//')
  TOOLSDIR := $(call updir,$(CURDIR))
  SRCDIR := $(call updir,$(TOOLSDIR))
  BLDDIR := $(SRCDIR)
endif
SUBDIR := tools/xmlrpc

default: all

include $(BLDDIR)/Makefile.config

PROGRAMS_TO_INSTALL = xmlrpc

include $(SRCDIR)/tools/Makefile.common

INCLUDES = \
  -I$(BLDDIR) \
  -I$(BLDDIR)/include \
  -I$(SRCDIR)/include \
  -I$(SRCDIR)/lib/util/include

CFLAGS = $(CFLAGS_COMMON) $(INCLUDES) $(CFLAGS_PERSONAL) $(CADD)

LDFLAGS = $(LADD)

all: xmlrpc

UTIL_OBJS = \
  casprintf.o \
  cmdline_parser.o \
  getoptx.o \
  stripcaseeq.o \
  string_parser.o \

UTILS = $(UTIL_OBJS:%=$(UTIL_DIR)/%)

# Can we just use $(LIBS) in the link?

LIBS = $(LIBXMLRPC_CLIENT) $(LIBXMLRPC) $(LIBXMLRPC_XML) $(LIBXMLRPC_UTIL)

XMLRPC_OBJS = xmlrpc.o dumpvalue.o

xmlrpc: $(XMLRPC_OBJS) $(LIBS) $(UTILS)
	$(CCLD) -o $@ $(LDFLAGS) $(XMLRPC_OBJS) $(UTILS) $(CLIENT_LDLIBS)

%.o:%.c $(BLDDIR)/include/xmlrpc-c/config.h
	$(CC) -c $(CFLAGS) $<

# This Makefile.config dependency makes sure the symlinks get built before
# this make file is used for anything.

$(BLDDIR)/Makefile.config: srcdir

include Makefile.depend

.PHONY: dep
dep: dep-common

.PHONY: clean
clean: clean-common
	rm -f xmlrpc

.PHONY: distclean
distclean: clean distclean-common
