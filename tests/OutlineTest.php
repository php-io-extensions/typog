<?php

declare(strict_types=1);

use Typog\Tests\Support\TinyFont;

/* Outlines: TrueType's quadratic contours as path steps, in font units, y up. */

it('draws on-curve points as lines and off-curve ones as quadratic controls, with an implied point between two', function (): void {
    $font = new TinyFont();
    $glyph = $font->simple(600, [[[0, 0, true], [600, 0, true], [600, 700, false], [0, 700, false]]]);

    expect(typogFace($font)->outline($glyph))->toBe([
        ['M', 0.0, 0.0], ['L', 600.0, 0.0], ['Q', 600.0, 700.0, 300.0, 700.0], ['Q', 0.0, 700.0, 0.0, 0.0], ['Z'],
    ]);
});

it('starts a contour that begins off the curve at its last point, or halfway between its ends when both are off', function (): void {
    $font = new TinyFont();
    $lastOn = $font->simple(600, [[[100, 0, false], [200, 100, true], [0, 100, true]]]);
    $bothOff = $font->simple(600, [[[100, 0, false], [200, 100, true], [0, 100, false]]]);
    $face = typogFace($font);

    expect($face->outline($lastOn))->toBe([['M', 0.0, 100.0], ['Q', 100.0, 0.0, 200.0, 100.0], ['L', 0.0, 100.0], ['Z']])
        ->and($face->outline($bothOff))->toBe([['M', 50.0, 50.0], ['Q', 100.0, 0.0, 200.0, 100.0], ['Q', 0.0, 100.0, 50.0, 50.0], ['Z']]);
});

it('places composite components by offset, by scale with one-byte offsets, and by matching points', function (): void {
    $font = new TinyFont();
    $box = $font->simple(600, [[[0, 0, true], [100, 0, true], [100, 100, true]]]);
    $composite = $font->composite(600, [
        ['glyph' => $box, 'x' => 30, 'y' => -40],
        ['glyph' => $box, 'x' => -5, 'y' => 7, 'scale' => 0.5, 'bytes' => true],
        ['glyph' => $box, 'match' => [2, 0]],
    ], [0, -40, 130, 60]);

    expect(typogFace($font)->outline($composite))->toBe([
        ['M', 30.0, -40.0], ['L', 130.0, -40.0], ['L', 130.0, 60.0], ['L', 30.0, -40.0], ['Z'],
        ['M', -5.0, 7.0], ['L', 45.0, 7.0], ['L', 45.0, 57.0], ['L', -5.0, 7.0], ['Z'],
        ['M', 130.0, 60.0], ['L', 230.0, 60.0], ['L', 230.0, 160.0], ['L', 130.0, 60.0], ['Z'],
    ]);
});

it('moves an outline to the origin hmtx gives it, a composite to its USE_MY_METRICS component\'s', function (): void {
    $font = new TinyFont();
    $shifted = $font->simple(600, [[[50, 0, true], [150, 0, true], [150, 100, true]]], bearing: 40);
    $composite = $font->composite(600, [['glyph' => $shifted, 'x' => 0, 'y' => 0, 'metrics' => true]], [50, 0, 150, 100], bearing: 0);
    $face = typogFace($font);

    expect($face->outline($shifted)[0])->toBe(['M', 40.0, 0.0])
        ->and($face->outline($composite)[0])->toBe(['M', 40.0, 0.0]);
});

it('gives an empty outline for a glyph that draws nothing, and refuses a glyph the face does not have', function (): void {
    $font = new TinyFont();
    $space = $font->simple(250, []);
    $face = typogFace($font);

    expect($face->outline($space))->toBe([])
        ->and(fn () => $face->outline(99))->toThrow(TypogException::class, 'there is no glyph 99');
});

it('refuses the outlines of a face with no glyf, CFF or CFF2 table by TYPOG_NO_OUTLINES, naming it, and reads the rest of the face', function (): void {
    $font = new TinyFont();
    $glyph = $font->simple(600, [[[0, 0, true], [600, 0, true], [600, 700, true]]]);
    $font->otherOutlines = 'hvgl';
    $face = typogFace($font, 'hvgl.ttf');

    expect($face->advance($glyph))->toBe(600)
        ->and($face->vertical())->toBe([800.0, -200.0, 90.0]);
    try {
        $face->outline($glyph);
        $this->fail('an outline was given');
    } catch (TypogException $e) {
        expect($e->getCode())->toBe(TYPOG_NO_OUTLINES)
            ->and($e->getMessage())->toBe($face->path()."#0 has no outlines this reader draws: no 'glyf' or 'CFF ' table.");
    }
});
