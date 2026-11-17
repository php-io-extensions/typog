/*
 * sfnt: OpenType and TrueType files read in plain C. See sfnt.h.
 */
#include "sfnt.h"
#include "cff.h"

#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TAG(a, b, c, d) (((uint32_t) (a) << 24) | ((uint32_t) (b) << 16) | ((uint32_t) (c) << 8) | (uint32_t) (d))

/* Composite glyphs nested deeper than this are taken as broken. */
#define TG_DEPTH 8

static int fail(char *message, int code, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(message, TG_MESSAGE, format, args);
	va_end(args);
	return code;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t) ((p[0] << 8) | p[1]); }
static int16_t bes16(const uint8_t *p) { return (int16_t) be16(p); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }

/* True when [at, at + n) lies inside the data. */
static bool inside(uint32_t length, uint32_t at, uint32_t n) { return at <= length && n <= length - at; }

/* Exactly length bytes from offset, or false. */
static bool read_at(int fd, uint64_t offset, uint8_t *into, uint32_t length)
{
	uint32_t done = 0;
	while (done < length) {
		ssize_t got = pread(fd, into + done, length - done, (off_t) (offset + done));
		if (got < 0 && errno == EINTR) {
			continue;
		}
		if (got <= 0) {
			return false;
		}
		done += (uint32_t) got;
	}
	return true;
}

static bool sfnt_version(const uint8_t *p)
{
	uint32_t v = be32(p);
	return v == 0x00010000u || v == TAG('O', 'T', 'T', 'O') || v == TAG('t', 'r', 'u', 'e');
}

/* The face count of a collection header. A file has room for at most size / 32 faces, FreeType's bound:
 * each face needs its 4-byte offset, a 12-byte header and one 16-byte table record. A larger count is a
 * broken header, which would otherwise have every face of it opened in turn. */
static int collection_faces(int fd, const char *path, const uint8_t *header, uint32_t *faces, char *message)
{
	struct stat file;

	*faces = be32(header + 8);
	if (fstat(fd, &file) != 0) {
		return fail(message, TG_CANNOT_OPEN, "The font file %s cannot be read: %s.", path, strerror(errno));
	}
	if ((uint64_t) *faces > (uint64_t) file.st_size / 32) {
		return fail(message, TG_NOT_A_FONT, "The font collection %s claims %u faces; its %lld bytes have room for %lld.",
			path, *faces, (long long) file.st_size, (long long) file.st_size / 32);
	}
	return TG_OK;
}

int tg_count(const char *path, uint32_t *count, char *message)
{
	uint8_t header[12];
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return fail(message, TG_CANNOT_OPEN, "The font file %s cannot be opened.", path);
	}
	bool whole = read_at(fd, 0, header, 12);
	if (!whole) {
		close(fd);
		return fail(message, TG_NOT_A_FONT, "The file %s is too short to be a font.", path);
	}
	if (be32(header) == TAG('t', 't', 'c', 'f')) {
		int code = collection_faces(fd, path, header, count, message);
		close(fd);
		return code;
	}
	close(fd);
	if (!sfnt_version(header)) {
		return fail(message, TG_NOT_A_FONT, "The file %s is not an OpenType or TrueType font.", path);
	}
	*count = 1;
	return TG_OK;
}

int tg_open(tg_face *face, const char *path, uint32_t index)
{
	memset(face, 0, sizeof *face);
	face->fd = -1;
	uint8_t header[12];
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return fail(face->message, TG_CANNOT_OPEN, "The font file %s cannot be opened.", path);
	}
	uint64_t start = 0;
	if (!read_at(fd, 0, header, 12)) {
		close(fd);
		return fail(face->message, TG_NOT_A_FONT, "The font file %s ends before byte 12.", path);
	}
	if (be32(header) == TAG('t', 't', 'c', 'f')) {
		uint32_t faces;
		uint8_t offset[4];
		int code = collection_faces(fd, path, header, &faces, face->message);
		if (code != TG_OK) {
			close(fd);
			return code;
		}
		if (index >= faces) {
			close(fd);
			return fail(face->message, TG_NO_FACE, "The font collection %s has %u faces; there is no face %u.", path, faces, index);
		}
		if (!read_at(fd, 12 + 4 * (uint64_t) index, offset, 4) || !read_at(fd, be32(offset), header, 12)) {
			close(fd);
			return fail(face->message, TG_NOT_A_FONT, "The font collection %s ends before face %u.", path, index);
		}
		start = be32(offset);
	} else if (index != 0) {
		close(fd);
		return fail(face->message, TG_NO_FACE, "The font file %s is not a collection; its one face is 0, not %u.", path, index);
	}
	if (!sfnt_version(header)) {
		close(fd);
		return fail(face->message, TG_NOT_A_FONT, "The file %s is not an OpenType or TrueType font.", path);
	}
	uint32_t count = be16(header + 4);
	uint8_t *directory = malloc(16 * (size_t) count + 1);
	tg_record *records = malloc(sizeof(tg_record) * (size_t) count + 1);
	char *copy = strdup(path);
	if (directory == NULL || records == NULL || copy == NULL) {
		free(directory);
		free(records);
		free(copy);
		close(fd);
		return fail(face->message, TG_NO_MEMORY, "Out of memory opening %s.", path);
	}
	if (!read_at(fd, start + 12, directory, 16 * count)) {
		free(directory);
		free(records);
		free(copy);
		close(fd);
		return fail(face->message, TG_NOT_A_FONT, "The font file %s ends inside its table directory.", path);
	}
	for (uint32_t i = 0; i < count; i++) {
		records[i].tag = be32(directory + 16 * i);
		records[i].offset = be32(directory + 16 * i + 8);
		records[i].length = be32(directory + 16 * i + 12);
	}
	free(directory);
	face->fd = fd;
	face->path = copy;
	face->index = index;
	face->records = records;
	face->nrecords = count;
	return TG_OK;
}

void tg_close(tg_face *face)
{
	if (face->fd >= 0) {
		close(face->fd);
		face->fd = -1;
	}
}

void tg_free(tg_face *face)
{
	tg_close(face);
	tg_blob *blobs[] = {&face->head, &face->hhea, &face->maxp, &face->hmtx, &face->cmap, &face->loca, &face->os2, &face->kern, &face->name,
		&face->fvar, &face->avar, &face->hvar, &face->mvar, &face->cff};
	for (size_t i = 0; i < sizeof blobs / sizeof blobs[0]; i++) {
		free(blobs[i]->data);
		blobs[i]->data = NULL;
	}
	tg_cff_free(face->cff_reader);
	face->cff_reader = NULL;
	free(face->records);
	free(face->pairs);
	free(face->path);
	free(face->coords);
	free(face->shared_peaks);
	free(face->shared_scalars);
	free(face->advances);
	free(face->hvar_store.region);
	free(face->hvar_store.scalar);
	free(face->hvar_store.subtable);
	free(face->mvar_store.region);
	free(face->mvar_store.scalar);
	free(face->mvar_store.subtable);
	face->coords = face->shared_peaks = face->shared_scalars = NULL;
	face->advances = NULL;
	memset(&face->hvar_store, 0, sizeof face->hvar_store);
	memset(&face->mvar_store, 0, sizeof face->mvar_store);
	face->records = NULL;
	face->pairs = NULL;
	face->path = NULL;
}

static const tg_record *record(const tg_face *face, uint32_t tag)
{
	for (uint32_t i = 0; i < face->nrecords; i++) {
		if (face->records[i].tag == tag) {
			return &face->records[i];
		}
	}
	return NULL;
}

static void tag_text(uint32_t tag, char out[5])
{
	out[0] = (char) (tag >> 24);
	out[1] = (char) (tag >> 16);
	out[2] = (char) (tag >> 8);
	out[3] = (char) tag;
	out[4] = '\0';
}

/* Reads the table once; a face without it leaves data NULL. */
static int load(tg_face *face, tg_blob *blob, uint32_t tag)
{
	if (blob->read) {
		return TG_OK;
	}
	const tg_record *found = record(face, tag);
	if (found == NULL) {
		blob->read = true;
		return TG_OK;
	}
	char name[5];
	tag_text(tag, name);
	if (face->fd < 0) {
		return fail(face->message, TG_CLOSED, "%s#%u is closed; its '%s' table was not read before.", face->path, face->index, name);
	}
	uint8_t *data = malloc(found->length + 1);
	if (data == NULL) {
		return fail(face->message, TG_NO_MEMORY, "Out of memory reading '%s' of %s#%u.", name, face->path, face->index);
	}
	if (!read_at(face->fd, found->offset, data, found->length)) {
		free(data);
		return fail(face->message, TG_MALFORMED, "The font file %s ends before byte %llu.", face->path,
			(unsigned long long) found->offset + found->length);
	}
	blob->data = data;
	blob->length = found->length;
	blob->read = true;
	return TG_OK;
}

static int required(tg_face *face, tg_blob *blob, uint32_t tag)
{
	int code = load(face, blob, tag);
	if (code != TG_OK) {
		return code;
	}
	if (blob->data == NULL) {
		char name[5];
		tag_text(tag, name);
		return fail(face->message, TG_MISSING_TABLE, "%s#%u has no '%s' table.", face->path, face->index, name);
	}
	return TG_OK;
}

static int short_table(tg_face *face)
{
	return fail(face->message, TG_MALFORMED, "A font table of %s#%u ends before a value it should hold.", face->path, face->index);
}

int tg_basics(tg_face *face)
{
	if (face->basics) {
		return TG_OK;
	}
	int code;
	if ((code = required(face, &face->head, TAG('h', 'e', 'a', 'd'))) != TG_OK
		|| (code = required(face, &face->hhea, TAG('h', 'h', 'e', 'a'))) != TG_OK) {
		return code;
	}
	if (!inside(face->head.length, 18, 2) || !inside(face->head.length, 50, 2) || !inside(face->hhea.length, 34, 2)) {
		return short_table(face);
	}
	uint32_t em = be16(face->head.data + 18);
	if (em < 16 || em > 16384) {
		return fail(face->message, TG_MALFORMED, "%s#%u has %u units an em; a font has 16 to 16384.", face->path, face->index, em);
	}
	if ((code = required(face, &face->maxp, TAG('m', 'a', 'x', 'p'))) != TG_OK) {
		return code;
	}
	if (!inside(face->maxp.length, 4, 2)) {
		return short_table(face);
	}
	if ((code = required(face, &face->hmtx, TAG('h', 'm', 't', 'x'))) != TG_OK) {
		return code;
	}
	uint32_t long_metrics = be16(face->hhea.data + 34);
	if (long_metrics == 0 || face->hmtx.length < 4 * long_metrics) {
		return fail(face->message, TG_MALFORMED, "%s#%u's 'hmtx' table is shorter than its 'hhea' says.", face->path, face->index);
	}
	face->units_per_em = em;
	face->long_offsets = bes16(face->head.data + 50) == 1;
	face->glyph_count = be16(face->maxp.data + 4);
	face->long_metrics = long_metrics;
	face->basics = true;
	return TG_OK;
}

static int mvar_metric(tg_face *face, const char tag[4], double *net);

