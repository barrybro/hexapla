# hexapla

Look up Bible verses on the command line in Greek, Latin, and English at
once — the Septuagint / SBL Greek New Testament, the Clementine Vulgate,
and the Douay-Rheims (Challoner) translation. Named after Origen's
six-column parallel Bible.

Inspired by [Luke Smith's command line bibles](https://lukesmith.xyz/articles/command-line-bibles/),
written in C.

```
$ hexapla John 3:16
John 3:16
GNT │ Οὕτως γὰρ ἠγάπησεν ὁ θεὸς τὸν κόσμον ὥστε τὸν υἱὸν τὸν μονογενῆ ἔδωκεν,
      ἵνα πᾶς ὁ πιστεύων εἰς αὐτὸν μὴ ἀπόληται ἀλλὰ ἔχῃ ζωὴν αἰώνιον.
VUL │ Sic enim Deus dilexit mundum, ut Filium suum unigenitum daret : ut omnis
      qui credit in eum, non pereat, sed habeat vitam aeternam.
DRB │ For God so loved the world, as to give his only begotten Son; that
      whosoever believeth in him, may not perish, but may have life everlasting.
```

## Usage

```
hexapla [-gle] [book [chapter[:verse[-verse]]]]
hexapla [-gle] -s pattern
hexapla -L
```

- `hexapla Gen 1:1-10` — a range of verses
- `hexapla Psalms 22` — a whole chapter
- `hexapla Jude` — a whole book
- `hexapla Gen 1:31-2:2` — a range across chapters
- `hexapla -g Matt 5:3-12` — choose languages (`-g` Greek, `-l` Latin,
  `-e` English); flags combine, and their order sets the column order
  (`-eg` shows English then Greek)
- `hexapla -s 'vale of tears'` — search verse text. The language flags
  scope the search, because `-s` only looks at the columns being shown:
  `hexapla -e -s mercy` searches the English alone, `hexapla -l -s
  dominus` the Latin. Greek is matched accent for accent, but text and
  pattern are both normalized to Unicode NFC first, so it does not
  matter which of two identical-looking accent characters you type
- `hexapla -L` — list all books, the editions each is available in, and
  accepted abbreviations

Output is paged through `$PAGER` (default `less`) when on a terminal.

## The prompt

Run `hexapla` with no arguments and it opens at a prompt; quitting the
pager after a lookup returns there too, so you can keep reading without
restarting:

```
hexapla> John 3:16
hexapla> -g Matt 5:3-12
hexapla> -s vale of tears
```

Anything that works on the command line works at the prompt, and search
patterns need no quoting. Up and down arrows walk earlier references,
and that history is kept between sessions (in
`$XDG_STATE_HOME/hexapla/history`, or `~/.local/state/hexapla/history`;
override with `HEXAPLA_HISTFILE`). A line of nothing but language flags
— `-el` — sets the default columns for the rest of the session.

Type `?` for help, `books` to list every book, `q` (or ctrl-D) to quit.

The prompt only appears when input and output are both terminals, so
pipes and redirects still print once and exit; `-1` forces that
behaviour on a terminal too.

To change the default columns without typing flags every time, set
`HEXAPLA_LANGS` in your shell rc — e.g. `export HEXAPLA_LANGS=ge` shows
Greek and English only, Greek first. Command-line flags override it.

## Piping to other tools

The reading layout — a reference above its wrapped columns — is built for
eyes, not for `fzf` or `grep`, which work a line at a time and would only
ever see fragments. `-p` switches to one whole verse per line, tab
separated and never wrapped:

```
$ hexapla -pe Gen 1:1
Genesis 1:1	DRB	In the beginning God created heaven, and earth.
```

The fields are `reference`, `edition tag`, `text`. Because the reference
comes first and is spelled the way hexapla reads it, a chosen line goes
straight back in — fuzzy-find an English psalm, then read it in all three
languages:

```sh
hexapla -pe Psalms | fzf | cut -f1 | xargs hexapla
```

Everything else composes the usual way:

```sh
hexapla -p Gen | cut -f3            # bare text
hexapla -pL | awk -F'\t' '$2 ~ /VUL/'
hexapla -pe Matt | grep -i 'blessed'
```

Two flags help elsewhere:

- `-C` keeps colour when the output is not a terminal, for `fzf --ansi`
  or `less -R`. Leave it off when something is parsing the fields.
- `-w 60` wraps to a fixed width instead of the terminal's, for a fixed
  column in a file or a pane.

Without `-p`, piping still works and simply prints once and exits —
colour, pager and the prompt all switch off when the output is not a
terminal.

## Names and numbering

Books are displayed under their Douay-Rheims names and follow the
Vulgate/Septuagint tradition throughout:

- **1–4 Kings** are the books called 1–2 Samuel and 1–2 Kings in modern
  bibles (`1sam`, `2sam`, `3kgs`, `4kgs` are accepted as aliases).
- **Psalms use the Septuagint/Vulgate numbering** in all three columns —
  Psalm 22 is *Dominus regit me* ("The Lord is my shepherd"), not 23.
- **1 Esdras / 2 Esdras** are Ezra and Nehemiah (`ezra`, `neh` work too).
- Greek **Daniel is Theodotion's text**, with Susanna and Bel and the
  Dragon folded in as chapters 13–14 to match the Vulgate; the Letter of
  Jeremiah is Baruch 6.
- Verse numbering within a chapter follows each edition's own tradition,
  so the columns can occasionally be offset by a verse (e.g. where the
  Greek counts a psalm title or superscription as verse 1).

Modern names and common abbreviations (`Joshua`, `Rev`, `1 Chronicles`,
`Sirach`, …) are accepted on input.

## Coverage

- **DRB** — complete 73-book Catholic canon (Challoner revision).
- **LXX/GNT** — complete, plus Septuagint-only books (Esdras A,
  3–4 Machabees, Odes, Psalms of Solomon) available with Greek text only.
- **VUL** — the upstream dataset lacks Tobias, Judith, Wisdom,
  Ecclesiasticus, and Baruch; those verses show only Greek and English.

## Building

```
make
sudo make install    # installs to /usr/local, data to /usr/local/share/hexapla
```

The prompt uses GNU readline for line editing and history. To build
without that dependency, `make READLINE=0` — the prompt still works and
still records history, but without arrow-key recall.

To run from the source tree without installing, run it from this
directory (it finds `./data`), or point `HEXAPLA_DATA` at the data
directory.

## Text sources

- Douay-Rheims: [thenewmantis/drb](https://github.com/thenewmantis/drb)
- Clementine Vulgate: [lukesmithxyz/vul](https://github.com/lukesmithxyz/vul)
- Septuagint & SBL Greek New Testament: [lukesmithxyz/grb](https://github.com/lukesmithxyz/grb)
