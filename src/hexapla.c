/* hexapla - look up Bible verses in Greek (LXX/GNT), Latin (Vulgate)
 * and English (Douay-Rheims) side by side on the command line.
 *
 * Text data comes from the TSV files of the lukesmithxyz/vul,
 * lukesmithxyz/grb and thenewmantis/drb projects. */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

#include "books.h"

#ifndef DATADIR
#define DATADIR "/usr/local/share/hexapla"
#endif

/* verse key packs (chapter, verse) so ranges compare as one number */
#define KEY(c, v)   ((long)(c) * 1000L + (v))
#define CHAP_ALL(c) KEY(c, 999)

#define HISTMAX 500     /* entries kept in the on-disk history file */

typedef struct {
	int book;
	int chap, verse;
	const char *text;
} Verse;

typedef struct {
	Verse *v;
	size_t n, cap;
	char *raw;              /* whole TSV file; verse texts point into it */
	int loaded;
	int hasbook[128];
} Edition;

static Edition eds[NED];
static const char *edfile[NED]  = { "grb.tsv", "vul.tsv", "drb.tsv" };
static const char *edlabel[NED] = { "LXX", "VUL", "DRB" };
static const char *edcolor[NED] = { "\033[36m", "\033[33m", "\033[32m" };

static int is_tty;      /* stdout is a terminal: controls paging and the prompt */
static int use_color;   /* is_tty, or forced on with -C for 'fzf --ansi' */
static int plain;       /* -p: one verse per line, tab separated */
static int termwidth = 80;

/* active editions, in the order their columns are displayed */
static int order[NED];
static int norder;

/* column order the interactive prompt falls back to; a per-command flag
 * such as "-g John 3:16" overrides it for that one command only */
static int deforder[NED];
static int defnorder;

/* -w from the command line: the session's baseline width, which a -w on a
 * single prompt command overrides for that command only */
static int defwidth;

/* The terminal can be resized between prompt commands, so measure it afresh
 * for each one rather than trusting the size we saw at startup.  With no
 * terminal to measure (output redirected) the width is whatever -w said, or
 * the 80 columns everything else assumes. */
static void measure_term(void)
{
	struct winsize ws;

	termwidth = 80;
	if (is_tty && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		termwidth = ws.ws_col;
	if (defwidth)
		termwidth = defwidth;
}

static void add_lang(int ed)
{
	int i;
	for (i = 0; i < norder; i++)
		if (order[i] == ed)
			return;
	order[norder++] = ed;
}

static const char *color(const char *c) { return use_color ? c : ""; }
#define C_RESET color("\033[0m")
#define C_BOLD  color("\033[1m")
#define C_DIM   color("\033[2m")

static void die(const char *msg)
{
	fprintf(stderr, "hexapla: %s\n", msg);
	exit(1);
}

/* ---------- UTF-8 ---------- */

/* Decode one UTF-8 sequence and return the bytes consumed.  The data files
 * are always UTF-8, so decoding them here keeps the layout correct whatever
 * locale the caller happens to be in; mbrtowc would fall back to single
 * bytes under LANG=C and mis-measure every Greek and accented Latin word.
 * Malformed bytes are consumed one at a time as U+FFFD so we always advance. */
static int utf8_decode(const char *s, unsigned *cp)
{
	const unsigned char *u = (const unsigned char *)s;
	unsigned c = u[0];
	int len, i;

	if (c < 0x80) {
		*cp = c;
		return 1;
	} else if ((c & 0xe0) == 0xc0) {
		c &= 0x1f;
		len = 2;
	} else if ((c & 0xf0) == 0xe0) {
		c &= 0x0f;
		len = 3;
	} else if ((c & 0xf8) == 0xf0) {
		c &= 0x07;
		len = 4;
	} else {
		*cp = 0xfffd;
		return 1;
	}

	for (i = 1; i < len; i++) {
		if ((u[i] & 0xc0) != 0x80) {
			*cp = 0xfffd;
			return 1;
		}
		c = (c << 6) | (u[i] & 0x3f);
	}
	*cp = c;
	return len;
}

/* Encode one codepoint, returning the bytes written.  Only the one to
 * three byte forms are needed: every codepoint this writes is a
 * normalization target below U+FFFF. */
static int utf8_encode(char *out, unsigned cp)
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xc0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3f));
		return 2;
	}
	out[0] = (char)(0xe0 | (cp >> 12));
	out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
	out[2] = (char)(0x80 | (cp & 0x3f));
	return 3;
}

/* ---------- data loading ---------- */

static const char *datadir(void)
{
	static char buf[PATH_MAX];
	const char *cands[3];
	int i;

	cands[0] = getenv("HEXAPLA_DATA");
	cands[1] = DATADIR;
	cands[2] = "data";
	for (i = 0; i < 3; i++) {
		if (!cands[i])
			continue;
		snprintf(buf, sizeof buf, "%s/%s", cands[i], edfile[ED_DRB]);
		if (access(buf, R_OK) == 0)
			return cands[i];
	}
	die("cannot find data files (set HEXAPLA_DATA or run 'make install')");
	return NULL;
}

static void addverse(Edition *e, int book, int chap, int verse, const char *text)
{
	if (e->n) {
		Verse *p = &e->v[e->n - 1];
		/* grb.tsv opens with a duplicated Genesis 1:1 */
		if (p->book == book && p->chap == chap && p->verse == verse)
			return;
	}
	if (e->n == e->cap) {
		e->cap = e->cap ? e->cap * 2 : 40000;
		e->v = realloc(e->v, e->cap * sizeof *e->v);
		if (!e->v)
			die("out of memory");
	}
	e->v[e->n++] = (Verse){ book, chap, verse, text };
	e->hasbook[book] = 1;
}

/* Remove SBLGNT critical sigla (U+2E00-U+2E0F: ⸀ ⸂ ⸃ ...) in place;
 * without the apparatus they are just noise. */
