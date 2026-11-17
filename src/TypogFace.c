/*
 * TypogFace: one face of a font file, opened, over the plain C reader in sfnt.c.
 */

#include "runtime.h"
#include "zend_exceptions.h"
#include "../stubs/TypogFace_arginfo.h"

zend_class_entry *phptg_ce_TypogFace;
static zend_object_handlers phptg_face_handlers;

static zend_object *phptg_face_create(zend_class_entry *ce)
{
	phptg_face *intern = zend_object_alloc(sizeof(phptg_face), ce);
	intern->face.fd = -1;
	intern->opened = false;
	zend_object_std_init(&intern->std, ce);
	object_properties_init(&intern->std, ce);
	intern->std.handlers = &phptg_face_handlers;
	return &intern->std;
}

static void phptg_face_free(zend_object *object)
{
	phptg_face *intern = phptg_face_from(object);
	if (intern->opened) {
		tg_free(&intern->face);
	}
	zend_object_std_dtor(object);
}

/* The face, or NULL with an Error thrown when the constructor did not finish. */
static tg_face *opened(zval *self)
{
	phptg_face *intern = phptg_face_from(Z_OBJ_P(self));
	if (!intern->opened) {
		zend_throw_error(NULL, "TypogFace was not opened: its constructor did not finish");
		return NULL;
	}
	return &intern->face;
}

/* A glyph number or codepoint from PHP: 0..UINT32_MAX, else a ValueError naming the argument. */
static bool unsigned_argument(zend_long value, uint32_t position, uint32_t *out)
{
	if (value < 0 || (uint64_t) value > UINT32_MAX) {
		zend_argument_value_error(position, "must be between 0 and %u", UINT32_MAX);
		return false;
	}
	*out = (uint32_t) value;
	return true;
}

PHP_METHOD(TypogFace, __construct)
{
	zend_string *path;
	zend_long index = 0;
	HashTable *variations = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_PATH_STR(path)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(index)
		Z_PARAM_ARRAY_HT(variations)
	ZEND_PARSE_PARAMETERS_END();

	phptg_face *intern = phptg_face_from(Z_OBJ_P(ZEND_THIS));
	if (intern->opened) {
		zend_throw_error(NULL, "TypogFace is already open");
		RETURN_THROWS();
	}
	uint32_t face_index;
	if (!unsigned_argument(index, 2, &face_index)) {
		RETURN_THROWS();
	}
	/* the variations: four-character axis tags, finite values */
	uint32_t count = variations == NULL ? 0 : zend_hash_num_elements(variations);
	char (*tags)[4] = count ? emalloc(sizeof(*tags) * count) : NULL;
	double *values = count ? emalloc(sizeof(double) * count) : NULL;
	uint32_t n = 0;
	if (count) {
		zend_string *key;
		zval *value;
		ZEND_HASH_FOREACH_STR_KEY_VAL(variations, key, value) {
			if (key == NULL || ZSTR_LEN(key) != 4) {
				efree(tags);
				efree(values);
				zend_argument_value_error(3, "must map four-character axis tags to numbers");
				RETURN_THROWS();
			}
			ZVAL_DEREF(value);
			if (Z_TYPE_P(value) != IS_LONG && Z_TYPE_P(value) != IS_DOUBLE) {
				efree(tags);
				efree(values);
				zend_argument_value_error(3, "must map four-character axis tags to numbers");
				RETURN_THROWS();
			}
			double v = zval_get_double(value);
			if (!zend_finite(v)) {
				efree(tags);
				efree(values);
				zend_argument_value_error(3, "must hold finite numbers");
				RETURN_THROWS();
			}
			memcpy(tags[n], ZSTR_VAL(key), 4);
			values[n++] = v;
		} ZEND_HASH_FOREACH_END();
	}
	int code = tg_open(&intern->face, ZSTR_VAL(path), face_index);
	if (code == TG_OK && n > 0) {
		code = tg_vary(&intern->face, (const char (*)[4]) tags, values, n);
		if (code != TG_OK) {
			char message[TG_MESSAGE];
			memcpy(message, intern->face.message, TG_MESSAGE);
			tg_free(&intern->face);
			memcpy(intern->face.message, message, TG_MESSAGE);
		}
	}
	if (tags != NULL) {
		efree(tags);
		efree(values);
	}
	if (code != TG_OK) {
		phptg_throw(code, intern->face.message);
		RETURN_THROWS();
	}
	intern->opened = true;
}

