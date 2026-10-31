# The recipes use cmd's commands, whichever shell make was started from.
SHELL := cmd.exe
.SHELLFLAGS := /c

BUILD ?= build
CFLAGS := -O2 -Wall -Wextra -static -Isrc/sdk -D_WIN32_WINNT=0x0A00
PATCHES := $(notdir $(wildcard patches/*))
SDK_HEADERS := $(wildcard src/sdk/*.h)

all: $(BUILD)/aside-late.exe $(BUILD)/aside-late-mod.dll $(addprefix $(BUILD)/sdk/,$(notdir $(SDK_HEADERS))) $(PATCHES:%=$(BUILD)/patches/%/patch.dll)

$(BUILD)/aside-late.exe: $(wildcard src/launcher/*) | $(BUILD)
	gcc $(CFLAGS) -municode -o $@ $(wildcard src/launcher/*.c) -lole32 -luuid

$(BUILD)/aside-late-mod.dll: $(wildcard src/mod/*) $(SDK_HEADERS) | $(BUILD)
	gcc $(CFLAGS) -DASIDE_LATE_MOD -Isrc/mod -shared -o $@ $(wildcard src/mod/*.c) -lwinhttp

# The same headers a patch installed later is compiled against.
$(BUILD)/sdk/%.h: src/sdk/%.h | $(BUILD)/sdk
	copy /Y $(subst /,\,$<) $(subst /,\,$@) >NUL

define PATCH_RULES
$(BUILD)/patches/$(1)/patch.dll: $(wildcard patches/$(1)/*) $(SDK_HEADERS) $(BUILD)/aside-late-mod.dll
	@if not exist $(subst /,\,$(BUILD)/patches/$(1)) mkdir $(subst /,\,$(BUILD)/patches/$(1))
	copy /Y $(subst /,\,patches/$(1)/patch.json $(BUILD)/patches/$(1)/patch.json) >NUL
	gcc $(CFLAGS) -shared -o $$@ $(wildcard patches/$(1)/*.c) $(BUILD)/aside-late-mod.dll
endef
$(foreach patch,$(PATCHES),$(eval $(call PATCH_RULES,$(patch))))

$(BUILD) $(BUILD)/sdk:
	mkdir $(subst /,\,$@)

clean:
	git clean -fdX build

.PHONY: all clean
