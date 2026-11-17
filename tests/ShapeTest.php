<?php

declare(strict_types=1);

use Typog\Tests\Support\TinyFont;

/* shape(): one glyph a character, advances at a size, 'kern' pairs on the first glyph. */

it('gives one glyph a character, its advance at the size, and the byte it starts at', function (): void {
    $font = new TinyFont();
    $font->unitsPerEm = 2000;
    [$a, $e] = [$font->simple(1200, []), $font->simple(900, [])];
    $font->map(0x41, $a)->map(0xE9, $e);

    expect(typogFace($font)->shape('AéA', 10.0))->toBe([[$a, $e, $a], [0, 1, 3], [6.0, 4.5, 6.0]]);
});

it('adds a kerning pair to the first glyph; values add across subtables unless one overrides', function (): void {
    $font = new TinyFont();
    [$a, $v] = [$font->simple(600, []), $font->simple(600, [])];
    $font->map(0x41, $a)->map(0x56, $v);
    $font->kern([[$a, $v, -80], [$v, $a, -30]])->kern([[$a, $v, -20]])->kern([[$v, $a, -5]], override: true);
    $face = typogFace($font);

    expect([$face->kerning($a, $v), $face->kerning($v, $a), $face->kerning($a, $a)])->toBe([-100, -5, 0])
        ->and($face->shape('AVA', 1000.0)[2])->toBe([500.0, 595.0, 600.0]);
});

it('gives a default ignorable character the face lacks the space glyph and no width', function (): void {
    $font = new TinyFont();
    [$a, $space] = [$font->simple(600, []), $font->simple(250, [])];
    $font->map(0x41, $a)->map(0x20, $space);

    expect(typogFace($font)->shape("A\u{200D}A", 1000.0))->toBe([[$a, $space, $a], [0, 1, 4], [600.0, 0.0, 600.0]]);
});

it('refuses text that is not UTF-8, naming the byte', function (): void {
    $face = typogFace(new TinyFont());

    expect(fn () => $face->shape("ok\xE2\x82", 10.0))->toThrow(ValueError::class, 'byte 2')
        ->and(fn () => $face->shape("\xED\xA0\x80", 10.0))->toThrow(ValueError::class, 'byte 0');
});
