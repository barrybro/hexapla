# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`hexapla` is a C command-line tool that looks up Bible verses in Greek
(Septuagint / SBL Greek New Testament), Latin (Clementine Vulgate), and
English (Douay-Rheims) side by side. Books are displayed under
Douay-Rheims names/numbering (e.g. 1-4 Kings, Vulgate Psalm numbering),
but modern names/abbreviations are accepted as input aliases.

## Build and run

```
make                  # builds ./hexapla (runs from source tree, finds ./data)
make READLINE=0       # build without the GNU readline dependency
sudo make install     # installs to $(PREFIX)/bin and $(PREFIX)/share/hexapla (PREFIX=/usr/local)
make uninstall
make clean
```

No test suite or linter is configured. To sanity-check a change, run the
binary directly against real references, e.g.:

```
./hexapla John 3:16
./hexapla -g Matt 5:3-12
./hexapla -s 'vale of tears'
./hexapla -L
```

Piping (as above) exercises the non-interactive path. The interactive
prompt only engages when stdin *and* stdout are both TTYs, so testing it
from a tool call needs a pty — drive it with `pty.fork()` from Python,
setting `PAGER=cat` to keep output inline. Set `HEXAPLA_HISTFILE` to a
scratch path so tests don't touch the real history.

`HEXAPLA_DATA` env var overrides where the `.tsv` data files are read
from (useful for testing against a modified copy of `data/`).

## Architecture

Two source files, one header:

- `src/books.c` / `src/books.h` — the canonical book list (`books[]` in
  Douay-Rheims order) and name resolution. Each `Book` entry has a
  display name, per-edition source name (`src[ED_GRB]`, `src[ED_VUL]`,
  `src[ED_DRB]` — NULL if that edition lacks the book), comma-separated
  aliases, and an NT flag (controls whether the Greek column is
  labelled "GNT" vs "LXX"). `book_find()` resolves user input
  (exact/alias/unique-prefix match); `book_from_source()` maps a raw
  TSV book-name string to a canonical index, applying the
  Septuagint→Vulgate remaps below.
- `src/hexapla.c` — everything else: TSV loading, verse storage,
  reference parsing, wrapped/paged/colorized output, search, `-L`
  listing, and `main()`.

### Data files (`data/*.tsv`)

