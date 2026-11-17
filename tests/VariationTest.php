<?php

declare(strict_types=1);

use Typog\Tests\Support\TinyFont;

/*
 * A variable font drawn at an instance: TypogFace's $variations. Each test
 * writes a font with known 'fvar', 'avar', 'gvar', 'HVAR' or 'MVAR' data; every expected value is
 * worked out by hand from the OpenType font variations spec, in the comments beside it.
 *
 * The fonts have two axes: wght 100..400..900 and wdth 50..100..200. So wght 650 normalizes to
 * 0.5, 900 to 1, 250 to -0.5, 100 to -1; wdth 150 to 0.5.
 */

beforeEach(function (): void {
    $this->directory = fontDirectory();
});

function variableFont(): TinyFont
{
    $font = new TinyFont();
    $font->axes = [['wght', 100, 400, 900], ['wdth', 50, 100, 200]];

    return $font;
}

/**
 * The path ops of contours whose points are all on the curve: a move, a line to each point, a line
 * back, a close.
 *
 * @param  list<list<array{float|int, float|int}>>  $contours
 */
function allOnContours(array $contours): array
{
    $ops = [];
    foreach ($contours as $contour) {
        $ops[] = ['M', (float) $contour[0][0], (float) $contour[0][1]];
        foreach ([...array_slice($contour, 1), $contour[0]] as [$x, $y]) {
            $ops[] = ['L', (float) $x, (float) $y];
        }
        $ops[] = ['Z'];
    }

    return $ops;
}

function variedFace(TinyFont $font, string $directory, array $variations): TypogFace
{
    return new TypogFace($font->write("{$directory}/variable.ttf"), 0, $variations);
}

/** A 100,0 - 300,200 square, its left edge its bearing, so its origin is at 0. */
function variableSquare(TinyFont $font, int $advance = 600): int
{
    return $font->simple($advance, [[[100, 0, true], [300, 0, true], [300, 200, true], [100, 200, true]]]);
}

it('normalizes coordinates against fvar: clamped, -1..0..1 around the default, in 2.14; unknown axes ignored, missing ones default', function (array $variations, float $x): void {
    $font = variableFont();
    $glyph = variableSquare($font);
    $all = fn (int $dx) => [[$dx, 0], [$dx, 0], [$dx, 0], [$dx, 0], [0, 0], [0, 0], [0, 0], [0, 0]];
    $font->vary($glyph, [
        ['peak' => [1, 0], 'points' => 'all', 'deltas' => $all(10)],
        ['peak' => [-1, 0], 'points' => 'all', 'deltas' => $all(-40)],
    ]);

    expect(variedFace($font, $this->directory, $variations)->outline($glyph)[0])->toEqual(['M', $x, 0.0]);
})->with([
    'wght 650: 0.5 of +10' => [['wght' => 650], 105.0],
    'wght 900: the maximum, all of +10' => [['wght' => 900], 110.0],
    'wght 2000: clamped to 900' => [['wght' => 2000], 110.0],
    'wght 250: -0.5, half of the -1 tuple\'s -40' => [['wght' => 250], 80.0],
    'wght 50: clamped to 100, -1' => [['wght' => 50], 60.0],
    'wght 400: the default' => [['wght' => 400], 100.0],
    'wght 500: 0.2 is 13107 in 16.16, 3277 in 2.14, 0.20001220703125' => [['wght' => 500], 102.0001220703125],
    'an axis the font lacks is ignored' => [['wght' => 650, 'XXXX' => 5], 105.0],
    'wght not given: its default, 0, so no wght tuple applies' => [['wdth' => 200], 100.0],
]);

