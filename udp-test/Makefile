CFLAGS?=-pipe -O2 -g -Wall -Werror -Wchar-subscripts -Wundef -Wshadow -Wwrite-strings -Wsign-compare -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self -Wpointer-arith -Wredundant-decls \
-Wformat-nonliteral -Wno-format-zero-length -Wno-format-y2k -Wmissing-format-attribute -Wsequence-point -Wparentheses -Wmissing-declarations

LDLIBS+=-levent

all: server client

server: server.o
	$(LINK.cpp) $^ $(LOADLIBES) $(LDLIBS) -lz -o $@

client: client.o
	$(LINK.cpp) $^ $(LOADLIBES) $(LDLIBS) -lrt -o $@


clean:
	-rm *.o
	-rm server
	-rm client