static void strip_sigla(char *s)
{
	char *w = s;
	while (*s) {
		if ((unsigned char)s[0] == 0xe2 && (unsigned char)s[1] == 0xb8 &&
		    ((unsigned char)s[2] & 0xf0) == 0x80) {
			s += 3;
			continue;
		}
		*w++ = *s++;
	}
	*w = '\0';
}

/* grb.tsv is two upstream texts joined end to end, and they disagree on
 * how to spell an accent: the Septuagint half uses the Greek Extended
 * "oxia" characters, the SBL New Testament half the canonically
 * equivalent "tonos" ones.  They render identically but differ in bytes,
 * so a search for a word spelled one way silently skipped every verse
 * that spelled it the other -- searching for kuriou found 228 verses
 * while missing Genesis 6:8, which plainly contains it.
 *
 * These are exactly the canonical singleton decompositions of the two
 * Greek blocks, i.e. NFC.  Normalizing the text on load and the pattern
 * before matching makes the corpus internally consistent and lets either
 * spelling find both halves.  The corpus has no combining marks, so
 * these singletons are all that NFC needs here. */
static const struct { unsigned from, to; } greek_nfc[] = {
	{ 0x0374, 0x02B9 },  /* GREEK NUMERAL SIGN */
	{ 0x037E, 0x003B },  /* GREEK QUESTION MARK */
	{ 0x0387, 0x00B7 },  /* GREEK ANO TELEIA */
	{ 0x1F71, 0x03AC },  /* SMALL ALPHA WITH OXIA */
	{ 0x1F73, 0x03AD },  /* SMALL EPSILON WITH OXIA */
	{ 0x1F75, 0x03AE },  /* SMALL ETA WITH OXIA */
	{ 0x1F77, 0x03AF },  /* SMALL IOTA WITH OXIA */
	{ 0x1F79, 0x03CC },  /* SMALL OMICRON WITH OXIA */
	{ 0x1F7B, 0x03CD },  /* SMALL UPSILON WITH OXIA */
	{ 0x1F7D, 0x03CE },  /* SMALL OMEGA WITH OXIA */
	{ 0x1FBB, 0x0386 },  /* CAPITAL ALPHA WITH OXIA */
	{ 0x1FBE, 0x03B9 },  /* GREEK PROSGEGRAMMENI */
	{ 0x1FC9, 0x0388 },  /* CAPITAL EPSILON WITH OXIA */
	{ 0x1FCB, 0x0389 },  /* CAPITAL ETA WITH OXIA */
	{ 0x1FD3, 0x0390 },  /* SMALL IOTA WITH DIALYTIKA AND OXIA */
	{ 0x1FDB, 0x038A },  /* CAPITAL IOTA WITH OXIA */
	{ 0x1FE3, 0x03B0 },  /* SMALL UPSILON WITH DIALYTIKA AND OXIA */
	{ 0x1FEB, 0x038E },  /* CAPITAL UPSILON WITH OXIA */
	{ 0x1FEE, 0x0385 },  /* GREEK DIALYTIKA AND OXIA */
	{ 0x1FEF, 0x0060 },  /* GREEK VARIA */
	{ 0x1FF9, 0x038C },  /* CAPITAL OMICRON WITH OXIA */
	{ 0x1FFB, 0x038F },  /* CAPITAL OMEGA WITH OXIA */
	{ 0x1FFD, 0x00B4 },  /* GREEK OXIA */
};

/* Rewrite s in place into NFC.  Every mapping above is the same length or
 * shorter in UTF-8 (a 3-byte Greek Extended character becoming a 2-byte
 * one), so the result always fits.  Unmapped text is copied through as
 * raw bytes rather than re-encoded, because re-encoding would turn a
 * malformed byte into a 3-byte U+FFFD and overrun. */
static void normalize_greek(char *s)
{
	char *w = s;

	while (*s) {
		unsigned cp;
		int len = utf8_decode(s, &cp);
		size_t k;

		/* every mapped codepoint lives in one of these two spans, so
		 * ordinary Greek letters skip the search entirely */
		if ((cp >= 0x0374 && cp <= 0x0387) ||
		    (cp >= 0x1F71 && cp <= 0x1FFD)) {
			for (k = 0; k < sizeof greek_nfc / sizeof greek_nfc[0]; k++)
				if (greek_nfc[k].from == cp) {
					w += utf8_encode(w, greek_nfc[k].to);
					goto next;
				}
		}
		memmove(w, s, len);
		w += len;
next:
		s += len;
	}
	*w = '\0';
}

static void load_edition(int ed, const char *dir)
{
	Edition *e = &eds[ed];
	char path[PATH_MAX];
	FILE *f;
	long size;
	char *p, *line;
	const char *prevname = "", *prevtok = "";
	int prevbook = -1, prevoff = 0, mergedchap = 0;

	snprintf(path, sizeof path, "%s/%s", dir, edfile[ed]);
	f = fopen(path, "r");
	if (!f)
		die(path);
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	e->raw = malloc(size + 1);
	if (!e->raw || fread(e->raw, 1, size, f) != (size_t)size)
		die("read error");
	e->raw[size] = '\0';
	fclose(f);

	p = e->raw;
	if (strncmp(p, "\xef\xbb\xbf", 3) == 0)
		p += 3; /* UTF-8 BOM */

	while (p && *p) {
		char *fields[6], *text;
		int nf = 0, book, choff, chap, verse;

		line = p;
		p = strchr(p, '\n');
		if (p) {
			if (p > line && p[-1] == '\r')
				p[-1] = '\0';
			*p++ = '\0';
		}
		fields[nf++] = line;
		while (nf < 6 && (line = strchr(line, '\t'))) {
			*line++ = '\0';
			fields[nf++] = line;
		}
		if (nf < 5)
			continue;

		if (strcmp(fields[0], prevname) == 0) {
			book = prevbook;
			choff = prevoff;
		} else {
			book = book_from_source(ed, fields[0], &choff);
			prevname = fields[0];
			prevbook = book;
			prevoff = choff;
			prevtok = "";
			mergedchap = 0;
		}
		if (book < 0)
			continue;

		if (nf < 6) {
			/* Hosea and Zechariah in grb.tsv lack the tab between the
			 * book number and the chapter, so "28" and "1" arrive as
			 * the single field "281" and the row is a column short.
			 * The chapters still appear in order, so count them off as
			 * that joined value changes instead of trying to guess
			 * where the book number ends. */
			if (strcmp(fields[2], prevtok) != 0) {
				prevtok = fields[2];
				mergedchap++;
			}
			chap = mergedchap;
			verse = atoi(fields[3]);
			text = fields[4];
		} else {
			chap = atoi(fields[3]);
			verse = atoi(fields[4]);
			text = fields[5];
		}
		if (ed == ED_GRB) {
			strip_sigla(text);
			normalize_greek(text);
		}
		addverse(e, book, chap + choff, verse, text);
	}
}