it('maps normalized coordinates through avar, in 16.16 then 2.14, and ignores a map without -1, 0 and 1', function (array $maps, array $variations, float $x): void {
    $font = variableFont();
    $glyph = variableSquare($font);
    $all = fn (int $dx) => [[$dx, 0], [$dx, 0], [$dx, 0], [$dx, 0], [0, 0], [0, 0], [0, 0], [0, 0]];
    $font->vary($glyph, [
        ['peak' => [1, 0], 'points' => 'all', 'deltas' => $all(10)],
        ['peak' => [-1, 0], 'points' => 'all', 'deltas' => $all(-40)],
    ]);
    $font->avar = [0 => $maps];

    expect(variedFace($font, $this->directory, $variations)->outline($glyph)[0])->toEqual(['M', $x, 0.0]);
})->with([
    // 0.5 is a map's from: its to, 0.8, is F2DOT14 13107, 52428 in 16.16, 13107 in 2.14
    'on a map point' => [[[-1, -1], [0, 0], [0.5, 0.8], [1, 1]], ['wght' => 650], 100 + 10 * 13107 / 16384],
    // 0.75: 52428 + (49152 - 32768) * (65536 - 52428) / (65536 - 32768) = 58982 in 16.16, 14746 in 2.14
    'between map points' => [[[-1, -1], [0, 0], [0.5, 0.8], [1, 1]], ['wght' => 775], 100 + 10 * 14746 / 16384],
    'below 0, on the -1..0 segment: unchanged' => [[[-1, -1], [0, 0], [0.5, 0.8], [1, 1]], ['wght' => 250], 80.0],
    'at -1, the first map point' => [[[-1, -1], [0, 0], [0.5, 0.8], [1, 1]], ['wght' => 100], 60.0],
    'a map without 1 -> 1 changes nothing' => [[[-1, -1], [0, 0], [0.5, 0.8]], ['wght' => 650], 105.0],
]);

it('infers deltas for the points a tuple does not name, contour by contour, by the spec\'s rules', function (): void {
    $font = variableFont();
    $glyph = $font->simple(600, [
        [[0, 0, true], [50, 0, true], [100, 0, true], [150, 100, true], [0, 100, true], [-20, 50, true]],   // points 0-5
        [[300, 0, true], [400, 0, true], [400, 100, true], [300, 100, true]],                               // 6-9, none named
        [[500, 0, true], [600, 0, true], [550, 100, true]],                                                 // 10-12, one named
        [[0, 300, true], [-50, 300, true], [100, 300, true]],                                               // 13-15
    ], bearing: -50);
    $font->vary($glyph, [[
        'peak' => [1, 0],
        'points' => [0, 2, 4, 11, 13, 15],
        'deltas' => [[10, 20], [30, 40], [-10, 50], [7, -3], [4, 0], [8, 0]],
    ]]);

    expect(variedFace($font, $this->directory, ['wght' => 900])->outline($glyph))->toEqual(allOnContours([
        [
            [10, 20],      // named
            [70, 0],       // x 50 between 0 (+10) and 100 (+30): +20; y 0 = 0 = 0 with +20 and +40: 0
            [130, 40],     // named
            [180, 150],    // x 150 past both, 100 the larger: its +30; y 100 at or past both, 100 the following: its +50
            [-10, 150],    // named
            [-20, 85],     // x 0 = 0 with -10 and +10: 0; y 50 halfway from 100 (+50) to 0 (+20): +35
        ],
        [[300, 0], [400, 0], [400, 100], [300, 100]],    // no named point: unmoved
        [[507, -3], [607, -3], [557, 97]],               // one named point: all take its delta
        [[4, 300], [-46, 300], [108, 300]],              // x -50 below both, 0 the smaller: its +4
    ]));
});

