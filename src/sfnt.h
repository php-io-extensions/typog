/*
 * sfnt: OpenType and TrueType files read in plain C, no Zend in it. One face of
 * a file (or of a collection) is opened, its tables read when first asked for:
 * metrics, the character map, advances, TrueType outlines, 'kern' pairs, and
 * what a catalog matches a face by.
 *
 * A font file is bytes this project did not write: every read is checked
 * against what the file and the table hold, and a short or broken one is an
 * error code with a message, never a read past the end.
 */
#ifndef TYPOG_SFNT_H
#define TYPOG_SFNT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Error codes: TypogException's code. */
enum {
	TG_OK = 0,
	TG_CANNOT_OPEN = 1,   /* the file cannot be opened or read */
	TG_NOT_A_FONT = 2,    /* not an OpenType, TrueType or collection file */
	TG_NO_FACE = 3,       /* no face by that index */
	TG_MISSING_TABLE = 4, /* a table the call needs is not there */
	TG_MALFORMED = 5,     /* a table is short or its values contradict each other */
	TG_CFF = 6,           /* the face's outlines are CFF2, or a 'CFF ' table this cannot read */
	TG_NO_GLYPH = 7,      /* no glyph by that number */
	TG_CLOSED = 8,        /* the file was closed and the table was not read before */
	TG_NO_MEMORY = 9,
	TG_NO_OUTLINES = 10   /* the face has no 'glyf', 'CFF ' or 'CFF2' table: its outlines are in a form this does not read */
};

/* Slants, as a catalog reports them. */
enum { TG_UPRIGHT = 0, TG_ITALIC = 1, TG_OBLIQUE = 2 };

#define TG_MESSAGE 320

typedef struct {
	uint8_t *data;
	uint32_t length;
	bool read;     /* asked for once: data is the table, or NULL when the face has none */
} tg_blob;

typedef struct {
	uint32_t tag, offset, length;
} tg_record;

typedef struct {
	uint32_t key;   /* left << 16 | right */
	int32_t value;
} tg_pair;

/* A variation axis of 'fvar', in its own units. */
typedef struct {
	char tag[5];
	double min, def, max;
} tg_axis;

/* An ItemVariationStore (HVAR's, MVAR's) at the face's instance; region scalars worked out once. */
typedef struct {
	const uint8_t *data;   /* inside its table's blob */
	uint32_t length;
	uint32_t regions;
	double *region;        /* regions x axes x (start, peak, end) */
	double *scalar;        /* regions; NAN until worked out */
	uint32_t subtables;
	uint32_t *subtable;    /* offsets in the store; 0 for none */
} tg_store;

typedef struct {
	int fd;              /* -1 once closed */
	char *path;
	uint32_t index;

	tg_record *records;
	uint32_t nrecords;

	tg_blob head, hhea, maxp, hmtx, cmap, loca, os2, kern, name;

	/* known once 'head', 'hhea', 'maxp' and 'hmtx' are read (tg_face_basics) */
	bool basics;
	uint32_t units_per_em, glyph_count, long_metrics;
	bool long_offsets;

	/* the character map's subtable, chosen once: map_found false when there is none to read */
	bool map_chosen;
	bool map_found;
	uint32_t map_at;
	uint16_t map_format;
	bool map_symbol;

	bool pairs_read;
	tg_pair *pairs;
	uint32_t npairs;

	/* The instance (tg_vary): varied false for a face drawn as it is. */
	tg_blob fvar, avar, hvar, mvar;
	tg_blob cff;               /* 'CFF ', read at the first outline */
	struct tg_cff *cff_reader;  /* its reader (cff.h), opened with it */
	bool varied;
	uint32_t axes;
	double *coords;            /* normalized coordinates, fvar's axis order */
	bool gvar_read, gvar_found, gvar_long;
	uint32_t gvar_shared, gvar_glyphs, gvar_array;
	double *shared_peaks;      /* gvar_shared x axes */
	double *shared_scalars;    /* gvar_shared; NAN until worked out */
	bool hvar_ready, mvar_ready;
	tg_store hvar_store, mvar_store;
	uint32_t mvar_count, mvar_size;
	int32_t *advances;         /* glyph_count; INT32_MIN until worked out */

	char message[TG_MESSAGE];
} tg_face;

/* A point of an outline: font units, on the curve or a quadratic control. */
typedef struct {
	double x, y;
	bool on;
} tg_point;

/* One step of an outline as a path draws it. */
typedef struct {
	char kind;       /* 'M', 'L', 'Q', 'C', 'Z' */
	double v[6];     /* M, L: x y. Q: cx cy x y. C: c1x c1y c2x c2y x y. */
} tg_op;

typedef struct {
	tg_op *ops;
	uint32_t n, cap;
} tg_ops;

/* What a catalog matches a face by. */
typedef struct {
	uint32_t index;
	char *family;     /* UTF-8, owned */
	int weight;       /* 1..1000 */
	int slant;        /* TG_UPRIGHT, TG_ITALIC, TG_OBLIQUE */
	int width;        /* OS/2 width class, 5 when unset */
	tg_axis *axes;    /* 'fvar''s, owned; none for a static face */
	uint32_t naxes;
	char outlines[5]; /* the outline table: "glyf", "CFF ", "CFF2", or "" for none of them */
} tg_described;

/* Opens one face. On failure returns the code and leaves the message; nothing is to be closed. */
int tg_open(tg_face *face, const char *path, uint32_t index);

/* Closes the file; tables read before stay readable. Safe to call twice. */
void tg_close(tg_face *face);

/* Frees everything. */
void tg_free(tg_face *face);

/* How many faces the file holds: a collection's count, or 1. */
int tg_count(const char *path, uint32_t *count, char *message);

/* Reads 'head', 'hhea', 'maxp' and 'hmtx' and checks they agree. */
int tg_basics(tg_face *face);

/*
 * Draws the face at an instance: user coordinates by axis tag, normalized against 'fvar', mapped
 * through 'avar' version 1. An axis the face lacks is ignored, one not given takes its default; a
 * face without 'fvar', or coordinates that all normalize to the default, leave it drawn as it is.
 */
int tg_vary(tg_face *face, const char (*tags)[4], const double *values, uint32_t count);

/* Ascender (up), descender (down, negative), line gap: font units, by OpenType's recommendation, varied by MVAR. */
int tg_vertical(tg_face *face, double out[3]);

/* The glyph for a codepoint, 0 when there is none. */
int tg_glyph(tg_face *face, uint32_t codepoint, uint32_t *glyph);

/* How far the pen moves after the glyph: font units (rounded at an instance), 0 for a glyph the face does not have. */
int tg_advance(tg_face *face, uint32_t glyph, int32_t *advance);

/* The glyph's outline as path steps, font units, y up, moved to its origin. */
int tg_outline(tg_face *face, uint32_t glyph, tg_ops *ops);

/* The value 'kern' gives a pair, 0 when none. */
int tg_kerning(tg_face *face, uint32_t left, uint32_t right, int32_t *value);

/* Every face of the file, described; a face that cannot be read is skipped when skip_broken, else the call fails. */
int tg_describe(const char *path, bool skip_broken, tg_described **faces, uint32_t *count, char *message);

void tg_described_free(tg_described *faces, uint32_t count);

void tg_ops_free(tg_ops *ops);

/* True for Unicode's default ignorable code points. */
bool tg_default_ignorable(uint32_t codepoint);

#endif