/* Editions are loaded on first use and kept: the interactive prompt can
 * switch columns between commands without paying to re-read a 10MB TSV. */
static const char *datapath;

static void ensure_loaded(int ed)
{
	if (eds[ed].loaded)
		return;
	load_edition(ed, datapath);
	eds[ed].loaded = 1;
}

/* ---------- output ---------- */

/* Combining marks and zero-width formatting characters sit on the preceding
 * character and occupy no column of their own.  Everything these texts
 * actually contain -- Greek, polytonic Greek, Latin, punctuation -- is a
 * single column, so no East Asian wide ranges are needed here. */
static int cp_width(unsigned cp)
{
	if ((cp >= 0x0300 && cp <= 0x036f) ||
	    (cp >= 0x1ab0 && cp <= 0x1aff) ||
	    (cp >= 0x20d0 && cp <= 0x20ff) ||
	    (cp >= 0xfe20 && cp <= 0xfe2f) ||
	    (cp >= 0x200b && cp <= 0x200f) ||
	    cp == 0xfeff)
		return 0;
	return 1;
}

static int disp_width(const char *s)
{
	unsigned cp;
	int w = 0;

	while (*s) {
		s += utf8_decode(s, &cp);
		w += cp_width(cp);
	}
	return w;
}

/* Wrap `text` to the terminal width with a hanging indent, counting
 * display columns of UTF-8 text rather than bytes. */
static void print_wrapped(FILE *out, int indent, const char *text)
{
	int width = termwidth - indent;
	int col = 0, linestart = 1;
	const char *word = text;

	if (width < 20)
		width = 20;
	while (*word) {
		const char *end = word;
		int wlen = 0;

		while (*end == ' ')
			end++;
		word = end;
		while (*end && *end != ' ') {
			unsigned cp;

			end += utf8_decode(end, &cp);
			wlen += cp_width(cp);
		}
		if (word == end)
			break;
		/* Track whether the line already holds a word separately from
		 * its width: a word whose width counts as zero must still be
		 * followed by a space. */
		if (!linestart && col + 1 + wlen > width) {
			fprintf(out, "\n%*s", indent, "");
			col = 0;
		} else if (!linestart) {
			fputc(' ', out);
			col++;
		}
		fwrite(word, 1, end - word, out);
		col += wlen;
		linestart = 0;
		word = end;
	}
	fputc('\n', out);
}

static void print_verse_line(FILE *out, int ed, const char *text)
{
	fprintf(out, "%s%s%s │ ", color(edcolor[ed]), edlabel[ed], C_RESET);
	if (text)
		print_wrapped(out, 6, text);
	else
		fprintf(out, "%s—%s\n", C_DIM, C_RESET);
}

static const char *greek_label(int book)
{
	return books[book].nt ? "GNT" : "LXX";
}

/* One whole verse per line, "Book C:V<TAB>TAG<TAB>text", never wrapped.
 * Line-oriented tools -- fzf, grep, awk, cut -- work a line at a time, so
 * the human layout (a reference above its wrapped columns) gives them
 * fragments with no reference attached.  Keeping the reference first and
 * unpadded means a selected line feeds straight back into hexapla. */
static void print_plain(FILE *out, int book, int chap, int verse, int ed,
                        const char *text)
{
	fprintf(out, "%s%s %d:%d%s\t%s%s%s\t%s\n",
	        C_BOLD, books[book].display, chap, verse, C_RESET,
	        color(edcolor[ed]), edlabel[ed], C_RESET, text);
}

/* ---------- paging ---------- */

/* The pager gets its own process; quitting it early (q in less) leaves us
 * writing to a closed pipe.  SIGPIPE is ignored program-wide so that just
 * fails the write instead of killing us mid-session -- output loops watch
 * ferror() to stop early rather than grinding through a whole book. */
static FILE *open_pager(void)
{
	const char *pager;
	FILE *pg;

	if (!is_tty)
		return stdout;
	pager = getenv("PAGER");
	pg = popen(pager && *pager ? pager : "less -FRX", "w");
	return pg ? pg : stdout;
}

static void close_pager(FILE *out)
{
	if (out != stdout)
		pclose(out);
}

/* ---------- lookup ---------- */

typedef struct {
	int book;
	long from, to;          /* inclusive verse-key range */
} Ref;

/* Grammar: C | C-C | C:V | C:V-V | C:V-C:V */
static int parse_spec(const char *s, Ref *r)
{
	long c1, v1 = -1, c2 = -1, v2 = -1;
	char *p;

	c1 = strtol(s, &p, 10);
	if (p == s || c1 <= 0)
		return 0;
	if (*p == ':') {
		s = p + 1;
		v1 = strtol(s, &p, 10);
		if (p == s)
			return 0;
	}
	if (*p == '-') {
		s = p + 1;
		c2 = strtol(s, &p, 10);
		if (p == s)
			return 0;
		if (*p == ':') {
			s = p + 1;
			v2 = strtol(s, &p, 10);
			if (p == s)
				return 0;
		} else if (v1 >= 0) {
			/* "3:16-18" means verses 16-18 of chapter 3 */
			v2 = c2;
			c2 = c1;
		}
	}
	if (*p)
		return 0;
	r->from = v1 >= 0 ? KEY(c1, v1) : KEY(c1, 0);
	if (c2 >= 0)
		r->to = v2 >= 0 ? KEY(c2, v2) : CHAP_ALL(c2);
	else
		r->to = v1 >= 0 ? KEY(c1, v1) : CHAP_ALL(c1);
	return r->from <= r->to;
}

