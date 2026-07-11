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
hexapla [-gle] book [chapter[:verse[-verse]]]
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
- `hexapla -s 'vale of tears'` — search verse text (Greek search is
  byte-exact: match accents and case)
- `hexapla -L` — list all books, the editions each is available in, and
  accepted abbreviations

Output is paged through `$PAGER` (default `less`) when on a terminal.

To change the default columns without typing flags every time, set
`HEXAPLA_LANGS` in your shell rc — e.g. `export HEXAPLA_LANGS=ge` shows
Greek and English only, Greek first. Command-line flags override it.

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

To run from the source tree without installing, run it from this
directory (it finds `./data`), or point `HEXAPLA_DATA` at the data
directory.

## Text sources

- Douay-Rheims: [thenewmantis/drb](https://github.com/thenewmantis/drb)
- Clementine Vulgate: [lukesmithxyz/vul](https://github.com/lukesmithxyz/vul)
- Septuagint & SBL Greek New Testament: [lukesmithxyz/grb](https://github.com/lukesmithxyz/grb)
