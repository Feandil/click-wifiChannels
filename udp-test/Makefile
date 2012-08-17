CFLAGS?=-pipe -O2 -g -Wall -Wextra -Werror \
	-Wbad-function-cast \
	-Wcast-qual \
	-Wcast-align \
	-Wconversion \
	-Wformat-nonliteral \
	-Wformat-security \
	-Winit-self \
	-Wjump-misses-init \
	-Wlogical-op \
	-Wmissing-declarations \
	-Wmissing-include-dirs \
	-Wmissing-format-attribute \
	-Wmissing-prototypes \
	-Wnested-externs \
	-Wpointer-arith \
	-Wredundant-decls \
	-Wshadow \
	-Wstrict-prototypes \
	-Wtrampolines \
	-Wwrite-strings \
	-Wundef \
	-Wunused \
	-Wno-unused-parameter \
	-Wno-format-zero-length \
	-Wno-format-y2k \
	-Wsuggest-attribute=pure \
	-Wsuggest-attribute=const \
	-Wsuggest-attribute=noreturn \
	-Wunsafe-loop-optimizations

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

all: server client evallink extract

monitor.o: monitor.c
	$(COMPILE.c) $(SOTIMESTAMP_INCLUDE) $(NL_INCLUDE) $(OUTPUT_OPTION) $<

#As we cast something to zlib structure that do not support "const", zutil cannot be compiled with cast-qual
zutil.o: zutil.c
	$(COMPILE.c) -Wno-cast-qual $(OUTPUT_OPTION) $<

server: server.o zutil.o $(MONITOR_DEP)
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EV_LIBS) $(RT_LIBS) $(NL_LIBS) -o $@

client: client.o zutil.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EV_LIBS) $(RT_LIBS) -o $@

extract: extract.o zutil.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(MATH_LIBS) -o $@

test-zcat: zutil.c
	$(COMPILE.c) -Wno-cast-qual -DTEST $(OUTPUT_OPTION)  $< -o $@.o
	$(LINK.c) $@.o $(LOADLIBES) $(LDLIBS) -o $@

clean:
	-rm *.o
	-rm server
	-rm client
	-rm transfo
	-rm evallink
	-rm test-zcat
	-rm -r html
	-rm doxygen_*.tmp

evallink: evallink.o $(MONITOR_DEP)
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) $(EV_LIBS) $(RT_LIBS) $(NL_LIBS) $(NCURSES_LIBS) -o $@

doc:
	@doxygen
