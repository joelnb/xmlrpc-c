ifeq ($(SRCDIR)x,x)
SRCDIR = $(CURDIR)/..
endif
SUBDIR = lib

include $(SRCDIR)/Makefile.config

# Build up SUBDIRS:
SUBDIRS = 
SUBDIRS += util libutil
ifeq ($(ENABLE_ABYSS_SERVER),yes)
  SUBDIRS += abyss
endif
ifeq ($(MUST_BUILD_WININET_CLIENT),yes)
  SUBDIRS += wininet_transport
endif
ifeq ($(MUST_BUILD_CURL_CLIENT),yes)
  SUBDIRS += curl_transport
endif
ifeq ($(MUST_BUILD_LIBWWW_CLIENT),yes)
  SUBDIRS += libwww_transport
endif
ifneq ($(ENABLE_LIBXML2),yes)
  SUBDIRS += expat
endif

default: all

.PHONY: all clean distclean tags distdir intall check dep

all: $(SUBDIRS:%=%/all)

clean: $(SUBDIRS:%=%/clean) clean-common

distclean: $(SUBDIRS:%=%/distclean) distclean-common

tags: $(SUBDIRS:%=%/tags) TAGS

DISTFILES = 

distdir: distdir-common

install: $(SUBDIRS:%=%/install)

check:

dep: $(SUBDIRS:%=%/dep)

include $(SRCDIR)/Makefile.common



