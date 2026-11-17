#include "cff.h"

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STACK 48
#define DEPTH 10
#define TRANSIENT 32
#define DICT_OPERANDS 48

typedef struct {
	uint32_t count, size, offsets;   /* offsets: where the offset array starts */
} index_t;

typedef struct {
	index_t subrs;
	int32_t bias;
	double m[6];
	bool transform;
} font_t;

struct tg_cff {
	const uint8_t *data;
	size_t length;
	index_t chars, global;
	int32_t global_bias;
	font_t top;          /* a font that is not CID-keyed: its subrs and matrix */
	font_t *fonts;       /* CID-keyed: each Font DICT's */
	uint32_t nfonts;
	int64_t fdselect;    /* -1: not CID-keyed */
	uint32_t charset;
};

/* StandardEncoding: code => SID, for seac. */
static const uint16_t STANDARD[256] = {
	[32] = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
	31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
	61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90,
	91, 92, 93, 94, 95,
	[161] = 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
	[177] = 111, 112, 113, 114,
	[182] = 115, 116, 117, 118, 119, 120, 121, 122,
	[191] = 123,
	[193] = 124, 125, 126, 127, 128, 129, 130, 131,
	[202] = 132, 133,
	[205] = 134, 135, 136, 137,
	[225] = 138,
	[227] = 139,
	[232] = 140, 141, 142, 143,
	[241] = 144,
	[245] = 145,
	[248] = 146, 147, 148, 149,
};

static int refuse(char *message, size_t size, const char *format, ...)
{
	char why[256];
	va_list args;

	va_start(args, format);
	vsnprintf(why, sizeof(why), format, args);
	va_end(args);
	snprintf(message, size, "its CFF outlines cannot be read: %s.", why);

	return TG_CFF_BAD;
}

/* ------------------------------------------------------------------ table */

static bool u8(const tg_cff *c, size_t at, uint32_t *v)
{
	if (at >= c->length) {
		return false;
	}
	*v = c->data[at];
	return true;
}

static bool u16(const tg_cff *c, size_t at, uint32_t *v)
{
	if (at + 2 > c->length || at + 2 < at) {
		return false;
	}
	*v = (uint32_t) c->data[at] << 8 | c->data[at + 1];
	return true;
}

static bool offset_at(const tg_cff *c, const index_t *x, uint32_t i, uint32_t *v)
{
	size_t at = (size_t) x->offsets + (size_t) i * x->size;

	if (at + x->size > c->length) {
		return false;
	}
	*v = 0;
	for (uint32_t k = 0; k < x->size; k++) {
		*v = *v << 8 | c->data[at + k];
	}
	return true;
}

/* An INDEX at `at`; *next is where the structure after it starts. */
static int read_index(const tg_cff *c, size_t at, index_t *x, size_t *next, char *m, size_t ms)
{
	uint32_t count, size, last;

	if (!u16(c, at, &count)) {
		return refuse(m, ms, "bytes %zu..%zu lie past the table", at, at + 2);
	}
	if (count == 0) {
		*x = (index_t) {0, 1, (uint32_t) at + 2};
		if (next) {
			*next = at + 2;
		}
		return TG_CFF_OK;
	}
	if (!u8(c, at + 2, &size)) {
		return refuse(m, ms, "byte %zu lies past the table", at + 2);
	}
	if (size < 1 || size > 4) {
		return refuse(m, ms, "an INDEX at %zu has offsets of %u bytes", at, size);
	}
	*x = (index_t) {count, size, (uint32_t) at + 3};
	if (!offset_at(c, x, count, &last)) {
		return refuse(m, ms, "an INDEX is cut short");
	}
	size_t end = at + 3 + ((size_t) count + 1) * size + last - 1;
	if (last < 1 || end > c->length) {
		return refuse(m, ms, "an INDEX at %zu reaches past the table", at);
	}
	if (next) {
		*next = end;
	}
	return TG_CFF_OK;
}

static int item(const tg_cff *c, const index_t *x, uint32_t i, size_t *from, size_t *to, char *m, size_t ms)
{
	uint32_t a, b;
	size_t data = (size_t) x->offsets + ((size_t) x->count + 1) * x->size - 1;

	if (!offset_at(c, x, i, &a) || !offset_at(c, x, i + 1, &b)) {
		return refuse(m, ms, "an INDEX is cut short");
	}
	*from = data + a;
	*to = data + b;
	if (a < 1 || a > b || *to > c->length) {
		return refuse(m, ms, "item %u of an INDEX at %u lies outside it", i, x->offsets - 3);
	}
	return TG_CFF_OK;
}

/* ------------------------------------------------------------------- DICT */

typedef struct {
	double v[DICT_OPERANDS];
	uint32_t n;
	bool set;
} entry_t;

/* The operators this reader asks a DICT for. */
enum { D_CHARSET, D_CHARSTRINGS, D_PRIVATE, D_SUBRS, D_TYPE, D_MATRIX, D_ROS, D_FDARRAY, D_FDSELECT, D_COUNT };

static int dict_slot(uint32_t op)
{
	switch (op) {
		case 15: return D_CHARSET;
		case 17: return D_CHARSTRINGS;
		case 18: return D_PRIVATE;
		case 19: return D_SUBRS;
		case 1206: return D_TYPE;
		case 1207: return D_MATRIX;
		case 1230: return D_ROS;
		case 1236: return D_FDARRAY;
		case 1237: return D_FDSELECT;
		default: return -1;
	}
}

