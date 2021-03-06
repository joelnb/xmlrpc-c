# Since the programs in this directories are examples for the user, this
# make file should be as ordinary as possible.  It should not rely heavily
# on included make files or configuration parameters.  It should not use
# libtool.  Also, we don't try to build or rebuild the libraries on which
# these programs depend.

# We need the following make variables to be defined by Makefile.config
# or be passed in:
#
# LIBWWW_CFLAGS, LIBWWW_LDADD, LIBWWW_LIBDIR, LIBWWW_RPATH, LIBWWW_WL_PATH:
#   As set by the 'libwww-config' program.  These tell how libwww is installed
#   on this system.

ifeq ($(SRCDIR)x,x)
SRCDIR = $(CURDIR)/..
endif

default: all

include $(SRCDIR)/Makefile.config

CXXFLAGS = 

CFLAGS = $(CFLAGS_COMMON) $(LIBWWW_CFLAGS) $(CFLAGS_PERSONAL) $(CADD)
LDFLAGS = $(LADD)


CLIENTPROGS_C = \
  synch_client \
  asynch_client \
  auth_client \
  xmlrpc_sample_add_client \
  query-meerkat \

CLIENTPROGS_CPP = \
  meerkat-app-list \
  interop-client \

CGICLIENTPROGS = \
  sample-cgi.cgi \
  interop-cgi.cgi \

SERVERPROGS = \
  xmlrpc_sample_add_server \
  xmlrpc_server_validatee \

PROGS = $(CLIENTPROGS_C) $(SERVERPROGS)

ifeq ($(ENABLE_CPLUSPLUS),yes)
  PROGS += $(CLIENTPROGS_CPP) 
endif
ifeq ($(ENABLE_CGI_SERVER),yes)
  PROGS += $(CGICLIENTPROGS) 
endif

INCLUDES = -I$(SRCDIR) -I$(SRCDIR)/src

ifneq ($(ENABLE_LIBXML2)x,x)
  LIBXML = $(LIBXML2_LIBS)
else
  LIBXML = $(SRCDIR)/lib/expat/xmlparse/.libs/libxmlrpc_xmlparse.a \
           $(SRCDIR)/lib/expat/xmltok/.libs/libxmlrpc_xmltok.a
endif

LIBABYSS = $(SRCDIR)/lib/abyss/src/.libs/libxmlrpc_abyss.a
LIBXMLRPC = $(SRCDIR)/src/.libs/libxmlrpc.a $(LIBXML)
LIBXMLRPC_CPP = $(SRCDIR)/src/libxmlrpc_cpp.a
LIBCLIENT = $(SRCDIR)/src/.libs/libxmlrpc_client.a \
  $(LIBWWW_LDADD) $(LIBXMLRPC)
LIBSERVER = $(SRCDIR)/src/.libs/libxmlrpc_abyss_server.a \
  $(LIBABYSS) $(LIBXMLRPC)
LIBCGI = $(SRCDIR)/src/.libs/libxmlrpc_cgi.a $(LIBXMLRPC)

all: $(PROGS)

$(CLIENTPROGS_C):%:%.o
	$(CCLD) -o $@ $(LDFLAGS) $^ $(LIBCLIENT)

$(CLIENTPROGS_CPP):%:%.o
	$(CXXLD) -o $@ $(LDFLAGS) $^ $(LIBXMLRPC_CPP) $(LIBCLIENT)

$(CGICLIENTPROGS):%.cgi:%.o
	$(CCLD) -o $@ $(LDFLAGS) $^ $(LIBCGI)

$(SERVERPROGS):%:%.o
	$(CCLD) -o $@ $(LDFLAGS) $^ $(LIBSERVER)

%.o:%.c
	$(CC) -c $(INCLUDES) $(CFLAGS) $<

%.o:%.cc
	$(CXX) -c $(INCLUDES) $(CFLAGS) $<

*.c: config.h

config.h:
	$(LN_S) $(SRCDIR)/xmlrpc_config.h $@

include $(SRCDIR)/Makefile.common

.PHONY: clean
clean: clean-common
	rm -f $(PROGS) config.h

.PHONY: distclean
distclean: clean

BINDIR=$(DESTDIR)$(bindir)

FILENAME_GENERATOR = "echo $$p|sed 's/$(EXEEXT)$$//'|sed '$(transform)'|sed 's/$$/$(EXEEXT)/'"

INSTCMD =  "$(LIBTOOL) --mode=install $(INSTALL_PROGRAM) $$p \
  $(BINDIR)/`$(FILENAME_GENERATOR)`"

.PHONY: install
install: $(PROGS)
	@$(NORMAL_INSTALL)
	$(MKINSTALLDIRS) $(BINDIR)
	@list='$(bin_PROGRAMS)'; for p in $$list; do \
	  if test -f $$p; then \
	    echo "$(INSTCMD)"; $(INSTCMD); \
	  else :; \
          fi; \
	done

.PHONY: check
check:
