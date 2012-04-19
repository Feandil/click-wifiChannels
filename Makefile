CFLAGS=-pipe -O2 -g -Wall -Werror -Wchar-subscripts -Wundef -Wshadow -Wwrite-strings -Wsign-compare -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self -Wpointer-arith -Wredundant-decls -Wformat-nonliteral -Wno-format-zero-length -Wno-format-y2k -Wmissing-format-attribute -Wsequence-point -Wparentheses -Wmissing-declarations
CXXFLAGS=-pipe -O2 -g -Wall -Werror -Wchar-subscripts -Wundef -Wshadow -Wwrite-strings -Wsign-compare -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self -Wpointer-arith -Wredundant-decls -Wformat-nonliteral -Wno-format-y2k -Wmissing-format-attribute -Wsequence-point -Wparentheses -Wmissing-declarations

CPP_LIBS=-lstdc++

all: parseInput

parseInput: parseInput.o parseModule.o parseMarkocChain.o parseBasicOnOff.o
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) $(TC_CFLAGS) $(CPP_LIBS) -o $@

clean:
	-rm *.o
	-rm parseInput
