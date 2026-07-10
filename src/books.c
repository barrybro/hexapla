#include <ctype.h>
#include <string.h>

#include "books.h"

/* Canonical Catholic canon in Douay-Rheims order, plus a few books found
 * only in the Septuagint.  src[] holds the exact book-name strings used by
 * each TSV: { grb.tsv, vul.tsv, drb.tsv }. */
const Book books[] = {
	{ "Genesis",           { "Genesis", "Genesis", "Genesis" },                    "gen,gn", 0 },
	{ "Exodus",            { "Exodus", "Exodus", "Exodus" },                       "exo,ex,exod", 0 },
	{ "Leviticus",         { "Leviticus", "Leviticus", "Leviticus" },              "lev,lv", 0 },
	{ "Numbers",           { "Numbers", "Numbers", "Numbers" },                    "num,nm", 0 },
	{ "Deuteronomy",       { "Deuteronomy", "Deuteronomy", "Deuteronomy" },        "deut,dt,deu", 0 },
	{ "Josue",             { "Joshua", "Joshua", "Josue" },                        "jos,josh,joshua", 0 },
	{ "Judges",            { "Judges", "Judges", "Judges" },                       "judg,jdg,jgs", 0 },
	{ "Ruth",              { "Ruth", "Ruth", "Ruth" },                             "ru,rt", 0 },
	{ "1 Kings",           { "1 Samuel", "1 Samuel", "1 Kings" },                  "1sam,1sa,1sm,1kgs,1samuel", 0 },
	{ "2 Kings",           { "2 Samuel", "2 Samuel", "2 Kings" },                  "2sam,2sa,2sm,2kgs,2samuel", 0 },
	{ "3 Kings",           { "1 Kings", "1 Kings", "3 Kings" },                    "3kgs,3ki", 0 },
	{ "4 Kings",           { "2 Kings", "2 King", "4 Kings" },                     "4kgs,4ki", 0 },
	{ "1 Paralipomena",    { "1 Chronicles", "1 Chronicles", "1 Paralipomena" },   "1chr,1chron,1chronicles,1par", 0 },
	{ "2 Paralipomena",    { "2 Chronicles", "2 Chronicles", "2 Paralipomena" },   "2chr,2chron,2chronicles,2par", 0 },
	{ "1 Esdras",          { "Ezra", "Ezra", "1 Esdras" },                         "ezra,ezr,esd", 0 },
	{ "2 Esdras",          { "Nehemiah", "Nehemiah", "2 Esdras" },                 "neh,nehemiah,nehemias", 0 },
	{ "Tobias",            { "Tobit", NULL, "Tobias" },                            "tob,tb,tobit", 0 },
	{ "Judith",            { "Judith", NULL, "Judith" },                           "jdt", 0 },
	{ "Esther",            { "Esther", "Esther", "Esther" },                       "est,esth", 0 },
	{ "Job",               { "Job", "Job", "Job" },                                "jb", 0 },
	{ "Psalms",            { "Psalms", "Psalms", "Psalms" },                       "ps,pss,psa,psalm", 0 },
	{ "Proverbs",          { "Proverbs", "Proverbs", "Proverbs" },                 "prov,prv,pr", 0 },
	{ "Ecclesiastes",      { "Ecclesiastes", "Ecclesiastes", "Ecclesiastes" },     "eccl,ecc,qoheleth", 0 },
	{ "Canticle of Canticles", { "Song of Solomon", "Song of Solomon", "Song of Songs" }, "song,sg,cant,canticles,songofsongs,songofsolomon", 0 },
	{ "Wisdom",            { "Wisdom of Solomon", NULL, "Wisdom" },                "wis,ws,wisd,wisdomofsolomon", 0 },
	{ "Ecclesiasticus",    { "Sirach", NULL, "Ecclesiasticus" },                   "sir,sirach,ecclus", 0 },
	{ "Isaiah",            { "Isaiah", "Isaiah", "Isaiah" },                       "isa,is,isaias", 0 },
	{ "Jeremiah",          { "Jeremiah", "Jeremiah", "Jeremiah" },                 "jer,jeremias", 0 },
	{ "Lamentations",      { "Lamentations", "Lamentations", "Lamentations" },     "lam", 0 },
	{ "Baruch",            { "Baruch", NULL, "Baruch" },                           "bar", 0 },
	{ "Ezechiel",          { "Ezekiel", "Ezekiel", "Ezechiel" },                   "eze,ezek,ezekiel", 0 },
	{ "Daniel",            { "Daniel (Theodotion)", "Daniel", "Daniel" },          "dan,dn", 0 },
	{ "Osee",              { "Hosea", "Hosea", "Osee" },                           "hos,hosea", 0 },
	{ "Joel",              { "Joel", "Joel", "Joel" },                             "jl", 0 },
	{ "Amos",              { "Amos", "Amos", "Amos" },                             "am", 0 },
	{ "Abdias",            { "Obadiah", "Obadiah", "Abdias" },                     "obad,ob,obadiah,abd", 0 },
	{ "Jonas",             { "Jonah", "Jonah", "Jonas" },                          "jon,jonah", 0 },
	{ "Michaeas",          { "Micah", "Micah", "Michaeas" },                       "mic,micah", 0 },
	{ "Nahum",             { "Nahum", "Nahum", "Nahum" },                          "nah,na", 0 },
	{ "Habacuc",           { "Habakkuk", "Habakkuk", "Habacuc" },                  "hab,habakkuk", 0 },
	{ "Sophonias",         { "Zephaniah", "Zephaniah", "Sophonias" },              "zeph,zep,zephaniah,soph", 0 },
	{ "Aggaeus",           { "Haggai", "Haggai", "Aggaeus" },                      "hag,haggai,agg", 0 },
	{ "Zacharias",         { "Zechariah", "Zechariah", "Zacharias" },              "zech,zec,zechariah,zach", 0 },
	{ "Malachias",         { "Malachi", "Malachi", "Malachias" },                  "mal,malachi", 0 },
	{ "1 Machabees",       { "1 Maccabees", "1 Maccabees", "1 Machabees" },        "1macc,1mac,1mc,1maccabees", 0 },
	{ "2 Machabees",       { "2 Maccabees", "2 Maccabees", "2 Machabees" },        "2macc,2mac,2mc,2maccabees", 0 },
	{ "Matthew",           { "Matthew", "Matthew", "Matthew" },                    "mt,matt,mat", 1 },
	{ "Mark",              { "Mark", "Mark", "Mark" },                             "mk,mr", 1 },
	{ "Luke",              { "Luke", "Luke", "Luke" },                             "lk,lu", 1 },
	{ "John",              { "John", "John", "John" },                             "jn,jo", 1 },
	{ "Acts",              { "The Acts", "The Acts", "The Acts" },                 "ac,act,theacts", 1 },
	{ "Romans",            { "Romans", "Romans", "Romans" },                       "rom,rm", 1 },
	{ "1 Corinthians",     { "1 Corinthians", "1 Corinthians", "1 Corinthians" },  "1cor,1co", 1 },
	{ "2 Corinthians",     { "2 Corinthians", "2 Corinthians", "2 Corinthians" },  "2cor,2co", 1 },
	{ "Galatians",         { "Galatians", "Galatians", "Galatians" },              "gal", 1 },
	{ "Ephesians",         { "Ephesians", "Ephesians", "Ephesians" },              "eph", 1 },
	{ "Philippians",       { "Philippians", "Philippians", "Philippians" },        "phil,php", 1 },
	{ "Colossians",        { "Colossians", "Colossians", "Colossians" },           "col", 1 },
	{ "1 Thessalonians",   { "1 Thessalonians", "1 Thessalonians", "1 Thessalonians" }, "1thess,1thes,1th", 1 },
	{ "2 Thessalonians",   { "2 Thessalonians", "2 Thessalonians", "2 Thessalonians" }, "2thess,2thes,2th", 1 },
	{ "1 Timothy",         { "1 Timothy", "1 Timothy", "1 Timothy" },              "1tim,1tm,1ti", 1 },
	{ "2 Timothy",         { "2 Timothy", "2 Timothy", "2 Timothy" },              "2tim,2tm,2ti", 1 },
	{ "Titus",             { "Titus", "Titus", "Titus" },                          "tit,ti", 1 },
	{ "Philemon",          { "Philemon", "Philemon", "Philemon" },                 "phlm,philem,phm", 1 },
	{ "Hebrews",           { "Hebrews", "Hebrews", "Hebrews" },                    "heb", 1 },
	{ "James",             { "James", "James", "James" },                          "jas,jam,jm", 1 },
	{ "1 Peter",           { "1 Peter", "1 Peter", "1 Peter" },                    "1pet,1pt,1pe", 1 },
	{ "2 Peter",           { "2 Peter", "2 Peter", "2 Peter" },                    "2pet,2pt,2pe", 1 },
	{ "1 John",            { "1 John", "1 John", "1 John" },                       "1jn,1jo", 1 },
	{ "2 John",            { "2 John", "2 John", "2 John" },                       "2jn,2jo", 1 },
	{ "3 John",            { "3 John", "3 John", "3 John" },                       "3jn,3jo", 1 },
	{ "Jude",              { "Jude", "Jude", "Jude" },                             "jud,jde", 1 },
	{ "Apocalypse",        { "Revelation", "Revelation", "Apocalypse" },           "apoc,rev,revelation,re", 1 },
	/* Septuagint-only books (Greek text only) */
	{ "Esdras A (Greek)",  { "1 Esdras", NULL, NULL },                             "esdrasa,greekesdras,gesd", 0 },
	{ "3 Machabees",       { "3 Maccabees", NULL, NULL },                          "3macc,3mac,3mc,3maccabees", 0 },
	{ "4 Machabees",       { "4 Maccabees", NULL, NULL },                          "4macc,4mac,4mc,4maccabees", 0 },
	{ "Odes",              { "Odes", NULL, NULL },                                 "ode", 0 },
	{ "Psalms of Solomon", { "Psalms of Solomon", NULL, NULL },                    "pssol,pssolomon", 0 },
};

