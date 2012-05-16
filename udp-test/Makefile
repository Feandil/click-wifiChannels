CFLAGS?=-pipe -O2 -g -Wall -Werror -Wchar-subscripts -Wundef -Wshadow -Wwrite-strings -Wsign-compare -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self -Wpointer-arith -Wredundant-decls \
-Wformat-nonliteral -Wno-format-zero-length -Wno-format-y2k -Wmissing-format-attribute -Wsequence-point -Wparentheses -Wmissing-declarations

all: server client transfo

server: server.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -levent -lz -o $@

client: client.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -levent -lrt -o $@

transfo: transfo.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -lz -o $@

clean:
	-rm *.o
	-rm server
	-rm client
	-rm transfo