PHP_METHOD(TypogFace, describe)
{
	zend_string *path;
	bool skip_broken = false;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_PATH_STR(path)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(skip_broken)
	ZEND_PARSE_PARAMETERS_END();

	tg_described *faces;
	uint32_t count;
	char message[TG_MESSAGE];
	int code = tg_describe(ZSTR_VAL(path), skip_broken, &faces, &count, message);
	if (code != TG_OK) {
		phptg_throw(code, message);
		RETURN_THROWS();
	}
	array_init_size(return_value, count);
	for (uint32_t i = 0; i < count; i++) {
		zval entry;
		array_init_size(&entry, 7);
		add_next_index_long(&entry, faces[i].index);
		add_next_index_string(&entry, faces[i].family);
		add_next_index_long(&entry, faces[i].weight);
		add_next_index_long(&entry, faces[i].slant);
		add_next_index_long(&entry, faces[i].width);
		zval axes;
		array_init_size(&axes, faces[i].naxes);
		for (uint32_t a = 0; a < faces[i].naxes; a++) {
			zval axis;
			array_init_size(&axis, 4);
			add_next_index_string(&axis, faces[i].axes[a].tag);
			add_next_index_double(&axis, faces[i].axes[a].min);
			add_next_index_double(&axis, faces[i].axes[a].def);
			add_next_index_double(&axis, faces[i].axes[a].max);
			add_next_index_zval(&axes, &axis);
		}
		add_next_index_zval(&entry, &axes);
		add_next_index_string(&entry, faces[i].outlines);
		add_next_index_zval(return_value, &entry);
	}
	tg_described_free(faces, count);
}

PHP_METHOD(TypogFace, path)
{
	ZEND_PARSE_PARAMETERS_NONE();
	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	RETURN_STRING(face->path);
}

PHP_METHOD(TypogFace, index)
{
	ZEND_PARSE_PARAMETERS_NONE();
	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	RETURN_LONG(face->index);
}

PHP_METHOD(TypogFace, unitsPerEm)
{
	ZEND_PARSE_PARAMETERS_NONE();
	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	int code = tg_basics(face);
	if (code != TG_OK) {
		phptg_throw(code, face->message);
		RETURN_THROWS();
	}
	RETURN_LONG(face->units_per_em);
}

PHP_METHOD(TypogFace, glyphCount)
{
	ZEND_PARSE_PARAMETERS_NONE();
	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	int code = tg_basics(face);
	if (code != TG_OK) {
		phptg_throw(code, face->message);
		RETURN_THROWS();
	}
	RETURN_LONG(face->glyph_count);
}

PHP_METHOD(TypogFace, vertical)
{
	ZEND_PARSE_PARAMETERS_NONE();
	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	double vertical[3];
	int code = tg_vertical(face, vertical);
	if (code != TG_OK) {
		phptg_throw(code, face->message);
		RETURN_THROWS();
	}
	array_init_size(return_value, 3);
	for (int i = 0; i < 3; i++) {
		add_next_index_double(return_value, vertical[i]);
	}
}

PHP_METHOD(TypogFace, glyph)
{
	zend_long codepoint;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(codepoint)
	ZEND_PARSE_PARAMETERS_END();

	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	uint32_t wanted, glyph;
	if (!unsigned_argument(codepoint, 1, &wanted)) {
		RETURN_THROWS();
	}
	int code = tg_glyph(face, wanted, &glyph);
	if (code != TG_OK) {
		phptg_throw(code, face->message);
		RETURN_THROWS();
	}
	RETURN_LONG(glyph);
}

PHP_METHOD(TypogFace, advance)
{
	zend_long glyph;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(glyph)
	ZEND_PARSE_PARAMETERS_END();

	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	uint32_t which;
	int32_t advance;
	if (!unsigned_argument(glyph, 1, &which)) {
		RETURN_THROWS();
	}
	int code = tg_advance(face, which, &advance);
	if (code != TG_OK) {
		phptg_throw(code, face->message);
		RETURN_THROWS();
	}
	RETURN_LONG(advance);
}

PHP_METHOD(TypogFace, outline)
{
	zend_long glyph;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(glyph)
	ZEND_PARSE_PARAMETERS_END();

	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	uint32_t which;
	if (!unsigned_argument(glyph, 1, &which)) {
		RETURN_THROWS();
	}
	tg_ops ops = {0};
	int code = tg_outline(face, which, &ops);
	if (code != TG_OK) {
		tg_ops_free(&ops);
		phptg_throw(code, face->message);
		RETURN_THROWS();
	}
	array_init_size(return_value, ops.n);
	for (uint32_t i = 0; i < ops.n; i++) {
		const tg_op *op = &ops.ops[i];
		zval step;
		char kind[2] = {op->kind, '\0'};
		int values = op->kind == 'C' ? 6 : op->kind == 'Q' ? 4 : op->kind == 'Z' ? 0 : 2;
		array_init_size(&step, 1 + values);
		add_next_index_stringl(&step, kind, 1);
		for (int v = 0; v < values; v++) {
			add_next_index_double(&step, op->v[v]);
		}
		add_next_index_zval(return_value, &step);
	}
	tg_ops_free(&ops);
}

PHP_METHOD(TypogFace, kerning)
{
	zend_long left, right;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_LONG(left)
		Z_PARAM_LONG(right)
	ZEND_PARSE_PARAMETERS_END();

	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	if (left < 0 || left > 0xFFFF) {
		zend_argument_value_error(1, "must be a glyph number, 0 to 65535");
		RETURN_THROWS();
	}
	if (right < 0 || right > 0xFFFF) {
		zend_argument_value_error(2, "must be a glyph number, 0 to 65535");
		RETURN_THROWS();
	}
	int32_t value;
	int code = tg_kerning(face, (uint32_t) left, (uint32_t) right, &value);
	if (code != TG_OK) {
		phptg_throw(code, face->message);
		RETURN_THROWS();
	}
	RETURN_LONG(value);
}