static int read_dict(const tg_cff *c, size_t from, size_t to, entry_t *e, char *m, size_t ms)
{
	double operands[DICT_OPERANDS];
	uint32_t n = 0;
	size_t at = from;

	memset(e, 0, sizeof(entry_t) * D_COUNT);
	while (at < to) {
		uint8_t b0 = c->data[at];
		double v;

		if (b0 <= 21) {
			uint32_t op = b0;
			at++;
			if (b0 == 12) {
				if (at >= to) {
					return refuse(m, ms, "a DICT is cut short in an operator");
				}
				op = 1200 + c->data[at++];
			}
			int slot = dict_slot(op);
			if (slot >= 0) {
				memcpy(e[slot].v, operands, sizeof(double) * n);
				e[slot].n = n;
				e[slot].set = true;
			}
			n = 0;
			continue;
		}
		if (b0 >= 32 && b0 <= 246) {
			v = b0 - 139;
			at++;
		} else if (b0 >= 247 && b0 <= 250) {
			if (at + 1 >= to) {
				return refuse(m, ms, "a DICT is cut short in a number");
			}
			v = (b0 - 247) * 256 + c->data[at + 1] + 108;
			at += 2;
		} else if (b0 >= 251 && b0 <= 254) {
			if (at + 1 >= to) {
				return refuse(m, ms, "a DICT is cut short in a number");
			}
			v = -(b0 - 251) * 256 - c->data[at + 1] - 108;
			at += 2;
		} else if (b0 == 28) {
			if (at + 2 >= to) {
				return refuse(m, ms, "a DICT is cut short in a number");
			}
			v = (int16_t) (c->data[at + 1] << 8 | c->data[at + 2]);
			at += 3;
		} else if (b0 == 29) {
			if (at + 4 >= to) {
				return refuse(m, ms, "a DICT is cut short in a number");
			}
			v = (int32_t) ((uint32_t) c->data[at + 1] << 24 | (uint32_t) c->data[at + 2] << 16 | (uint32_t) c->data[at + 3] << 8 | c->data[at + 4]);
			at += 5;
		} else if (b0 == 30) {
			char text[64];
			size_t len = 0;
			bool done = false;

			at++;
			while (!done) {
				if (at >= to) {
					return refuse(m, ms, "a DICT is cut short in a number");
				}
				uint8_t byte = c->data[at++];
				for (int half = 0; half < 2 && !done; half++) {
					uint8_t nib = half == 0 ? byte >> 4 : byte & 0xF;
					const char *add = NULL;
					char digit[2] = {0, 0};

					switch (nib) {
						case 0xA: add = "."; break;
						case 0xB: add = "E"; break;
						case 0xC: add = "E-"; break;
						case 0xD: return refuse(m, ms, "a DICT real number has a reserved nibble");
						case 0xE: add = "-"; break;
						case 0xF: done = true; break;
						default: digit[0] = (char) ('0' + nib); add = digit;
					}
					if (add != NULL) {
						size_t k = strlen(add);
						if (len + k >= sizeof(text)) {
							return refuse(m, ms, "a DICT real number is too long");
						}
						memcpy(text + len, add, k);
						len += k;
					}
				}
			}
			text[len] = '\0';
			char *end;
			v = strtod(text, &end);
			if (len == 0 || *end != '\0' || !isfinite(v)) {
				return refuse(m, ms, "a DICT real number reads '%s'", text);
			}
		} else {
			return refuse(m, ms, "a DICT has byte %u, which is neither operand nor operator", b0);
		}
		if (n >= DICT_OPERANDS) {
			return refuse(m, ms, "a DICT has more than %d operands", DICT_OPERANDS);
		}
		operands[n++] = v;
	}
	return TG_CFF_OK;
}

static int32_t bias(uint32_t count)
{
	return count < 1240 ? 107 : count < 33900 ? 1131 : 32768;
}

/* A DICT's Private DICT read for its local subrs. */
static int private_subrs(const tg_cff *c, const entry_t *dict, font_t *font, char *m, size_t ms)
{
	font->subrs = (index_t) {0, 1, 0};
	font->bias = 0;
	if (!dict[D_PRIVATE].set) {
		return TG_CFF_OK;
	}
	if (dict[D_PRIVATE].n < 2) {
		return refuse(m, ms, "its Private DICT lies outside the table");
	}
	double size = dict[D_PRIVATE].v[0], offset = dict[D_PRIVATE].v[1];
	if (size < 0 || offset < 0 || offset + size > (double) c->length) {
		return refuse(m, ms, "its Private DICT lies outside the table");
	}
	entry_t private[D_COUNT];
	int rc = read_dict(c, (size_t) offset, (size_t) (offset + size), private, m, ms);
	if (rc != TG_CFF_OK) {
		return rc;
	}
	if (!private[D_SUBRS].set || private[D_SUBRS].n < 1) {
		return TG_CFF_OK;
	}
	double at = offset + private[D_SUBRS].v[0];
	if (at < 0 || at > (double) c->length) {
		return refuse(m, ms, "its local subrs lie outside the table");
	}
	rc = read_index(c, (size_t) at, &font->subrs, NULL, m, ms);
	font->bias = bias(font->subrs.count);
	return rc;
}

