all: agit alsp apty avim

agit alsp apty avim: Makefile acmd.h avim.h base.h vec.h

CFLAGS += -O3
LDLIBS_alsp = -ljansson

.c:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS) $(LDLIBS_$@)
