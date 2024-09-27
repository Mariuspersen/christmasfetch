# Detect if it's Windows or Unix-based
ifeq ($(OS),Windows_NT)
    EXE_EXT := .exe
    RM := del
    MKDIR := mkdir
    PREFIX := $(APPDATA)\local
    CACHE := $(APPDATA)\cache
    BATTERY_PATH := 
else
    EXE_EXT :=
    RM := rm -f
    MKDIR := mkdir -p
    PREFIX := $(HOME)/.local
    CACHE := $(shell if [ "$$XDG_CACHE_HOME" ]; then echo "$$XDG_CACHE_HOME"; else echo "$$HOME"/.cache; fi)
    BATTERY_PATH := -D $(shell ./config_scripts/battery_config.sh)
endif

CFLAGS=-O2 -Wall -Wextra

all: christmasfetch$(EXE_EXT)

clean:
	$(RM) christmasfetch$(EXE_EXT) $(CACHE)/christmasfetch

christmasfetch$(EXE_EXT): christmasfetch.c christmasfetch.h config.h
	$(CC) christmasfetch.c -o christmasfetch$(EXE_EXT) $(CFLAGS) $(BATTERY_PATH)
	strip christmasfetch$(EXE_EXT)

install: christmasfetch$(EXE_EXT)
	$(MKDIR) $(PREFIX)/bin
	install ./christmasfetch$(EXE_EXT) $(PREFIX)/bin/christmasfetch$(EXE_EXT)