int tg_vertical(tg_face *face, double out[3])
{
	int code = tg_basics(face);
	if (code != TG_OK || (code = load(face, &face->os2, TAG('O', 'S', '/', '2'))) != TG_OK) {
		return code;
	}
	const uint8_t *hhea = face->hhea.data;
	if (!inside(face->hhea.length, 4, 6)) {
		return short_table(face);
	}
	/* MVAR's tags vary OS/2's typographic ('hasc', 'hdsc', 'hlgp') and Windows ('hcla', 'hcld') values; 'hhea''s have none */
	double m[5] = {0, 0, 0, 0, 0};
	static const char tags[5][4] = {{'h', 'a', 's', 'c'}, {'h', 'd', 's', 'c'}, {'h', 'l', 'g', 'p'}, {'h', 'c', 'l', 'a'}, {'h', 'c', 'l', 'd'}};
	for (int i = 0; i < 5; i++) {
		if ((code = mvar_metric(face, tags[i], &m[i])) != TG_OK) {
			return code;
		}
	}
	bool typo_known = face->os2.data != NULL && face->os2.length >= 78;
	int32_t typo[3] = {0, 0, 0}, windows[2] = {0, 0};
	if (typo_known) {
		const uint8_t *os2 = face->os2.data;
		typo[0] = bes16(os2 + 68);
		typo[1] = bes16(os2 + 70);
		typo[2] = bes16(os2 + 72);
		windows[0] = be16(os2 + 74);
		windows[1] = be16(os2 + 76);
	}
	int32_t own[3] = {bes16(hhea + 4), bes16(hhea + 6), bes16(hhea + 8)};
	bool use_typo = typo_known && (be16(face->os2.data + 62) & 0x80) != 0;   /* USE_TYPO_METRICS */
	if (!use_typo && (own[0] != 0 || own[1] != 0 || !typo_known)) {
		out[0] = own[0];
		out[1] = own[1];
		out[2] = own[2];
	} else if (use_typo || typo[0] != 0 || typo[1] != 0) {
		out[0] = typo[0] + m[0];
		out[1] = typo[1] + m[1];
		out[2] = typo[2] + m[2];
	} else {
		out[0] = windows[0] + m[3];
		out[1] = -(windows[1] + m[4]);
		out[2] = 0.0;
	}
	return TG_OK;
}

/* [platform, encoding, format], best first: every plane before the first, Windows before Unicode, symbol last. */
static const uint16_t PREFERRED[][3] = {
	{3, 10, 12}, {0, 6, 12}, {0, 4, 12}, {0, 3, 12}, {0, 2, 12}, {0, 1, 12}, {0, 0, 12}, {3, 1, 12},
	{3, 1, 4}, {0, 3, 4}, {0, 2, 4}, {0, 1, 4}, {0, 0, 4},
	{3, 0, 4},
};

static int choose_map(tg_face *face)
{
	if (face->map_chosen) {
		return TG_OK;
	}
	int code = load(face, &face->cmap, TAG('c', 'm', 'a', 'p'));
	if (code != TG_OK) {
		return code;
	}
	const uint8_t *cmap = face->cmap.data;
	uint32_t length = face->cmap.length;
	if (cmap == NULL) {
		face->map_chosen = true;
		return TG_OK;
	}
	if (!inside(length, 2, 2)) {
		return short_table(face);
	}
	uint32_t records = be16(cmap + 2);
	size_t best_rank = sizeof PREFERRED / sizeof PREFERRED[0];
	for (uint32_t i = 0; i < records; i++) {
		if (!inside(length, 4 + 8 * i, 8)) {
			return short_table(face);
		}
		uint16_t platform = be16(cmap + 4 + 8 * i), encoding = be16(cmap + 6 + 8 * i);
		uint32_t offset = be32(cmap + 8 + 8 * i);
		if (!inside(length, offset, 2)) {
			return short_table(face);
		}
		uint16_t format = be16(cmap + offset);
		for (size_t rank = 0; rank < best_rank; rank++) {
			if (PREFERRED[rank][0] == platform && PREFERRED[rank][1] == encoding && PREFERRED[rank][2] == format) {
				best_rank = rank;
				face->map_found = true;
				face->map_at = offset;
				face->map_format = format;
				face->map_symbol = platform == 3 && encoding == 0;
				break;
			}
		}
	}
	face->map_chosen = true;
	return TG_OK;
}

static int look12(tg_face *face, uint32_t codepoint, uint32_t *glyph)
{
	const uint8_t *cmap = face->cmap.data;
	uint32_t length = face->cmap.length, at = face->map_at;
	if (!inside(length, at + 12, 4)) {
		return short_table(face);
	}
	int64_t low = 0, high = (int64_t) be32(cmap + at + 12) - 1;
	*glyph = 0;
	while (low <= high) {
		int64_t middle = (low + high) >> 1;
		uint32_t group = at + 16 + 12 * (uint32_t) middle;
		if (!inside(length, group, 12)) {
			return short_table(face);
		}
		uint32_t first = be32(cmap + group), last = be32(cmap + group + 4);
		if (codepoint < first) {
			high = middle - 1;
		} else if (codepoint > last) {
			low = middle + 1;
		} else {
			*glyph = be32(cmap + group + 8) + codepoint - first;
			return TG_OK;
		}
	}
	return TG_OK;
}

