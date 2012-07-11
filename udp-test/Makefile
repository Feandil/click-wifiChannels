CFLAGS?=-pipe -O2 -g -Wall -Werror -Wchar-subscripts -Wundef -Wshadow -Wwrite-strings -Wsign-compare -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self -Wredundant-decls \
-Wformat-nonliteral -Wno-format-zero-length -Wno-format-y2k -Wmissing-format-attribute -Wsequence-point -Wparentheses -Wmissing-declarations -Wpointer-arith

LZ_LIBS ?= -lz
NL_LIBS ?= -lnl-3 -lnl-genl-3
EV_LIBS ?= -levent
RT_LIBS ?= -lrt

LDLIBS ?= $(LZ_LIBS)

CFLAGS += -I/usr/include/libnl3

all: server client transfo

server: server.o zutil.o monitor.o radiotap-parser.o crc.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EV_LIBS) $(RT_LIBS) $(NL_LIBS) -o $@

client: client.o zutil.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EV_LIBS) $(RT_LIBS) -o $@

transfo: transfo.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

clean:
	-rm *.o
	-rm server
	-rm client
	-rm transfo
