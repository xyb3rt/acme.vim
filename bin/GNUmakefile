all: agit alsp apty avim

agit alsp apty avim: acmd.h avim.h base.h vec.h

CFLAGS += -O3
alsp: LDLIBS += -ljansson

.c:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)