/* Join the arguments and split the result into a book and an optional
 * chapter/verse spec.  Splitting the joined string rather than trusting
 * the shell's word boundaries means it no longer matters how the
 * reference was quoted: "John 3:16" as one argument works as well as two,
 * and John3:16 works with no space at all.  The spec is the trailing run
 * of digits and the punctuation a spec can contain; no book name ends in
 * a digit, so this never eats part of one ("1 John" keeps its 1). */
static int parse_ref(int argc, char **argv, Ref *r)
{
	char query[256] = "", norm[256];
	int i;
	size_t len, cut;

	for (i = 0; i < argc; i++) {
		if (i)
			strncat(query, " ", sizeof query - strlen(query) - 1);
		strncat(query, argv[i], sizeof query - strlen(query) - 1);
	}

	r->from = 0;
	r->to = LONG_MAX;

	len = strlen(query);
	for (cut = len; cut > 0; cut--) {
		char c = query[cut - 1];
		if (!isdigit((unsigned char)c) && c != ':' && c != '-')
			break;
	}
	/* cut > 0 keeps a bare "22" from being read as a spec with no book */
	if (cut > 0 && cut < len) {
		char book[256];
		Ref split = { 0 };

		memcpy(book, query, cut);
		book[cut] = '\0';
		if (parse_spec(query + cut, &split)) {
			normalize(book, norm, sizeof norm);
			r->book = book_find(norm);
			if (r->book >= 0) {
				r->from = split.from;
				r->to = split.to;
				return 1;
			}
		}
	}

	/* no spec, or the part before it named no book: try the whole thing */
	normalize(query, norm, sizeof norm);
	r->book = book_find(norm);
	if (r->book < 0) {
		fprintf(stderr, "hexapla: unknown book '%s'"
		        " (see 'hexapla -L' for the list)\n", query);
		return 0;
	}
	return 1;
}

static size_t range_start(Edition *e, const Ref *r)
{
	size_t i;
	for (i = 0; i < e->n; i++)
		if (e->v[i].book == r->book && KEY(e->v[i].chap, e->v[i].verse) >= r->from)
			return i;
	return e->n;
}

static int in_range(Edition *e, size_t i, const Ref *r)
{
	return i < e->n && e->v[i].book == r->book &&
	       KEY(e->v[i].chap, e->v[i].verse) <= r->to;
}

static int lookup(FILE *out, const Ref *r)
{
	size_t idx[NED];
	int i, ed, printed = 0;

	for (i = 0; i < norder; i++)
		idx[order[i]] = range_start(&eds[order[i]], r);

	for (;;) {
		long key = LONG_MAX;
		int chap = 0, verse = 0;

		for (i = 0; i < norder; i++) {
			ed = order[i];
			if (in_range(&eds[ed], idx[ed], r)) {
				Verse *v = &eds[ed].v[idx[ed]];
				if (KEY(v->chap, v->verse) < key) {
					key = KEY(v->chap, v->verse);
					chap = v->chap;
					verse = v->verse;
				}
			}
		}
		if (key == LONG_MAX)
			break;

		if (!plain) {
			if (printed)
				fputc('\n', out);
			fprintf(out, "%s%s %d:%d%s\n", C_BOLD,
			        books[r->book].display, chap, verse, C_RESET);
		}
		for (i = 0; i < norder; i++) {
			ed = order[i];
			if (in_range(&eds[ed], idx[ed], r) &&
			    KEY(eds[ed].v[idx[ed]].chap, eds[ed].v[idx[ed]].verse) == key) {
				edlabel[ED_GRB] = greek_label(r->book);
				if (plain)
					print_plain(out, r->book, chap, verse, ed,
					            eds[ed].v[idx[ed]].text);
				else
					print_verse_line(out, ed, eds[ed].v[idx[ed]].text);
				while (in_range(&eds[ed], idx[ed], r) &&
				       KEY(eds[ed].v[idx[ed]].chap, eds[ed].v[idx[ed]].verse) == key)
					idx[ed]++;
			} else if (!plain && eds[ed].hasbook[r->book]) {
				/* a placeholder row is for reading, not for piping */
				print_verse_line(out, ed, NULL);
			}
		}
		printed++;
		if (ferror(out))        /* pager quit on us */
			break;
	}
	return printed;
}

/* ---------- search and listing ---------- */

static void search(FILE *out, const char *pat)
{
	int j, ed;
	size_t i;

	for (j = 0; j < norder; j++) {
		ed = order[j];
		for (i = 0; i < eds[ed].n; i++) {
			Verse *v = &eds[ed].v[i];
			if (!strcasestr(v->text, pat))
				continue;
			edlabel[ED_GRB] = greek_label(v->book);
			if (plain) {
				print_plain(out, v->book, v->chap, v->verse, ed,
				            v->text);
			} else {
				fprintf(out, "%s%s %d:%d%s\n", C_BOLD,
				        books[v->book].display, v->chap,
				        v->verse, C_RESET);
				print_verse_line(out, ed, v->text);
				fputc('\n', out);
			}
			if (ferror(out))
				return;
		}
	}
}

static void list_books(FILE *out)
{
	int i, ed;

	if (plain) {
		for (i = 0; i < nbooks; i++) {
			int first = 1;

			fprintf(out, "%s\t", books[i].display);
			for (ed = 0; ed < NED; ed++) {
				edlabel[ED_GRB] = greek_label(i);
				if (!eds[ed].hasbook[i])
					continue;
				fprintf(out, "%s%s", first ? "" : ",", edlabel[ed]);
				first = 0;
			}
			fprintf(out, "\t%s\n", books[i].aliases);
		}
		return;
	}
	for (i = 0; i < nbooks; i++) {
		fprintf(out, "%s%-24s%s ", C_BOLD, books[i].display, C_RESET);
		for (ed = 0; ed < NED; ed++) {
			edlabel[ED_GRB] = greek_label(i);
			if (eds[ed].hasbook[i])
				fprintf(out, " %s%s%s", color(edcolor[ed]),
				        edlabel[ed], C_RESET);
			else
				fprintf(out, " %s···%s", C_DIM, C_RESET);
		}
		fprintf(out, "   %s%s%s\n", C_DIM, books[i].aliases, C_RESET);
	}
}