const int nbooks = sizeof(books) / sizeof(books[0]);

/* grb.tsv carries some texts as separate books that the Vulgate tradition
 * counts as chapters of Daniel and Baruch; fold them back in. */
static const struct { const char *name; int choff; const char *canon; } grb_remap[] = {
	{ "Sussana (Theodotion)",            12, "Daniel" },
	{ "Bel and the Dragon (Theodotion)", 13, "Daniel" },
	{ "Letter of Jeremiah",               5, "Baruch" },
};

/* Duplicate text-forms in grb.tsv that we drop in favour of the mapped ones:
 * Old Greek where Theodotion is the received text, codex variants, and
 * Esdras B which duplicates the split Ezra/Nehemiah also present. */
static const char *grb_skip[] = {
	"Daniel", "Sussana", "Bel and the Dragon",
	"Judges (Vaticanus)", "Tobit (Sinaiticus)", "2 Esdras",
};

void normalize(const char *in, char *out, int outsz)
{
	int n = 0;
	for (; *in && n < outsz - 1; in++) {
		unsigned char c = (unsigned char)*in;
		if (c == ' ' || c == '.' || c == '\t')
			continue;
		out[n++] = (char)tolower(c);
	}
	out[n] = '\0';
}

static int alias_match(const char *aliases, const char *norm)
{
	const char *p = aliases;
	size_t len = strlen(norm);
	while (p && *p) {
		const char *end = strchr(p, ',');
		size_t alen = end ? (size_t)(end - p) : strlen(p);
		if (alen == len && strncmp(p, norm, len) == 0)
			return 1;
		p = end ? end + 1 : NULL;
	}
	return 0;
}

