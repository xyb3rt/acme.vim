all: agit alsp apty avim

alsp: CXXFLAGS += -fpic $(shell pkg-config --cflags Qt6Core)
alsp: LDLIBS += $(shell pkg-config --libs Qt6Core)

agit alsp apty avim: acmd.h avim.h base.h vec.h

.c:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

.cpp:
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)
