CC64    ?= x86_64-w64-mingw32-gcc
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
LDFLAGS ?= -static -s

BUILD := build

all: $(BUILD)/weshim.dll $(BUILD)/welaunch.exe $(BUILD)/we-linux-agent

$(BUILD):
	mkdir -p $@

# Windows side, loaded into Wallpaper Engine under Proton.
$(BUILD)/weshim.dll: shim/weshim.c shim/native.c shim/shim.h | $(BUILD)
	$(CC64) $(CFLAGS) -shared -o $@ shim/weshim.c shim/native.c $(LDFLAGS) -lgdi32 -ldwmapi

$(BUILD)/welaunch.exe: shim/welaunch.c | $(BUILD)
	$(CC64) $(CFLAGS) -municode -mwindows -o $@ $< $(LDFLAGS)

# Linux side.
$(BUILD)/we-linux-agent: agent/we-linux-agent.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(shell pkg-config --cflags --libs libsystemd)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
