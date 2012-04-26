CXXFLAGS=-pipe -O2 -g -Wall -Werror -Wchar-subscripts -Wundef -Wshadow -Wwrite-strings -Wsign-compare -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self -Wpointer-arith -Wredundant-decls -Wformat-nonliteral -Wno-format-y2k -Wmissing-format-attribute -Wsequence-point -Wparentheses -Wmissing-declarations

all: generateTest

generateTest: basiconoffchannel.o markovchainchannel.o basicmtachannel.o utils.o
	$(LINK.cpp) $^ $(LOADLIBES) $(LDLIBS) -o $@
  
clean:
	-rm *.o
	-rm generateTest
