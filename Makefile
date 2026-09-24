PREFIX  ?= /usr/local
DATADIR ?= $(PREFIX)/share/hexapla
CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -DDATADIR='"$(DATADIR)"'

# Line editing and persistent history at the interactive prompt.  Build with
# READLINE=0 for a dependency-free binary; the prompt then reads plain lines
# without arrow-key recall.
READLINE ?= 1
ifeq ($(READLINE),1)
CFLAGS += -DHAVE_READLINE
LDLIBS += -lreadline
endif

SRC = src/hexapla.c src/books.c
TSV = data/grb.tsv data/vul.tsv data/drb.tsv

hexapla: $(SRC) src/books.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

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
