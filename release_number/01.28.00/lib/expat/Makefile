ifeq ($(SRCDIR),)
  updir = $(shell echo $(dir $(1)) | sed 's/.$$//')
  LIBDIR := $(call updir,$(CURDIR))
  SRCDIR := $(call updir,$(LIBDIR))
  BLDDIR := $(SRCDIR)
endif
SUBDIR := lib/expat

include $(BLDDIR)/config.mk

# Build up SUBDIRS:
SUBDIRS = gennmtab xmlparse xmltok

default: all

.PHONY: all clean distclean tags distdir intall check dep

all: $(SUBDIRS:%=%/all)

# Extra dependencies to make parallel make work in spite of all the submakes
# (See top level make file for details)
xmlparse/all: gennmtab/all xmltok/all
xmltok/all: gennmtab/all

clean: $(SUBDIRS:%=%/clean) clean-common

distclean: $(SUBDIRS:%=%/distclean) distclean-common

tags: $(SUBDIRS:%=%/tags) TAGS

DISTFILES = 

distdir: distdir-common

install: $(SUBDIRS:%=%/install)

check:

dep: $(SUBDIRS:%=%/dep)

include $(SRCDIR)/common.mk



