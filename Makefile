CC64    ?= x86_64-w64-mingw32-gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
LDFLAGS ?= -static -s

BUILD := build

all: $(BUILD)/weshim.dll $(BUILD)/welaunch.exe

$(BUILD):
	mkdir -p $@

$(BUILD)/weshim.dll: shim/weshim.c | $(BUILD)
	$(CC64) $(CFLAGS) -shared -o $@ $< $(LDFLAGS) -lgdi32

$(BUILD)/welaunch.exe: shim/welaunch.c | $(BUILD)
	$(CC64) $(CFLAGS) -municode -mwindows -o $@ $< $(LDFLAGS)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
