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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "books.h"

#ifndef DATADIR
#define DATADIR "/usr/local/share/hexapla"
#endif

/* verse key packs (chapter, verse) so ranges compare as one number */
#define KEY(c, v)   ((long)(c) * 1000L + (v))
#define CHAP_ALL(c) KEY(c, 999)

typedef struct {
	int book;
	int chap, verse;
	const char *text;
} Verse;

typedef struct {
	Verse *v;
	size_t n, cap;
	char *raw;              /* whole TSV file; verse texts point into it */
	int hasbook[128];
} Edition;

static Edition eds[NED];
static const char *edfile[NED]  = { "grb.tsv", "vul.tsv", "drb.tsv" };
static const char *edlabel[NED] = { "LXX", "VUL", "DRB" };
static const char *edcolor[NED] = { "\033[36m", "\033[33m", "\033[32m" };

static int use_color;
static int termwidth = 80;

/* active editions, in the order their columns are displayed */
static int order[NED];
static int norder;

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
		if (ed == ED_GRB)
			strip_sigla(text);
		addverse(e, book, chap + choff, verse, text);
	}
}

/* ---------- output ---------- */

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

static int parse_ref(int argc, char **argv, Ref *r)
{
	char query[256] = "", norm[256];
	int i, nbook = argc;

	r->from = 0;
	r->to = LONG_MAX;
	if (argc > 1 && parse_spec(argv[argc - 1], r))
		nbook = argc - 1;
	for (i = 0; i < nbook; i++) {
		if (i)
			strncat(query, " ", sizeof query - strlen(query) - 1);
		strncat(query, argv[i], sizeof query - strlen(query) - 1);
	}
	normalize(query, norm, sizeof norm);
	r->book = book_find(norm);
	if (r->book < 0) {
		fprintf(stderr, "hexapla: unknown book '%s'\n", query);
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

		if (printed)
			fputc('\n', out);
		fprintf(out, "%s%s %d:%d%s\n", C_BOLD, books[r->book].display,
		        chap, verse, C_RESET);
		for (i = 0; i < norder; i++) {
			ed = order[i];
			if (in_range(&eds[ed], idx[ed], r) &&
			    KEY(eds[ed].v[idx[ed]].chap, eds[ed].v[idx[ed]].verse) == key) {
				edlabel[ED_GRB] = greek_label(r->book);
				print_verse_line(out, ed, eds[ed].v[idx[ed]].text);
				while (in_range(&eds[ed], idx[ed], r) &&
				       KEY(eds[ed].v[idx[ed]].chap, eds[ed].v[idx[ed]].verse) == key)
					idx[ed]++;
			} else if (eds[ed].hasbook[r->book]) {
				print_verse_line(out, ed, NULL);
			}
		}
		printed++;
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
			fprintf(out, "%s%s %d:%d%s\n", C_BOLD,
			        books[v->book].display, v->chap, v->verse, C_RESET);
			print_verse_line(out, ed, v->text);
			fputc('\n', out);
		}
	}
}

static void list_books(FILE *out)
{
	int i, ed;

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

/* ---------- main ---------- */

static void usage(void)
{
	fputs("usage: hexapla [-gle] book [chapter[:verse[-verse]]]\n"
	      "       hexapla [-gle] -s pattern\n"
	      "       hexapla -L\n"
	      "\n"
	      "Look up Bible verses in Greek (Septuagint / Greek NT), Latin\n"
	      "(Vulgate) and English (Douay-Rheims).\n"
	      "\n"
	      "  -g  Greek     -l  Latin     -e  English\n"
	      "      combine to choose the columns and their order:\n"
	      "      -eg shows English then Greek (default: all three)\n"
	      "  -s  search verse text for a pattern\n"
	      "  -L  list books and the editions each is available in\n"
	      "\n"
	      "Set HEXAPLA_LANGS (e.g. 'gl' or 'elg') to change the default\n"
	      "columns and order; command-line flags override it.\n"
	      "\n"
	      "examples:\n"
	      "  hexapla John 3:16          hexapla Gen 1:1-10\n"
	      "  hexapla Psalms 22          hexapla 1 Cor 13:1-13\n"
	      "  hexapla -g Matt 5:3-12     hexapla -s 'shepherd'\n"
	      "\n"
	      "Books use Douay-Rheims names and numbering (1-4 Kings, Psalms\n"
	      "numbered per the Vulgate); modern abbreviations like 1Sam are\n"
	      "also accepted. See -L for the full list.\n", stderr);
	exit(2);
}

int main(int argc, char **argv)
{
	int opt, list = 0, i, ed;
	const char *pat = NULL, *dir;
	FILE *out = stdout;
	Ref r;

	setlocale(LC_ALL, "");

	while ((opt = getopt(argc, argv, "gleLs:h")) != -1) {
		switch (opt) {
		case 'g': add_lang(ED_GRB); break;
		case 'l': add_lang(ED_VUL); break;
		case 'e': add_lang(ED_DRB); break;
		case 'L': list = 1; break;
		case 's': pat = optarg; break;
		default: usage();
		}
	}
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
	argc -= optind;
	argv += optind;
	if (!list && !pat && argc == 0)
		usage();

	use_color = isatty(STDOUT_FILENO);
	if (use_color) {
		struct winsize ws;
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
			termwidth = ws.ws_col;
	}

	if (!list && !pat && !parse_ref(argc, argv, &r))
		return 1;

	dir = datadir();
	if (list)
		for (ed = 0; ed < NED; ed++)
			load_edition(ed, dir);
	else
		for (i = 0; i < norder; i++)
			load_edition(order[i], dir);

	if (use_color) {
		const char *pager = getenv("PAGER");
		FILE *pg = popen(pager && *pager ? pager : "less -FRX", "w");
		if (pg)
			out = pg;
	}

	if (list)
		list_books(out);
	else if (pat)
		search(out, pat);
	else if (!lookup(out, &r))
		fprintf(stderr, "hexapla: no verses found for that reference\n");

	if (out != stdout)
		pclose(out);
	return 0;
}
