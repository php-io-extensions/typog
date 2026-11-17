---
type: API
title: Face
description: "TypogFace: one face of an OpenType or TrueType file, its metrics, character map, advances, outlines (TrueType and CFF), kerning and shaping, and describe() for catalogs."
resource: stubs/TypogFace.stub.php
tags: [api, font, truetype, opentype, cff, variable-fonts]
status: draft
generated: { by: claude-opus/5.5, at: 2026-11-17T12:00:00Z }
sources:
  - id: stub
    resource: stubs/TypogFace.stub.php
    title: TypogFace, the declaration
  - id: reader
    resource: src/sfnt.c
    title: The reader, plain C
  - id: tests
    resource: tests/
    title: Pest, over fonts the tests write
---

# Overview

`new TypogFace($path, $index = 0, $variations = [])` opens one face of a file or collection, at the instance `$variations` names (axis tag => number): the table directory is read then; every other table when a call first needs it, and kept. `close()` closes the file; tables already read stay readable, a call that needs one more is `TYPOG_CLOSED`. Lengths are font units unless a size is given.[^stub]

| Method | Answers |
|---|---|
| `unitsPerEm()`, `glyphCount()` | `head`, `maxp`; a face with fewer than 16 or more than 16384 units an em is `TYPOG_MALFORMED` |
| `vertical()` | `[ascender, descender, lineGap]` as floats, descender negative |
| `glyph($codepoint)` | the character map's glyph, 0 when none (not checked against `glyphCount()`) |
| `advance($glyph)` | `hmtx`; a glyph past the long metrics takes the last one; 0 past `glyphCount()` |
| `outline($glyph)` | path steps `['M', x, y]`, `['L', x, y]`, `['Q', cx, cy, x, y]`, `['Z']`, floats, y up |
| `kerning($left, $right)` | the `kern` value of a pair, 0 when none |
| `shape($text, $size)` | `[glyphs, byte offsets, advances]` |
| `TypogFace::describe($path, $skipBroken)` | every face: `[index, family, weight, slant, width, axes, outlines]`, axes `[tag, min, default, max]` from `fvar`, outlines its outline table (`'glyf'`, `'CFF '`, `'CFF2'`, or `''`) |

# The rules

**Vertical metrics.** OS/2's typographic ascender, descender and gap when its `fsSelection` sets USE_TYPO_METRICS (bit 7); else `hhea`'s; else, when `hhea`'s ascender and descender are both 0, OS/2's typographic ones, and when those are 0 too its Windows ascent and (negated) descent with no gap. An OS/2 shorter than 78 bytes is not read.[^reader]

**Character map.** The first subtable found in this order: (3,10), (0,6), (0,4), (0,3), (0,2), (0,1), (0,0), (3,1) in format 12; (3,1), (0,3), (0,2), (0,1), (0,0) in format 4; (3,0) in format 4. In a (3,0) symbol map, a codepoint up to U+00FF that maps to nothing is looked for again at U+F000 plus it, as Windows and HarfBuzz's own map do. Format 4 maps by delta or through its glyph array.

**Outlines.** TrueType `glyf`, unhinted. Two off-curve points in a row have an implied on-curve point halfway between. A contour starts at its first point when it is on the curve, else at its last when that is, else halfway between the two. Composite glyphs: each component's points through its transform (`x' = a·x + c·y`, `y' = b·x + d·y`; one scale, two scales or a 2×2, F2Dot14), then moved by its offset (scaled only with SCALED_COMPONENT_OFFSET and not UNSCALED) or so its point matches a point placed before it in the same composite. Nesting deeper than 8 is `TYPOG_MALFORMED`. The whole outline then moves across by minus its first phantom point: its left edge less its `hmtx` left side bearing, or, in a composite, the phantom point of the last component that says USE_MY_METRICS, as it stands in that component (FreeType's `ttgload.c`). A face with `CFF2` and no `glyf` is `TYPOG_CFF`: `<path>#<index> has CFF2 outlines, which this reader does not draw.` A face with no `glyf`, `CFF ` or `CFF2` (bitmaps only, or `hvgl`) is `TYPOG_NO_OUTLINES`: `<path>#<index> has no outlines this reader draws: no 'glyf' or 'CFF ' table.`; a glyph that draws nothing in a face it reads is an empty outline.

