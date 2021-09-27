CFLAGS=-O2 -Wall -Wextra -lX11 -lpci
PREFIX=$(HOME)/.local
CACHE=$(shell if [ "$$XDG_CACHE_HOME" ]; then echo "$$XDG_CACHE_HOME"; else echo "$$HOME"/.cache; fi)

all: christmasfetch

clean:
	rm -f christmasfetch $(CACHE)/christmasfetch

christmasfetch: christmasfetch.c christmasfetch.h config.h
	$(eval battery_path := $(shell ./config_scripts/battery_config.sh))
	$(CC) christmasfetch.c -o christmasfetch $(CFLAGS) -D $(battery_path)
	strip christmasfetch

install: christmasfetch
	mkdir -p $(PREFIX)/bin
	install ./christmasfetch $(PREFIX)/bin/christmasfetch
