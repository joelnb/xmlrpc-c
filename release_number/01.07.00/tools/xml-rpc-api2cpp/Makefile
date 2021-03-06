ifeq ($(SRCDIR),)
  updir = $(shell echo $(dir $(1)) | sed 's/.$$//')
  TOOLSDIR := $(call updir,$(CURDIR))
  SRCDIR := $(call updir,$(TOOLSDIR))
  BLDDIR := $(SRCDIR)
endif
SUBDIR := tools/xml-rpc-api2cpp

default: all

include $(BLDDIR)/Makefile.config

include $(SRCDIR)/tools/Makefile.common

INCLUDES = -I$(BLDDIR) -I$(BLDDIR)/include -I$(SRCDIR)/include

CXXFLAGS = $(INCLUDES) $(CXXFLAGS_COMMON) $(CFLAGS_PERSONAL) $(CADD)

LDFLAGS = $(LADD)

LDLIBS = -L$(BLDDIR)/src/cpp -lxmlrpc_cpp -lxmlrpc_server $(CLIENT_LDLIBS)

all: xml-rpc-api2cpp

OBJECTS = \
  xml-rpc-api2cpp.o \
  DataType.o \
  XmlRpcFunction.o \
  XmlRpcClass.o \
  SystemProxy.o \

xml-rpc-api2cpp: \
  $(OBJECTS) \
  $(LIBXMLRPC_CPP) \
  $(LIBXMLRPC_CLIENT) \
  $(LIBXMLRPC_SERVER) \
  $(LIBXMLRPC) \
  $(LIBXMLRPC_XML) \
  $(LIBXMLRPC_UTIL)
	$(CXXLD) -o $@ $(LDFLAGS) $(OBJECTS) $(LDLIBS)

%.o:%.cpp
	$(CXX) -c $(CXXFLAGS) $<

# This Makefile.config dependency makes sure the symlinks get built before
# this make file is used for anything.

$(BLDDIR)/Makefile.config: srcdir

include Makefile.depend

.PHONY: clean
clean: clean-common
	rm -f xml-rpc-api2cpp

.PHONY: distclean
distclean: clean distclean-common

.PHONY: dep
dep: dep-common
