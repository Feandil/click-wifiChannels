
all:
	$(MAKE) -C parameters
	$(MAKE) -C tests
	$(MAKE) -C udp-test

clean:
	$(MAKE) -C parameters clean
	$(MAKE) -C tests      clean
	$(MAKE) -C udp-test   clean