/* A FontMatrix as FreeType holds it: divided by its scale (yy, or yx when yy is 0); *em is 1 / the scale. */
static int normalized(const entry_t *e, double out[6], double *em, char *m, size_t ms)
{
	double d[6] = {0.001, 0, 0, 0.001, 0, 0};
	const double *v = d;

	if (e->set) {
		if (e->n != 6) {
			return refuse(m, ms, "a FontMatrix has %u numbers; it has 6", e->n);
		}
		v = e->v;
	}
	double scale = v[3] != 0 ? fabs(v[3]) : fabs(v[1]);
	if (scale == 0 || v[0] * v[3] - v[1] * v[2] == 0) {
		return refuse(m, ms, "a FontMatrix flattens the plane");
	}
	for (int i = 0; i < 6; i++) {
		out[i] = v[i] / scale;
	}
	*em = 1.0 / scale;
	return TG_CFF_OK;
}

static void set_matrix(font_t *font, const double v[6])
{
	static const double identity[6] = {1, 0, 0, 1, 0, 0};

	font->transform = false;
	for (int i = 0; i < 6; i++) {
		font->m[i] = v[i];
		if (fabs(v[i] - identity[i]) > 1e-9) {
			font->transform = true;
		}
	}
}

int tg_cff_open(const uint8_t *data, size_t length, tg_cff **out, char *m, size_t ms)
{
	tg_cff *c = calloc(1, sizeof(tg_cff));
	index_t names, tops, strings;
	size_t at, from, to;
	entry_t top[D_COUNT];
	int rc;

	*out = NULL;
	if (c == NULL) {
		snprintf(m, ms, "out of memory");
		return TG_CFF_NO_MEMORY;
	}
	c->data = data;
	c->length = length;
	c->fdselect = -1;
	if (length < 4 || data[0] != 1) {
		free(c);
		return refuse(m, ms, "it is not a CFF table of major version 1");
	}
	if ((rc = read_index(c, data[2], &names, &at, m, ms)) != TG_CFF_OK
		|| (rc = read_index(c, at, &tops, &at, m, ms)) != TG_CFF_OK
		|| (rc = read_index(c, at, &strings, &at, m, ms)) != TG_CFF_OK
		|| (rc = read_index(c, at, &c->global, NULL, m, ms)) != TG_CFF_OK) {
		free(c);
		return rc;
	}
	c->global_bias = bias(c->global.count);
	if (names.count < 1 || tops.count < 1) {
		free(c);
		return refuse(m, ms, "it has no font in it");
	}
	if ((rc = item(c, &tops, 0, &from, &to, m, ms)) != TG_CFF_OK || (rc = read_dict(c, from, to, top, m, ms)) != TG_CFF_OK) {
		free(c);
		return rc;
	}
	if (top[D_TYPE].set && top[D_TYPE].n >= 1 && (int) top[D_TYPE].v[0] != 2) {
		free(c);
		return refuse(m, ms, "its charstrings are not Type 2");
	}
	if (!top[D_CHARSTRINGS].set || top[D_CHARSTRINGS].n < 1 || top[D_CHARSTRINGS].v[0] < 0 || top[D_CHARSTRINGS].v[0] > (double) length) {
		free(c);
		return refuse(m, ms, "its Top DICT names no CharStrings");
	}
	if ((rc = read_index(c, (size_t) top[D_CHARSTRINGS].v[0], &c->chars, NULL, m, ms)) != TG_CFF_OK) {
		free(c);
		return rc;
	}
	c->charset = top[D_CHARSET].set && top[D_CHARSET].n >= 1 && top[D_CHARSET].v[0] >= 0 ? (uint32_t) top[D_CHARSET].v[0] : 0;

	double shape[6], top_em;
	if ((rc = normalized(&top[D_MATRIX], shape, &top_em, m, ms)) != TG_CFF_OK) {
		free(c);
		return rc;
	}
	if (!top[D_ROS].set) {
		set_matrix(&c->top, shape);
		if ((rc = private_subrs(c, top, &c->top, m, ms)) != TG_CFF_OK) {
			free(c);
			return rc;
		}
		*out = c;
		return TG_CFF_OK;
	}

	/* CID-keyed: each glyph's Font DICT gives its Private DICT */
	if (!top[D_FDARRAY].set || !top[D_FDSELECT].set || top[D_FDARRAY].n < 1 || top[D_FDSELECT].n < 1) {
		free(c);
		return refuse(m, ms, "it is CID-keyed and has no FDArray or no FDSelect");
	}
	index_t array;
	if (top[D_FDARRAY].v[0] < 0 || top[D_FDSELECT].v[0] < 0
		|| (rc = read_index(c, (size_t) top[D_FDARRAY].v[0], &array, NULL, m, ms)) != TG_CFF_OK) {
		free(c);
		return rc != TG_CFF_OK ? rc : refuse(m, ms, "its FDArray lies outside the table");
	}
	c->fonts = calloc(array.count ? array.count : 1, sizeof(font_t));
	if (c->fonts == NULL) {
		free(c);
		snprintf(m, ms, "out of memory");
		return TG_CFF_NO_MEMORY;
	}
	c->nfonts = array.count;
	for (uint32_t i = 0; i < array.count; i++) {
		entry_t font[D_COUNT];
		double sub[6], sub_em, scaled[6];

		if ((rc = item(c, &array, i, &from, &to, m, ms)) != TG_CFF_OK || (rc = read_dict(c, from, to, font, m, ms)) != TG_CFF_OK
			|| (rc = normalized(&font[D_MATRIX], sub, &sub_em, m, ms)) != TG_CFF_OK || (rc = private_subrs(c, font, &c->fonts[i], m, ms)) != TG_CFF_OK) {
			tg_cff_free(c);
			return rc;
		}
		if (top[D_MATRIX].set) {
			/* FreeType: a Top DICT matrix multiplies the Font DICT's, in ems held as whole units */
			double scaling = top_em > 1 && sub_em > 1 ? fmin(top_em, sub_em) : 1.0;
			sub_em = sub_em * top_em / scaling;
		}
		double ratio = top_em / sub_em;
		for (int k = 0; k < 4; k++) {
			scaled[k] = shape[k] * ratio;
		}
		scaled[4] = shape[4];
		scaled[5] = shape[5];
		set_matrix(&c->fonts[i], scaled);
	}
	c->fdselect = (int64_t) top[D_FDSELECT].v[0];
	uint32_t format;
	if (!u8(c, (size_t) c->fdselect, &format)) {
		tg_cff_free(c);
		return refuse(m, ms, "its FDSelect lies past the table");
	}
	if (format != 0 && format != 3) {
		tg_cff_free(c);
		return refuse(m, ms, "its FDSelect is format %u; CFF has formats 0 and 3", format);
	}
	*out = c;
	return TG_CFF_OK;
}

