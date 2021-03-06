ifeq ($(SRCDIR),)
  updir = $(shell echo $(dir $(1)) | sed 's/.$$//')
  srcDIR := $(call updir,$(CURDIR))
  SRCDIR := $(call updir,$(srcDIR))
  BLDDIR := $(SRCDIR)
endif
SUBDIR := src/test

include $(BLDDIR)/config.mk

XMLRPC_C_CONFIG = $(BLDDIR)/xmlrpc-c-config.test

LDADD_CLIENT = \
  $(shell $(XMLRPC_C_CONFIG) client --ldadd)
LDADD_ABYSS_SERVER = \
  $(shell $(XMLRPC_C_CONFIG) abyss-server --ldadd)
LDADD_CGI_SERVER = \
  $(shell $(XMLRPC_C_CONFIG) cgi-server --ldadd)

default: all

INCLUDES = -I$(BLDDIR) -I $(SRCDIR)/include -I$(SRCDIR)/lib/util/include \

PROGS = test cgitest1

all: $(PROGS)

LDFLAGS = $(LADD)

TEST_OBJS = \
  test.o \
  abyss.o \
  cgi.o \
  method_registry.o \
  parse_xml.o \
  serialize.o \
  serialize_value.o \
  server_abyss.o \
  value.o \
  value_datetime.o \
  xml_data.o \

ifeq ($(MUST_BUILD_CLIENT),yes)
  TEST_OBJS += client.o
  LIBXMLRPC_CLIENT_DEP = $(LIBXMLRPC_CLIENT_A)
else
  TEST_OBJS += client_dummy.o
  LIBXMLRPC_CLIENT_DEP =
endif

include $(SRCDIR)/common.mk

test: \
  $(XMLRPC_C_CONFIG) \
  $(TEST_OBJS) $(LIBXMLRPC_A) $(LIBXMLRPC_UTIL_A) \
  $(LIBXMLRPC_SERVER_A) $(LIBXMLRPC_SERVER_ABYSS_A) $(LIBXMLRPC_XML) \
  $(LIBXMLRPC_CLIENT_DEP) $(LIBXMLRPC_ABYSS_A) \
  $(LIBXMLRPC_XMLPARSE_A) $(LIBXMLRPC_XMLTOK_A) \
  $(CASPRINTF)
	$(CCLD) -o $@ $(LDFLAGS) \
	    $(TEST_OBJS) $(LDADD_CLIENT) $(LDADD_ABYSS_SERVER) $(CASPRINTF)

cgitest1:%:%.o $(LIBXMLRPC_SERVER_A) $(LIBXMLRPC_SERVER_CGI_A) \
  $(LIBXMLRPC_A) $(LIBXMLRPC_UTIL_A) $(LIBXMLRPC_XML)
	$(CCLD) -o $@ $< $(LDFLAGS) $(LDADD_CGI_SERVER)

CFLAGS = $(CFLAGS_COMMON) $(CFLAGS_PERSONAL) $(CADD)

OBJS = $(TEST_OBJS) cgitest1.o

$(OBJS):%.o:%.c
	$(CC) -c $(INCLUDES) $(CFLAGS) $<

# Note the difference between 'check' and 'runtests'.  'check' means to check
# our own correctness.  'runtests' means to run the tests that check our
# parent's correctness

.PHONY: check
check:

.PHONY: runtests
runtests: test cgitest1
	./test

.PHONY: install
install:

.PHONY: clean clean-local distclean
clean: clean-common clean-local
clean-local:
	rm -f $(PROGS)

distclean: clean distclean-common

.PHONY: dep
dep: dep-common

include Makefile.depend
