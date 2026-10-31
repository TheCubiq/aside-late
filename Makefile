CFLAGS := -O2 -Wall -Wextra -municode -static -Isrc
SOURCES := $(wildcard src/*.c src/patches/*/patch.c)
DEPENDS := $(wildcard src/*.h src/patches/*/patch.json)

build/aside-late.exe: $(SOURCES) $(DEPENDS) | build
	gcc $(CFLAGS) -o $@ $(SOURCES)

build:
	mkdir build

clean:
	git clean -fdX build

.PHONY: clean
