CFLAGS := -O2 -Wall -Wextra -static -Isrc -D_WIN32_WINNT=0x0A00
SHARED := src/mod/lookup.c $(wildcard src/patches/*/resolve.c)
SOURCES := $(wildcard src/*.c src/patches/*/patch.c) $(SHARED)
MOD_SOURCES := $(wildcard src/mod/*.c src/patches/*/mod.c src/patches/*/flyout.c $(wildcard src/patches/*/resolve.c))
DEPENDS := $(wildcard src/*.h src/mod/*.h src/patches/*/*.h src/patches/*/patch.json)

all: build/aside-late.exe build/aside-late-mod.dll

build/aside-late.exe: $(SOURCES) $(DEPENDS) | build
	gcc $(CFLAGS) -municode -o $@ $(SOURCES)

build/aside-late-mod.dll: $(MOD_SOURCES) $(DEPENDS) | build
	gcc $(CFLAGS) -shared -o $@ $(MOD_SOURCES) -lgdi32 -lgdiplus -ldwmapi -ladvapi32

build:
	mkdir build

clean:
	git clean -fdX build

.PHONY: all clean
