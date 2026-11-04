#
# FTA-850 programmer, built on the AMI tool kit (graphical model, static),
# plus a command line tool on its plain library.
#
# AMIDIR is where amitk lives; ". setpath" in amitk sets it, otherwise the
# default below is used. On Linux the link follows the graphics backend
# amitk was built with (lib/graph_backend: wayland, x11 or fb).
#
AMIDIR ?= $(HOME)/projects/amitk

OSTYPE := $(shell uname)
AMILIB = $(AMIDIR)/lib/libami_graph.a

ifeq ($(OSTYPE),Darwin)
    SSL_PREFIX := $(shell /opt/homebrew/bin/brew --prefix openssl@3)
    LIBS = $(SSL_PREFIX)/lib/libssl.dylib $(SSL_PREFIX)/lib/libcrypto.dylib \
           -framework CoreFoundation -framework CoreGraphics \
           -framework ImageIO -framework CoreMIDI -framework AudioToolbox \
           -framework Cocoa -framework CoreText -framework QuartzCore \
           -framework IOKit
    KEEP = -Wl,-u,_ami_maxx
    AMILINK = -Wl,-force_load,$(AMILIB) -Wl,-e,_pa_entry
    CAPTURE = $(AMIDIR)/macosx/screen_capture.o
else
    GRAPHICS_BACKEND := $(shell cat $(AMIDIR)/lib/graph_backend 2>/dev/null)
    ifeq ($(GRAPHICS_BACKEND),wayland)
        GBACKLIBS = -lwayland-client -lwayland-cursor -lxkbcommon \
                    -lfreetype -lfontconfig
    else ifeq ($(GRAPHICS_BACKEND),fb)
        GBACKLIBS = -lfreetype -lfontconfig -lpng -lz
    else
        GBACKLIBS = -lX11 -lXtst -lfreetype -lfontconfig
    endif
    LIBS = -lasound -lfluidsynth -lssl -lcrypto $(GBACKLIBS) -lm -lpthread
    KEEP = $(AMIDIR)/stub/keeper.o -Wl,-u,ami_maxx
    AMILINK = $(AMILIB)
    CAPTURE = $(AMIDIR)/linux/$(GRAPHICS_BACKEND)/screen_capture.o
endif

CC = gcc
CFLAGS = -g3 -Wall -I$(AMIDIR)/include -I$(AMIDIR)/libc
# the command line tool takes only amitk's plain (non graphical) library,
# for the host position (ami_latitude) that "nearest here" uses
CLICFLAGS = -g3 -Wall -I$(AMIDIR)/include -I$(AMIDIR)/libc
PLAINLIB = $(AMIDIR)/lib/libami_plain.a

CORE = radio.c cfg.c gpslog.c defaults.c defaults750.c stations.c
HDRS = radio.h cfg.h gpslog.h stations.h

all: fta850 fta850cli

fta850: fta850.c $(CORE) $(HDRS) $(AMILIB)
	$(CC) $(CFLAGS) fta850.c $(CORE) $(KEEP) $(AMILINK) $(LIBS) -lm -o $@

fta850cli: cli.c $(CORE) $(HDRS) $(PLAINLIB)
	$(CC) $(CLICFLAGS) cli.c $(CORE) $(PLAINLIB) -lpthread -lm -o $@

sim: fta850
	./fta850 --sim

# test build: takes a picture of its own window, see FTA850_SHOT in fta850.c
shot: fta850.c $(CORE) $(HDRS) $(AMILIB)
	$(CC) $(CFLAGS) -DSHOT fta850.c $(CORE) $(CAPTURE) $(KEEP) $(AMILINK) $(LIBS) -lpng -lz -lm -o fta850_shot

#
# Desktop entries (Linux): "FTA-850" and "FTA-850 (simulated)" in the
# application menu. They run the program from this folder, so it finds
# ami.cfg; run "make install" again if you move the folder.
#
APPDIR = $(HOME)/.local/share/applications

install: fta850
	mkdir -p $(APPDIR)
	sed 's|@DIR@|$(CURDIR)|' fta850.desktop > $(APPDIR)/fta850.desktop
	sed 's|@DIR@|$(CURDIR)|' fta850-sim.desktop > $(APPDIR)/fta850-sim.desktop
	-update-desktop-database $(APPDIR) 2>/dev/null

uninstall:
	rm -f $(APPDIR)/fta850.desktop $(APPDIR)/fta850-sim.desktop
	-update-desktop-database $(APPDIR) 2>/dev/null

#
# Station data: the OurAirports public domain CSV files (ourairports.com/data),
# refreshed from their daily GitHub copy. See data/README.md.
#
OADATA = https://davidmegginson.github.io/ourairports-data

data:
	mkdir -p data
	for f in airports.csv airport-frequencies.csv navaids.csv; do \
	    curl -sSL -o data/$$f.new $(OADATA)/$$f && mv data/$$f.new data/$$f; done
	@echo "Station data updated from $(OADATA)"

#
# The manual: doc/manual.md, and a PDF of it printed by Google Chrome from
# an HTML rendering (doc/md2html.py).
#
manual: doc/manual.pdf

doc/manual.pdf: doc/manual.md doc/md2html.py
	python3 doc/md2html.py doc/manual.md > doc/manual.html
	cd doc && google-chrome --headless=new --disable-gpu --no-sandbox --no-pdf-header-footer \
	    --print-to-pdf=manual.pdf manual.html 2>/dev/null

clean:
	rm -rf fta850 fta850cli fta850_shot *.dSYM

.PHONY: all sim shot data manual install uninstall clean
