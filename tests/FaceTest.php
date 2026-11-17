<?php

declare(strict_types=1);

use Typog\Tests\Support\TinyFont;

/* TypogFace reads font files this project did not write: each test writes one holding known values. */

it('reads the em, the glyph count and the hhea metrics', function (): void {
    $font = new TinyFont();
    $font->unitsPerEm = 2048;
    $font->hhea = [1638, -410, 67];
    $font->simple(500, []);
    $face = typogFace($font);

    expect([$face->unitsPerEm(), $face->glyphCount(), $face->vertical()])->toBe([2048, 2, [1638.0, -410.0, 67.0]]);
});

it('takes the OS/2 typographic metrics when USE_TYPO_METRICS is set, and when hhea gives none, then the Windows ones', function (): void {
    $flagged = new TinyFont();
    $flagged->useTypoMetrics = true;
    $empty = new TinyFont();
    $empty->hhea = [0, 0, 0];
    $nothing = new TinyFont();
    $nothing->hhea = [0, 0, 0];
    $nothing->typo = [0, 0, 0];

    expect(typogFace($flagged, 'a.ttf')->vertical())->toBe([700.0, -300.0, 50.0])
        ->and(typogFace($empty, 'b.ttf')->vertical())->toBe([700.0, -300.0, 50.0])
        ->and(typogFace($nothing, 'c.ttf')->vertical())->toBe([900.0, -250.0, 0.0]);
});

it('maps codepoints through format 4 by delta and through the glyph array, format 12 before it, and 0 for the rest', function (): void {
    $font = new TinyFont();
    [$a, $b, $c] = [$font->simple(500, []), $font->simple(500, []), $font->simple(500, [])];
    $font->map(0x41, $a)->map(0x42, $b)->map(0x43, $c)->map(0x61, $c)->map(0x62, $a);
    $face = typogFace($font, 'four.ttf');
    $both = new TinyFont();
    [$x, $y] = [$both->simple(500, []), $both->simple(500, [])];
    $both->map(0x41, $x, 4)->map(0x41, $y, 12)->map(0x1F600, $x, 12);
    $twelve = typogFace($both, 'twelve.ttf');

    expect(array_map(fn (int $cp) => $face->glyph($cp), [0x41, 0x42, 0x43, 0x61, 0x62, 0x44, 0x60, 0x1F600]))
        ->toBe([$a, $b, $c, $c, $a, 0, 0, 0])
        ->and([$twelve->glyph(0x41), $twelve->glyph(0x1F600), $twelve->glyph(0x1F601)])->toBe([$y, $x, 0]);
});

it('looks a codepoint up to U+00FF up again at U+F000 in a Windows symbol map', function (): void {
    $font = new TinyFont();
    $font->symbol = true;
    $star = $font->simple(500, []);
    $font->map(0xF041, $star);
    $face = typogFace($font);

    expect([$face->glyph(0x41), $face->glyph(0xF041), $face->glyph(0x141)])->toBe([$star, $star, 0]);
});

it('gives a glyph past the long metrics the last advance, and an unknown glyph none', function (): void {
    $font = new TinyFont();
    $font->simple(610, []);
    $font->simple(720, []);
    $font->simple(999, []);
    $font->longMetrics = 2;
    $face = typogFace($font);

    expect(array_map(fn (int $g) => $face->advance($g), [0, 1, 2, 3, 4]))->toBe([500, 610, 610, 610, 0]);
});

it('reads a face of a collection by its index, and names how many there are when there is no such face', function (): void {
    $regular = new TinyFont();
    $bold = new TinyFont();
    $bold->hhea = [900, -100, 0];
    $path = TinyFont::collection(fontDirectory().'/pair.ttc', $regular, $bold);

    expect((new TypogFace($path, 1))->vertical())->toBe([900.0, -100.0, 0.0])
        ->and(fn () => new TypogFace($path, 2))->toThrow(TypogException::class, 'has 2 faces');
    try {
        new TypogFace($path, 2);
    } catch (TypogException $e) {
        expect($e->getCode())->toBe(TYPOG_NO_FACE);
    }
});

it('refuses what is not a font, and a font cut short, as a TypogException and not a warning', function (): void {
    $directory = fontDirectory();
    file_put_contents("{$directory}/text.ttf", 'not a font at all');
    $whole = (new TinyFont())->write("{$directory}/whole.ttf");
    file_put_contents("{$directory}/cut.ttf", substr((string) file_get_contents($whole), 0, 200));
    $codes = [];
    foreach (["{$directory}/text.ttf", "{$directory}/cut.ttf", "{$directory}/none.ttf"] as $path) {
        try {
            (new TypogFace($path))->vertical();
        } catch (TypogException $e) {
            $codes[] = $e->getCode();
        }
    }

    expect($codes)->toBe([TYPOG_NOT_A_FONT, TYPOG_MALFORMED, TYPOG_CANNOT_OPEN]);
});

it('keeps the tables it read readable after close(), and says a table it never read is closed', function (): void {
    $font = new TinyFont();
    $glyph = $font->simple(600, [[[0, 0, true], [100, 0, true], [100, 100, true]]]);
    $font->map(0x41, $glyph);
    $face = typogFace($font);
    $face->glyph(0x41);
    $face->close();

    expect($face->glyph(0x41))->toBe($glyph)
        ->and(fn () => $face->outline($glyph))->toThrow(TypogException::class, 'closed');
});
