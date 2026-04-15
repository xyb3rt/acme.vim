BINARIES := agit alsp apty avim
all: $(BINARIES)

$(BINARIES): Makefile avim.h base.h vec.h
agit alsp apty: acmd.h
alsp: io.h

CFLAGS += -O3
LDLIBS_alsp = -ljansson

.c:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS) $(LDLIBS_$@)

clean:
	rm -f $(BINARIES) *~