it('applies shared and embedded tuples, shared and private point numbers, intermediate regions and several axes', function (array $variations, array $square): void {
    $font = variableFont();
    $glyph = variableSquare($font);
    $font->sharedTuples = [[1, 0], [0, 1]];
    $font->vary($glyph, [
        // inferred: p1 x 300 past both, p2 (300) following: +60; p3 x 100 at the lower, p0 following: +100
        ['shared' => 0, 'deltas' => [[100, 0], [60, 0]]],
        // inferred on the y axis: every point +40
        ['peak' => [0.5, 0], 'start' => [0.25, 0], 'end' => [1, 0], 'points' => [1, 3], 'deltas' => [[0, 40], [0, 40]]],
        // every point +10 in y; its scalar is wght's times wdth's
        ['peak' => [1, 1], 'deltas' => [[0, 10], [0, 10]]],
        // the second shared tuple, wdth alone: every point +5 in x
        ['shared' => 1, 'deltas' => [[5, 0], [5, 0]]],
    ], shared: [0, 2]);

    expect(variedFace($font, $this->directory, $variations)->outline($glyph))->toEqual(allOnContours([$square]));
})->with([
    // shared 0.75; intermediate past its peak: (1 - 0.75) / (1 - 0.5) = 0.5; [1, 1] at wdth 0: 0
    'wght 0.75' => [['wght' => 775], [[175, 20], [345, 20], [345, 220], [175, 220]]],
    // shared 0.375; intermediate before its peak: (0.375 - 0.25) / (0.5 - 0.25) = 0.5
    'wght 0.375' => [['wght' => 587.5], [[137.5, 20], [322.5, 20], [322.5, 220], [137.5, 220]]],
    // shared 0.125; intermediate: below its start, 0
    'wght 0.125' => [['wght' => 462.5], [[112.5, 0], [307.5, 0], [307.5, 200], [112.5, 200]]],
    // shared 0.5; intermediate at its peak, 1; [1, 1]: 0.5 x 0.75 = 0.375 of +10; shared wdth 0.75 of +5
    'wght 0.5, wdth 0.75' => [['wght' => 650, 'wdth' => 175], [[153.75, 43.75], [333.75, 43.75], [333.75, 243.75], [153.75, 243.75]]],
]);

it('keeps a shared peak\'s scalar apart from the same peak with an intermediate region', function (): void {
    $font = variableFont();
    [$alone, $between] = [variableSquare($font), variableSquare($font)];
    $font->sharedTuples = [[0.5, 0]];
    $all = [[0, 10], [0, 10], [0, 10], [0, 10], [0, 0], [0, 0], [0, 0], [0, 0]];
    $font->vary($alone, [['shared' => 0, 'points' => 'all', 'deltas' => $all]]);
    $font->vary($between, [['shared' => 0, 'start' => [0, 0], 'end' => [1, 0], 'points' => 'all', 'deltas' => $all]]);
    $face = variedFace($font, $this->directory, ['wght' => 775]);

    // 0.75: past the peak alone's 0..0.5, so 0; inside 0..0.5..1, (1 - 0.75) / (1 - 0.5) = 0.5
    expect($face->outline($alone)[0])->toEqual(['M', 100.0, 0.0])
        ->and($face->outline($between)[0])->toEqual(['M', 100.0, 5.0]);
});

it('ignores an axis a region is not valid for: start past peak, or start and end either side of 0 with a peak', function (array $start, array $peak, array $end): void {
    $font = variableFont();
    $glyph = variableSquare($font);
    $font->vary($glyph, [['peak' => $peak, 'start' => $start, 'end' => $end, 'points' => 'all',
        'deltas' => [[0, -100], [0, -100], [0, -100], [0, -100], [0, 0], [0, 0], [0, 0], [0, 0]]]]);

    // wght 462.5 is 0.125, outside either region: only an ignored axis lets the tuple apply, whole
    expect(variedFace($font, $this->directory, ['wght' => 462.5])->outline($glyph)[0])->toEqual(['M', 100.0, -100.0]);
})->with([
    'start past peak' => [[0.75, 0], [0.5, 0], [1, 0]],
    'start and end either side of 0' => [[-0.5, 0], [0.5, 0], [1, 0]],
]);

it('reads point numbers in word runs, a count of 128 or more in two bytes, and deltas in words', function (): void {
    $font = variableFont();
    $points = array_map(fn (int $i) => [$i, 0, true], range(0, 299));
    $glyph = $font->simple(600, [$points], bearing: 0);
    $font->vary($glyph, [
        // point 280 is 280 past point 0: a word; its delta 300 is a word
        ['peak' => [1, 0], 'points' => [0, 280], 'deltas' => [[0, 0], [300, 0]]],
        // 150 points: a two-byte count; every point +1 in y (the rest inferred from equal deltas)
        ['peak' => [0, 1], 'points' => range(0, 149), 'deltas' => array_fill(0, 150, [0, 1])],
    ]);
    $ops = variedFace($font, $this->directory, ['wght' => 900, 'wdth' => 200])->outline($glyph);

    // 140 is between 0 (+0) and 280 (+300): +150; 290 is past both, 280 the larger: +300
    expect($ops[140])->toEqual(['L', 290.0, 1.0])
        ->and($ops[280])->toEqual(['L', 580.0, 1.0])
        ->and($ops[290])->toEqual(['L', 590.0, 1.0]);
});

