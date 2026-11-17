/*
 * cff: a 'CFF ' table read for its outlines (Adobe TN #5176 and #5177), plain C, no Zend, no
 * dependency on the sfnt reader. Name-keyed and CID-keyed fonts (FDArray, FDSelect formats 0 and
 * 3); Type 2 charstrings run as written: every path operator and flex, hints passed over with the
 * hintmask and cntrmask bytes counted from the stems, subroutines with their bias, the width as an
 * optional first value, the arithmetic and storage operators, endchar's seac form. Outlines come out
 * cubic, in font units, y up, each contour closed, through a sink. The FontMatrix is applied as
 * FreeType applies it. The rules are those of Lustrous's PHP reader (CompactFontFormat).
 *
 * The table is bytes this project did not write: every read is checked; a broken table, a stack
 * past 48, subroutines nested past 10, an operator Type 2 lacks, 'random' and a flattening
 * FontMatrix are refused with a message.
 */
#ifndef TYPOG_CFF_H
#define TYPOG_CFF_H

#include <stddef.h>
#include <stdint.h>

/* What tg_cff_* answer: 0, or this when the table cannot be read (the message says why). */
#define TG_CFF_OK 0
#define TG_CFF_BAD 1
#define TG_CFF_NO_GLYPH 2
#define TG_CFF_NO_MEMORY 3

/* Where an outline goes: font units, y up. Each returns 0, or non-zero to stop. */
typedef struct {
	int (*move)(void *user, double x, double y);
	int (*line)(void *user, double x, double y);
	int (*cubic)(void *user, double x1, double y1, double x2, double y2, double x, double y);
	int (*close)(void *user);
} tg_path_sink;

typedef struct tg_cff tg_cff;

/* Reads the table's structure; the bytes are borrowed and must outlive the reader. */
int tg_cff_open(const uint8_t *data, size_t length, tg_cff **out, char *message, size_t size);

uint32_t tg_cff_glyph_count(const tg_cff *cff);

/* The glyph's outline through the sink. */
int tg_cff_outline(tg_cff *cff, uint32_t glyph, const tg_path_sink *sink, void *user, char *message, size_t size);

void tg_cff_free(tg_cff *cff);

#endif
