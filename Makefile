PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
LIBDIR  ?= $(PREFIX)/lib/we-linux
DATADIR ?= $(PREFIX)/share/we-linux
DOCDIR  ?= $(PREFIX)/share/doc/we-linux

# Linux side. CFLAGS/LDFLAGS from the environment (e.g. makepkg) apply here only.
CC      ?= cc
CFLAGS  ?= -O2
# Windows side, run inside Wallpaper Engine under Proton. Kept separate from the host flags,
# which don't all make sense for a PE target.
MINGW         ?= x86_64-w64-mingw32-gcc
MINGW_CFLAGS  ?= -O2
MINGW_LDFLAGS ?= -static -s

WARN := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
BUILD := build
SHIM_SRC := shim/weshim.c shim/native.c shim/media.c

all: $(BUILD)/weshim.dll $(BUILD)/welaunch.exe $(BUILD)/we-linux-agent

$(BUILD):
	mkdir -p $@

$(BUILD)/weshim.dll: $(SHIM_SRC) shim/shim.h | $(BUILD)
	$(MINGW) $(MINGW_CFLAGS) $(WARN) -shared -o $@ $(SHIM_SRC) $(MINGW_LDFLAGS) -lgdi32 -ldwmapi -lole32 -lmfplat

$(BUILD)/welaunch.exe: shim/welaunch.c | $(BUILD)
	$(MINGW) $(MINGW_CFLAGS) $(WARN) -municode -mwindows -o $@ $< $(MINGW_LDFLAGS)

$(BUILD)/we-linux-agent: agent/we-linux-agent.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -o $@ $< $(LDFLAGS) $(shell pkg-config --cflags --libs libsystemd)

install: all
	install -Dm755 $(BUILD)/we-linux-agent $(DESTDIR)$(LIBDIR)/we-linux-agent
	install -Dm644 $(BUILD)/weshim.dll     $(DESTDIR)$(LIBDIR)/weshim.dll
	install -Dm644 $(BUILD)/welaunch.exe   $(DESTDIR)$(LIBDIR)/welaunch.exe
	install -Dm644 kwin/we-desktop.js      $(DESTDIR)$(DATADIR)/kwin/we-desktop.js
	install -Dm644 README.md               $(DESTDIR)$(DOCDIR)/README.md
	install -d $(DESTDIR)$(BINDIR)
	sed -e 's|@LIBDIR@|$(LIBDIR)|g' -e 's|@DATADIR@|$(DATADIR)|g' bin/we-linux > $(DESTDIR)$(BINDIR)/we-linux
	chmod 755 $(DESTDIR)$(BINDIR)/we-linux

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/we-linux
	rm -rf $(DESTDIR)$(LIBDIR) $(DESTDIR)$(DATADIR) $(DESTDIR)$(DOCDIR)

clean:
	rm -rf $(BUILD)

.PHONY: all install uninstall clean