uint32_t tg_cff_glyph_count(const tg_cff *c)
{
	return c->chars.count;
}

void tg_cff_free(tg_cff *c)
{
	if (c != NULL) {
		free(c->fonts);
		free(c);
	}
}

/* The Font DICT a glyph of a CID-keyed font takes. */
static int font_of(const tg_cff *c, uint32_t glyph, const font_t **font, char *m, size_t ms)
{
	uint32_t format, fd = UINT32_MAX;
	size_t at = (size_t) c->fdselect;

	if (c->fdselect < 0) {
		*font = &c->top;
		return TG_CFF_OK;
	}
	u8(c, at, &format);
	if (format == 0) {
		if (!u8(c, at + 1 + glyph, &fd)) {
			return refuse(m, ms, "byte %zu lies past the table", at + 1 + glyph);
		}
	} else {
		uint32_t ranges;
		if (!u16(c, at + 1, &ranges)) {
			return refuse(m, ms, "its FDSelect lies past the table");
		}
		if (ranges < 1) {
			return refuse(m, ms, "its FDSelect has no ranges");
		}
		int64_t low = 0, high = (int64_t) ranges - 1;
		while (low <= high) {
			int64_t mid = (low + high) / 2;
			uint32_t first, next, value;
			if (!u16(c, at + 3 + 3 * (size_t) mid, &first) || !u16(c, at + 3 + 3 * (size_t) (mid + 1), &next)) {
				return refuse(m, ms, "its FDSelect lies past the table");
			}
			if (glyph < first) {
				high = mid - 1;
			} else if (glyph >= next) {
				low = mid + 1;
			} else {
				if (!u8(c, at + 5 + 3 * (size_t) mid, &value)) {
					return refuse(m, ms, "its FDSelect lies past the table");
				}
				fd = value;
				break;
			}
		}
		if (fd == UINT32_MAX) {
			return refuse(m, ms, "its FDSelect gives glyph %u no Font DICT", glyph);
		}
	}
	if (fd >= c->nfonts) {
		return refuse(m, ms, "its FDSelect gives glyph %u Font DICT %u; it has %u", glyph, fd, c->nfonts);
	}
	*font = &c->fonts[fd];
	return TG_CFF_OK;
}

/* ------------------------------------------------------------------ Type 2 */

typedef struct {
	tg_cff *cff;
	const tg_path_sink *sink;
	void *user;
	const font_t *font;         /* the glyph's subrs */
	const double *m;            /* the matrix its points go through; NULL for none */
	double stack[STACK];
	uint32_t n;
	double transient[TRANSIENT];
	double x, y;
	uint32_t stems;
	bool open, width_taken;
	char *message;
	size_t size;
} run_t;

enum { RUN_END = 0, RUN_ENDCHAR = 1, RUN_SEAC = 2, RUN_FAIL = -1 };

#define FAIL(...) (refuse(r->message, r->size, __VA_ARGS__), RUN_FAIL)

static void apply(const run_t *r, double x, double y, double *ox, double *oy)
{
	if (r->m == NULL) {
		*ox = x;
		*oy = y;
		return;
	}
	*ox = r->m[0] * x + r->m[2] * y + r->m[4];
	*oy = r->m[1] * x + r->m[3] * y + r->m[5];
}

static int sink_fail(run_t *r)
{
	snprintf(r->message, r->size, "the outline's sink stopped it");
	return RUN_FAIL;
}

static int close_open(run_t *r)
{
	if (r->open) {
		r->open = false;
		if (r->sink->close(r->user) != 0) {
			return sink_fail(r);
		}
	}
	return 0;
}

