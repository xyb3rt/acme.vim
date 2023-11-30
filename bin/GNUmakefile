all: acmegit acmelsp acmevim

acmelsp: CXXFLAGS += -fpic $(shell pkg-config --cflags Qt6Core)
acmelsp: LDLIBS += $(shell pkg-config --libs Qt6Core)

acmegit acmelsp acmevim: acmecmd.h acmevim.h indispensbl/call.h \
	indispensbl/core.h indispensbl/cwd.h indispensbl/fmt.h \
	indispensbl/vec.h

.c:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

.cpp:
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)