static int look4(tg_face *face, uint32_t codepoint, uint32_t *glyph)
{
	const uint8_t *cmap = face->cmap.data;
	uint32_t length = face->cmap.length, at = face->map_at;
	*glyph = 0;
	if (codepoint > 0xFFFF) {
		return TG_OK;
	}
	if (!inside(length, at + 6, 2)) {
		return short_table(face);
	}
	uint32_t segments = be16(cmap + at + 6) >> 1;
	if (segments == 0) {
		return TG_OK;
	}
	uint32_t ends = at + 14, starts = ends + 2 * segments + 2, deltas = starts + 2 * segments, ranges = starts + 4 * segments;
	if (!inside(length, ends, 2 * segments) || !inside(length, starts, 6 * segments)) {
		return short_table(face);
	}
	uint32_t low = 0, high = segments - 1;
	while (low < high) {   /* the first segment whose end is at or past the codepoint */
		uint32_t middle = (low + high) >> 1;
		if (be16(cmap + ends + 2 * middle) < codepoint) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	if (be16(cmap + ends + 2 * low) < codepoint) {
		return TG_OK;
	}
	uint32_t start = be16(cmap + starts + 2 * low);
	if (start > codepoint) {
		return TG_OK;
	}
	uint32_t delta = be16(cmap + deltas + 2 * low);
	uint32_t range_at = ranges + 2 * low;
	uint32_t range = be16(cmap + range_at);
	if (range == 0) {
		*glyph = (codepoint + delta) & 0xFFFF;
		return TG_OK;
	}
	uint32_t entry = range_at + range + 2 * (codepoint - start);
	if (!inside(length, entry, 2)) {
		return short_table(face);
	}
	uint32_t found = be16(cmap + entry);
	*glyph = found == 0 ? 0 : (found + delta) & 0xFFFF;
	return TG_OK;
}

int tg_glyph(tg_face *face, uint32_t codepoint, uint32_t *glyph)
{
	int code = choose_map(face);
	*glyph = 0;
	if (code != TG_OK || !face->map_found) {
		return code;
	}
	code = face->map_format == 12 ? look12(face, codepoint, glyph) : look4(face, codepoint, glyph);
	if (code == TG_OK && *glyph == 0 && face->map_symbol && codepoint <= 0xFF) {
		code = face->map_format == 12 ? look12(face, 0xF000 + codepoint, glyph) : look4(face, 0xF000 + codepoint, glyph);
	}
	return code;
}

/* ---- outlines ---- */

typedef struct {
	tg_point *points;
	uint32_t n, cap;
	uint32_t *ends;   /* index of each contour's last point */
	uint32_t contours, ends_cap;
} tg_shape;

static void shape_free(tg_shape *shape)
{
	free(shape->points);
	free(shape->ends);
	memset(shape, 0, sizeof *shape);
}

static bool shape_point(tg_shape *shape, double x, double y, bool on)
{
	if (shape->n == shape->cap) {
		uint32_t cap = shape->cap ? 2 * shape->cap : 64;
		tg_point *grown = realloc(shape->points, sizeof(tg_point) * cap);
		if (grown == NULL) {
			return false;
		}
		shape->points = grown;
		shape->cap = cap;
	}
	shape->points[shape->n++] = (tg_point) {x, y, on};
	return true;
}

static bool shape_end(tg_shape *shape)
{
	if (shape->contours == shape->ends_cap) {
		uint32_t cap = shape->ends_cap ? 2 * shape->ends_cap : 8;
		uint32_t *grown = realloc(shape->ends, sizeof(uint32_t) * cap);
		if (grown == NULL) {
			return false;
		}
		shape->ends = grown;
		shape->ends_cap = cap;
	}
	shape->ends[shape->contours++] = shape->n - 1;
	return true;
}

static int no_memory(tg_face *face)
{
	return fail(face->message, TG_NO_MEMORY, "Out of memory reading a glyph of %s#%u.", face->path, face->index);
}

/* ---- variations ----
 *
 * One instance of a variable font, as the OpenType font variations spec draws it. tg_vary
 * normalizes the coordinates against 'fvar' (16.16, clamped), maps them through 'avar' version 1
 * (16.16) and converts them once to 2.14; the shared tuples' and the stores' region scalars are
 * worked out once. 'gvar' is read a glyph at a time from the file.
 */

static int32_t fixed32(const uint8_t *p) { return (int32_t) be32(p); }

/* a / b in 16.16, rounded to the nearest. */
static int32_t divided(int64_t a, int64_t b) { return (int32_t) llround((double) a * 65536.0 / (double) b); }

/* 'fvar''s axes: *axes (owned) in the axis's units, *fixed (owned) min, default, max in 16.16. */
static int read_axes(const uint8_t *fvar, uint32_t length, const char *path, uint32_t index, tg_axis **axes, int32_t **fixed, uint32_t *count, char *message)
{
	*axes = NULL;
	*fixed = NULL;
	*count = 0;
	if (!inside(length, 0, 16)) {
		return fail(message, TG_MALFORMED, "A font table of %s#%u ends before a value it should hold.", path, index);
	}
	if (be16(fvar) != 1) {
		return fail(message, TG_MALFORMED, "%s#%u's 'fvar' table is version %u; OpenType has version 1.", path, index, be16(fvar));
	}
	uint32_t offset = be16(fvar + 4), n = be16(fvar + 8), size = be16(fvar + 10);
	if (size < 20) {
		return fail(message, TG_MALFORMED, "%s#%u's 'fvar' axis records are %u bytes; each holds 20.", path, index, size);
	}
	uint32_t instance_size = be16(fvar + 14);   /* FreeType and HarfBuzz read any other size as not variable */
	if (instance_size != 4 * n + 4 && instance_size != 4 * n + 6) {
		return fail(message, TG_MALFORMED, "%s#%u's 'fvar' instance records are %u bytes; with %u axes OpenType has %u or %u.", path, index, instance_size, n, 4 * n + 4, 4 * n + 6);
	}
	if (n > 0 && !inside(length, offset, (n - 1) * size + 20)) {
		return fail(message, TG_MALFORMED, "A font table of %s#%u ends before a value it should hold.", path, index);
	}
	tg_axis *list = malloc(sizeof(tg_axis) * (n ? n : 1));
	int32_t *values = malloc(sizeof(int32_t) * 3 * (n ? n : 1));
	if (list == NULL || values == NULL) {
		free(list);
		free(values);
		return fail(message, TG_NO_MEMORY, "Out of memory reading 'fvar' of %s#%u.", path, index);
	}
	for (uint32_t a = 0; a < n; a++) {
		const uint8_t *p = fvar + offset + a * size;
		memcpy(list[a].tag, p, 4);
		list[a].tag[4] = '\0';
		for (int k = 0; k < 3; k++) {
			values[3 * a + k] = fixed32(p + 4 + 4 * k);
		}
		list[a].min = values[3 * a] / 65536.0;
		list[a].def = values[3 * a + 1] / 65536.0;
		list[a].max = values[3 * a + 2] / 65536.0;
	}
	*axes = list;
	*fixed = values;
	*count = n;
	return TG_OK;
}

/* The default normalization of a 16.16 user value, in 16.16: clamped to the axis, -1..0 below the default, 0..1 above it. */
static int32_t normalized(int32_t value, int32_t min, int32_t def, int32_t max)
{
	if (value > max) {
		value = max;
	}
	if (value < min) {
		value = min;
	}
	if (value < def) {
		return -divided((int64_t) def - value, (int64_t) def - min);
	}
	if (value > def) {
		return divided((int64_t) value - def, (int64_t) max - def);
	}
	return 0;
}

/* Normalized coordinates through 'avar' version 1, in 16.16: a map without -1, 0 and 1 changes nothing; a point that does not move forward is passed over. */
static int map_avar(tg_face *face, int32_t *fixed)
{
	const uint8_t *d = face->avar.data;
	uint32_t length = face->avar.length;
	if (!inside(length, 0, 8)) {
		return short_table(face);
	}
	if (be16(d) != 1) {
		return fail(face->message, TG_MALFORMED, "%s#%u's 'avar' table is version %u; the OpenType spec has version 1.", face->path, face->index, be16(d));
	}
	if (be16(d + 6) != face->axes) {
		return fail(face->message, TG_MALFORMED, "%s#%u's 'avar' maps %u axes; its 'fvar' has %u.", face->path, face->index, be16(d + 6), face->axes);
	}
	uint32_t at = 8;
	for (uint32_t a = 0; a < face->axes; a++) {
		if (!inside(length, at, 2)) {
			return short_table(face);
		}
		uint32_t count = be16(d + at);
		if (!inside(length, at + 2, 4 * count)) {
			return short_table(face);
		}
		int32_t *from = malloc(sizeof(int32_t) * (count ? count : 1)), *to = malloc(sizeof(int32_t) * (count ? count : 1));
		if (from == NULL || to == NULL) {
			free(from);
			free(to);
			return fail(face->message, TG_NO_MEMORY, "Out of memory reading 'avar' of %s#%u.", face->path, face->index);
		}
		uint32_t kept = 0;
		bool low = false, zero = false, high = false;
		for (uint32_t m = 0; m < count; m++) {
			int32_t f = bes16(d + at + 2 + 4 * m) * 4, t = bes16(d + at + 4 + 4 * m) * 4;
			if (kept == 0 || (f > from[kept - 1] && t >= to[kept - 1])) {
				from[kept] = f;
				to[kept] = t;
				kept++;
				low = low || (f == -65536 && t == -65536);
				zero = zero || (f == 0 && t == 0);
				high = high || (f == 65536 && t == 65536);
			}
		}
		at += 2 + 4 * count;
		if (low && zero && high) {
			/* the first point is at or below -1: interpolate in the segment ending at or past the value */
			int32_t value = fixed[a];
			uint32_t m = 1;
			while (from[m] < value) {
				m++;
			}
			fixed[a] = to[m - 1] + divided((int64_t) (value - from[m - 1]) * (to[m] - to[m - 1]), (int64_t) (from[m] - from[m - 1]) * 65536);
		}
		free(from);
		free(to);
	}
	return TG_OK;
}

/* The spec's tuple scalar: per axis 1 when the region is not valid for it or its peak is 0, 0 outside start..end, else the triangle. */
static double tuple_scalar(const double *coords, uint32_t axes, const double *start, const double *peak, const double *end)
{
	double scalar = 1.0;
	for (uint32_t a = 0; a < axes; a++) {
		double s = start[a], p = peak[a], e = end[a], c = coords[a];
		if (s > p || p > e || (s < 0 && e > 0 && p != 0) || p == 0) {
			continue;
		}
		if (c < s || c > e) {
			return 0.0;
		}
		if (c == p) {
			continue;
		}
		scalar *= c < p ? (c - s) / (p - s) : (e - c) / (e - p);
	}
	return scalar;
}

/* A peak-only tuple's scalar: its region runs from 0 to the peak. */
static double peak_scalar(const tg_face *face, const double *peak)
{
	double start[64], end[64];
	double *s = face->axes <= 64 ? start : malloc(sizeof(double) * face->axes), *e = face->axes <= 64 ? end : malloc(sizeof(double) * face->axes);
	if (s == NULL || e == NULL) {
		if (s != start) free(s);
		if (e != end) free(e);
		return 0.0;
	}
	for (uint32_t a = 0; a < face->axes; a++) {
		s[a] = peak[a] < 0 ? peak[a] : 0.0;
		e[a] = peak[a] > 0 ? peak[a] : 0.0;
	}
	double scalar = tuple_scalar(face->coords, face->axes, s, peak, e);
	if (s != start) free(s);
	if (e != end) free(e);
	return scalar;
}

static int store_init(tg_face *face, tg_store *store, const uint8_t *data, uint32_t length)
{
	memset(store, 0, sizeof *store);
	store->data = data;
	store->length = length;
	if (!inside(length, 0, 8)) {
		return short_table(face);
	}
	if (be16(data) != 1) {
		return fail(face->message, TG_MALFORMED, "An ItemVariationStore is format %u; the OpenType format is 1.", be16(data));
	}
	uint32_t list = be32(data + 2), n = be16(data + 6);
	if (!inside(length, 8, 4 * n) || !inside(length, list, 4)) {
		return short_table(face);
	}
	store->subtable = malloc(sizeof(uint32_t) * (n ? n : 1));
	if (store->subtable == NULL) {
		return no_memory(face);
	}
	for (uint32_t i = 0; i < n; i++) {
		store->subtable[i] = be32(data + 8 + 4 * i);
	}
	store->subtables = n;
	uint32_t axes = be16(data + list), regions = be16(data + list + 2);
	if (axes != face->axes) {
		return fail(face->message, TG_MALFORMED, "An ItemVariationStore's regions are for %u axes; the font has %u.", axes, face->axes);
	}
	if (!inside(length, list + 4, 6 * axes * regions)) {
		return short_table(face);
	}
	store->region = malloc(sizeof(double) * 3 * axes * (regions ? regions : 1));
	store->scalar = malloc(sizeof(double) * (regions ? regions : 1));
	if (store->region == NULL || store->scalar == NULL) {
		return no_memory(face);
	}
	const uint8_t *p = data + list + 4;
	for (uint32_t r = 0; r < regions; r++) {
		for (uint32_t a = 0; a < axes; a++, p += 6) {
			double *at = store->region + 3 * (r * axes + a);
			at[0] = bes16(p) / 16384.0;
			at[1] = bes16(p + 2) / 16384.0;
			at[2] = bes16(p + 4) / 16384.0;
		}
		store->scalar[r] = NAN;
	}
	store->regions = regions;
	return TG_OK;
}

/* A region's scalar at the instance, worked out once. */
static double region_scalar(tg_face *face, tg_store *store, uint32_t region)
{
	if (isnan(store->scalar[region])) {
		uint32_t axes = face->axes;
		double start[64], peak[64], end[64];
		bool heap = axes > 64;
		double *s = heap ? malloc(sizeof(double) * axes) : start, *p = heap ? malloc(sizeof(double) * axes) : peak, *e = heap ? malloc(sizeof(double) * axes) : end;
		if (s == NULL || p == NULL || e == NULL) {
			if (heap) {
				free(s);
				free(p);
				free(e);
			}
			return 0.0;
		}
		for (uint32_t a = 0; a < axes; a++) {
			const double *at = store->region + 3 * (region * axes + a);
			s[a] = at[0];
			p[a] = at[1];
			e[a] = at[2];
		}
		store->scalar[region] = tuple_scalar(face->coords, axes, s, p, e);
		if (heap) {
			free(s);
			free(p);
			free(e);
		}
	}
	return store->scalar[region];
}

/* A delta set's net adjustment: 0 for an absent subtable or a row past its items. */
static int store_delta(tg_face *face, tg_store *store, uint32_t outer, uint32_t inner, double *net)
{
	*net = 0.0;
	if (outer >= store->subtables || store->subtable[outer] == 0) {
		return TG_OK;
	}
	const uint8_t *d = store->data;
	uint32_t at = store->subtable[outer];
	if (!inside(store->length, at, 6)) {
		return short_table(face);
	}
	uint32_t items = be16(d + at), word_count = be16(d + at + 2), count = be16(d + at + 4);
	if (!inside(store->length, at + 6, 2 * count)) {
		return short_table(face);
	}
	uint32_t words = word_count & 0x7FFF;
	bool longs = (word_count & 0x8000) != 0;
	if (words > count) {
		return fail(face->message, TG_MALFORMED, "An ItemVariationData has %u word deltas a row and %u regions.", words, count);
	}
	uint32_t rows = at + 6 + 2 * count;
	uint64_t row = longs ? 4 * words + 2 * (count - words) : 2 * words + (count - words);
	if ((uint64_t) rows + (uint64_t) items * row > store->length) {
		return fail(face->message, TG_MALFORMED, "An ItemVariationData holds fewer rows than it says.");
	}
	if (inner >= items) {
		return TG_OK;
	}
	uint32_t p = rows + (uint32_t) (inner * row);
	for (uint32_t k = 0; k < count; k++) {
		int32_t delta;
		if (k < words) {
			delta = longs ? (int32_t) be32(d + p) : bes16(d + p);
			p += longs ? 4 : 2;
		} else {
			delta = longs ? bes16(d + p) : (int8_t) d[p];
			p += longs ? 2 : 1;
		}
		if (delta != 0) {
			uint32_t region = be16(d + at + 6 + 2 * k);
			if (region >= store->regions) {
				return fail(face->message, TG_MALFORMED, "An ItemVariationStore's delta set names region %u; it has %u.", region, store->regions);
			}
			*net += delta * region_scalar(face, store, region);
		}
	}
	return TG_OK;
}

/* The (outer, inner) delta-set index a DeltaSetIndexMap gives an item: past the map, its last entry. */
static int index_map(tg_face *face, const uint8_t *data, uint32_t length, uint32_t offset, uint32_t index, uint32_t *outer, uint32_t *inner)
{
	if (!inside(length, offset, 2)) {
		return fail(face->message, TG_MALFORMED, "A DeltaSetIndexMap lies past its table.");
	}
	uint32_t format = data[offset], entry_format = data[offset + 1], count, at;
	if (format == 0 && inside(length, offset + 2, 2)) {
		count = be16(data + offset + 2);
		at = offset + 4;
	} else if (format == 1 && inside(length, offset + 2, 4)) {
		count = be32(data + offset + 2);
		at = offset + 6;
	} else if (format > 1) {
		return fail(face->message, TG_MALFORMED, "A DeltaSetIndexMap is format %u; OpenType has 0 and 1.", format);
	} else {
		return short_table(face);
	}
	if (count == 0) {
		return fail(face->message, TG_MALFORMED, "A DeltaSetIndexMap has no entries.");
	}
	uint32_t size = ((entry_format & 0x30) >> 4) + 1, bits = (entry_format & 0x0F) + 1;
	uint64_t from = (uint64_t) at + (uint64_t) size * (index < count ? index : count - 1);
	if (from > UINT32_MAX || !inside(length, (uint32_t) from, size)) {
		return fail(face->message, TG_MALFORMED, "A DeltaSetIndexMap ends before its entries.");
	}
	uint32_t entry = 0;
	for (uint32_t b = 0; b < size; b++) {
		entry = (entry << 8) | data[from + b];
	}
	*outer = entry >> bits;
	*inner = entry & ((1u << bits) - 1);
	return TG_OK;
}

/* MVAR's net adjustment for a value tag; 0 for a tag it does not vary. */
static int mvar_metric(tg_face *face, const char tag[4], double *net)
{
	*net = 0.0;
	if (!face->mvar_ready) {
		return TG_OK;
	}
	for (uint32_t r = 0; r < face->mvar_count; r++) {
		const uint8_t *p = face->mvar.data + 12 + face->mvar_size * r;
		if (memcmp(p, tag, 4) == 0) {
			return store_delta(face, &face->mvar_store, be16(p + 4), be16(p + 6), net);
		}
	}
	return TG_OK;
}

static int gvar_short(tg_face *face)
{
	return fail(face->message, TG_MALFORMED, "A 'gvar' glyph's variation data of %s#%u ends early.", face->path, face->index);
}

/* Reads the 'gvar' header and its shared tuples. */
static int gvar_header(tg_face *face)
{
	const tg_record *gvar = record(face, TAG('g', 'v', 'a', 'r'));
	face->gvar_read = true;
	if (gvar == NULL) {
		return TG_OK;
	}
	uint8_t h[20];
	if (gvar->length < 20 || !read_at(face->fd, gvar->offset, h, 20)) {
		return short_table(face);
	}
	if (be16(h) != 1) {
		return fail(face->message, TG_MALFORMED, "%s#%u's 'gvar' table is version %u; OpenType has version 1.", face->path, face->index, be16(h));
	}
	if (be16(h + 4) != face->axes) {
		return fail(face->message, TG_MALFORMED, "%s#%u's 'gvar' is for %u axes; its 'fvar' has %u.", face->path, face->index, be16(h + 4), face->axes);
	}
	face->gvar_shared = be16(h + 6);
	uint32_t shared_at = be32(h + 8);
	face->gvar_glyphs = be16(h + 12);
	face->gvar_long = (be16(h + 14) & 1) != 0;
	face->gvar_array = be32(h + 16);
	uint32_t bytes = 2 * face->axes * face->gvar_shared;
	if (!inside(gvar->length, shared_at, bytes)) {
		return short_table(face);
	}
	face->shared_peaks = malloc(sizeof(double) * (bytes ? face->axes * face->gvar_shared : 1));
	face->shared_scalars = malloc(sizeof(double) * (face->gvar_shared ? face->gvar_shared : 1));
	uint8_t *raw = malloc(bytes ? bytes : 1);
	if (face->shared_peaks == NULL || face->shared_scalars == NULL || raw == NULL) {
		free(raw);
		return no_memory(face);
	}
	if (bytes > 0 && !read_at(face->fd, (uint64_t) gvar->offset + shared_at, raw, bytes)) {
		free(raw);
		return short_table(face);
	}
	for (uint32_t i = 0; i < face->axes * face->gvar_shared; i++) {
		face->shared_peaks[i] = bes16(raw + 2 * i) / 16384.0;
	}
	for (uint32_t i = 0; i < face->gvar_shared; i++) {
		face->shared_scalars[i] = NAN;
	}
	free(raw);
	face->gvar_found = true;
	return TG_OK;
}

/* Packed point numbers: a count (0: every point), then runs of differences, bytes or words. *points owned; *all when every point. */
static int point_numbers(tg_face *face, const uint8_t *d, uint32_t length, uint32_t *at, uint32_t **points, uint32_t *n, bool *all)
{
	*points = NULL;
	*n = 0;
	*all = false;
	if (*at >= length) {
		return gvar_short(face);
	}
	uint32_t count = d[(*at)++];
	if (count == 0) {
		*all = true;
		return TG_OK;
	}
	if ((count & 0x80) != 0) {
		if (*at >= length) {
			return gvar_short(face);
		}
		count = ((count & 0x7F) << 8) | d[(*at)++];
	}
	uint32_t *list = malloc(sizeof(uint32_t) * count);
	if (list == NULL) {
		return no_memory(face);
	}
	uint32_t point = 0, got = 0;
	while (got < count) {
		if (*at >= length) {
			free(list);
			return gvar_short(face);
		}
		uint8_t control = d[(*at)++];
		bool words = (control & 0x80) != 0;
		for (uint32_t run = (control & 0x7Fu) + 1; run > 0 && got < count; run--) {
			if (words) {
				if (!inside(length, *at, 2)) {
					free(list);
					return gvar_short(face);
				}
				point += be16(d + *at);
				*at += 2;
			} else {
				if (*at >= length) {
					free(list);
					return gvar_short(face);
				}
				point += d[(*at)++];
			}
			list[got++] = point;
		}
	}
	*points = list;
	*n = count;
	return TG_OK;
}

/* One packed delta array: runs of zeros, signed bytes or signed words, until count are read; *end
 * is where it ends. A run holding more than the array has left is refused: x and y are separate
 * arrays, and HarfBuzz refuses a glyph whose run crosses between them. */
static int packed_deltas(tg_face *face, const uint8_t *d, uint32_t length, uint32_t at, uint32_t count, uint32_t limit, int32_t *out, uint32_t *end)
{
	uint32_t have = 0;
	while (have < count) {
		if (at >= limit || at >= length) {
			return fail(face->message, TG_MALFORMED, "A 'gvar' tuple's deltas run past its data.");
		}
		uint8_t control = d[at++];
		if ((control & 0xC0) == 0xC0) {
			return fail(face->message, TG_MALFORMED, "A 'gvar' delta run sets DELTAS_ARE_ZERO and DELTAS_ARE_WORDS together, which the OpenType spec does not define.");
		}
		uint32_t run = (control & 0x3Fu) + 1;
		if (run > count - have) {
			return fail(face->message, TG_MALFORMED, "A 'gvar' delta run holds %u deltas where its array has %u left; x and y deltas are separate packed arrays.", run, count - have);
		}
		if ((control & 0x80) != 0) {
			for (uint32_t i = 0; i < run; i++) {
				out[have++] = 0;
			}
		} else if ((control & 0x40) != 0) {
			if (!inside(length, at, 2 * run)) {
				return gvar_short(face);
			}
			for (uint32_t i = 0; i < run; i++, at += 2) {
				out[have++] = bes16(d + at);
			}
		} else {
			if (!inside(length, at, run)) {
				return gvar_short(face);
			}
			for (uint32_t i = 0; i < run; i++) {
				out[have++] = (int8_t) d[at++];
			}
		}
	}
	*end = at;
	return TG_OK;
}

/* The spec's inferred delta of one coordinate, from the named points before and after it. */
static double infer(double target, double before, double before_delta, double after, double after_delta)
{
	if (before == after) {
		return before_delta == after_delta ? before_delta : 0.0;
	}
	double low = before < after ? before : after, high = before > after ? before : after;
	if (target <= low) {
		return before < after ? before_delta : after_delta;
	}
	if (target >= high) {
		return before > after ? before_delta : after_delta;
	}
	double proportion = (target - before) / (after - before);
	return (1 - proportion) * before_delta + proportion * after_delta;
}

/* Deltas for the points a tuple does not name, contour by contour, from the named points around each (wrapping), from default positions. */
static void inferred(const double *xs, const double *ys, const uint32_t *ends, uint32_t contours, double *fx, double *fy, bool *has, uint32_t *named)
{
	uint32_t first = 0;
	for (uint32_t c = 0; c < contours; c++) {
		uint32_t last = ends[c], k = 0;
		for (uint32_t i = first; i <= last; i++) {
			if (has[i]) {
				named[k++] = i;
			}
		}
		if (k > 0 && k <= last - first) {
			for (uint32_t j = 0; j < k; j++) {
				uint32_t before = named[j], after = named[(j + 1) % k];
				for (uint32_t i = before + 1;; i++) {
					if (i > last) {
						i = first;
					}
					if (i == after) {
						break;
					}
					fx[i] = infer(xs[i], xs[before], fx[before], xs[after], fx[after]);
					fy[i] = infer(ys[i], ys[before], fy[before], ys[after], fy[after]);
					has[i] = true;
				}
			}
		}
		first = last + 1;
	}
}

/*
 * The 'gvar' deltas of one glyph's points (its phantom points last), added into dx and dy; *any
 * when it has variation data. A simple glyph's unnamed points take inferred deltas.
 */
static int gvar_glyph(tg_face *face, uint32_t glyph, const double *xs, const double *ys, uint32_t count, const uint32_t *ends, uint32_t contours, bool simple, double *dx, double *dy, bool *any)
{
	*any = false;
	if (!face->gvar_found || glyph >= face->gvar_glyphs) {
		return TG_OK;
	}
	const tg_record *gvar = record(face, TAG('g', 'v', 'a', 'r'));
	if (face->fd < 0) {
		return fail(face->message, TG_CLOSED, "%s#%u is closed; its glyph variations are read from the file.", face->path, face->index);
	}
	uint8_t raw[8];
	uint32_t from, to;
	if (face->gvar_long) {
		if (!inside(gvar->length, 20 + 4 * glyph, 8) || !read_at(face->fd, (uint64_t) gvar->offset + 20 + 4 * glyph, raw, 8)) {
			return short_table(face);
		}
		from = be32(raw);
		to = be32(raw + 4);
	} else {
		if (!inside(gvar->length, 20 + 2 * glyph, 4) || !read_at(face->fd, (uint64_t) gvar->offset + 20 + 2 * glyph, raw, 4)) {
			return short_table(face);
		}
		from = 2u * be16(raw);
		to = 2u * be16(raw + 2);
	}
	if (to <= from) {
		return TG_OK;
	}
	uint32_t length = to - from;
	if ((uint64_t) face->gvar_array + to > gvar->length) {
		return short_table(face);
	}
	uint8_t *d = malloc(length);
	if (d == NULL) {
		return no_memory(face);
	}
	if (!read_at(face->fd, (uint64_t) gvar->offset + face->gvar_array + from, d, length)) {
		free(d);
		return short_table(face);
	}
	uint32_t axes = face->axes;
	double *peak = malloc(sizeof(double) * (axes ? axes : 1)), *start = malloc(sizeof(double) * (axes ? axes : 1)), *end = malloc(sizeof(double) * (axes ? axes : 1));
	double *fx = malloc(sizeof(double) * count), *fy = malloc(sizeof(double) * count);
	bool *has = malloc(sizeof(bool) * count);
	uint32_t *scratch = malloc(sizeof(uint32_t) * count);
	int32_t *deltas = malloc(sizeof(int32_t) * 2 * count);
	uint32_t *shared = NULL, *own = NULL;
	int code = TG_OK;
	if (peak == NULL || start == NULL || end == NULL || fx == NULL || fy == NULL || has == NULL || scratch == NULL || deltas == NULL) {
		code = no_memory(face);
		goto done;
	}
	if (!inside(length, 0, 4)) {
		code = gvar_short(face);
		goto done;
	}
	uint32_t header = be16(d), tuples = header & 0x0FFF, at = be16(d + 2), hat = 4, nshared = 0;
	bool shared_all = true;
	if ((header & 0x8000) != 0 && (code = point_numbers(face, d, length, &at, &shared, &nshared, &shared_all)) != TG_OK) {
		goto done;
	}
	for (uint32_t t = 0; t < tuples; t++) {
		if (!inside(length, hat, 4)) {
			code = gvar_short(face);
			goto done;
		}
		uint32_t size = be16(d + hat), index = be16(d + hat + 2);
		hat += 4;
		const double *tuple_peak;
		if ((index & 0x8000) != 0) {
			if (!inside(length, hat, 2 * axes)) {
				code = gvar_short(face);
				goto done;
			}
			for (uint32_t a = 0; a < axes; a++) {
				peak[a] = bes16(d + hat + 2 * a) / 16384.0;
			}
			hat += 2 * axes;
			tuple_peak = peak;
		} else {
			uint32_t which = index & 0x0FFF;
			if (which >= face->gvar_shared) {
				code = fail(face->message, TG_MALFORMED, "%s#%u's 'gvar' names shared tuple %u; it has %u.", face->path, face->index, which, face->gvar_shared);
				goto done;
			}
			tuple_peak = face->shared_peaks + which * axes;
		}
		double scalar;
		if ((index & 0x4000) != 0) {
			if (!inside(length, hat, 4 * axes)) {
				code = gvar_short(face);
				goto done;
			}
			for (uint32_t a = 0; a < axes; a++) {
				start[a] = bes16(d + hat + 2 * a) / 16384.0;
				end[a] = bes16(d + hat + 2 * axes + 2 * a) / 16384.0;
			}
			hat += 4 * axes;
			scalar = tuple_scalar(face->coords, axes, start, tuple_peak, end);
		} else if ((index & 0x8000) == 0) {
			double *cached = &face->shared_scalars[index & 0x0FFF];
			if (isnan(*cached)) {
				*cached = peak_scalar(face, tuple_peak);
			}
			scalar = *cached;
		} else {
			scalar = peak_scalar(face, tuple_peak);
		}
		uint32_t block = at;
		at += size;
		if (scalar == 0.0) {
			continue;
		}
		const uint32_t *named = shared;
		uint32_t nnamed = nshared;
		bool all = shared_all;
		if ((index & 0x2000) != 0) {
			free(own);
			own = NULL;
			if ((code = point_numbers(face, d, length, &block, &own, &nnamed, &all)) != TG_OK) {
				goto done;
			}
			named = own;
		}
		uint32_t n = all ? count : nnamed;
		if (n > count) {
			int32_t *grown = realloc(deltas, sizeof(int32_t) * 2 * n);
			if (grown == NULL) {
				code = no_memory(face);
				goto done;
			}
			deltas = grown;
		}
		uint32_t ys_at;
		if ((code = packed_deltas(face, d, length, block, n, at, deltas, &ys_at)) != TG_OK
			|| (code = packed_deltas(face, d, length, ys_at, n, at, deltas + n, &ys_at)) != TG_OK) {
			goto done;
		}
		if (all) {
			for (uint32_t i = 0; i < n; i++) {
				dx[i] += scalar * deltas[i];
				dy[i] += scalar * deltas[n + i];
			}
		} else {
			memset(has, 0, sizeof(bool) * count);
			for (uint32_t i = 0; i < n; i++) {
				uint32_t point = named[i];
				if (point >= count) {
					code = fail(face->message, TG_MALFORMED, "%s#%u's 'gvar' names point %u of a glyph with %u.", face->path, face->index, point, count);
					goto done;
				}
				fx[point] = deltas[i];
				fy[point] = deltas[n + i];
				has[point] = true;
			}
			if (simple) {
				inferred(xs, ys, ends, contours, fx, fy, has, scratch);
			}
			for (uint32_t i = 0; i < count; i++) {
				if (has[i]) {
					dx[i] += scalar * fx[i];
					dy[i] += scalar * fy[i];
				}
			}
		}
		*any = true;
	}
done:
	free(d);
	free(peak);
	free(start);
	free(end);
	free(fx);
	free(fy);
	free(has);
	free(scratch);
	free(deltas);
	free(shared);
	free(own);
	return code;
}

int tg_vary(tg_face *face, const char (*tags)[4], const double *values, uint32_t count)
{
	face->varied = false;
	if (count == 0) {
		return TG_OK;
	}
	int code = load(face, &face->fvar, TAG('f', 'v', 'a', 'r'));
	if (code != TG_OK || face->fvar.data == NULL) {
		return code;   /* no 'fvar': the coordinates have nothing to apply to */
	}
	tg_axis *axes;
	int32_t *fixed_axes;
	uint32_t n;
	if ((code = read_axes(face->fvar.data, face->fvar.length, face->path, face->index, &axes, &fixed_axes, &n, face->message)) != TG_OK) {
		return code;
	}
	int32_t *fixed = calloc(n ? n : 1, sizeof(int32_t));
	if (fixed == NULL) {
		free(axes);
		free(fixed_axes);
		return no_memory(face);
	}
	for (uint32_t a = 0; a < n; a++) {
		for (uint32_t i = 0; i < count; i++) {
			if (memcmp(tags[i], axes[a].tag, 4) == 0) {
				double user = floor(values[i] * 65536.0 + 0.5);
				user = user > INT32_MAX ? INT32_MAX : user < INT32_MIN ? INT32_MIN : user;
				fixed[a] = normalized((int32_t) user, fixed_axes[3 * a], fixed_axes[3 * a + 1], fixed_axes[3 * a + 2]);
				break;
			}
		}
	}
	free(axes);
	free(fixed_axes);
	face->axes = n;
	if ((code = load(face, &face->avar, TAG('a', 'v', 'a', 'r'))) != TG_OK || (face->avar.data != NULL && (code = map_avar(face, fixed)) != TG_OK)) {
		free(fixed);
		return code;
	}
	bool all_default = true;
	for (uint32_t a = 0; a < n; a++) {
		fixed[a] = (int32_t) floor((fixed[a] + 2) / 4.0);   /* 16.16 to 2.14, once */
		all_default = all_default && fixed[a] == 0;
	}
	if (all_default) {
		free(fixed);
		return TG_OK;
	}
	face->coords = malloc(sizeof(double) * n);
	if (face->coords == NULL) {
		free(fixed);
		return no_memory(face);
	}
	for (uint32_t a = 0; a < n; a++) {
		face->coords[a] = fixed[a] / 16384.0;
	}
	free(fixed);
	face->varied = true;
	if ((code = gvar_header(face)) != TG_OK) {
		return code;
	}
	if ((code = load(face, &face->hvar, TAG('H', 'V', 'A', 'R'))) != TG_OK) {
		return code;
	}
	if (face->hvar.data != NULL) {
		if (!inside(face->hvar.length, 0, 8)) {
			return short_table(face);
		}
		if (be16(face->hvar.data) != 1) {
			return fail(face->message, TG_MALFORMED, "%s#%u's 'HVAR' table is version %u; OpenType has version 1.", face->path, face->index, be16(face->hvar.data));
		}
		uint32_t at = be32(face->hvar.data + 4);
		if (at > face->hvar.length) {
			return short_table(face);
		}
		if ((code = store_init(face, &face->hvar_store, face->hvar.data + at, face->hvar.length - at)) != TG_OK) {
			return code;
		}
		face->hvar_ready = true;
	}
	if ((code = load(face, &face->mvar, TAG('M', 'V', 'A', 'R'))) != TG_OK) {
		return code;
	}
	if (face->mvar.data != NULL) {
		const uint8_t *m = face->mvar.data;
		if (!inside(face->mvar.length, 0, 12)) {
			return short_table(face);
		}
		if (be16(m) != 1) {
			return fail(face->message, TG_MALFORMED, "%s#%u's 'MVAR' table is version %u; OpenType has version 1.", face->path, face->index, be16(m));
		}
		uint32_t size = be16(m + 6), records = be16(m + 8), at = be16(m + 10);
		if (records > 0) {
			if (size < 8) {
				return fail(face->message, TG_MALFORMED, "%s#%u's 'MVAR' records are %u bytes; each holds at least 8.", face->path, face->index, size);
			}
			if (!inside(face->mvar.length, 12, size * records) || at > face->mvar.length) {
				return short_table(face);
			}
			if ((code = store_init(face, &face->mvar_store, m + at, face->mvar.length - at)) != TG_OK) {
				return code;
			}
			face->mvar_count = records;
			face->mvar_size = size;
			face->mvar_ready = true;
		}
	}
	return TG_OK;
}


/* The glyph's bytes in 'glyf' (owned, free them); length 0 for a glyph that draws nothing. */
static int glyph_data(tg_face *face, uint32_t glyph, uint8_t **data, uint32_t *length)
{
	*data = NULL;
	*length = 0;
	int code = required(face, &face->loca, TAG('l', 'o', 'c', 'a'));
	if (code != TG_OK) {
		return code;
	}
	uint32_t start, end;
	if (face->long_offsets) {
		if (!inside(face->loca.length, 4 * glyph, 8)) {
			return short_table(face);
		}
		start = be32(face->loca.data + 4 * glyph);
		end = be32(face->loca.data + 4 * glyph + 4);
	} else {
		if (!inside(face->loca.length, 2 * glyph, 4)) {
			return short_table(face);
		}
		start = 2u * be16(face->loca.data + 2 * glyph);
		end = 2u * be16(face->loca.data + 2 * glyph + 2);
	}
	if (end < start) {
		return fail(face->message, TG_MALFORMED, "%s#%u's glyph %u ends before it starts.", face->path, face->index, glyph);
	}
	if (end == start) {
		return TG_OK;
	}
	const tg_record *glyf = record(face, TAG('g', 'l', 'y', 'f'));
	if (glyf == NULL) {
		return fail(face->message, TG_MISSING_TABLE, "%s has no 'glyf' table.", face->path);
	}
	if (end > glyf->length) {
		return fail(face->message, TG_MALFORMED, "%s: bytes %u..%u are outside its 'glyf' table of %u.", face->path, start, end, glyf->length);
	}
	if (face->fd < 0) {
		return fail(face->message, TG_CLOSED, "%s#%u is closed; its glyphs are read from the file.", face->path, face->index);
	}
	uint8_t *bytes = malloc(end - start);
	if (bytes == NULL) {
		return no_memory(face);
	}
	if (!read_at(face->fd, (uint64_t) glyf->offset + start, bytes, end - start)) {
		free(bytes);
		return fail(face->message, TG_MALFORMED, "The font file %s ends before byte %llu.", face->path,
			(unsigned long long) glyf->offset + end);
	}
	*data = bytes;
	*length = end - start;
	return TG_OK;
}

static int load_glyph(tg_face *face, uint32_t glyph, uint32_t depth, tg_shape *out, double *first, double *second);

static int simple(tg_face *face, const uint8_t *data, uint32_t length, uint32_t count, tg_shape *out)
{
	if (count == 0) {
		return TG_OK;
	}
	if (!inside(length, 10, 2 * count + 2)) {
		return short_table(face);
	}
	uint32_t points = (uint32_t) be16(data + 10 + 2 * (count - 1)) + 1;
	uint32_t at = 12 + 2 * count + be16(data + 10 + 2 * count);
	uint8_t *flags = malloc(points + 256);
	double *xs = malloc(sizeof(double) * points), *ys = malloc(sizeof(double) * points);
	if (flags == NULL || xs == NULL || ys == NULL) {
		free(flags);
		free(xs);
		free(ys);
		return no_memory(face);
	}
	int code = TG_OK;
	uint32_t n = 0;
	while (n < points) {
		if (at >= length) {
			code = fail(face->message, TG_MALFORMED, "%s#%u: a glyph's flags run past its data.", face->path, face->index);
			goto done;
		}
		uint8_t flag = data[at++];
		flags[n++] = flag;
		if ((flag & 0x08) != 0) {   /* REPEAT_FLAG */
			if (at >= length) {
				code = fail(face->message, TG_MALFORMED, "%s#%u: a glyph's flags run past its data.", face->path, face->index);
				goto done;
			}
			for (uint32_t repeat = data[at++]; repeat > 0; repeat--) {
				flags[n++] = flag;   /* room: points + 256 */
			}
		}
	}
	int32_t value = 0;
	for (uint32_t i = 0; i < points; i++) {
		uint8_t flag = flags[i];
		if ((flag & 0x02) != 0) {   /* X_SHORT_VECTOR; X_IS_SAME_OR_POSITIVE gives the sign */
			if (at >= length) {
				code = fail(face->message, TG_MALFORMED, "%s#%u: a glyph's coordinates run past its data.", face->path, face->index);
				goto done;
			}
			uint8_t byte = data[at++];
			value += (flag & 0x10) != 0 ? byte : -(int32_t) byte;
		} else if ((flag & 0x10) == 0) {
			if (!inside(length, at, 2)) {
				code = short_table(face);
				goto done;
			}
			value += bes16(data + at);
			at += 2;
		}
		xs[i] = value;
	}
	value = 0;
	for (uint32_t i = 0; i < points; i++) {
		uint8_t flag = flags[i];
		if ((flag & 0x04) != 0) {   /* Y_SHORT_VECTOR; Y_IS_SAME_OR_POSITIVE gives the sign */
			if (at >= length) {
				code = fail(face->message, TG_MALFORMED, "%s#%u: a glyph's coordinates run past its data.", face->path, face->index);
				goto done;
			}
			uint8_t byte = data[at++];
			value += (flag & 0x20) != 0 ? byte : -(int32_t) byte;
		} else if ((flag & 0x20) == 0) {
			if (!inside(length, at, 2)) {
				code = short_table(face);
				goto done;
			}
			value += bes16(data + at);
			at += 2;
		}
		ys[i] = value;
	}
	uint32_t first = 0;
	for (uint32_t c = 0; c < count; c++) {
		uint32_t last = be16(data + 10 + 2 * c);
		if (last < first || last >= points) {
			code = fail(face->message, TG_MALFORMED, "%s#%u: a glyph's contour ends are out of order.", face->path, face->index);
			goto done;
		}
		for (uint32_t i = first; i <= last; i++) {
			if (!shape_point(out, xs[i], ys[i], (flags[i] & 0x01) != 0)) {
				code = no_memory(face);
				goto done;
			}
		}
		if (!shape_end(out)) {
			code = no_memory(face);
			goto done;
		}
		first = last + 1;
	}
done:
	free(flags);
	free(xs);
	free(ys);
	return code;
}

typedef struct {
	uint16_t flags, glyph;
	int32_t one, two;
	double a, b, c, d;
} tg_component;

/* A composite glyph's components; *count of them in *components (owned). */
static int components(tg_face *face, const uint8_t *data, uint32_t length, tg_component **list, uint32_t *count)
{
	uint32_t at = 10, n = 0, cap = 4;
	tg_component *items = malloc(sizeof(tg_component) * cap);
	if (items == NULL) {
		return no_memory(face);
	}
	uint16_t flags;
	do {
		if (!inside(length, at, 4)) {
			free(items);
			return short_table(face);
		}
		tg_component item = {.a = 1.0, .b = 0.0, .c = 0.0, .d = 1.0};
		flags = item.flags = be16(data + at);
		item.glyph = be16(data + at + 2);
		at += 4;
		bool xy = (flags & 0x0002) != 0;   /* ARGS_ARE_XY_VALUES: signed offsets, else point numbers */
		if ((flags & 0x0001) != 0) {       /* ARG_1_AND_2_ARE_WORDS */
			if (!inside(length, at, 4)) {
				free(items);
				return short_table(face);
			}
			item.one = xy ? bes16(data + at) : be16(data + at);
			item.two = xy ? bes16(data + at + 2) : be16(data + at + 2);
			at += 4;
		} else {
			if (!inside(length, at, 2)) {
				free(items);
				return fail(face->message, TG_MALFORMED, "A composite glyph runs past its data.");
			}
			item.one = xy ? (int8_t) data[at] : data[at];
			item.two = xy ? (int8_t) data[at + 1] : data[at + 1];
			at += 2;
		}
		if ((flags & 0x0008) != 0) {          /* WE_HAVE_A_SCALE */
			if (!inside(length, at, 2)) {
				free(items);
				return short_table(face);
			}
			item.a = item.d = bes16(data + at) / 16384.0;
			at += 2;
		} else if ((flags & 0x0040) != 0) {   /* WE_HAVE_AN_X_AND_Y_SCALE */
			if (!inside(length, at, 4)) {
				free(items);
				return short_table(face);
			}
			item.a = bes16(data + at) / 16384.0;
			item.d = bes16(data + at + 2) / 16384.0;
			at += 4;
		} else if ((flags & 0x0080) != 0) {   /* WE_HAVE_A_TWO_BY_TWO */
			if (!inside(length, at, 8)) {
				free(items);
				return short_table(face);
			}
			item.a = bes16(data + at) / 16384.0;
			item.b = bes16(data + at + 2) / 16384.0;
			item.c = bes16(data + at + 4) / 16384.0;
			item.d = bes16(data + at + 6) / 16384.0;
			at += 8;
		}
		if (n == cap) {
			cap *= 2;
			tg_component *grown = realloc(items, sizeof(tg_component) * cap);
			if (grown == NULL) {
				free(items);
				return no_memory(face);
			}
			items = grown;
		}
		items[n++] = item;
	} while ((flags & 0x0020) != 0);   /* MORE_COMPONENTS */
	*list = items;
	*count = n;
	return TG_OK;
}

/* The glyph's left side bearing in 'hmtx'. */
static int hmtx_bearing(tg_face *face, uint32_t glyph, int32_t *bearing)
{
	uint32_t at = glyph < face->long_metrics ? 4 * glyph + 2 : 4 * face->long_metrics + 2 * (glyph - face->long_metrics);
	if (!inside(face->hmtx.length, at, 2)) {
		return short_table(face);
	}
	*bearing = bes16(face->hmtx.data + at);
	return TG_OK;
}

static uint32_t hmtx_advance(const tg_face *face, uint32_t glyph)
{
	return be16(face->hmtx.data + 4 * (glyph < face->long_metrics ? glyph : face->long_metrics - 1));
}

/*
 * A composite glyph: each component loaded (as it varies), transformed and placed by its offset
 * (which 'gvar' varies, one point a component) or by matching points. Its phantom points vary by
 * its own deltas, or are a USE_MY_METRICS component's.
 */
static int composite(tg_face *face, uint32_t glyph, const uint8_t *data, uint32_t length, uint32_t depth, tg_shape *out, double *first, double *second)
{
	if (depth >= TG_DEPTH) {
		return fail(face->message, TG_MALFORMED, "%s#%u: composite glyphs nested more than %d deep.", face->path, face->index, TG_DEPTH);
	}
	tg_component *items;
	uint32_t count;
	int code = components(face, data, length, &items, &count);
	if (code != TG_OK) {
		return code;
	}
	double *dxs = NULL, *dys = NULL;
	bool any = false;
	if (face->varied) {
		double *xs = calloc(count + 4, sizeof(double)), *ys = calloc(count + 4, sizeof(double));
		dxs = calloc(count + 4, sizeof(double));
		dys = calloc(count + 4, sizeof(double));
		if (xs == NULL || ys == NULL || dxs == NULL || dys == NULL) {
			code = no_memory(face);
		} else {
			for (uint32_t k = 0; k < count; k++) {
				xs[k] = items[k].one;
				ys[k] = items[k].two;
			}
			xs[count] = *first;
			xs[count + 1] = *second;
			code = gvar_glyph(face, glyph, xs, ys, count + 4, NULL, 0, false, dxs, dys, &any);
		}
		free(xs);
		free(ys);
		if (code == TG_OK && any) {
			*first += dxs[count];
			*second += dxs[count + 1];
		}
	}
	bool metrics = false;
	double metrics_first = 0, metrics_second = 0;
	for (uint32_t k = 0; k < count && code == TG_OK; k++) {
		tg_component *item = &items[k];
		tg_shape child = {0};
		double child_first, child_second;
		code = load_glyph(face, item->glyph, depth + 1, &child, &child_first, &child_second);
		if (code != TG_OK) {
			shape_free(&child);
			break;
		}
		if ((item->flags & 0x0200) != 0) {   /* USE_MY_METRICS: the last one says */
			metrics = true;
			metrics_first = child_first;
			metrics_second = child_second;
		}
		for (uint32_t i = 0; i < child.n; i++) {
			double x = child.points[i].x, y = child.points[i].y;
			child.points[i].x = item->a * x + item->c * y;
			child.points[i].y = item->b * x + item->d * y;
		}
		double dx, dy;
		if ((item->flags & 0x0002) != 0) {
			dx = item->one + (any ? dxs[k] : 0.0);
			dy = item->two + (any ? dys[k] : 0.0);
			if ((item->flags & 0x0800) != 0 && (item->flags & 0x1000) == 0) {   /* SCALED_COMPONENT_OFFSET */
				double ox = dx, oy = dy;
				dx = item->a * ox + item->c * oy;
				dy = item->b * ox + item->d * oy;
			}
		} else {
			if ((uint32_t) item->one >= out->n || (uint32_t) item->two >= child.n) {
				shape_free(&child);
				code = fail(face->message, TG_MALFORMED, "%s#%u: a composite glyph matches a point it does not have.", face->path, face->index);
				break;
			}
			dx = out->points[item->one].x - child.points[item->two].x;
			dy = out->points[item->one].y - child.points[item->two].y;
		}
		uint32_t start = 0;
		for (uint32_t c = 0; c < child.contours && code == TG_OK; c++) {
			for (uint32_t i = start; i <= child.ends[c]; i++) {
				if (!shape_point(out, child.points[i].x + dx, child.points[i].y + dy, child.points[i].on)) {
					code = no_memory(face);
					break;
				}
			}
			if (code == TG_OK && !shape_end(out)) {
				code = no_memory(face);
			}
			start = child.ends[c] + 1;
		}
		shape_free(&child);
	}
	if (code == TG_OK && metrics) {
		*first = metrics_first;
		*second = metrics_second;
	}
	free(items);
	free(dxs);
	free(dys);
	return code;
}

/* A simple glyph's points (and its phantoms) moved by its 'gvar' deltas. */
static int vary_simple(tg_face *face, uint32_t glyph, tg_shape *shape, double *first, double *second)
{
	uint32_t n = shape->n, count = n + 4;
	double *xs = malloc(sizeof(double) * count), *ys = malloc(sizeof(double) * count);
	double *dx = calloc(count, sizeof(double)), *dy = calloc(count, sizeof(double));
	int code = TG_OK;
	if (xs == NULL || ys == NULL || dx == NULL || dy == NULL) {
		code = no_memory(face);
		goto done;
	}
	for (uint32_t i = 0; i < n; i++) {
		xs[i] = shape->points[i].x;
		ys[i] = shape->points[i].y;
	}
	xs[n] = *first;
	xs[n + 1] = *second;
	xs[n + 2] = xs[n + 3] = 0.0;
	ys[n] = ys[n + 1] = ys[n + 2] = ys[n + 3] = 0.0;
	bool any;
	code = gvar_glyph(face, glyph, xs, ys, count, shape->ends, shape->contours, true, dx, dy, &any);
	if (code == TG_OK && any) {
		for (uint32_t i = 0; i < n; i++) {
			shape->points[i].x += dx[i];
			shape->points[i].y += dy[i];
		}
		*first += dx[n];
		*second += dx[n + 1];
	}
done:
	free(xs);
	free(ys);
	free(dx);
	free(dy);
	return code;
}

/*
 * The glyph's contours, and the x of its first two phantom points: its left edge less its left
 * side bearing, and that plus its advance; at the instance when the face is varied.
 */
static int load_glyph(tg_face *face, uint32_t glyph, uint32_t depth, tg_shape *out, double *first, double *second)
{
	*first = *second = 0.0;
	if (glyph >= face->glyph_count) {
		return fail(face->message, TG_NO_GLYPH, "%s#%u has %u glyphs; there is no glyph %u.", face->path, face->index, face->glyph_count, glyph);
	}
	uint8_t *data;
	uint32_t length;
	int code = glyph_data(face, glyph, &data, &length);
	if (code != TG_OK) {
		return code;
	}
	if (length > 0 && !inside(length, 0, 10)) {
		free(data);
		return short_table(face);
	}
	int32_t bearing;
	if ((code = hmtx_bearing(face, glyph, &bearing)) != TG_OK) {
		free(data);
		return code;
	}
	*first = (double) ((length > 0 ? bes16(data + 2) : 0) - bearing);
	*second = *first + hmtx_advance(face, glyph);
	if (length == 0) {
		code = face->varied ? vary_simple(face, glyph, out, first, second) : TG_OK;   /* no points; its phantoms may vary */
		return code;
	}
	int16_t count = bes16(data);
	if (count >= 0) {
		code = simple(face, data, length, (uint32_t) count, out);
		if (code == TG_OK && face->varied) {
			code = vary_simple(face, glyph, out, first, second);
		}
	} else {
		code = composite(face, glyph, data, length, depth, out, first, second);
	}
	free(data);
	return code;
}

static bool add_op(tg_ops *ops, char kind, double a, double b, double c, double d)
{
	if (ops->n == ops->cap) {
		uint32_t cap = ops->cap ? 2 * ops->cap : 32;
		tg_op *grown = realloc(ops->ops, sizeof(tg_op) * cap);
		if (grown == NULL) {
			return false;
		}
		ops->ops = grown;
		ops->cap = cap;
	}
	ops->ops[ops->n++] = (tg_op) {kind, {a, b, c, d}};
	return true;
}

/* ---- CFF outlines (cff.c) into path steps ---- */

static int cff_move(void *u, double x, double y) { return add_op(u, 'M', x, y, 0, 0) ? 0 : 1; }
static int cff_line(void *u, double x, double y) { return add_op(u, 'L', x, y, 0, 0) ? 0 : 1; }
static int cff_close(void *u) { return add_op(u, 'Z', 0, 0, 0, 0) ? 0 : 1; }
static int cff_cubic(void *u, double x1, double y1, double x2, double y2, double x, double y)
{
	tg_ops *ops = u;
	if (!add_op(ops, 'C', x1, y1, x2, y2)) {
		return 1;
	}
	ops->ops[ops->n - 1].v[4] = x;
	ops->ops[ops->n - 1].v[5] = y;
	return 0;
}

/* The glyph's CFF outline: the table read and its structure checked once, at the first outline asked for. */
static int cff_outline(tg_face *face, uint32_t glyph, tg_ops *ops)
{
	if (glyph >= face->glyph_count) {
		return fail(face->message, TG_NO_GLYPH, "%s#%u has %u glyphs; there is no glyph %u.", face->path, face->index, face->glyph_count, glyph);
	}
	char why[TG_MESSAGE];
	int code;
	if (face->cff_reader == NULL) {
		if ((code = load(face, &face->cff, TAG('C', 'F', 'F', ' '))) != TG_OK) {
			return code;
		}
		code = tg_cff_open(face->cff.data, face->cff.length, &face->cff_reader, why, sizeof why);
		if (code == TG_CFF_NO_MEMORY) {
			return no_memory(face);
		}
		if (code != TG_CFF_OK) {
			face->cff_reader = NULL;
			return fail(face->message, TG_CFF, "%s#%u: %s", face->path, face->index, why);
		}
	}
	static const tg_path_sink sink = {cff_move, cff_line, cff_cubic, cff_close};
	code = tg_cff_outline(face->cff_reader, glyph, &sink, ops, why, sizeof why);
	if (code == TG_CFF_OK) {
		return TG_OK;
	}
	ops->n = 0;
	if (code == TG_CFF_NO_MEMORY) {
		return no_memory(face);
	}
	return fail(face->message, TG_CFF, "%s#%u: %s", face->path, face->index, why);
}

/* TrueType contours as path steps: quadratic curves, two off-curve points in a row with an implied on-curve point between. */
static bool path(const tg_shape *shape, double across, tg_ops *ops)
{
	uint32_t first = 0;
	for (uint32_t c = 0; c < shape->contours; c++) {
		uint32_t last = shape->ends[c];
		tg_point p0 = shape->points[first], pn = shape->points[last];
		p0.x += across;
		pn.x += across;
		tg_point start;
		int64_t from, to;   /* the points after the start, then the start again */
		if (p0.on) {
			start = p0;
			from = (int64_t) first + 1;
			to = last;
		} else if (pn.on) {
			start = pn;
			from = first;
			to = (int64_t) last - 1;
		} else {
			start = (tg_point) {(p0.x + pn.x) / 2, (p0.y + pn.y) / 2, true};
			from = first;
			to = last;
		}
		if (!add_op(ops, 'M', start.x, start.y, 0, 0)) {
			return false;
		}
		bool control = false;
		double cx = 0, cy = 0;
		for (int64_t i = from; i <= to + 1; i++) {
			tg_point p = start;
			if (i <= to) {
				p = shape->points[i];
				p.x += across;
			}
			if (p.on) {
				if (!(control ? add_op(ops, 'Q', cx, cy, p.x, p.y) : add_op(ops, 'L', p.x, p.y, 0, 0))) {
					return false;
				}
				control = false;
			} else if (!control) {
				cx = p.x;
				cy = p.y;
				control = true;
			} else {
				if (!add_op(ops, 'Q', cx, cy, (cx + p.x) / 2, (cy + p.y) / 2)) {
					return false;
				}
				cx = p.x;
				cy = p.y;
			}
		}
		if (!add_op(ops, 'Z', 0, 0, 0, 0)) {
			return false;
		}
		first = last + 1;
	}
	return true;
}

int tg_outline(tg_face *face, uint32_t glyph, tg_ops *ops)
{
	ops->n = 0;
	int code = tg_basics(face);
	if (code != TG_OK) {
		return code;
	}
	if (record(face, TAG('g', 'l', 'y', 'f')) == NULL) {
		if (record(face, TAG('C', 'F', 'F', '2')) != NULL) {
			return fail(face->message, TG_CFF, "%s#%u has CFF2 outlines, which this reader does not draw.", face->path, face->index);
		}
		if (record(face, TAG('C', 'F', 'F', ' ')) != NULL) {
			return cff_outline(face, glyph, ops);
		}
		/* bitmaps only, or outlines in a table this does not read ('hvgl', ...): nothing it can draw */
		return fail(face->message, TG_NO_OUTLINES, "%s#%u has no outlines this reader draws: no 'glyf' or 'CFF ' table.", face->path, face->index);
	}
	tg_shape shape = {0};
	double first, second;
	code = load_glyph(face, glyph, 0, &shape, &first, &second);
	if (code == TG_OK && !path(&shape, -first, ops)) {
		code = no_memory(face);
	}
	shape_free(&shape);
	return code;
}

int tg_advance(tg_face *face, uint32_t glyph, int32_t *advance)
{
	int code = tg_basics(face);
	*advance = 0;
	if (code != TG_OK || glyph >= face->glyph_count) {
		return code;
	}
	int32_t base = (int32_t) hmtx_advance(face, glyph);
	if (!face->varied) {
		*advance = base;
		return TG_OK;
	}
	if (face->advances == NULL) {
		face->advances = malloc(sizeof(int32_t) * face->glyph_count);
		if (face->advances == NULL) {
			return no_memory(face);
		}
		for (uint32_t g = 0; g < face->glyph_count; g++) {
			face->advances[g] = INT32_MIN;
		}
	}
	if (face->advances[glyph] != INT32_MIN) {
		*advance = face->advances[glyph];
		return TG_OK;
	}
	int32_t value = base;
	if (face->hvar_ready) {
		/* HVAR: through its advance map, or the glyph as the inner index */
		uint32_t map = be32(face->hvar.data + 8), outer = 0, inner = glyph;
		double delta;
		if ((map != 0 && (code = index_map(face, face->hvar.data, face->hvar.length, map, glyph, &outer, &inner)) != TG_OK)
			|| (code = store_delta(face, &face->hvar_store, outer, inner, &delta)) != TG_OK) {
			return code;
		}
		value = (int32_t) lround(base + delta);
	} else if (record(face, TAG('g', 'l', 'y', 'f')) != NULL) {
		/* no HVAR: from the phantom points */
		tg_shape shape = {0};
		double first, second;
		code = load_glyph(face, glyph, 0, &shape, &first, &second);
		shape_free(&shape);
		if (code != TG_OK) {
			return code;
		}
		value = (int32_t) lround(second - first);
	}
	face->advances[glyph] = value;
	*advance = value;
	return TG_OK;
}

void tg_ops_free(tg_ops *ops)
{
	free(ops->ops);
	ops->ops = NULL;
	ops->n = ops->cap = 0;
}

/* ---- kerning ---- */

static int pair_order(const void *a, const void *b)
{
	uint32_t x = ((const tg_pair *) a)->key, y = ((const tg_pair *) b)->key;
	return x < y ? -1 : x > y;
}

/* Adds or replaces in a sorted list kept in *pairs; values for one key add unless override. */
static int read_pairs(tg_face *face)
{
	face->pairs_read = true;
	int code = load(face, &face->kern, TAG('k', 'e', 'r', 'n'));
	if (code != TG_OK || face->kern.data == NULL) {
		return code;
	}
	const uint8_t *kern = face->kern.data;
	uint32_t length = face->kern.length;
	if (!inside(length, 0, 4)) {
		return short_table(face);
	}
	bool apple = be16(kern) == 1;
	if (apple && !inside(length, 4, 4)) {
		return short_table(face);
	}
	uint32_t tables = apple ? be32(kern + 4) : be16(kern + 2);
	uint32_t at = apple ? 8 : 4;
	/* every usable pair in table order, with its subtable's override; folded below */
	typedef struct { uint32_t key; int32_t value; bool override; } entry;
	entry *all = NULL;
	uint32_t n = 0, cap = 0;
	for (uint32_t t = 0; t < tables; t++) {
		uint32_t sublength, body;
		uint16_t coverage;
		bool usable, override;
		if (apple) {
			if (!inside(length, at, 8)) {
				free(all);
				return short_table(face);
			}
			sublength = be32(kern + at);
			coverage = be16(kern + at + 4);
			usable = (coverage & 0xE000) == 0 && (coverage & 0xFF) == 0;
			override = false;
			body = at + 8;
		} else {
			if (!inside(length, at, 6)) {
				free(all);
				return short_table(face);
			}
			sublength = be16(kern + at + 2);
			coverage = be16(kern + at + 4);
			usable = (coverage & 0x0001) != 0 && (coverage & 0x0004) == 0 && (coverage >> 8) == 0;
			override = (coverage & 0x0008) != 0;
			body = at + 6;
		}
		if (sublength < 6) {
			free(all);
			return fail(face->message, TG_MALFORMED, "A 'kern' subtable is shorter than its own header.");
		}
		if (usable) {
			if (!inside(length, body, 2)) {
				free(all);
				return short_table(face);
			}
			uint32_t count = be16(kern + body);
			if (!inside(length, body + 8, 6 * count)) {
				free(all);
				return fail(face->message, TG_MALFORMED, "A 'kern' subtable holds fewer pairs than it says.");
			}
			if (n + count > cap) {
				cap = (n + count) * 2;
				entry *grown = realloc(all, sizeof(entry) * cap);
				if (grown == NULL) {
					free(all);
					return no_memory(face);
				}
				all = grown;
			}
			for (uint32_t i = 0; i < count; i++) {
				const uint8_t *pair = kern + body + 8 + 6 * i;
				all[n] = (entry) {((uint32_t) be16(pair) << 16) | be16(pair + 2), bes16(pair + 4), override};
				n++;
			}
		}
		at += sublength;
	}
	if (n == 0) {
		free(all);
		return TG_OK;
	}
	/* fold in table order, key by key: sort by key, keeping table order within a key */
	tg_pair *pairs = malloc(sizeof(tg_pair) * n);
	if (pairs == NULL) {
		free(all);
		return no_memory(face);
	}
	for (uint32_t i = 0; i < n; i++) {   /* pack (key, order) into a sortable pair list first */
		pairs[i].key = all[i].key;
		pairs[i].value = (int32_t) i;
	}
	qsort(pairs, n, sizeof(tg_pair), pair_order);
	/* qsort is not stable: within a key, put the entries back in table order */
	uint32_t written = 0;
	for (uint32_t i = 0; i < n;) {
		uint32_t j = i;
		while (j < n && pairs[j].key == pairs[i].key) {
			j++;
		}
		for (uint32_t x = i + 1; x < j; x++) {   /* insertion sort by order; runs are short */
			tg_pair hold = pairs[x];
			uint32_t y = x;
			while (y > i && pairs[y - 1].value > hold.value) {
				pairs[y] = pairs[y - 1];
				y--;
			}
			pairs[y] = hold;
		}
		int32_t value = 0;
		for (uint32_t x = i; x < j; x++) {
			const entry *e = &all[pairs[x].value];
			value = e->override ? e->value : value + e->value;
		}
		pairs[written++] = (tg_pair) {pairs[i].key, value};
		i = j;
	}
	free(all);
	face->pairs = pairs;
	face->npairs = written;
	return TG_OK;
}

int tg_kerning(tg_face *face, uint32_t left, uint32_t right, int32_t *value)
{
	*value = 0;
	if (!face->pairs_read) {
		int code = read_pairs(face);
		if (code != TG_OK) {
			face->pairs_read = false;
			return code;
		}
	}
	tg_pair wanted = {(left << 16) | right, 0};
	const tg_pair *found = face->npairs == 0 ? NULL : bsearch(&wanted, face->pairs, face->npairs, sizeof(tg_pair), pair_order);
	if (found != NULL) {
		*value = found->value;
	}
	return TG_OK;
}

/* ---- describing a face for a catalog ---- */

/* Mac OS Roman's upper half as Unicode. */
static const uint16_t MAC_ROMAN[128] = {
	0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
	0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
	0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
	0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
	0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
	0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
	0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
	0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

/* Appends one codepoint as UTF-8; the buffer has room (4 bytes a codepoint). */
static size_t put_utf8(char *out, uint32_t cp)
{
	if (cp < 0x80) {
		out[0] = (char) cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char) (0xC0 | (cp >> 6));
		out[1] = (char) (0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char) (0xE0 | (cp >> 12));
		out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char) (0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char) (0xF0 | (cp >> 18));
	out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char) (0x80 | (cp & 0x3F));
	return 4;
}

