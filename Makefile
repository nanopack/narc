# Top level makefile, the real deal is at src/Makefile

default: all

.DEFAULT:
	cd src && $(MAKE) $@

install:
	cd src && $(MAKE) $@

.PHONY: install