/* The next codepoint of valid UTF-8 at *at, advancing it; -1 when the bytes there are not valid UTF-8. */
static int32_t next_utf8(const unsigned char *s, size_t length, size_t *at)
{
	size_t i = *at;
	unsigned char c = s[i];
	uint32_t cp, need;
	if (c < 0x80) {
		*at = i + 1;
		return c;
	}
	if (c >= 0xC2 && c <= 0xDF) {
		cp = c & 0x1F;
		need = 1;
	} else if (c >= 0xE0 && c <= 0xEF) {
		cp = c & 0x0F;
		need = 2;
	} else if (c >= 0xF0 && c <= 0xF4) {
		cp = c & 0x07;
		need = 3;
	} else {
		return -1;
	}
	if (i + need >= length) {
		return -1;   /* the sequence runs past the end */
	}
	for (uint32_t k = 1; k <= need; k++) {
		unsigned char next = s[i + k];
		if ((next & 0xC0) != 0x80) {
			return -1;
		}
		cp = (cp << 6) | (next & 0x3F);
	}
	if ((need == 2 && cp < 0x800) || (need == 3 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF)) {
		return -1;
	}
	*at = i + 1 + need;
	return (int32_t) cp;
}

PHP_METHOD(TypogFace, shape)
{
	zend_string *text;
	double size;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_STR(text)
		Z_PARAM_DOUBLE(size)
	ZEND_PARSE_PARAMETERS_END();

	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	if (!zend_finite(size)) {
		zend_argument_value_error(2, "must be a finite number");
		RETURN_THROWS();
	}
	int code = tg_basics(face);
	uint32_t space = 0;
	if (code == TG_OK) {
		code = tg_glyph(face, 0x20, &space);
	}
	if (code != TG_OK) {
		phptg_throw(code, face->message);
		RETURN_THROWS();
	}
	if (space >= face->glyph_count) {
		space = 0;
	}
	const double scale = size / face->units_per_em;
	const unsigned char *bytes = (const unsigned char *) ZSTR_VAL(text);
	size_t length = ZSTR_LEN(text);

	zval glyphs, clusters, advances;
	array_init(&glyphs);
	array_init(&clusters);
	array_init(&advances);
	uint32_t previous = 0, count = 0;
	for (size_t at = 0; at < length;) {
		size_t offset = at;
		int32_t codepoint = next_utf8(bytes, length, &at);
		if (codepoint < 0) {
			zval_ptr_dtor(&glyphs);
			zval_ptr_dtor(&clusters);
			zval_ptr_dtor(&advances);
			zend_argument_value_error(1, "must be valid UTF-8; byte %zu is not", offset);
			RETURN_THROWS();
		}
		uint32_t glyph;
		int32_t advance;
		code = tg_glyph(face, (uint32_t) codepoint, &glyph);
		if (code == TG_OK && glyph >= face->glyph_count) {
			glyph = 0;
		}
		bool ignored = code == TG_OK && glyph == 0 && tg_default_ignorable((uint32_t) codepoint);
		if (ignored) {
			glyph = space;
		}
		int32_t pair = 0;
		if (code == TG_OK && count > 0) {
			code = tg_kerning(face, previous, glyph, &pair);
		}
		if (code == TG_OK) {
			code = tg_advance(face, glyph, &advance);
		}
		if (code != TG_OK) {
			zval_ptr_dtor(&glyphs);
			zval_ptr_dtor(&clusters);
			zval_ptr_dtor(&advances);
			phptg_throw(code, face->message);
			RETURN_THROWS();
		}
		if (count > 0 && pair != 0) {
			zval *before = zend_hash_index_find(Z_ARRVAL(advances), count - 1);
			ZVAL_DOUBLE(before, Z_DVAL_P(before) + pair * scale);
		}
		add_next_index_long(&glyphs, glyph);
		add_next_index_long(&clusters, (zend_long) offset);
		add_next_index_double(&advances, ignored ? 0.0 : advance * scale);
		previous = glyph;
		count++;
	}
	array_init_size(return_value, 3);
	add_next_index_zval(return_value, &glyphs);
	add_next_index_zval(return_value, &clusters);
	add_next_index_zval(return_value, &advances);
}

PHP_METHOD(TypogFace, close)
{
	ZEND_PARSE_PARAMETERS_NONE();
	tg_face *face = opened(ZEND_THIS);
	if (face == NULL) {
		RETURN_THROWS();
	}
	tg_close(face);
}

void phptg_register_TypogFace(void)
{
	phptg_ce_TypogFace = register_class_TypogFace();
	phptg_ce_TypogFace->create_object = phptg_face_create;

	memcpy(&phptg_face_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	phptg_face_handlers.offset = XtOffsetOf(phptg_face, std);
	phptg_face_handlers.free_obj = phptg_face_free;
	phptg_face_handlers.clone_obj = NULL;
}