int book_find(const char *norm)
{
	char buf[64];
	int i;

	if (!*norm)
		return -1;
	/* exact match on display name or alias */
	for (i = 0; i < nbooks; i++) {
		normalize(books[i].display, buf, sizeof buf);
		if (strcmp(buf, norm) == 0 || alias_match(books[i].aliases, norm))
			return i;
	}
	/* unique prefix of a display name */
	{
		int hit = -1;
		size_t len = strlen(norm);
		for (i = 0; i < nbooks; i++) {
			normalize(books[i].display, buf, sizeof buf);
			if (strncmp(buf, norm, len) == 0) {
				if (hit >= 0)
					return -1; /* ambiguous */
				hit = i;
			}
		}
		return hit;
	}
}

int book_from_source(int ed, const char *name, int *choff)
{
	int i;

	*choff = 0;
	if (ed == ED_GRB) {
		for (i = 0; i < (int)(sizeof grb_remap / sizeof grb_remap[0]); i++)
			if (strcmp(grb_remap[i].name, name) == 0) {
				int b;
				*choff = grb_remap[i].choff;
				for (b = 0; b < nbooks; b++)
					if (strcmp(books[b].display, grb_remap[i].canon) == 0)
						return b;
				return -1;
			}
		for (i = 0; i < (int)(sizeof grb_skip / sizeof grb_skip[0]); i++)
			if (strcmp(grb_skip[i], name) == 0)
				return -1;
	}
	for (i = 0; i < nbooks; i++)
		if (books[i].src[ed] && strcmp(books[i].src[ed], name) == 0)
			return i;
	return -1;
}
