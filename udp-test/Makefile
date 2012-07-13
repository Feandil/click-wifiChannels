CFLAGS?=-pipe -O2 -g -Wall -Werror -Wchar-subscripts -Wundef -Wshadow -Wwrite-strings -Wsign-compare -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self -Wredundant-decls \
-Wformat-nonliteral -Wno-format-zero-length -Wno-format-y2k -Wmissing-format-attribute -Wsequence-point -Wparentheses -Wmissing-declarations -Wpointer-arith

LZ_LIBS ?= -lz
NL_LIBS ?= -lnl-3 -lnl-genl-3
EVENT_LIBS ?= -levent
EV_LIBS ?= -lev
RT_LIBS ?= -lrt
NCURSES_LIBS ?= -lncursesw

LDLIBS ?= $(LZ_LIBS)

CFLAGS += -I/usr/include/libnl3

MONITOR_DEP = monitor.o radiotap-parser.o crc.o

all: server client transfo evallink

server: server.o zutil.o $(MONITOR_DEP)
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EVENT_LIBS) $(RT_LIBS) $(NL_LIBS) -o $@

client: client.o zutil.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EV_LIBS) $(RT_LIBS) -o $@

transfo: transfo.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

clean:
	-rm *.o
	-rm server
	-rm client
	-rm transfo
	-rm evallink

evallink: evallink.o $(MONITOR_DEP)
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EV_LIBS) $(RT_LIBS) $(NL_LIBS) $(NCURSES_LIBS) -o $@
