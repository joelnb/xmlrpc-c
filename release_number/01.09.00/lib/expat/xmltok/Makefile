ifeq ($(SRCDIR),)
  updir = $(shell echo $(dir $(1)) | sed 's/.$$//')
  EXPATDIR := $(call updir,$(CURDIR))
  LIBDIR := $(call updir,$(EXPATDIR))
  SRCDIR := $(call updir,$(LIBDIR))
  BLDDIR := $(SRCDIR)
endif
SUBDIR := lib/expat/xmltok

include $(BLDDIR)/Makefile.config

# I can't figure out what XML_BYTE_ORDER is, but it doesn't look like the
# code has ever defined it.  That means it's treated like 0 in #if.  Since
# we started using the Gcc -Wundef option, that generates a warning, so
# se set it explicitly to 0 here.

CFLAGS = $(CFLAGS_COMMON) -DXML_BYTE_ORDER=0 $(CFLAGS_PERSONAL) $(CADD)

LIBLDFLAGS = $(LDFLAGS_VERSINFO) $(RPATH) $(LADD)

# -I. is necessary when blddir != srcdir
INCLUDES = -I. -I$(BLDDIR) -I$(SRCDIR)/lib/util/include

default: all


OMIT_XMLTOK_LIB_RULE = Y

include $(SRCDIR)/Makefile.common

.PHONY: all
all: libxmlrpc_xmltok.la

.PHONY: clean
clean: clean-common
	rm -f nametab.h

.PHONY: distclean
distclean: clean distclean-common

.PHONY: tags
tags: TAGS

.PHONY: distdir
distdir:

.PHONY: install
install: install-common

.PHONY: dep
dep: dep-common

LTLIBRARIES_TO_INSTALL = libxmlrpc_xmltok.la

LIBXMLRPC_XMLTOK_OBJS = xmltok.lo xmlrole.lo

libxmlrpc_xmltok.la: $(LIBXMLRPC_XMLTOK_OBJS)
	$(LIBTOOL) --mode=link $(CCLD) -o $@ $(LIBLDFLAGS) $^

$(LIBXMLRPC_XMLTOK_OBJS):%.lo:%.c
	$(LIBTOOL) --mode=compile $(CC) -c $(INCLUDES) $(LIBXML_INCLUDES) \
	  $(CFLAGS) $<

GENNMTAB = ../gennmtab/gennmtab

nametab.h: $(GENNMTAB)
	rm -f $@
	$(GENNMTAB) >$@ || (rm -f $@ || false)

$(GENNMTAB):
	$(MAKE) -C $(dir $@) $(notdir $@)

xmltok.lo: nametab.h

include Makefile.depend