Three tab-separated files, one row per verse: `book\tabbrev\t...\tchapter\tverse\ttext`
(a row is 6 fields normally; two books in `grb.tsv` are missing a tab
and are handled as a special case in `load_edition()`). Text sourced
from three upstream projects with inconsistent conventions (see
README's "Text sources" section) — `load_edition()` in `hexapla.c`
absorbs those inconsistencies (BOM stripping, a duplicated Genesis 1:1
row in `grb.tsv`, SBLGNT critical sigla stripped from Greek text) so
the rest of the program can treat all three editions uniformly.

### Greek Unicode normalization

`grb.tsv` is two upstream texts concatenated, and they disagree on how to
spell an accent: the **Septuagint half uses Greek Extended "oxia"**
characters (U+1F71, U+1F77, …) while the **SBL New Testament half uses
the canonically equivalent "tonos"** ones (U+03AC, U+03AF, …). These
render identically but differ in bytes, so before this was fixed a
search matched one half of the bible and silently skipped the other —
`κυρίου` found 228 verses while missing Genesis 6:8, which plainly
contains it.

`normalize_greek()` maps text to NFC on load (and `run_command()` applies
it to the `-s` pattern) using `greek_nfc[]`, the canonical singleton
decompositions of the two Greek blocks. Both must stay in sync: **the
pattern has to be normalized wherever the corpus is**, or Greek typed the
other way silently fails again.

Facts that make this approach sound, worth rechecking if the data is ever
updated: the corpus contains **no combining marks**, so singleton mapping
is all NFC requires (no decomposition/recomposition pass); and every
mapping is same-length-or-shorter in UTF-8 (3-byte Greek Extended → 2-byte
Greek), so it rewrites safely in place like `strip_sigla`. `vul.tsv` and
`drb.tsv` are already pure NFC and are left alone.

To re-verify after a data update, compare against Python's Unicode tables:

```sh
./hexapla -pg -s ε | python3 -c 'import sys,unicodedata as u; t=sys.stdin.read(); print(u.normalize("NFC",t)==t)'
```

### Septuagint/Vulgate book folding

`grb.tsv` (Greek) splits some texts that the Vulgate tradition treats
as chapters of a larger book — Susanna and Bel and the Dragon fold
into Daniel (as chs. 13-14), the Letter of Jeremiah folds into Baruch
(as ch. 6). This is handled by `grb_remap[]` in `books.c`, which
supplies a chapter offset (`choff`) added to every verse's chapter
number when loading. `grb_skip[]` lists duplicate/alternate text-forms
in `grb.tsv` (e.g. Old Greek Daniel where Theodotion is the received
text, Codex Vaticanus/Sinaiticus variants) that are dropped entirely
in favor of the mapped text.

When adding or adjusting a book, update `books[]` in `books.c` (display
name, per-edition source string exactly as it appears in that TSV,
aliases) rather than special-casing it in `hexapla.c`.

### Verse ordering and ranges

Verses are stored per-edition as a flat sorted array; `(chapter, verse)`
pairs are packed into a single sortable long via the `KEY()` macro
(`chapter * 1000 + verse`) so ranges and chapter-only queries
(`CHAP_ALL`) compare correctly. `lookup()` walks all active editions in
lockstep by increasing key, printing one reference block per key so
columns for a given verse always line up even when an edition is
missing that verse (prints `—`) or editions' verse boundaries differ
slightly.

### Text layout

`print_wrapped()` manually decodes UTF-8 codepoints (`utf8_decode`) and
counts display columns (`cp_width`) rather than relying on the locale's
wide-character functions, so wrapping stays correct for Greek/Latin
text regardless of the caller's `LANG`. Combining marks and zero-width
characters are counted as zero-width.

### CLI conventions

- `-g`/`-l`/`-e` select and order the displayed columns (order = flag
  order on the command line); `HEXAPLA_LANGS` env var sets a default
  when no flags are given; default is all three in GRB/VUL/DRB order.
- Reference grammar (`parse_spec`): `C`, `C-C`, `C:V`, `C:V-V`,
  `C:V-C:V`.
- `parse_ref()` joins all the arguments and then splits the *string*,
  rather than trusting the shell's word boundaries, so quoting cannot
  change the meaning: `"John 3:16"`, `John 3:16` and `John3:16` all
  parse. It finds the spec by walking back over the trailing run of
  digits, `:` and `-`. This is safe only because **no book name ends in
  a digit** — if one ever did, `1 John 2` would lose its chapter. It
  also requires a non-empty book part, so a bare `22` stays an error
  rather than becoming a chapter of nothing.
- Output is piped through `$PAGER` (default `less -FRX`) when stdout is
  a terminal; color is enabled only when stdout is a terminal.
- `parse_flags()` is hand-rolled rather than getopt so the command line
  and the interactive prompt accept exactly the same syntax. `-s`
  deliberately swallows the rest of the line as its pattern, so search
  phrases need no quoting when typed at the prompt.
- Three output concerns are deliberately separate globals, and conflating
  them breaks a real case: `is_tty` gates paging and the prompt,
  `use_color` gates escapes (`is_tty` **or** forced by `-C`), and
  `plain` gates the record format. If `open_pager()` keyed off
  `use_color`, then `-C` into a pipe would spawn a pager into that pipe
  and hang.

### Line-oriented output (-p)

`print_plain()` emits `Book C:V<TAB>TAG<TAB>text`, one whole unwrapped
verse per line, for `fzf`/`grep`/`awk`/`cut`. Invariants worth keeping:

- The reference is the first field and is formatted exactly as
  `parse_ref()` accepts it, so `... | cut -f1 | xargs hexapla` round
  trips. Don't pad or decorate it.
- Missing-verse placeholder rows (`—`) are suppressed under `-p`; a
  record format should not carry rows with no text.
- `-p` implies one-shot, so it never drops into the prompt.

### Interactive prompt

`repl()` runs when stdin and stdout are both TTYs and `-1` was not
given; piped/redirected use keeps the old print-once-and-exit
behavior. Each command re-parses flags into `order[]`, and `deforder[]`
holds the session default that a bare `-el` line updates and every
other command falls back to.

Two things this mode depends on:

- **SIGPIPE is ignored program-wide.** Quitting the pager early (`q` in
  `less`) would otherwise kill the process mid-session instead of
  returning to the prompt. `lookup()` and `search()` check `ferror(out)`
  after each verse so a dead pager stops the loop immediately rather
  than grinding through a whole book.
- **Editions load lazily and stay loaded** (`ensure_loaded`), since the
  prompt can switch columns between commands and the TSVs are ~19MB
  total.

History uses readline when available (`HAVE_READLINE`), falling back to
an fgets reader that still appends to the history file but has no
arrow-key recall. History lives at `$HEXAPLA_HISTFILE`, else
`$XDG_STATE_HOME/hexapla/history`, else `~/.local/state/hexapla/history`.

Note that `split_line()` tokenizes **in place**, so anything wanting the
line as typed (the history entry) must copy it first.

### Banner

`print_logo()` assembles the open-book box at runtime and pads each page
with `pad_print()`/`disp_width()` — the same UTF-8 column counting used
for wrapping — so the polytonic Greek inside stays aligned. Never
hand-count padding for those strings. The info column beside it is
dropped when the terminal is too narrow.
