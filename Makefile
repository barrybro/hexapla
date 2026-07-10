PREFIX  ?= /usr/local
DATADIR ?= $(PREFIX)/share/hexapla
CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -DDATADIR='"$(DATADIR)"'

SRC = src/hexapla.c src/books.c
TSV = data/grb.tsv data/vul.tsv data/drb.tsv

hexapla: $(SRC) src/books.h
	$(CC) $(CFLAGS) -o $@ $(SRC)

install: hexapla
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(DATADIR)
	install -m 755 hexapla $(DESTDIR)$(PREFIX)/bin/
	install -m 644 $(TSV) $(DESTDIR)$(DATADIR)/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hexapla
	rm -rf $(DESTDIR)$(DATADIR)

clean:
	rm -f hexapla

.PHONY: install uninstall clean