static int move_to(run_t *r, double x, double y)
{
	double tx, ty;

	if (close_open(r) != 0) {
		return RUN_FAIL;
	}
	apply(r, x, y, &tx, &ty);
	if (r->sink->move(r->user, tx, ty) != 0) {
		return sink_fail(r);
	}
	r->x = x;
	r->y = y;
	r->open = true;
	return 0;
}

/* A drawing operator before any moveto starts at the current point, as Adobe's engine does. */
static int opened(run_t *r)
{
	double tx, ty;

	if (!r->open) {
		apply(r, r->x, r->y, &tx, &ty);
		if (r->sink->move(r->user, tx, ty) != 0) {
			return sink_fail(r);
		}
		r->open = true;
	}
	return 0;
}

static int line_to(run_t *r, double x, double y)
{
	double tx, ty;

	if (opened(r) != 0) {
		return RUN_FAIL;
	}
	apply(r, x, y, &tx, &ty);
	if (r->sink->line(r->user, tx, ty) != 0) {
		return sink_fail(r);
	}
	r->x = x;
	r->y = y;
	return 0;
}

static int curve(run_t *r, double dx1, double dy1, double dx2, double dy2, double dx3, double dy3)
{
	double x1 = r->x + dx1, y1 = r->y + dy1, x2 = x1 + dx2, y2 = y1 + dy2, x3 = x2 + dx3, y3 = y2 + dy3;
	double t[6];

	if (opened(r) != 0) {
		return RUN_FAIL;
	}
	apply(r, x1, y1, &t[0], &t[1]);
	apply(r, x2, y2, &t[2], &t[3]);
	apply(r, x3, y3, &t[4], &t[5]);
	if (r->sink->cubic(r->user, t[0], t[1], t[2], t[3], t[4], t[5]) != 0) {
		return sink_fail(r);
	}
	r->x = x3;
	r->y = y3;
	return 0;
}

/* The first stack-clearing operator may carry the advance width first: it is taken off. */
static void width(run_t *r, bool extra)
{
	if (!r->width_taken && extra) {
		memmove(r->stack, r->stack + 1, sizeof(double) * (r->n - 1));
		r->n--;
	}
	r->width_taken = true;
}

#define NEED_EXACT(k) do { if (r->n != (k)) return FAIL("an operator takes %d values, got %u", (k), r->n); } while (0)
#define NEED_AT_LEAST(k) do { if (r->n < (k)) return FAIL("an operator takes %d values, got %u", (k), r->n); } while (0)
#define CALL(e) do { if ((e) != 0) return RUN_FAIL; } while (0)