/* ---------- command parsing ---------- */

typedef struct {
	int list;               /* -L */
	int once;               /* -1 */
	int help;               /* -h */
	int forcecolor;         /* -C */
	int width;              /* -w */
	const char *pat;        /* -s */
} Opts;

/* Parse leading option flags, shared by the command line and the prompt so
 * both accept the same syntax.  Language flags accumulate into the column
 * order; -s takes the rest of the line as its pattern, so a search phrase
 * needs no quoting when typed at the prompt.  Returns the index of the
 * first non-flag token, or -1 on an unknown flag. */
static int parse_flags(int argc, char **argv, Opts *o, char *patbuf, size_t patsz)
{
	int i, j;

	for (i = 0; i < argc; i++) {
		const char *a = argv[i];

		if (a[0] != '-' || a[1] == '\0')
			break;
		for (j = 1; a[j]; j++) {
			switch (a[j]) {
			case 'g': add_lang(ED_GRB); break;
			case 'l': add_lang(ED_VUL); break;
			case 'e': add_lang(ED_DRB); break;
			case 'L': o->list = 1; break;
			case '1': o->once = 1; break;
			case 'h': o->help = 1; break;
			case 'p': plain = 1; break;
			case 'C': o->forcecolor = 1; break;
			case 'w':
				/* -w80 or -w 80 */
				if (a[j + 1]) {
					o->width = atoi(a + j + 1);
				} else if (i + 1 < argc) {
					o->width = atoi(argv[++i]);
				}
				if (o->width < 20) {
					fprintf(stderr, "hexapla: -w needs a width of 20 or more\n");
					return -1;
				}
				j = (int)strlen(a) - 1;
				break;
			case 's':
				patbuf[0] = '\0';
				if (a[j + 1]) {
					snprintf(patbuf, patsz, "%s", a + j + 1);
				} else {
					for (i++; i < argc; i++) {
						if (patbuf[0])
							strncat(patbuf, " ",
							        patsz - strlen(patbuf) - 1);
						strncat(patbuf, argv[i],
						        patsz - strlen(patbuf) - 1);
					}
				}
				if (!patbuf[0]) {
					fprintf(stderr, "hexapla: -s needs a pattern\n");
					return -1;
				}
				o->pat = patbuf;
				return argc;    /* -s consumed the remainder */
			default:
				fprintf(stderr, "hexapla: unknown option '-%c'\n", a[j]);
				return -1;
			}
		}
	}
	return i;
}

/* Split a line into whitespace-separated tokens in place, honouring single
 * and double quotes.  Returns the token count. */
static int split_line(char *line, char **argv, int max)
{
	int n = 0;

	while (*line && n < max) {
		char *w;
		char quote = 0;

		while (*line == ' ' || *line == '\t')
			line++;
		if (!*line)
			break;
		argv[n++] = w = line;
		while (*line) {
			if (quote) {
				if (*line == quote) {
					quote = 0;
					line++;
					continue;
				}
			} else if (*line == '\'' || *line == '"') {
				quote = *line++;
				continue;
			} else if (*line == ' ' || *line == '\t') {
				break;
			}
			*w++ = *line++;
		}
		if (*line)
			line++;
		*w = '\0';
	}
	return n;
}

/* Run one already-parsed command through the pager.  Returns 0 if the
 * reference was valid but matched nothing. */
static int run_command(const Opts *o, int argc, char **argv)
{
	FILE *out;
	Ref r = { 0 };  /* only read on the lookup path, where parse_ref filled it */
	int i, ok = 1;

	if (!o->list && !o->pat && !parse_ref(argc, argv, &r))
		return 1;       /* message already printed; not a "no verses" case */

	if (o->list)
		for (i = 0; i < NED; i++)
			ensure_loaded(i);
	else
		for (i = 0; i < norder; i++)
			ensure_loaded(order[i]);

	out = open_pager();
	if (o->list) {
		list_books(out);
	} else if (o->pat) {
		/* the corpus is stored in NFC, so the pattern must match in
		 * NFC too or Greek typed the other way would never be found */
		char pat[512];

		snprintf(pat, sizeof pat, "%s", o->pat);
		normalize_greek(pat);
		search(out, pat);
	} else if (!lookup(out, &r)) {
		ok = 0;
	}
	close_pager(out);

	if (!ok)
		fprintf(stderr, "hexapla: no verses found for that reference\n");
	return 1;
}

/* ---------- welcome banner ---------- */

static void pad_print(FILE *out, const char *s, int width)
{
	int w = disp_width(s);

	fputs(s, out);
	while (w++ < width)
		fputc(' ', out);
}

#define PAGEW 20        /* display columns inside each page of the book */
#define LOGOW (2 + 1 + PAGEW + 1 + PAGEW + 1)

/* An open book with John 1:1 across its two pages, drawn fastfetch-style
 * with a column of facts beside it.  The box is assembled at runtime and
 * padded by display width so the polytonic Greek stays inside the rules. */
