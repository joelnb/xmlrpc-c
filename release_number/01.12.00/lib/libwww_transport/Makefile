ifeq ($(SRCDIR),)
  updir = $(shell echo $(dir $(1)) | sed 's/.$$//')
  LIBDIR := $(call updir,$(CURDIR))
  SRCDIR := $(call updir,$(LIBDIR))
  BLDDIR := $(SRCDIR)
endif
SUBDIR := lib/libwww_transport

include $(BLDDIR)/Makefile.config

default: all

.PHONY: all
all: xmlrpc_libwww_transport.o xmlrpc_libwww_transport.osh

# Rules for the above dependencies are in Makefile.common,
# courtesy of TARGET_MODS.

TARGET_MODS = xmlrpc_libwww_transport

OMIT_LIBWWW_TRANSPORT_RULE=Y

include $(SRCDIR)/Makefile.common

LIBWWW_INCLUDES := $(shell libwww-config --cflags)

CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_PERSONAL) $(CADD)

INCLUDES = \
  -I$(BLDDIR) \
  -I$(BLDDIR)/include \
  -I$(SRCDIR)/include \
  -I$(SRCDIR)/lib/util/include \
  $(LIBWWW_INCLUDES)

.PHONY: clean
clean: clean-common

.PHONY: distclean
distclean: clean distclean-common

.PHONY: tags
tags: TAGS

.PHONY: distdir
distdir:

.PHONY: install
install:

.PHONY: dep
dep: dep-common

include Makefile.depend

# Need this dependency for those who don't use Makefile.depend.
# Without it, version.h doesn't get created.
xmlrpc_libwww_transport.o xmlrpc_libwww_transport.osh: version.h
