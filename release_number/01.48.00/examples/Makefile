# Since the programs in this directory are examples for the user, this make
# file should be as ordinary as possible.  It should not rely heavily on
# included make files or configuration parameters.  Also, we don't try to
# build or rebuild the libraries on which these programs depend or even
# recognize that they've changed on their own.


ifeq ($(SRCDIR),)
SRCDIR = $(CURDIR)/..
BLDDIR = $(SRCDIR)
endif
SUBDIR = examples

include $(BLDDIR)/config.mk

default: all

CFLAGS_ALL = $(CFLAGS_PERSONAL) $(CFLAGS) $(CADD)
LDFLAGS_ALL = $(LDFLAGS_PERSONAL) $(LDFLAGS) $(LADD)

# If this were a real application, working from an installed copy of
# Xmlrpc-c, XMLRPC_C_CONFIG would just be 'xmlrpc-c-config'.  It would be
# found in the user's PATH.
XMLRPC_C_CONFIG = $(BLDDIR)/xmlrpc-c-config.test

CLIENTPROGS = \
  auth_client \
  compound_value_client \
  synch_client \
  xmlrpc_sample_add_client \
  xmlrpc_asynch_client \

ifeq ($(MUST_BUILD_CURL_CLIENT),yes)
  ifneq ($(MSVCRT),yes)
    CLIENTPROGS += curl_client
    CLIENTPROGS += interrupted_client
  endif
endif

SERVERPROGS_CGI = \
  xmlrpc_sample_add_server.cgi

SERVERPROGS_ABYSS = \
  compound_value_server \
  xmlrpc_inetd_server \
  xmlrpc_socket_server \
  xmlrpc_loop_server \
  xmlrpc_sample_add_server \
  xmlrpc_server_validatee \

ifneq ($(MSVCRT),yes)
  SERVERPROGS_ABYSS += interrupted_server
endif

ifeq ($(HAVE_OPENSSL),yes)
  SERVERPROGS_ABYSS += ssl_server ssl_secure_server
endif

BASIC_PROGS = \
  json \
  gen_sample_add_xml \

# Build up PROGS:
PROGS = 

PROGS += $(BASIC_PROGS)

ifeq ($(ENABLE_ABYSS_SERVER),yes)
  PROGS += $(SERVERPROGS_ABYSS)
endif

ifeq ($(MUST_BUILD_CLIENT),yes)
  PROGS += $(CLIENTPROGS)
endif

ifeq ($(ENABLE_CGI_SERVER),yes)
  PROGS += $(SERVERPROGS_CGI) 
endif

INCLUDES = -I. $(shell $(XMLRPC_C_CONFIG) client abyss-server --cflags)

LIBS_CLIENT = \
  $(shell $(XMLRPC_C_CONFIG) client --libs)

LIBS_SERVER_ABYSS = \
  $(shell $(XMLRPC_C_CONFIG) abyss-server --libs)

LIBS_SERVER_CGI = \
  $(shell $(XMLRPC_C_CONFIG) cgi-server --libs)

LIBS_BASE = \
  $(shell $(XMLRPC_C_CONFIG) --libs)

all: $(PROGS)

ifeq ($(ENABLE_CPLUSPLUS),yes)
  all: cpp/all
endif

.PHONY: cpp/all
cpp/all: $(BLDDIR)/examples/cpp
	$(MAKE) -C cpp -f $(SRCDIR)/examples/cpp/Makefile all

# When building in separate tree, directory won't exist yet
$(BLDDIR)/examples/cpp:
	mkdir $@

$(CLIENTPROGS):%:%.o
	$(CCLD) -o $@ $^ $(LIBS_CLIENT) $(LDFLAGS_ALL)

$(SERVERPROGS_CGI):%.cgi:%_cgi.o
	$(CCLD) -o $@ $^ $(LIBS_SERVER_CGI) $(LDFLAGS_ALL)

$(SERVERPROGS_ABYSS):%:%.o
	$(CCLD) -o $@ $^ $(LIBS_SERVER_ABYSS) $(LDFLAGS_ALL)

$(BASIC_PROGS):%:%.o
	$(CCLD) -o $@ $^ $(LIBS_BASE) $(LDFLAGS_ALL)


OBJECTS = $(patsubst %,%.o,$(patsubst %.cgi,%_cgi,$(PROGS)))

$(OBJECTS):%.o:%.c
	$(CC) -c $(INCLUDES) $(CFLAGS_ALL) $<

# config.h just describes the build environment.  We use it so that the
# example programs will build in users' various environments.  If you're
# copying these examples, you can just remove this header from the programs
# and hardcode whatever is right for your build environment.

$(OBJECTS): config.h

config.h:
	$(LN_S) $(BLDDIR)/xmlrpc_config.h $@

.PHONY: clean
clean:
	rm -f $(PROGS) *.o config.h
	$(MAKE) -C cpp clean

.PHONY: distclean
distclean: clean

.PHONY: install
install:

.PHONY: uninstall
uninstall:

.PHONY: check
check:

.PHONY: dep depend
dep depend:
# We don't do dependencies in this directory, because it's supposed to be
# an example of what a program outside this package would do, so we can't
# go weaving it into the rest of the package.  Ergo, a developer must
# carefully clean and remake examples as he updates other parts of the tree.