it('moves the phantom points: the left one moves the outline\'s origin, the two give the advance without HVAR', function (array $variations, float $x, int $advance): void {
    $font = variableFont();
    $glyph = variableSquare($font);
    $font->map(0x41, $glyph);
    $font->vary($glyph, [['peak' => [1, 0], 'points' => 'all', 'deltas' => [[0, 0], [0, 0], [0, 0], [0, 0], [-15, 0], [25, 0], [0, 0], [0, 0]]]]);
    $face = variedFace($font, $this->directory, $variations);

    expect($face->outline($glyph)[0])->toEqual(['M', $x, 0.0])
        ->and($face->advance($glyph))->toBe($advance);
})->with([
    'wght 1: origin -15, advance 600 + 25 + 15' => [['wght' => 900], 115.0, 640],
    'wght 0.5' => [['wght' => 650], 107.5, 620],
    // 3277 / 16384 of -15 and +25: origin 3.0002, advance 608.0005, rounded to 608
    'wght 0.2000122' => [['wght' => 500], 100 + 15 * 3277 / 16384, 608],
]);

it('varies a composite: its components as they vary, their offsets by its deltas, its phantom points, or a USE_MY_METRICS component\'s', function (): void {
    $font = variableFont();
    $box = $font->simple(600, [[[0, 0, true], [100, 0, true], [100, 100, true]]]);
    $font->vary($box, [['peak' => [1, 0], 'points' => 'all', 'deltas' => [[10, 0], [10, 0], [10, 0], [0, 0], [0, 0], [0, 0], [0, 0]]]]);
    $composite = $font->composite(600, [
        ['glyph' => $box, 'x' => 30, 'y' => -40],
        ['glyph' => $box, 'x' => -5, 'y' => 7, 'scale' => 0.5, 'bytes' => true],
        ['glyph' => $box, 'match' => [2, 0]],
    ], [0, -40, 130, 60]);
    $font->vary($composite, [['peak' => [1, 0], 'points' => 'all',
        'deltas' => [[5, 5], [10, -10], [100, 100], [-20, 0], [30, 0], [0, 0], [0, 0]]]]);   // the matched component's delta has no effect

    $shifted = $font->simple(600, [[[50, 0, true], [150, 0, true], [150, 100, true]]], bearing: 40);   // phantoms 10 and 610
    $font->vary($shifted, [['peak' => [1, 0], 'points' => 'all', 'deltas' => [[0, 0], [0, 0], [0, 0], [-6, 0], [14, 0], [0, 0], [0, 0]]]]);
    $metrics = $font->composite(600, [['glyph' => $shifted, 'x' => 0, 'y' => 0, 'metrics' => true]], [50, 0, 150, 100], bearing: 0);
    $font->vary($metrics, [['peak' => [1, 0], 'points' => 'all', 'deltas' => [[0, 0], [-20, 0], [30, 0], [0, 0], [0, 0]]]]);   // not used
    $face = variedFace($font, $this->directory, ['wght' => 900]);

    // the box at wght 1: (10, 0) (110, 0) (110, 100); the composite's origin moves by its left phantom's -20: x + 20
    expect($face->outline($composite))->toEqual(allOnContours([
        [[65, -35], [165, -35], [165, 65]],    // offset (30 + 5, -40 + 5)
        [[30, -3], [80, -3], [80, 47]],        // scaled by 0.5, offset (-5 + 10, 7 - 10)
        [[165, 65], [265, 65], [265, 165]],    // its point 0 (10, 0) onto the third point placed, (145, 65)
    ]))->and($face->advance($composite))->toBe(650)    // 600 + 30 + 20
        // the component's phantoms 4 and 624 at wght 1: origin -4, advance 620; the composite's own deltas unused
        ->and($face->outline($metrics)[0])->toEqual(['M', 46.0, 0.0])
        ->and($face->advance($metrics))->toBe(620);
});