/* A name string as UTF-8 (owned), from UTF-16BE or Mac Roman; NULL when empty. An unpaired surrogate is '?'. */
static char *name_text(const uint8_t *bytes, uint32_t length, bool mac)
{
	char *out = malloc(4 * (size_t) length + 1);
	if (out == NULL) {
		return NULL;
	}
	size_t n = 0;
	if (mac) {
		for (uint32_t i = 0; i < length; i++) {
			n += put_utf8(out + n, bytes[i] < 0x80 ? bytes[i] : MAC_ROMAN[bytes[i] - 0x80]);
		}
	} else {
		for (uint32_t i = 0; i + 1 < length; i += 2) {
			uint32_t unit = be16(bytes + i);
			if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < length && be16(bytes + i + 2) >= 0xDC00 && be16(bytes + i + 2) <= 0xDFFF) {
				unit = 0x10000 + ((unit - 0xD800) << 10) + (be16(bytes + i + 2) - 0xDC00);
				i += 2;
			} else if (unit >= 0xD800 && unit <= 0xDFFF) {
				unit = '?';
			}
			n += put_utf8(out + n, unit);
		}
	}
	out[n] = '\0';
	if (n == 0) {
		free(out);
		return NULL;
	}
	return out;
}

/* PHP's trim(): spaces, tabs, newlines, carriage returns, NUL and vertical tabs at both ends. */
static void trim(char *text)
{
	static const char blank[] = " \t\n\r\v";
	size_t length = strlen(text), start = 0;
	while (start < length && (text[start] == '\0' || strchr(blank, text[start]) != NULL)) {
		start++;
	}
	while (length > start && strchr(blank, text[length - 1]) != NULL) {
		length--;
	}
	memmove(text, text + start, length - start);
	text[length - start] = '\0';
}

