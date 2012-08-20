include ../make.include

all: generateTest

generateTest: basiconoffchannel.o markovchainchannel.o basicmtachannel.o utils.o
	$(LINK.cpp) $^ $(LOADLIBES) $(LDLIBS) -o $@
  
clean:
	-rm *.o
	-rm generateTest
