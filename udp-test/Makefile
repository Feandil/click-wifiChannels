CFLAGS?=-pipe -O2 -g -Wall -Wextra -Werror -Wchar-subscripts -Wundef -Wshadow -Wwrite-strings -Wsign-compare -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self \
-Wredundant-decls -Wformat-nonliteral -Wno-format-zero-length -Wno-format-y2k -Wmissing-format-attribute -Wsequence-point -Wparentheses -Wmissing-declarations -Wpointer-arith \
-Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wconversion
# -Wtraditionnal -pedantic

LZ_LIBS ?= -lz
NL_LIBS ?= -lnl-3 -lnl-genl-3
EV_LIBS ?= -lev
RT_LIBS ?= -lrt
NCURSES_LIBS ?= -lncursesw
MATH_LIBS ?= $(shell pkg-config --libs gsl)

LDLIBS ?= $(LZ_LIBS)

NL_INCLUDE ?= -I/usr/include/libnl3
SOTIMESTAMP_INCLUDE ?=

MONITOR_DEP = monitor.o radiotap-parser.o crc.o

all: server client transfo evallink extract

monitor.o: monitor.c
	$(COMPILE.c) $(SOTIMESTAMP_INCLUDE) $(NL_INCLUDE) $(OUTPUT_OPTION) $<

#As we cast something to zlib structure that do not support "const", zutil cannot be compiled with cast-qual
zutil.o: zutil.c
	$(COMPILE.c) -Wno-cast-qual $(OUTPUT_OPTION) $<

server: server.o zutil.o $(MONITOR_DEP)
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EV_LIBS) $(RT_LIBS) $(NL_LIBS) -o $@

client: client.o zutil.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EV_LIBS) $(RT_LIBS) -o $@

transfo: transfo.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

extract: extract.o zutil.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(MATH_LIBS) -o $@

test-zcat: zutil.c
	$(COMPILE.c) -DTEST $(OUTPUT_OPTION)  $< -o $@.o
	$(LINK.c) $@.o $(LOADLIBES) $(LDLIBS) -o $@

clean:
	-rm *.o
	-rm server
	-rm client
	-rm transfo
	-rm evallink
	-rm test-zcat

evallink: evallink.o $(MONITOR_DEP)
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EV_LIBS) $(RT_LIBS) $(NL_LIBS) $(NCURSES_LIBS) -o $@