/* The family: 'name' 16, else 1; Windows English, any Windows language, Unicode, Mac Roman English, in that order. */
static char *family(const uint8_t *name, uint32_t length)
{
	if (name == NULL || length < 6) {
		return NULL;
	}
	uint32_t count = be16(name + 2), strings = be16(name + 4);
	if (6 + 12 * count > length) {
		return NULL;
	}
	int best_rank[2] = {99, 99};   /* for ID 1 and 16 */
	char *best[2] = {NULL, NULL};
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *r = name + 6 + 12 * i;
		uint16_t platform = be16(r), encoding = be16(r + 2), language = be16(r + 4), id = be16(r + 6);
		if (id != 1 && id != 16) {
			continue;
		}
		int rank;
		if (platform == 3 && (encoding == 1 || encoding == 10) && language == 0x409) {
			rank = 0;
		} else if (platform == 3 && (encoding == 1 || encoding == 10)) {
			rank = 1;
		} else if (platform == 0) {
			rank = 2;
		} else if (platform == 1 && encoding == 0 && language == 0) {
			rank = 3;
		} else {
			continue;
		}
		int slot = id == 16;
		if (best_rank[slot] <= rank) {
			continue;
		}
		uint32_t from = strings + be16(r + 10), size = be16(r + 8);
		if ((uint64_t) from + size > length) {
			continue;
		}
		char *text = name_text(name + from, size, platform == 1);
		if (text == NULL) {
			continue;
		}
		free(best[slot]);
		best[slot] = text;
		best_rank[slot] = rank;
	}
	char *chosen = best[1] != NULL ? best[1] : best[0];
	free(best[1] != NULL ? best[0] : best[1]);
	return chosen;
}

