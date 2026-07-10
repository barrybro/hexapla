#ifndef BOOKS_H
#define BOOKS_H

/* Editions, in display order. */
enum { ED_GRB, ED_VUL, ED_DRB, NED };

typedef struct {
	const char *display;   /* Douay-Rheims style display name */
	const char *src[NED];  /* book name as it appears in each TSV; NULL if absent */
	const char *aliases;   /* comma-separated, normalized (lowercase, no spaces) */
	int nt;                /* 1 if New Testament (Greek tag GNT instead of LXX) */
} Book;

extern const Book books[];
extern const int nbooks;

/* Normalize a name for matching: lowercase, strip spaces and dots. */
void normalize(const char *in, char *out, int outsz);

/* Find a book by user query (already normalized). Returns index or -1. */
int book_find(const char *norm);

/* Map a TSV source book name to a canonical index for the given edition.
 * Sets *choff to the chapter offset to add (e.g. Susanna -> Daniel 13).
 * Returns -1 if the name should be skipped. */
int book_from_source(int ed, const char *name, int *choff);

#endif