static void print_logo(FILE *out)
{
	static const char *pgl[] = {
		" Ἐν ἀρχῇ ἦν ὁ",
		" λόγος, καὶ ὁ",
		" λόγος ἦν πρὸς",
		" τὸν θεόν",
	};
	static const char *pgr[] = {
		" In principio erat",
		" Verbum, et Verbum",
		" erat apud Deum, et",
		" Deus erat Verbum",
	};
	static const char *key[] = { "", "", "Greek", "Latin", "English", "Books" };
	static const char *val[] = {
		"", "",
		"Septuagint · SBL GNT",
		"Clementine Vulgate",
		"Douay-Rheims",
		"73 + 5 Septuagint-only",
	};
	int wide = termwidth >= LOGOW + 3 + 30;
	int row, i;
	char rule[64];

	for (i = 0; i < PAGEW; i++)
		rule[i] = '-';
	rule[PAGEW] = '\0';

	for (row = 0; row < 6; row++) {
		if (row == 0)
			fprintf(out, "  %s.%s.%s.", C_DIM, rule, rule);
		else if (row == 5)
			fprintf(out, "  %s'%s'%s'", C_DIM, rule, rule);
		else {
			fprintf(out, "  %s|%s%s", C_DIM, C_RESET,
			        color(edcolor[ED_GRB]));
			pad_print(out, pgl[row - 1], PAGEW);
			fprintf(out, "%s|%s%s", C_DIM, C_RESET,
			        color(edcolor[ED_VUL]));
			pad_print(out, pgr[row - 1], PAGEW);
			fprintf(out, "%s|", C_DIM);
		}
		fputs(C_RESET, out);

		if (wide) {
			fputs("   ", out);
			if (row == 0)
				fprintf(out, "%shexapla%s", C_BOLD, C_RESET);
			else if (row == 1)
				fprintf(out, "%sa six-column bible%s", C_DIM, C_RESET);
			else
				fprintf(out, "%s%-8s%s %s", C_BOLD, key[row],
				        C_RESET, val[row]);
		}
		fputc('\n', out);
	}
	if (!wide)
		fprintf(out, "\n  %shexapla%s - a six-column bible\n",
		        C_BOLD, C_RESET);
	fprintf(out, "\n  Type a reference (%sJohn 3:16%s), %s?%s for help, "
	        "%sq%s to quit.\n\n",
	        C_BOLD, C_RESET, C_BOLD, C_RESET, C_BOLD, C_RESET);
}

static void repl_help(void)
{
	/* a NULL description breaks the list into groups */
	static const struct { const char *cmd, *desc; } rows[] = {
		{ "John 3:16",   "a single verse" },
		{ "Gen 1:1-10",  "a range; 1:31-2:2 crosses chapters" },
		{ "Psalms 22",   "a whole chapter" },
		{ "Gen 1-3",     "a range of chapters" },
		{ "Jude",        "a whole book" },
		{ NULL,          NULL },
		{ "-g Matt 5:3", "columns for one lookup: -g Greek, -l Latin," },
		{ "",            "-e English; -el is English then Latin" },
		{ "-el",         "on its own, sets the columns for the session" },
		{ NULL,          NULL },
		{ "-s shepherd", "search all shown columns; no quoting needed" },
		{ "-e -s mercy", "search one language only -- -s looks at just" },
		{ "",            "the columns shown, so -g/-l/-e scope it" },
		{ NULL,          NULL },
		{ "-p Gen 1",    "one verse per line, tab separated, unwrapped" },
		{ "-w 60",       "wrap to a fixed width for this lookup" },
		{ NULL,          NULL },
		{ "books",       "every book, its editions and abbreviations" },
		{ "?",           "this help" },
		{ "q",           "quit (ctrl-D works too)" },
	};
	size_t i;

	fputc('\n', stdout);
	for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
		if (!rows[i].desc)
			fputc('\n', stdout);
		else
			printf("  %s%-13s%s %s\n", C_BOLD, rows[i].cmd, C_RESET,
			       rows[i].desc);
	}
	fputs("\n  Up and down arrows recall earlier references, and that history\n"
	      "  is kept between sessions.  Run 'hexapla -h' in the shell for the\n"
	      "  command-line options and environment variables.\n\n", stdout);
}

/* ---------- history ---------- */

/* mkdir -p for the history file's parent directory. */
static void mkdir_p(char *path)
{
	char *p;

	for (p = path + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		mkdir(path, 0700);
		*p = '/';
	}
	mkdir(path, 0700);
}

/* $HEXAPLA_HISTFILE, else $XDG_STATE_HOME/hexapla/history, else
 * ~/.local/state/hexapla/history.  NULL when there is nowhere to put it. */
static const char *histfile(void)
{
	static char buf[PATH_MAX];
	static int done;
	const char *env;

	if (done)
		return buf[0] ? buf : NULL;
	done = 1;

	if ((env = getenv("HEXAPLA_HISTFILE")) && *env) {
		snprintf(buf, sizeof buf, "%s", env);
		return buf;
	}
	if ((env = getenv("XDG_STATE_HOME")) && *env)
		snprintf(buf, sizeof buf, "%s/hexapla", env);
	else if ((env = getenv("HOME")) && *env)
		snprintf(buf, sizeof buf, "%s/.local/state/hexapla", env);
	else
		return NULL;
	mkdir_p(buf);
	strncat(buf, "/history", sizeof buf - strlen(buf) - 1);
	return buf;
}

#ifdef HAVE_READLINE

static void hist_load(void)
{
	static int done;
	const char *f = histfile();

	if (done)
		return;
	done = 1;
	using_history();
	if (f)
		read_history(f);
}

/* Record a line, skipping blanks and immediate repeats. */
static void hist_add(const char *line)
{
	HIST_ENTRY *last;

	while (*line == ' ')
		line++;
	if (!*line)
		return;
	last = history_get(history_base + history_length - 1);
	if (last && strcmp(last->line, line) == 0)
		return;
	add_history(line);
}

static void hist_save(void)
{
	const char *f = histfile();

	if (!f)
		return;
	/* libedit (macOS's stand-in for readline) reports itself as 4.2 and
	 * has no append_history(); rewrite the whole file there instead. */
#if RL_READLINE_VERSION >= 0x0500
	if (append_history(1, f) != 0)
#endif
		write_history(f);
	history_truncate_file(f, HISTMAX);
}

static char *read_line(const char *prompt)
{
	return readline(prompt);
}

#else /* no readline: history still persists, but without arrow keys */

static void hist_load(void) {}