static int describe_face(const char *path, uint32_t index, tg_described *out, char *message)
{
	tg_face face;
	int code = tg_open(&face, path, index);
	if (code != TG_OK) {
		memcpy(message, face.message, TG_MESSAGE);
		return code;
	}
	if ((code = load(&face, &face.name, TAG('n', 'a', 'm', 'e'))) != TG_OK
		|| (code = load(&face, &face.os2, TAG('O', 'S', '/', '2'))) != TG_OK
		|| (code = required(&face, &face.head, TAG('h', 'e', 'a', 'd'))) != TG_OK) {
		memcpy(message, face.message, TG_MESSAGE);
		tg_free(&face);
		return code;
	}
	char *name = family(face.name.data, face.name.length);
	if (name != NULL) {
		trim(name);
	}
	if (name == NULL || name[0] == '\0') {
		free(name);
		code = fail(message, TG_MALFORMED, "%s#%u has no family name.", path, index);
		tg_free(&face);
		return code;
	}
	if (!inside(face.head.length, 44, 2)) {
		free(name);
		code = fail(message, TG_MALFORMED, "A font table of %s#%u ends before a value it should hold.", path, index);
		tg_free(&face);
		return code;
	}
	uint16_t mac_style = be16(face.head.data + 44);
	int weight = (mac_style & 0x01) != 0 ? 700 : 400;
	int slant = (mac_style & 0x02) != 0 ? TG_ITALIC : TG_UPRIGHT;
	int width = 5;
	if (face.os2.data != NULL && face.os2.length >= 64) {
		const uint8_t *os2 = face.os2.data;
		uint32_t w = be16(os2 + 4);
		weight = w == 0 ? 400 : w < 10 ? (int) w * 100 : (w > 1000 ? 1000 : (int) w);
		width = be16(os2 + 6) != 0 ? be16(os2 + 6) : 5;
		uint16_t selection = be16(os2 + 62);
		slant = (selection & 0x0001) != 0 ? TG_ITALIC : (selection & 0x0200) != 0 ? TG_OBLIQUE : slant;
	}
	const char *outlines = record(&face, TAG('g', 'l', 'y', 'f')) != NULL ? "glyf"
		: record(&face, TAG('C', 'F', 'F', ' ')) != NULL ? "CFF "
		: record(&face, TAG('C', 'F', 'F', '2')) != NULL ? "CFF2" : "";
	tg_axis *axes = NULL;
	uint32_t naxes = 0;
	if ((code = load(&face, &face.fvar, TAG('f', 'v', 'a', 'r'))) != TG_OK) {
		memcpy(message, face.message, TG_MESSAGE);
	} else if (face.fvar.data != NULL) {
		int32_t *fixed;
		code = read_axes(face.fvar.data, face.fvar.length, path, index, &axes, &fixed, &naxes, message);
		free(fixed);
	}
	tg_free(&face);
	if (code != TG_OK) {
		free(name);
		return code;
	}
	*out = (tg_described) {index, name, weight, slant, width, axes, naxes, {0}};
	memcpy(out->outlines, outlines, strlen(outlines) + 1);
	return TG_OK;
}

