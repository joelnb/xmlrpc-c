ifeq ($(SRCDIR),)
  updir = $(shell echo $(dir $(1)) | sed 's/.$$//')
  EXPATDIR := $(call updir,$(CURDIR))
  LIBDIR := $(call updir,$(EXPATDIR))
  SRCDIR := $(call updir,$(LIBDIR))
  BLDDIR := $(SRCDIR)
endif
SUBDIR := lib/expat/gennmtab

include $(BLDDIR)/config.mk

INCLUDES = -I$(BLDDIR) -Isrcdir/lib/util/include

default: all

include $(SRCDIR)/common.mk

.PHONY: all
all: gennmtab

.PHONY: clean
clean: clean-common
	rm -f gennmtab

.PHONY: distclean
distclean: clean distclean-common

.PHONY: tags
tags: TAGS

.PHONY: distdir
distdir:

.PHONY: install
install:

.PHONY: uninstall
uninstall:

.PHONY: dep
dep: dep-common

gennmtab.o:%.o:%.c
	$(CC_FOR_BUILD) -c $< -o $@ $(CFLAGS_ALL_FOR_BUILD)

gennmtab:%:%.o
	$(CC_FOR_BUILD) -o $@ $(LDFLAGS_ALL_FOR_BUILD) $^

include depend.mk
