# The recipes use cmd's commands, whichever shell make was started from.
SHELL := cmd.exe
.SHELLFLAGS := /c

BUILD ?= build
CFLAGS := -O2 -Wall -Wextra -static -Isrc/sdk -D_WIN32_WINNT=0x0A00
PATCHES := $(notdir $(wildcard patches/*))
SDK_HEADERS := $(wildcard src/sdk/*.h)
# Everything under a folder, however deep.
rwildcard = $(wildcard $(1)/*) $(foreach folder,$(wildcard $(1)/*/),$(call rwildcard,$(folder:/=)))

all: $(BUILD)/aside-late.exe $(BUILD)/aside-late-mod.dll $(addprefix $(BUILD)/sdk/,$(notdir $(SDK_HEADERS))) $(PATCHES:%=$(BUILD)/patches/%/patch.dll)

# A running Aside has these files loaded. Windows won't let them be overwritten or deleted, but they can be renamed, so
# what's in the way moves aside and the next launch uses the new file. Old copies go once nothing has them loaded.
MoveAside = @del /Q "$(subst /,\,$(basename $(1))).old-*$(suffix $(1))" >NUL 2>&1 & del /Q "$(subst /,\,$(1))" >NUL 2>&1 & if exist "$(subst /,\,$(1))" ren "$(subst /,\,$(1))" "$(notdir $(basename $(1))).old-%RANDOM%%RANDOM%$(suffix $(1))" & exit /b 0

$(BUILD)/aside-late.exe: $(wildcard src/launcher/*) | $(BUILD)
	$(call MoveAside,$@)
	gcc $(CFLAGS) -municode -o $@ $(wildcard src/launcher/*.c) -lole32 -luuid

$(BUILD)/aside-late-mod.dll: $(wildcard src/mod/*) $(SDK_HEADERS) | $(BUILD)
	$(call MoveAside,$@)
	gcc $(CFLAGS) -DASIDE_LATE_MOD -Isrc/mod -shared -o $@ $(wildcard src/mod/*.c) -lwinhttp -lole32

# The same headers a patch installed later is compiled against.
$(BUILD)/sdk/%.h: src/sdk/%.h | $(BUILD)/sdk
	copy /Y $(subst /,\,$<) $(subst /,\,$@) >NUL

define PATCH_RULES
$(BUILD)/patches/$(1)/patch.dll: $(call rwildcard,patches/$(1)) $(SDK_HEADERS) $(BUILD)/aside-late-mod.dll
	@if not exist $(subst /,\,$(BUILD)/patches/$(1)) mkdir $(subst /,\,$(BUILD)/patches/$(1))
	copy /Y $(subst /,\,patches/$(1)/patch.json $(BUILD)/patches/$(1)/patch.json) >NUL
	$(call MoveAside,$(BUILD)/patches/$(1)/patch.dll)
	gcc $(CFLAGS) -Ipatches/$(1) -shared -o $$@ $(filter %.c,$(call rwildcard,patches/$(1))) $(BUILD)/aside-late-mod.dll
endef
$(foreach patch,$(PATCHES),$(eval $(call PATCH_RULES,$(patch))))

$(BUILD) $(BUILD)/sdk:
	mkdir $(subst /,\,$@)

clean:
	git clean -fdX build

.PHONY: all clean
