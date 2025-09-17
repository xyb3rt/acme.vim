all: agit alsp apty avim

agit alsp apty avim: acmd.h avim.h base.h vec.h

CFLAGS += -O3
CXXFLAGS += -O3

alsp: CXXFLAGS += -fpic $(shell pkg-config --cflags Qt6Core)
QT_LIBS = $(shell pkg-config --libs-only-L Qt6Core)
RPATH_OPT = -Wl,-rpath,
alsp: LDFLAGS += $(patsubst -L%,$(RPATH_OPT)%,$(QT_LIBS))
alsp: LDLIBS += $(shell pkg-config --libs Qt6Core)

.c:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

.cpp:
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)
