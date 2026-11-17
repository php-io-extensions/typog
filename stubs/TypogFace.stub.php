<?php

/** @generate-class-entries */

/**
 * One face of an OpenType or TrueType file, or of a collection, opened. Its tables are read from
 * the file when a call first needs them and kept; close() closes the file, and what was read
 * stays readable. Lengths are font units unless a size is given.
 *
 * @strict-properties
 * @not-serializable
 */
final class TypogFace
{
    /**
     * Opens the face, drawn at the instance $variations names (axis tag => user value): normalized
     * against 'fvar', mapped through 'avar' version 1, then 'gvar', 'HVAR' and 'MVAR' applied. An
     * axis the face lacks is ignored, one not given takes its default; a face without 'fvar' is
     * drawn as it is.
     *
     * @param array $variations axis tag => int|float
     * @throws TypogException When the file cannot be opened, is not a font, has no such face, or its
     *                        variation tables are broken or of a version OpenType does not have.
     * @throws ValueError When a variation is not a four-character tag and a finite number.
     */
    public function __construct(string $path, int $index = 0, array $variations = []) {}

    /**
     * Every face of the file, as a catalog matches one: index, family ('name' 16, else 1),
     * weight (OS/2, else 'head'), slant (TYPOG_UPRIGHT, TYPOG_ITALIC, TYPOG_OBLIQUE), width class,
     * its 'fvar' axes (tag, minimum, default, maximum; none for a static face), and its outline
     * table: 'glyf', 'CFF ', 'CFF2', or '' when it has none of them (bitmaps only, or outlines in a
     * table such as 'hvgl'). A face that cannot be read is left out when $skipBroken, else it is a
     * TypogException.
     *
     * @return list<array{int, string, int, int, int, list<array{string, float, float, float}>, string}>
     * @throws TypogException
     */
    public static function describe(string $path, bool $skipBroken = false): array {}

    public function path(): string {}

    public function index(): int {}

    /** @throws TypogException */
    public function unitsPerEm(): int {}

    /** @throws TypogException */
    public function glyphCount(): int {}

    /**
     * Ascender (up), descender (down, negative) and line gap, as OpenType recommends: OS/2's
     * typographic ones when the face sets USE_TYPO_METRICS, else 'hhea''s, else (when those are
     * zero) OS/2's typographic ones, then its Windows ones; at an instance, the OS/2 ones varied by
     * MVAR's 'hasc', 'hdsc', 'hlgp', 'hcla', 'hcld'.
     *
     * @return array{float, float, float}
     * @throws TypogException
     */
    public function vertical(): array {}

    /**
     * The glyph the character map gives a codepoint, 0 when none: format 12 before format 4, and a
     * Windows symbol map also looked in at U+F000 up for a codepoint up to U+00FF.
     *
     * @throws TypogException
     */
    public function glyph(int $codepoint): int {}

    /**
     * How far the pen moves after the glyph; 0 for a glyph the face does not have. At an instance,
     * varied by 'HVAR', or without it by 'gvar''s phantom points, and rounded.
     *
     * @throws TypogException
     */
    public function advance(int $glyph): int {}

    /**
     * The glyph's outline, unhinted, y up, moved to the origin its metrics give (the
     * first phantom point, or a USE_MY_METRICS component's): path steps ['M', x, y],
     * ['L', x, y], ['Q', cx, cy, x, y], ['Z']; a 'CFF ' face's Type 2 charstrings as cubics,
     * ['C', c1x, c1y, c2x, c2y, x, y] (unhinted, the FontMatrix applied as FreeType applies it).
     * Empty for a glyph that draws nothing.
     *
     * @return list<array>
     * At an instance its points (and phantom points) are moved by 'gvar', unrounded.
     *
     * @throws TypogException TYPOG_CFF for a face whose outlines are CFF2, or whose 'CFF ' table
     *         cannot be read (the message names the font and why); TYPOG_NO_OUTLINES for a face
     *         with no 'glyf', 'CFF ' or 'CFF2' table.
     */
    public function outline(int $glyph): array {}

    /** The value the 'kern' table gives the pair, 0 when none. @throws TypogException */
    public function kerning(int $left, int $right): int {}

    /**
     * The text as glyphs of this face at a size: one glyph a character, its advance, the 'kern'
     * value of each pair added to the first glyph's advance; a default ignorable character the
     * face lacks takes the space glyph and no width. Glyphs, the byte offset each came from, and
     * advances at the size.
     *
     * @return array{list<int>, list<int>, list<float>}
     * @throws TypogException
     */
    public function shape(string $text, float $size): array {}

    /** Closes the file; the tables read so far stay readable. */
    public function close(): void {}
}
