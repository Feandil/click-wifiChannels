CXXFLAGS?=-pipe -O2 -g -Wall -Wextra -Werror \
	-Wcast-qual \
	-Wcast-align \
	-Wchar-subscripts \
	-Wconversion \
	-Wfloat-equal \
	-Wformat-nonliteral \
	-Wformat-security \
	-Winit-self \
	-Wlogical-op \
	-Wmissing-declarations \
	-Wmissing-include-dirs \
	-Wmissing-format-attribute \
	-Wpointer-arith \
	-Wredundant-decls \
	-Wsign-conversion \
	-Wshadow \
	-Wtrampolines \
	-Wwrite-strings \
	-Wundef \
	-Wunused \
	-Wno-unused-parameter \
	-Wno-format-zero-length \
	-Wno-format-y2k \
	-Wsuggest-attribute=pure \
	-Wsuggest-attribute=noreturn \
	-Wunsafe-loop-optimizations

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