static int run(run_t *r, size_t from, size_t to, int depth, double seac[4])
{
	const uint8_t *d = r->cff->data;
	size_t at = from;
	double *s = r->stack;

	while (at < to) {
		uint8_t b0 = d[at];
		uint32_t op;

		if (b0 >= 32 || b0 == 28) {
			double v;
			if (r->n >= STACK) {
				return FAIL("a charstring has more than %d values on its stack", STACK);
			}
			if (b0 >= 32 && b0 <= 246) {
				v = b0 - 139;
				at++;
			} else if (b0 >= 247 && b0 <= 250) {
				if (at + 2 > to) {
					return FAIL("a charstring is cut short in a number");
				}
				v = (b0 - 247) * 256 + d[at + 1] + 108;
				at += 2;
			} else if (b0 >= 251 && b0 <= 254) {
				if (at + 2 > to) {
					return FAIL("a charstring is cut short in a number");
				}
				v = -(b0 - 251) * 256 - d[at + 1] - 108;
				at += 2;
			} else if (b0 == 28) {
				if (at + 3 > to) {
					return FAIL("a charstring is cut short in a number");
				}
				v = (int16_t) (d[at + 1] << 8 | d[at + 2]);
				at += 3;
			} else {
				/* 255: a 16.16 fixed-point number */
				if (at + 5 > to) {
					return FAIL("a charstring is cut short in a number");
				}
				v = (int32_t) ((uint32_t) d[at + 1] << 24 | (uint32_t) d[at + 2] << 16 | (uint32_t) d[at + 3] << 8 | d[at + 4]) / 65536.0;
				at += 5;
			}
			s[r->n++] = v;
			continue;
		}
		at++;
		if (b0 == 12) {
			if (at + 1 > to) {
				return FAIL("a charstring is cut short in a number");
			}
			op = 1200 + d[at++];
		} else {
			op = b0;
		}
		uint32_t n = r->n;
		switch (op) {
			case 1: case 3: case 18: case 23:   /* stems */
				width(r, r->n % 2 == 1);
				r->stems += r->n / 2;
				r->n = 0;
				break;
			case 19: case 20:                   /* hintmask, cntrmask: values before it are vstems */
				if (r->n > 0) {
					width(r, r->n % 2 == 1);
					r->stems += r->n / 2;
				}
				r->width_taken = true;
				r->n = 0;
				at += (r->stems + 7) / 8;
				if (at > to) {
					return FAIL("a hintmask runs past its charstring");
				}
				break;
			case 21:                            /* rmoveto */
				width(r, r->n > 2);
				NEED_EXACT(2);
				CALL(move_to(r, r->x + s[0], r->y + s[1]));
				r->n = 0;
				break;
			case 22:                            /* hmoveto */
				width(r, r->n > 1);
				NEED_EXACT(1);
				CALL(move_to(r, r->x + s[0], r->y));
				r->n = 0;
				break;
			case 4:                             /* vmoveto */
				width(r, r->n > 1);
				NEED_EXACT(1);
				CALL(move_to(r, r->x, r->y + s[0]));
				r->n = 0;
				break;
			case 5:                             /* rlineto */
				if (n == 0 || n % 2 != 0) {
					return FAIL("an operator takes a multiple of 2 values, got %u", n);
				}
				for (uint32_t i = 0; i < n; i += 2) {
					CALL(line_to(r, r->x + s[i], r->y + s[i + 1]));
				}
				r->n = 0;
				break;
			case 6: case 7: {                   /* hlineto, vlineto */
				NEED_AT_LEAST(1);
				bool horizontal = op == 6;
				for (uint32_t i = 0; i < n; i++) {
					CALL(horizontal ? line_to(r, r->x + s[i], r->y) : line_to(r, r->x, r->y + s[i]));
					horizontal = !horizontal;
				}
				r->n = 0;
				break;
			}
			case 8:                             /* rrcurveto */
				if (n == 0 || n % 6 != 0) {
					return FAIL("an operator takes a multiple of 6 values, got %u", n);
				}
				for (uint32_t i = 0; i < n; i += 6) {
					CALL(curve(r, s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]));
				}
				r->n = 0;
				break;
			case 27: case 26: {                 /* hhcurveto, vvcurveto */
				uint32_t i = n % 4 == 1 ? 1 : 0;
				double first = i ? s[0] : 0.0;
				if (n - i < 4 || (n - i) % 4 != 0) {
					return FAIL("%s takes 4n or 4n + 1 values, got %u", op == 27 ? "hhcurveto" : "vvcurveto", n);
				}
				for (; i < n; i += 4) {
					CALL(op == 27 ? curve(r, s[i], first, s[i + 1], s[i + 2], s[i + 3], 0.0) : curve(r, first, s[i], s[i + 1], s[i + 2], 0.0, s[i + 3]));
					first = 0.0;
				}
				r->n = 0;
				break;
			}
			case 31: case 30: {                 /* hvcurveto, vhcurveto */
				if (n < 4 || (n % 4 != 0 && n % 4 != 1)) {
					return FAIL("%s takes 4n or 4n + 1 values, got %u", op == 31 ? "hvcurveto" : "vhcurveto", n);
				}
				bool horizontal = op == 31;
				for (uint32_t i = 0; i + 4 <= n; i += 4) {
					double last = i + 5 == n ? s[i + 4] : 0.0;
					CALL(horizontal ? curve(r, s[i], 0.0, s[i + 1], s[i + 2], last, s[i + 3]) : curve(r, 0.0, s[i], s[i + 1], s[i + 2], s[i + 3], last));
					horizontal = !horizontal;
				}
				r->n = 0;
				break;
			}
			case 24:                            /* rcurveline */
				if (n < 8 || (n - 2) % 6 != 0) {
					return FAIL("rcurveline takes 6n + 2 values, got %u", n);
				}
				for (uint32_t i = 0; i < n - 2; i += 6) {
					CALL(curve(r, s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]));
				}
				CALL(line_to(r, r->x + s[n - 2], r->y + s[n - 1]));
				r->n = 0;
				break;
			case 25:                            /* rlinecurve */
				if (n < 8 || (n - 6) % 2 != 0) {
					return FAIL("rlinecurve takes 2n + 6 values, got %u", n);
				}
				for (uint32_t i = 0; i < n - 6; i += 2) {
					CALL(line_to(r, r->x + s[i], r->y + s[i + 1]));
				}
				CALL(curve(r, s[n - 6], s[n - 5], s[n - 4], s[n - 3], s[n - 2], s[n - 1]));
				r->n = 0;
				break;
			case 1235:                          /* flex: the depth value passed over */
				NEED_EXACT(13);
				CALL(curve(r, s[0], s[1], s[2], s[3], s[4], s[5]));
				CALL(curve(r, s[6], s[7], s[8], s[9], s[10], s[11]));
				r->n = 0;
				break;
			case 1234: {                        /* hflex */
				NEED_EXACT(7);
				double y0 = r->y;
				CALL(curve(r, s[0], 0.0, s[1], s[2], s[3], 0.0));
				CALL(curve(r, s[4], 0.0, s[5], y0 - r->y, s[6], 0.0));
				r->n = 0;
				break;
			}
			case 1236: {                        /* hflex1: the second curve ends on the first's starting height */
				NEED_EXACT(9);
				double y0 = r->y;
				CALL(curve(r, s[0], s[1], s[2], s[3], s[4], 0.0));
				CALL(curve(r, s[5], 0.0, s[6], s[7], s[8], y0 - (r->y + s[7])));
				r->n = 0;
				break;
			}
			case 1237: {                        /* flex1: d6 along the longer way, the other ends where it began */
				NEED_EXACT(11);
				double x0 = r->x, y0 = r->y;
				double dx = s[0] + s[2] + s[4] + s[6] + s[8], dy = s[1] + s[3] + s[5] + s[7] + s[9];
				CALL(curve(r, s[0], s[1], s[2], s[3], s[4], s[5]));
				if (fabs(dx) > fabs(dy)) {
					CALL(curve(r, s[6], s[7], s[8], s[9], s[10], y0 - (r->y + s[7] + s[9])));
				} else {
					CALL(curve(r, s[6], s[7], s[8], s[9], x0 - (r->x + s[6] + s[8]), s[10]));
				}
				r->n = 0;
				break;
			}
			case 10: case 29: {                 /* callsubr, callgsubr */
				if (r->n == 0) {
					return FAIL("a subroutine call has no number");
				}
				bool local = op == 10;
				const index_t *subrs = local ? &r->font->subrs : &r->cff->global;
				double number = s[--r->n] + (local ? r->font->bias : r->cff->global_bias);
				if (!(number >= 0) || number >= subrs->count || subrs->count == 0) {
					return FAIL("%s subroutine %.0f is not in the font", local ? "local" : "global", number);
				}
				if (depth + 1 > DEPTH) {
					return FAIL("subroutines nest more than %d deep", DEPTH);
				}
				size_t sf, st;
				if (item(r->cff, subrs, (uint32_t) number, &sf, &st, r->message, r->size) != TG_CFF_OK) {
					return RUN_FAIL;
				}
				int ended = run(r, sf, st, depth + 1, seac);
				if (ended != RUN_END) {
					return ended;
				}
				break;
			}
			case 11:                            /* return */
				return RUN_END;
			case 14:                            /* endchar */
				width(r, n == 1 || n == 5);
				if (close_open(r) != 0) {
					return RUN_FAIL;
				}
				if (r->n == 4) {
					memcpy(seac, s, sizeof(double) * 4);
					return RUN_SEAC;
				}
				if (r->n != 0) {
					return FAIL("endchar takes 0 or 4 values, got %u", r->n);
				}
				return RUN_ENDCHAR;
			/* arithmetic and storage */
			case 1203: NEED_AT_LEAST(2); s[n - 2] = (s[n - 2] != 0 && s[n - 1] != 0) ? 1 : 0; r->n--; break;
			case 1204: NEED_AT_LEAST(2); s[n - 2] = (s[n - 2] != 0 || s[n - 1] != 0) ? 1 : 0; r->n--; break;
			case 1205: NEED_AT_LEAST(1); s[n - 1] = s[n - 1] == 0 ? 1 : 0; break;
			case 1209: NEED_AT_LEAST(1); s[n - 1] = fabs(s[n - 1]); break;
			case 1210: NEED_AT_LEAST(2); s[n - 2] = s[n - 2] + s[n - 1]; r->n--; break;
			case 1211: NEED_AT_LEAST(2); s[n - 2] = s[n - 2] - s[n - 1]; r->n--; break;
			case 1212:
				NEED_AT_LEAST(2);
				if (s[n - 1] == 0) {
					return FAIL("a charstring divides by 0");
				}
				s[n - 2] = s[n - 2] / s[n - 1];
				r->n--;
				break;
			case 1214: NEED_AT_LEAST(1); s[n - 1] = -s[n - 1]; break;
			case 1215: NEED_AT_LEAST(2); s[n - 2] = s[n - 2] == s[n - 1] ? 1 : 0; r->n--; break;
			case 1218: NEED_AT_LEAST(1); r->n--; break;
			case 1220: {                        /* put */
				NEED_AT_LEAST(2);
				int i = (int) s[n - 1];
				if (i < 0 || i >= TRANSIENT) {
					return FAIL("a charstring reaches transient slot %d; there are %d", i, TRANSIENT);
				}
				r->transient[i] = s[n - 2];
				r->n -= 2;
				break;
			}
			case 1221: {                        /* get */
				NEED_AT_LEAST(1);
				int i = (int) s[n - 1];
				if (i < 0 || i >= TRANSIENT) {
					return FAIL("a charstring reaches transient slot %d; there are %d", i, TRANSIENT);
				}
				s[n - 1] = r->transient[i];
				break;
			}
			case 1222:                          /* ifelse */
				NEED_AT_LEAST(4);
				s[n - 4] = s[n - 2] <= s[n - 1] ? s[n - 4] : s[n - 3];
				r->n -= 3;
				break;
			case 1223:
				return FAIL("a charstring asks for 'random', which draws a glyph differently each time");
			case 1224: NEED_AT_LEAST(2); s[n - 2] = s[n - 2] * s[n - 1]; r->n--; break;
			case 1226:
				NEED_AT_LEAST(1);
				if (s[n - 1] < 0) {
					return FAIL("a charstring takes the square root of a negative number");
				}
				s[n - 1] = sqrt(s[n - 1]);
				break;
			case 1227:                          /* dup */
				NEED_AT_LEAST(1);
				if (n >= STACK) {
					return FAIL("a charstring has more than %d values on its stack", STACK);
				}
				s[n] = s[n - 1];
				r->n++;
				break;
			case 1228: {                        /* exch */
				NEED_AT_LEAST(2);
				double t = s[n - 1];
				s[n - 1] = s[n - 2];
				s[n - 2] = t;
				break;
			}
			case 1229: {                        /* index */
				NEED_AT_LEAST(1);
				int i = (int) s[n - 1];
				r->n--;
				int64_t from_top = i < 0 ? 0 : i;
				if ((int64_t) r->n - 1 - from_top < 0) {
					return FAIL("index reaches past the stack");
				}
				s[r->n] = s[r->n - 1 - from_top];
				r->n++;
				break;
			}
			case 1230: {                        /* roll */
				NEED_AT_LEAST(2);
				int j = (int) s[n - 1], k = (int) s[n - 2];
				r->n -= 2;
				if (k < 0 || (uint32_t) k > r->n) {
					return FAIL("roll reaches past the stack");
				}
				if (k > 0) {
					double top[STACK];
					uint32_t base = r->n - (uint32_t) k;
					j = ((j % k) + k) % k;
					for (int q = 0; q < k; q++) {
						top[(q + j) % k] = s[base + q];
					}
					memcpy(s + base, top, sizeof(double) * k);
				}
				break;
			}
			default:
				if (op >= 1200) {
					return FAIL("a charstring has operator 12 %u, which Type 2 does not have", op - 1200);
				}
				return FAIL("a charstring has operator %u, which Type 2 does not have", op);
		}
	}
	return RUN_END;
}