int tg_describe(const char *path, bool skip_broken, tg_described **faces, uint32_t *count, char *message)
{
	uint32_t total;
	*faces = NULL;
	*count = 0;
	int code = tg_count(path, &total, message);
	if (code != TG_OK) {
		return code;
	}
	tg_described *list = malloc(sizeof(tg_described) * (total ? total : 1));
	if (list == NULL) {
		return fail(message, TG_NO_MEMORY, "Out of memory describing %s.", path);
	}
	uint32_t n = 0;
	for (uint32_t index = 0; index < total; index++) {
		char why[TG_MESSAGE];
		code = describe_face(path, index, &list[n], why);
		if (code == TG_OK) {
			n++;
		} else if (!skip_broken) {
			memcpy(message, why, TG_MESSAGE);
			tg_described_free(list, n);
			return code;
		}
	}
	*faces = list;
	*count = n;
	return TG_OK;
}

void tg_described_free(tg_described *faces, uint32_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		free(faces[i].family);
		free(faces[i].axes);
	}
	free(faces);
}

/* ---- default ignorables ---- */

static const uint32_t IGNORABLE[][2] = {
	{0x00AD, 0x00AD}, {0x034F, 0x034F}, {0x061C, 0x061C}, {0x115F, 0x1160}, {0x17B4, 0x17B5},
	{0x180B, 0x180F}, {0x200B, 0x200F}, {0x202A, 0x202E}, {0x2060, 0x206F}, {0x3164, 0x3164},
	{0xFE00, 0xFE0F}, {0xFEFF, 0xFEFF}, {0xFFA0, 0xFFA0}, {0xFFF0, 0xFFF8}, {0x1BCA0, 0x1BCA3},
	{0x1D173, 0x1D17A}, {0xE0000, 0xE0FFF},
};

bool tg_default_ignorable(uint32_t codepoint)
{
	for (size_t i = 0; i < sizeof IGNORABLE / sizeof IGNORABLE[0]; i++) {
		if (codepoint < IGNORABLE[i][0]) {
			return false;
		}
		if (codepoint <= IGNORABLE[i][1]) {
			return true;
		}
	}
	return false;
}
