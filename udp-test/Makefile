CFLAGS?=-pipe -O2 -g -Wall -Werror -Wchar-subscripts -Wundef -Wshadow -Wwrite-strings -Wsign-compare -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self -Wpointer-arith -Wredundant-decls \
-Wformat-nonliteral -Wno-format-zero-length -Wno-format-y2k -Wmissing-format-attribute -Wsequence-point -Wparentheses -Wmissing-declarations

LDLIBS +=-lz
all: server client transfo

server: server.o zutil.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -levent -o $@

client: client.o zutil.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -levent -lrt -o $@

transfo: transfo.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

clean:
	-rm *.o
	-rm server
	-rm client
	-rm transfo