static int sid_glyph(const tg_cff *c, uint32_t sid, uint32_t *glyph)
{
	uint32_t count = c->chars.count, format, g = 1;
	size_t at = c->charset;

	if (c->charset == 0) {
		/* ISOAdobe: glyph i is SID i */
		if (sid < count && sid < 229) {
			*glyph = sid;
			return 1;
		}
		return 0;
	}
	if (c->charset < 3) {
		return -1;
	}
	if (!u8(c, at++, &format)) {
		return -2;
	}
	while (g < count) {
		uint32_t first, left;
		if (format == 0) {
			if (!u16(c, at, &first)) {
				return -2;
			}
			if (first == sid) {
				*glyph = g;
				return 1;
			}
			at += 2;
			g++;
			continue;
		}
		if (format != 1 && format != 2) {
			return -3;
		}
		if (!u16(c, at, &first)) {
			return -2;
		}
		if (format == 1) {
			if (!u8(c, at + 2, &left)) {
				return -2;
			}
			at += 3;
		} else {
			if (!u16(c, at + 2, &left)) {
				return -2;
			}
			at += 4;
		}
		if (sid >= first && sid <= first + left && g + (sid - first) < count) {
			*glyph = g + (sid - first);
			return 1;
		}
		g += left + 1;
	}
	return 0;
}

