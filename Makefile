CFLAGS=-pipe -O2 -g -Wall -Werror -Wchar-subscripts -Wundef -Wshadow -Wwrite-strings -Wsign-compare -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self -Wpointer-arith -Wredundant-decls -Wformat-nonliteral -Wno-format-zero-length -Wno-format-y2k -Wmissing-format-attribute -Wsequence-point -Wparentheses -Wmissing-declarations

all: parseInput

parseInput: parseInput.o parseModule.o parseMarkocChain.o
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) $(TC_CFLAGS) $(TC_LIBS) -o $@

clean:
	-rm *.o
	-rm parseInput