**CFF outlines** (`src/cff.c`, Adobe TN #5176 and #5177). A face with `CFF ` and no `glyf` has its Type 2 charstrings run as written, into cubics: `['C', c1x, c1y, c2x, c2y, x, y]`, in font units, y up, unhinted, each contour closed. The table is read and its structure checked at the first outline asked for; a glyph past the face's count is `TYPOG_NO_GLYPH` before CFF is read. Name-keyed fonts take the Top DICT's Private DICT; CID-keyed fonts (ROS) take, per glyph, the Font DICT FDSelect (format 0 or 3) names, with its Private DICT and local subrs. Subroutine numbers add the bias their INDEX's count sets (107 under 1240, 1131 under 33900, else 32768); nesting is bounded at 10. The advance width is an optional first value of the first stack-clearing operator and is dropped. Stem hints are counted, never applied: a `hintmask` or `cntrmask` is followed by one mask byte per 8 stems, values before it counting as vstems. Flex, hflex, hflex1 and flex1 draw their two curves (the depth passed over; hflex1 and flex1 end on the starting height or width as the spec says). The arithmetic and storage operators work as Type 2 defines them. `endchar` with four values (seac) draws the base and the accent moved by (adx, ady), each found through StandardEncoding and the charset; in a CID-keyed font or inside an accent it is refused. The FontMatrix is applied as FreeType applies it: a Top DICT matrix that only scales is dropped; any other is applied, a CID Font DICT's multiplied in at its em against the Top DICT's. A stack past 48, nesting past 10, an operator Type 2 lacks, `random`, a division by 0, a FontMatrix that flattens the plane, an FDSelect naming a Font DICT the font lacks, or any read past the table is `TYPOG_CFF`, its message naming the font and why. The same rules as Lustrous's PHP reader (`CompactFontFormat`).

**Variations.** Per the OpenType font variations spec. Each coordinate is clamped to its `fvar` axis and normalized to -1..0..1 around the default in 16.16, mapped through `avar` (a map without -1, 0 and 1 is ignored), then rounded to 2.14 once. An axis the face lacks is ignored; one not given takes its default. Coordinates on a face without `fvar` have nothing to apply to: the face is drawn as it is, as FreeType and HarfBuzz draw it. `gvar`: shared and embedded peaks, intermediate regions, shared and private point numbers, packed deltas (x and y two separate arrays), and IUP inference contour by contour for points a tuple does not name; simple glyphs and composite offsets; the four phantom points move the origin and the advance. Advances from `HVAR` (its advance map, else the glyph as inner index) when the face has one, else from the phantom points. `vertical()`'s OS/2 values vary by `MVAR`'s `hasc`, `hdsc`, `hlgp`, `hcla`, `hcld`; `hhea`'s have no MVAR tag. Region scalars are worked out once per face, not per glyph. Refused as `TYPOG_MALFORMED`: `fvar` or `avar` of a version OpenType does not have, `fvar` instance records other than 4n+4 or 4n+6 bytes, a delta run flagged both zero and words, a delta run longer than its array has left (FreeType and HarfBuzz will not vary by either of the last two).

**Kerning.** `kern` format 0 subtables, Microsoft's (version 0) and Apple's (version 1): horizontal, not cross-stream (and, for Apple's, not variation). Values for the same pair add across subtables; a Microsoft subtable with the override bit replaces the sum so far for each pair it lists.

**Shaping.** One glyph a character, in order. A glyph past `glyphCount()` is 0. A default ignorable character (Unicode's Default_Ignorable_Code_Point) the face lacks takes the space glyph and no advance. Each pair's `kern` value times `size / unitsPerEm` is added to the first glyph's advance. No GSUB, GPOS or AAT tables are read.

**Describing.** Family: `name` ID 16, else ID 1, each from Windows English, then any Windows language, then Unicode, then Mac Roman English, trimmed; none is `TYPOG_MALFORMED`. Weight: OS/2 `usWeightClass` (0 is 400, 1..9 are hundreds, past 1000 is 1000), else 700 when `head` says bold, else 400. Slant: OS/2 italic bit, then oblique bit, else `head`'s italic bit. Width: OS/2 `usWidthClass`, 5 when 0 or absent. Outlines: `'glyf'` when it has `glyf`, else `'CFF '`, else `'CFF2'`, else `''` (bitmaps only, or outlines in a table such as Apple's `hvgl`), so a catalog can pass over a face it cannot draw.

# Refusals

`TypogException` codes: `TYPOG_CANNOT_OPEN`, `TYPOG_NOT_A_FONT`, `TYPOG_NO_FACE`, `TYPOG_MISSING_TABLE`, `TYPOG_MALFORMED`, `TYPOG_CFF`, `TYPOG_NO_GLYPH`, `TYPOG_CLOSED`, `TYPOG_NO_OUTLINES`. A glyph number or codepoint below 0 or past 2³²−1, a kerning glyph past 65535, a size that is not finite, or text that is not UTF-8 (overlong forms and surrogates included) is a `ValueError`. Each refusal has a test.[^tests]

[^stub]: TypogFace, the declaration
[^reader]: The reader, plain C
[^tests]: Pest, over fonts the tests write