static void hist_add(const char *line)
{
	const char *f = histfile();
	FILE *h;

	while (*line == ' ')
		line++;
	if (!*line || !f || !(h = fopen(f, "a")))
		return;
	fprintf(h, "%s\n", line);
	fclose(h);
}

static void hist_save(void) {}

static char *read_line(const char *prompt)
{
	char buf[512];
	size_t n;

	fputs(prompt, stdout);
	fflush(stdout);
	if (!fgets(buf, sizeof buf, stdin))
		return NULL;
	n = strlen(buf);
	if (n && buf[n - 1] == '\n')
		buf[n - 1] = '\0';
	return strdup(buf);
}

#endif

/* ---------- interactive prompt ---------- */

static int is_word(const char *s, const char *word)
{
	return strcasecmp(s, word) == 0;
}

/* readline needs non-printing bytes bracketed by \001..\002 to keep its
 * idea of the cursor column right. */
static const char *prompt_str(void)
{
	static char buf[64];

	if (!use_color)
		return "hexapla> ";
	snprintf(buf, sizeof buf, "\001\033[1;36m\002hexapla\001\033[0m\002> ");
	return buf;
}

static void repl(void)
{
	hist_load();

	for (;;) {
		char *line = read_line(prompt_str());
		char *argv[32], whole[512];
		Opts o = { 0 };
		char patbuf[512];
		int argc, first;

		if (!line) {             /* EOF (ctrl-D) */
			fputc('\n', stdout);
			break;
		}
		/* split_line() cuts the line into tokens in place, so keep the
		 * text the user actually typed for the history */
		snprintf(whole, sizeof whole, "%s", line);
		argc = split_line(line, argv, 32);
		if (argc == 0) {
			free(line);
			continue;
		}
		if (is_word(argv[0], "q") || is_word(argv[0], "quit") ||
		    is_word(argv[0], "exit") || is_word(argv[0], ":q")) {
			free(line);
			break;
		}
		hist_add(whole);
		if (is_word(argv[0], "?") || is_word(argv[0], "help")) {
			repl_help();
			free(line);
			continue;
		}
		if (is_word(argv[0], "books")) {
			o.list = 1;
			argc = 0;
		}

		/* Flags on a command apply to that command only; a line of
		 * nothing but language flags sets the session default. */
		norder = 0;
		plain = 0;
		first = o.list ? 0 : parse_flags(argc, argv, &o, patbuf, sizeof patbuf);
		if (first < 0) {
			free(line);
			continue;
		}
		if (o.help) {
			repl_help();
			free(line);
			continue;
		}
		if (norder && !o.list && !o.pat && first >= argc) {
			memcpy(deforder, order, sizeof order);
			defnorder = norder;
			printf("  columns:");
			for (int i = 0; i < norder; i++)
				printf(" %s%s%s", color(edcolor[order[i]]),
				       order[i] == ED_GRB ? "Greek" :
				       order[i] == ED_VUL ? "Latin" : "English",
				       C_RESET);
			printf("\n");
			free(line);
			continue;
		}
		if (!norder) {
			memcpy(order, deforder, sizeof order);
			norder = defnorder;
		}
		if (!o.list && !o.pat && first >= argc) {
			free(line);
			continue;
		}
		measure_term();
		if (o.width)
			termwidth = o.width;
		run_command(&o, argc - first, argv + first);
		free(line);
	}
	hist_save();
}

/* ---------- main ---------- */

/* -h prints to stdout and succeeds so it can be paged or grepped; a usage
 * error prints to stderr and fails. */