static int standard_glyph(run_t *r, double code, uint32_t glyph, uint32_t *out)
{
	int c = (int) code;
	uint32_t sid = c >= 0 && c < 256 ? STANDARD[c] : 0;

	if (sid == 0) {
		return FAIL("glyph %u's accent form names code %d, which StandardEncoding has no glyph for", glyph, c);
	}
	switch (sid_glyph(r->cff, sid, out)) {
		case 1: return 0;
		case -1: return FAIL("its accented glyphs need the Expert charset, which this reader does not carry");
		case -2: return FAIL("its charset lies past the table");
		case -3: return FAIL("its charset is not format 0, 1 or 2");
		default: return FAIL("glyph %u's accent form names a glyph the font does not have", glyph);
	}
}

/* One glyph's charstring run through the sink, moved by (dx, dy); seac may run two more. */
static int draw(run_t *r, uint32_t glyph, double dx, double dy, bool seac_allowed)
{
	size_t from, to;
	double seac[4];
	uint32_t base, accent;

	r->n = 0;
	memset(r->transient, 0, sizeof(r->transient));
	r->x = dx;
	r->y = dy;
	r->open = false;
	r->stems = 0;
	r->width_taken = false;
	if (item(r->cff, &r->cff->chars, glyph, &from, &to, r->message, r->size) != TG_CFF_OK) {
		return RUN_FAIL;
	}
	int ended = run(r, from, to, 0, seac);
	if (ended == RUN_FAIL || close_open(r) != 0) {
		return RUN_FAIL;
	}
	if (ended != RUN_SEAC) {
		return 0;
	}
	if (!seac_allowed || r->cff->fdselect >= 0) {
		return FAIL("glyph %u uses endchar's accent form %s", glyph, r->cff->fdselect >= 0 ? "in a CID-keyed font" : "inside an accent");
	}
	if (standard_glyph(r, seac[2], glyph, &base) != 0 || standard_glyph(r, seac[3], glyph, &accent) != 0) {
		return RUN_FAIL;
	}
	if (draw(r, base, dx, dy, false) != 0) {
		return RUN_FAIL;
	}
	return draw(r, accent, dx + seac[0], dy + seac[1], false);
}

int tg_cff_outline(tg_cff *c, uint32_t glyph, const tg_path_sink *sink, void *user, char *message, size_t size)
{
	run_t r;
	const font_t *font;

	if (glyph >= c->chars.count) {
		snprintf(message, size, "it has %u CFF glyphs; there is no glyph %u.", c->chars.count, glyph);
		return TG_CFF_NO_GLYPH;
	}
	if (font_of(c, glyph, &font, message, size) != TG_CFF_OK) {
		return TG_CFF_BAD;
	}
	memset(&r, 0, sizeof(r));
	r.cff = c;
	r.sink = sink;
	r.user = user;
	r.font = font;
	r.m = font->transform ? font->m : NULL;
	r.message = message;
	r.size = size;

	return draw(&r, glyph, 0.0, 0.0, true) == 0 ? TG_CFF_OK : TG_CFF_BAD;
}
