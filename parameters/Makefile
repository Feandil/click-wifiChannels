include ../make.include

all: parseInput

parseInput: main.o module.o markovchain.o basiconoff.o basicmta.o
	$(LINK.cpp) $^ $(LOADLIBES) $(LDLIBS) -o $@

doc: html
html: Doxyfile *.cpp *.h
	@doxygen $<
	
clean:
	-rm *.o
	-rm parseInput
	-rm -r html