it('varies advances by HVAR, through the glyph as the inner index or an advance map, over gvar\'s phantom points', function (array $variations, ?array $map, array $advances): void {
    $font = variableFont();
    [$a, $b] = [variableSquare($font, 600), variableSquare($font, 500)];
    $font->vary($a, [['peak' => [1, 0], 'points' => 'all', 'deltas' => [[0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [25, 0], [0, 0], [0, 0]]]]);   // HVAR wins
    // region 0 peaks at wght 1; region 1 is 0..0.5..1 on wght
    $store = TinyFont::store([[[0, 1, 1], [0, 0, 0]], [[0, 0.5, 1], [0, 0, 0]]], [
        ['regions' => [0, 1], 'words' => 1, 'rows' => [[0, 0], [300, -20], [-200, 10]]],
    ]);
    $font->hvar = TinyFont::hvarTable($store, $map);
    $face = variedFace($font, $this->directory, $variations);

    expect([$face->advance($a), $face->advance($b)])->toBe($advances);
})->with([
    // region 0: 1; region 1 at its end: 0
    'wght 1, glyphs as rows' => [['wght' => 900], null, [600 + 300, 500 - 200]],
    // region 0: 0.5; region 1 at its peak: 1
    'wght 0.5, glyphs as rows' => [['wght' => 650], null, [600 + 150 - 20, 500 - 100 + 10]],
    'wght 1, through a map' => [['wght' => 900], [[0, 0], [0, 2], [0, 1]], [600 - 200, 500 + 300]],
]);

it('reads 32-bit word deltas from an ItemVariationData with LONG_WORDS', function (): void {
    $font = variableFont();
    $a = variableSquare($font, 600);
    $store = TinyFont::store([[[0, 1, 1], [0, 0, 0]], [[0, 0.5, 1], [0, 0, 0]]], [
        ['regions' => [0, 1], 'words' => 1, 'long' => true, 'rows' => [[0, 0], [70000, -20]]],
    ]);
    $font->hvar = TinyFont::hvarTable($store);

    expect(variedFace($font, $this->directory, ['wght' => 650])->advance($a))->toBe(600 + 35000 - 20);
});

it('varies the OS/2 metrics it chooses by MVAR\'s tags; hhea\'s have none', function (string $source, array $variations, array $metrics): void {
    $font = variableFont();
    $store = TinyFont::store([[[0, 1, 1], [0, 0, 0]]], [['regions' => [0], 'words' => 1, 'rows' => [[100], [-50], [20], [30], [-40]]]]);
    $font->mvar = TinyFont::mvarTable($store, ['hasc' => [0, 0], 'hdsc' => [0, 1], 'hlgp' => [0, 2], 'hcla' => [0, 3], 'hcld' => [0, 4]]);
    match ($source) {
        'typo' => $font->useTypoMetrics = true,
        'windows' => [$font->hhea, $font->typo] = [[0, 0, 0], [0, 0, 0]],
        'hhea' => null,
    };
    [$ascender, $descender, $gap] = variedFace($font, $this->directory, $variations)->vertical();

    expect([$ascender, -$descender, $gap])->toEqual($metrics);
})->with([
    // typo 700, -300, 50: + 100, - 50, + 20
    'typographic, wght 1' => ['typo', ['wght' => 900], [800.0, 350.0, 70.0]],
    'typographic, wght 0.5' => ['typo', ['wght' => 650], [750.0, 325.0, 60.0]],
    // Windows 900, 250: ascent + 30, descent - 40
    'Windows, wght 1' => ['windows', ['wght' => 900], [930.0, 210.0, 0.0]],
    'hhea, wght 1: unvaried' => ['hhea', ['wght' => 900], [800.0, 200.0, 90.0]],
]);

it('draws a face without fvar as it is, whatever coordinates the source gives', function (): void {
    $font = new TinyFont();
    $glyph = variableSquare($font);
    $path = $font->write("{$this->directory}/static.ttf");
    [$plain, $given] = [new TypogFace($path), new TypogFace($path, 0, ['wght' => 900])];

    expect($given->outline($glyph))->toEqual($plain->outline($glyph))
        ->and($given->advance($glyph))->toBe($plain->advance($glyph))
        ->and($given->vertical())->toEqual($plain->vertical());
});

it('varies a CFF2 face\'s advances, and refuses its outlines by name', function (): void {
    $font = variableFont();
    $font->cff2 = true;
    $font->hvar = TinyFont::hvarTable(TinyFont::store([[[0, 1, 1], [0, 0, 0]]], [['regions' => [0], 'words' => 1, 'rows' => [[100]]]]));
    $face = variedFace($font, $this->directory, ['wght' => 900]);

    expect($face->advance(0))->toBe(600)
        ->and(fn () => $face->outline(0))->toThrow(TypogException::class, 'has CFF2 outlines, which this reader does not draw.');
});

it('refuses what the OpenType spec does not define: avar version 2, a delta run flagged zero and words', function (): void {
    $font = variableFont();
    $font->avar = [0 => [[-1, -1], [0, 0], [1, 1]]];
    $font->avarVersion = 2;
    expect(fn () => variedFace($font, $this->directory, ['wght' => 900]))->toThrow(TypogException::class, "'avar' table is version 2");

    $font = variableFont();
    $glyph = variableSquare($font);
    $font->vary($glyph, [['peak' => [1, 0], 'points' => [0], 'raw' => "\xC1\x00\x00\x00\x00"]]);
    expect(fn () => variedFace($font, $this->directory, ['wght' => 900])->outline($glyph))->toThrow(TypogException::class, 'DELTAS_ARE_ZERO and DELTAS_ARE_WORDS');
});

it('refuses malformed data FreeType and HarfBuzz do not vary by: an fvar instance size that is not 4n+4 or 4n+6, a delta run crossing from x into y', function (): void {
    // two axes: instance records are 12 or 14 bytes
    foreach ([12, 14] as $size) {
        $font = variableFont();
        $font->instanceSize = $size;
        $glyph = variableSquare($font);
        expect(variedFace($font, $this->directory, ['wght' => 900])->outline($glyph))->toHaveCount(6);
    }
    $font = variableFont();
    $font->instanceSize = 4;
    expect(fn () => variedFace($font, $this->directory, ['wght' => 900]))->toThrow(TypogException::class, "'fvar' instance records are 4 bytes; with 2 axes OpenType has 12 or 14");

    // eight points (four and the phantoms): x is +10 +10 and six zeros, y all zero, written as one
    // array: a byte run of two, then a zero run of 14 that crosses into y
    $font = variableFont();
    $glyph = variableSquare($font);
    $font->vary($glyph, [['peak' => [1, 0], 'points' => 'all', 'raw' => "\x01\x0A\x0A\x8D"]]);
    expect(fn () => variedFace($font, $this->directory, ['wght' => 900])->outline($glyph))->toThrow(TypogException::class, 'delta run holds 14 deltas where its array has 6 left');
});

it('shapes a variable font with the advances of the instance', function (): void {
    $font = variableFont();
    [$a, $b] = [variableSquare($font, 600), variableSquare($font, 500)];
    $font->map(0x41, $a)->map(0x42, $b);
    $font->hvar = TinyFont::hvarTable(TinyFont::store([[[0, 1, 1], [0, 0, 0]]], [['regions' => [0], 'words' => 1, 'rows' => [[0], [300], [-200]]]]));
    [, , $advances] = (new TypogFace($font->write("{$this->directory}/variable.ttf"), 0, ['wght' => 900]))->shape('AB', 1000.0);

    expect($advances)->toBe([900.0, 300.0]);
});

it('describes a variable face\'s axes: tag, minimum, default, maximum', function (): void {
    $font = variableFont();
    $path = $font->write("{$this->directory}/axes.ttf");

    expect(TypogFace::describe($path)[0][5])->toBe([['wght', 100.0, 400.0, 900.0], ['wdth', 50.0, 100.0, 200.0]]);
});

it('refuses variations that are not four-character tags and finite numbers', function (array $variations): void {
    $path = variableFont()->write("{$this->directory}/refused.ttf");

    expect(fn () => new TypogFace($path, 0, $variations))->toThrow(ValueError::class);
})->with([
    'a short tag' => [['wgt' => 700]],
    'a list' => [[700]],
    'a string value' => [['wght' => 'bold']],
    'not finite' => [['wght' => INF]],
]);