static void usage(FILE *out, int status)
{
	fputs("usage: hexapla [options] [book [chapter[:verse[-verse]]]]\n"
	      "       hexapla [options] -s pattern\n"
	      "       hexapla [options] -L\n"
	      "\n"
	      "Look up Bible verses in Greek (Septuagint / SBL Greek NT), Latin\n"
	      "(Clementine Vulgate) and English (Douay-Rheims) side by side.\n"
	      "\n"
	      "reference (the arguments that are not options):\n"
	      "  book             a whole book:              Jude\n"
	      "  book C           a whole chapter:           Psalms 22\n"
	      "  book C-C         a range of chapters:       Gen 1-3\n"
	      "  book C:V         a single verse:            John 3:16\n"
	      "  book C:V-V       verses within a chapter:   Matt 5:3-12\n"
	      "  book C:V-C:V     a range across chapters:   Gen 1:31-2:2\n"
	      "               The book may be several words ('1 Cor'), and spaces\n"
	      "               and dots are ignored, so 1Cor and 1 Cor. are the\n"
	      "               same.  Abbreviations and any unique prefix of a name\n"
	      "               work too: Deut, Apoc, Sirach.  -L lists them all.\n"
	      "               Quoting makes no difference: \"John 3:16\" as one\n"
	      "               argument, John 3:16 as two, and John3:16 with no\n"
	      "               space are all read the same way.\n"
	      "\n"
	      "columns, and which languages -s searches:\n"
	      "  -g           Greek   (Septuagint, and the SBL Greek NT)\n"
	      "  -l           Latin   (Clementine Vulgate)\n"
	      "  -e           English (Douay-Rheims)\n"
	      "               Combine them to choose the columns and their order:\n"
	      "               -eg shows English then Greek.  The default is all\n"
	      "               three, Greek first.\n"
	      "               These also scope a search, because -s only looks at\n"
	      "               the columns being shown.  So 'hexapla -e -s mercy'\n"
	      "               searches the English alone, and 'hexapla -l -s\n"
	      "               dominus' the Latin alone.  Without one of these,\n"
	      "               -s searches all three.\n"
	      "\n"
	      "what to show:\n"
	      "  -s pattern   search verse text for a substring, rather than\n"
	      "               looking up a reference.  Takes the rest of the line,\n"
	      "               so the pattern needs no quoting: -s vale of tears.\n"
	      "               Use -g/-l/-e above to search one language only.\n"
	      "               Matching ignores case for ASCII.  Greek is matched\n"
	      "               accent for accent, but both the text and the pattern\n"
	      "               are put into Unicode NFC first, so it does not matter\n"
	      "               which of two identical-looking accent characters you\n"
	      "               type.  Searches the whole bible -- to scope\n"
	      "               one to a book or chapter, pipe -p output to grep:\n"
	      "                 hexapla -pe Gen 2 | grep -i 'the lord'\n"
	      "  -L           list every book, the editions it appears in, and\n"
	      "               every abbreviation accepted for it\n"
	      "\n"
	      "output:\n"
	      "  -p           one whole verse per line, tab separated and never\n"
	      "               wrapped: 'Book C:V<TAB>TAG<TAB>text'.  For fzf,\n"
	      "               grep, awk and cut, which read a line at a time.\n"
	      "               Implies -1\n"
	      "  -C           keep colour when the output is not a terminal, for\n"
	      "               'fzf --ansi' or 'less -R'.  Leave it off when\n"
	      "               something is parsing the fields\n"
	      "  -w cols      wrap to this width instead of the terminal's\n"
	      "               (minimum 20).  -w60 and -w 60 both work\n"
	      "  -1           print the result and exit instead of staying at\n"
	      "               the prompt.  Piped or redirected output does this\n"
	      "               anyway; -1 is for when you want it on a terminal\n"
	      "  -h           show this help and exit\n"
	      "\n"
	      "the prompt:\n"
	      "  With no reference -- or after one is shown -- hexapla stays at an\n"
	      "  interactive prompt.  Type another reference there, use the up and\n"
	      "  down arrows to recall earlier ones, '?' for help, 'q' to quit.\n"
	      "  Colour, paging and the prompt all switch off when the output is\n"
	      "  piped or redirected, so scripts still get one clean result.\n"
	      "\n"
	      "environment:\n"
	      "  HEXAPLA_LANGS      default columns and order, e.g. 'ge'; the\n"
	      "                     -g/-l/-e flags override it\n"
	      "  HEXAPLA_DATA       directory holding grb.tsv, vul.tsv, drb.tsv\n"
	      "  HEXAPLA_HISTFILE   prompt history file.  Defaults to\n"
	      "                     $XDG_STATE_HOME/hexapla/history, else\n"
	      "                     ~/.local/state/hexapla/history\n"
	      "  PAGER              pager for terminal output (default 'less -FRX')\n"
	      "\n"
	      "examples:\n"
	      "  hexapla John 3:16            a verse in all three languages\n"
	      "  hexapla Gen 1:1-10           a range of verses\n"
	      "  hexapla Gen 1:31-2:2         a range across chapters\n"
	      "  hexapla Psalms 22            a whole chapter\n"
	      "  hexapla Jude                 a whole book\n"
	      "  hexapla -g Matt 5:3-12       Greek only\n"
	      "  hexapla -el 1 Cor 13         English then Latin\n"
	      "\n"
	      "  hexapla -s vale of tears     search all three languages\n"
	      "  hexapla -e -s mercy          search the English only\n"
	      "  hexapla -l -s dominus        search the Latin only\n"
	      "  hexapla -es mercy            the same, flags bundled\n"
	      "  hexapla -pe Gen 2 | grep -i 'the lord'\n"
	      "                               search within one chapter\n"
	      "  hexapla -pe Psalms | fzf | cut -f1 | xargs hexapla\n"
	      "                               fuzzy-find a psalm, then read it\n"
	      "                               in all three languages\n"
	      "\n"
	      "Books use Douay-Rheims names and numbering (1-4 Kings; Psalms\n"
	      "numbered per the Vulgate); modern names and abbreviations like\n"
	      "1Sam are accepted on input.  See -L for the full list.\n", out);
	exit(status);
}

int main(int argc, char **argv)
{
	Opts o = { 0 };
	char patbuf[512];
	int first, interactive, ed;

	setlocale(LC_ALL, "");
	signal(SIGPIPE, SIG_IGN);

	first = parse_flags(argc - 1, argv + 1, &o, patbuf, sizeof patbuf);
	if (first < 0)
		return 1;
	if (o.help)
		usage(stdout, 0);
	argc -= first + 1;
	argv += first + 1;

	if (!norder) {
		const char *s = getenv("HEXAPLA_LANGS");
		for (; s && *s; s++)
			switch (*s) {
			case 'g': add_lang(ED_GRB); break;
			case 'l': add_lang(ED_VUL); break;
			case 'e': add_lang(ED_DRB); break;
			default:
				fprintf(stderr, "hexapla: ignoring unknown "
				        "language '%c' in HEXAPLA_LANGS\n", *s);
			}
	}
	if (!norder)
		for (ed = 0; ed < NED; ed++)
			add_lang(ed);
	memcpy(deforder, order, sizeof order);
	defnorder = norder;

	is_tty = isatty(STDOUT_FILENO);
	use_color = is_tty || o.forcecolor;
	defwidth = o.width;
	measure_term();
	/* -p is a format for other programs to read, so it stops at one
	 * result rather than dropping into the prompt */
	interactive = is_tty && isatty(STDIN_FILENO) && !o.once && !plain;

	/* Nothing to show and nowhere to prompt: explain and stop. */
	if (!interactive && !o.list && !o.pat && argc == 0)
		usage(stderr, 2);

	datapath = datadir();

	if (o.list || o.pat || argc > 0) {
		run_command(&o, argc, argv);
		if (interactive) {
			/* the reference typed on the command line is worth
			 * recalling at the prompt too */
			char joined[256] = "";
			int i;
			for (i = 0; i < argc; i++) {
				if (joined[0])
					strncat(joined, " ",
					        sizeof joined - strlen(joined) - 1);
				strncat(joined, argv[i],
				        sizeof joined - strlen(joined) - 1);
			}
			hist_load();
			if (joined[0])
				hist_add(joined);
			fprintf(stdout, "\n  %s?%s for help, %sq%s to quit.\n",
			        C_BOLD, C_RESET, C_BOLD, C_RESET);
		}
	} else if (interactive) {
		print_logo(stdout);
	}

	if (interactive)
		repl();
	return 0;
}
