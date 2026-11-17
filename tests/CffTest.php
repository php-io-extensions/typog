<?php

declare(strict_types=1);

use Typog\Tests\Support\TinyCff;
use Typog\Tests\Support\TinyFont;

/*
 * ext-typog reads CFF outlines (TN #5176, #5177): each test writes a CFF table whose charstrings
 * draw what the test states, worked by hand from the Type 2 operators, and checks the face draws
 * exactly that, cubics as 'C' steps.
 */

/** A face whose 'CFF ' table is $cff, with as many glyphs as $cff has charstrings. */
function cffFace(TinyCff $cff): TypogFace
{
    $font = new TinyFont();
    $count = max(count($cff->glyphs), 1);
    for ($i = 0; $i < $count; $i++) {
        $font->simple(600, []);
    }
    $font->cff = true;
    $font->cffTable = $cff->bytes();

    return typogFace($font, 'tiny.otf');
}

/** The ops of glyph $glyph of $cff, read through a TypogFace. */
function cffOps(TinyCff $cff, int $glyph = 0): array
{
    return cffFace($cff)->outline($glyph);
}

/** A name-keyed CFF of one glyph per charstring. */
function cffOf(string ...$charstrings): TinyCff
{
    $cff = new TinyCff();
    $cff->glyphs = $charstrings;

    return $cff;
}

/** A name-keyed CFF with its settings changed by $set. */
function cffWith(Closure $set, string ...$charstrings): TinyCff
{
    $cff = cffOf(...$charstrings);
    $set($cff);

    return $cff;
}

/** Subrs 0..n-1, each calling the next; the last draws a line. */
function subrChain(int $n): array
{
    $subrs = [];
    for ($i = 0; $i < $n - 1; $i++) {
        $subrs[] = TinyCff::cs($i + 1 - 107, 'callsubr', 'return');
    }
    $subrs[] = TinyCff::cs(10, 'hlineto', 'return');

    return $subrs;
}

it('draws lines from the moveto, alternating h and v as hlineto and vlineto say, and closes the contour at endchar', function (): void {
    expect(cffOps(cffOf(TinyCff::cs(10, 20, 'rmoveto', 30, 0, 0, 40, 'rlineto', -30, 'hlineto', 'endchar'))))->toBe([
        ['M', 10.0, 20.0], ['L', 40.0, 20.0], ['L', 40.0, 60.0], ['L', 10.0, 60.0], ['Z'],
    ])->and(cffOps(cffOf(TinyCff::cs(0, 0, 'rmoveto', 10, 20, 30, 'vlineto', 'endchar'))))->toBe([
        ['M', 0.0, 0.0], ['L', 0.0, 10.0], ['L', 20.0, 10.0], ['L', 20.0, 40.0], ['Z'],
    ])->and(cffOps(cffOf(TinyCff::cs(0, 0, 'rmoveto', 10, 20, 30, 'hlineto', 'endchar'))))->toBe([
        ['M', 0.0, 0.0], ['L', 10.0, 0.0], ['L', 10.0, 20.0], ['L', 40.0, 20.0], ['Z'],
    ]);
});

it('draws each curve operator as the cubics it names', function (array $tokens, array $segments): void {
    expect(cffOps(cffOf(TinyCff::cs(...[0, 0, 'rmoveto', ...$tokens, 'endchar']))))->toBe([['M', 0.0, 0.0], ...$segments, ['Z']]);
})->with([
    'rrcurveto' => [[10, 0, 20, 10, 0, 30, 'rrcurveto'], [['C', 10.0, 0.0, 30.0, 10.0, 30.0, 40.0]]],
    'hhcurveto, dy1 first' => [[5, 10, 20, 30, 40, 1, 2, 3, 4, 'hhcurveto'], [['C', 10.0, 5.0, 30.0, 35.0, 70.0, 35.0], ['C', 71.0, 35.0, 73.0, 38.0, 77.0, 38.0]]],
    'vvcurveto, dx1 first' => [[5, 10, 20, 30, 40, 'vvcurveto'], [['C', 5.0, 10.0, 25.0, 40.0, 25.0, 80.0]]],
    'hvcurveto, alternating' => [[10, 20, 30, 40, 1, 2, 3, 4, 'hvcurveto'], [['C', 10.0, 0.0, 30.0, 30.0, 30.0, 70.0], ['C', 30.0, 71.0, 32.0, 74.0, 36.0, 74.0]]],
    'hvcurveto, last df' => [[10, 20, 30, 40, 1, 2, 3, 4, 5, 'hvcurveto'], [['C', 10.0, 0.0, 30.0, 30.0, 30.0, 70.0], ['C', 30.0, 71.0, 32.0, 74.0, 36.0, 79.0]]],
    'vhcurveto, last df' => [[10, 20, 30, 40, 7, 'vhcurveto'], [['C', 0.0, 10.0, 20.0, 40.0, 60.0, 47.0]]],
    'rcurveline' => [[10, 0, 20, 10, 0, 30, 5, 7, 'rcurveline'], [['C', 10.0, 0.0, 30.0, 10.0, 30.0, 40.0], ['L', 35.0, 47.0]]],
    'rlinecurve' => [[5, 5, 10, 0, 20, 10, 0, 30, 'rlinecurve'], [['L', 5.0, 5.0], ['C', 15.0, 5.0, 35.0, 15.0, 35.0, 45.0]]],
]);

it('draws flex, hflex, hflex1 and flex1 as their two curves, the flex depth passed over', function (array $tokens, array $segments): void {
    expect(cffOps(cffOf(TinyCff::cs(...[0, 0, 'rmoveto', ...$tokens, 'endchar']))))->toBe([['M', 0.0, 0.0], ...$segments, ['Z']]);
})->with([
    'flex' => [[10, 0, 10, 10, 10, 0, 10, 0, 10, -10, 10, 0, 50, 'flex'], [['C', 10.0, 0.0, 20.0, 10.0, 30.0, 10.0], ['C', 40.0, 10.0, 50.0, 0.0, 60.0, 0.0]]],
    'hflex: back to the starting height' => [[10, 10, 10, 10, 10, 10, 10, 'hflex'], [['C', 10.0, 0.0, 20.0, 10.0, 30.0, 10.0], ['C', 40.0, 10.0, 50.0, 0.0, 60.0, 0.0]]],
    // dy5 -3: the end's dy is then -7, which no operand holds
    'hflex1: ends on the starting height' => [[10, 5, 10, 5, 10, 10, 10, -3, 10, 'hflex1'], [['C', 10.0, 5.0, 20.0, 10.0, 30.0, 10.0], ['C', 40.0, 10.0, 50.0, 7.0, 60.0, 0.0]]],
    'flex1, wider than tall: d6 across' => [[10, 5, 10, 5, 10, 0, 10, -5, 10, -4, 10, 'flex1'], [['C', 10.0, 5.0, 20.0, 10.0, 30.0, 10.0], ['C', 40.0, 5.0, 50.0, 1.0, 60.0, 0.0]]],
    'flex1, taller than wide: d6 up' => [[5, 10, 5, 10, 0, 10, -5, 10, -4, 10, 10, 'flex1'], [['C', 5.0, 10.0, 10.0, 20.0, 10.0, 30.0], ['C', 5.0, 40.0, 1.0, 50.0, 0.0, 60.0]]],
]);

it('takes the advance width off the first stack-clearing operator that carries it', function (array $tokens, array $ops): void {
    expect(cffOps(cffOf(TinyCff::cs(...$tokens))))->toBe($ops);
})->with([
    'rmoveto' => [[500, 10, 20, 'rmoveto', 5, 'hlineto', 'endchar'], [['M', 10.0, 20.0], ['L', 15.0, 20.0], ['Z']]],
    'hmoveto' => [[500, 10, 'hmoveto', 5, 'vlineto', 'endchar'], [['M', 10.0, 0.0], ['L', 10.0, 5.0], ['Z']]],
    'vmoveto' => [[500, 20, 'vmoveto', 5, 'hlineto', 'endchar'], [['M', 0.0, 20.0], ['L', 5.0, 20.0], ['Z']]],
    'endchar alone' => [[500, 'endchar'], []],
    'hintmask with vstems before it' => [[500, 0, 10, 'hintmask', ['raw', "\x80"], 10, 20, 'rmoveto', 'endchar'], [['M', 10.0, 20.0], ['Z']]],
]);

it('takes a width only once: a later operator with one value too many is refused', function (): void {
    expect(fn () => cffOps(cffOf(TinyCff::cs(0, 10, 'hstem', 500, 10, 20, 'rmoveto', 'endchar'))))
        ->toThrow(TypogException::class, 'takes 2 values, got 3');
});

it('passes over each hintmask\'s and cntrmask\'s mask, a byte per eight stems so far', function (): void {
    // 8 hstems and 1 vstem given at the hintmask: 9 stems, two mask bytes; 0x8B read as a number would be 0
    $stems = array_merge(...array_fill(0, 8, [0, 5]));
    expect(cffOps(cffOf(TinyCff::cs(...[...$stems, 'hstemhm', 0, 5, 'hintmask', ['raw', "\x8B\x8B"], 10, 20, 'rmoveto', 'cntrmask', ['raw', "\x8B\x8B"], 5, 'hlineto', 'endchar']))))
        ->toBe([['M', 10.0, 20.0], ['L', 15.0, 20.0], ['Z']]);
});

it('calls subroutines by number plus the bias their count sets: 107, 1131, 32768', function (int $count, int $call): void {
    $subrs = array_fill(0, $count, TinyCff::cs('return'));
    $subrs[$count - 1] = TinyCff::cs(30, 'hlineto', 'return');
    $cff = cffOf(TinyCff::cs(0, 0, 'rmoveto', $call, 'callsubr', 'endchar'), TinyCff::cs(0, 0, 'rmoveto', $call, 'callgsubr', 'endchar'));
    $cff->localSubrs = $subrs;
    $cff->globalSubrs = $subrs;
    $face = cffFace($cff);
    $ops = [['M', 0.0, 0.0], ['L', 30.0, 0.0], ['Z']];

    expect($face->outline(0))->toBe($ops)->and($face->outline(1))->toBe($ops);
})->with([
    'under 1240: 107' => [3, 2 - 107],
    '1240 to 33899: 1131' => [1240, 1239 - 1131],
    '33900 and over: 32768' => [33900, 33899 - 32768],
]);

it('runs subroutines nested 10 deep, the most Type 2 allows', function (): void {
    $cff = cffOf(TinyCff::cs(0, 0, 'rmoveto', -107, 'callsubr', 'endchar'));
    $cff->localSubrs = subrChain(10);

    expect(cffOps($cff))->toBe([['M', 0.0, 0.0], ['L', 10.0, 0.0], ['Z']]);
});

it('ends the glyph at an endchar inside a subroutine', function (): void {
    $cff = cffOf(TinyCff::cs(0, 0, 'rmoveto', -107, 'callsubr', 99, 'hlineto', 'endchar'));
    $cff->localSubrs = [TinyCff::cs(10, 'hlineto', 'endchar')];

    expect(cffOps($cff))->toBe([['M', 0.0, 0.0], ['L', 10.0, 0.0], ['Z']]);
});

it('reads every operand form: one byte, two bytes, three (28) and 16.16 fixed (255)', function (): void {
    expect(cffOps(cffOf(TinyCff::cs(['short', 300], ['two', -108], 'rmoveto', ['fixed', 1.5], ['fixed', -0.25], ['two', 1131], ['byte', -107], 'rlineto', ['short', -2000], ['short', 1500], 'rlineto', 'endchar'))))->toBe([
        ['M', 300.0, -108.0], ['L', 301.5, -108.25], ['L', 1432.5, -215.25], ['L', -567.5, 1284.75], ['Z'],
    ]);
});

it('gives each glyph of a CID-keyed font its Font DICT\'s local subrs, by FDSelect', function (int $format): void {
    $cff = new TinyCff();
    $cff->fonts = [[TinyCff::cs(10, 'hlineto', 'return')], [TinyCff::cs(20, 'hlineto', 'return')]];
    $cff->select = [0, 1, 1, 0, 0];
    $cff->selectFormat = $format;
    $cff->glyphs = array_fill(0, 5, TinyCff::cs(0, 0, 'rmoveto', -107, 'callsubr', 'endchar'));
    $face = cffFace($cff);

    expect(array_map(fn (int $glyph) => $face->outline($glyph)[1][1], range(0, 4)))->toBe([10.0, 20.0, 20.0, 10.0, 10.0]);
})->with(['format 0' => [0], 'format 3' => [3]]);

it('draws an accented glyph from endchar\'s four values: the base, then the accent moved, found through StandardEncoding and the charset', function (): void {
    $cff = cffOf(
        TinyCff::cs('endchar'),
        TinyCff::cs(0, 0, 'rmoveto', 10, 'hlineto', 'endchar'),          // 'A', SID 34, code 65
        TinyCff::cs(0, 0, 'rmoveto', 5, 'vlineto', 'endchar'),           // 'acute', SID 125, code 194
        TinyCff::cs(600, 100, 200, 65, 194, 'endchar'),                  // with its width first
    );
    $cff->charset = [34, 125, 400];

    expect(cffOps($cff, 3))->toBe([['M', 0.0, 0.0], ['L', 10.0, 0.0], ['Z'], ['M', 100.0, 200.0], ['L', 100.0, 205.0], ['Z']]);
});

it('works each arithmetic and storage operator as Type 2 defines it', function (array $tokens, float $x): void {
    expect(cffOps(cffOf(TinyCff::cs(...[0, 0, 'rmoveto', ...$tokens, 0, 'rlineto', 'endchar'])))[1])->toBe(['L', $x, 0.0]);
})->with([
    'add' => [[3, 4, 'add'], 7.0],
    'sub' => [[3, 4, 'sub'], -1.0],
    'mul' => [[3, 4, 'mul'], 12.0],
    'div' => [[3, 4, 'div'], 0.75],
    'neg' => [[3, 'neg'], -3.0],
    'abs' => [[-3, 'abs'], 3.0],
    'sqrt' => [[9, 'sqrt'], 3.0],
    'and' => [[1, 0, 'and'], 0.0],
    'or' => [[1, 0, 'or'], 1.0],
    'not' => [[0, 'not'], 1.0],
    'eq' => [[4, 4, 'eq'], 1.0],
    'ifelse: s1 when v1 <= v2' => [[7, 9, 1, 2, 'ifelse'], 7.0],
    'ifelse: s2 when v1 > v2' => [[7, 9, 3, 2, 'ifelse'], 9.0],
    'put then get' => [[7, 3, 'put', 3, 'get'], 7.0],
    'dup' => [[5, 'dup', 'add'], 10.0],
    'exch' => [[5, 1, 'exch', 'sub'], -4.0],
    'index: the value n down' => [[5, 6, 7, 2, 'index', 'exch', 'drop', 'exch', 'drop', 'exch', 'drop'], 5.0],
    // 1 2 3 rolled by 1 is 3 1 2; exch drop exch drop leaves 2
    'roll: n values by j' => [[1, 2, 3, 3, 1, 'roll', 'exch', 'drop', 'exch', 'drop'], 2.0],
    'drop' => [[5, 6, 'drop'], 5.0],
]);

it('lets a drawing operator before any moveto start at the current point, and closes each contour', function (): void {
    expect(cffOps(cffOf(TinyCff::cs(10, 'hlineto', 5, 5, 'rmoveto', 3, 'vlineto', 'endchar'))))->toBe([
        ['M', 0.0, 0.0], ['L', 10.0, 0.0], ['Z'], ['M', 15.0, 5.0], ['L', 15.0, 8.0], ['Z'],
    ]);
});

it('applies the FontMatrix as FreeType does: a plain scale of any em leaves the outline, a slant moves it, a Font DICT\'s other em scales it', function (): void {
    $glyph = TinyCff::cs(10, 20, 'rmoveto', 'endchar');
    $half = cffOf($glyph);
    $half->fontMatrix = [0.0005, 0.0, 0.0, 0.0005, 0.0, 0.0];
    $slant = cffOf($glyph);
    $slant->fontMatrix = [0.001, 0.0, 0.0005, 0.001, 0.0, 0.0];
    $cid = new TinyCff();
    $cid->fonts = [[], []];
    $cid->select = [0, 1];
    $cid->fdMatrices = [1 => [0.002, 0.0, 0.0, 0.002, 0.0, 0.0]];
    $cid->glyphs = [$glyph, $glyph];
    $cidFace = cffFace($cid);

    expect(cffOps($half)[0])->toBe(['M', 10.0, 20.0])
        // x' = x + 0.5 y
        ->and(cffOps($slant)[0])->toBe(['M', 20.0, 20.0])
        ->and($cidFace->outline(0)[0])->toBe(['M', 10.0, 20.0])
        // the Font DICT's em is 500 against the Top DICT's 1000: twice the size
        ->and($cidFace->outline(1)[0])->toBe(['M', 20.0, 40.0]);
});

it('refuses a CFF it cannot read by TYPOG_CFF, naming the font and why', function (TinyCff $cff, int $glyph, string $why): void {
    $face = cffFace($cff);
    try {
        $face->outline($glyph);
        $this->fail('an outline was given');
    } catch (TypogException $e) {
        expect($e->getCode())->toBe(TYPOG_CFF)
            ->and($e->getMessage())->toContain('tiny.otf')
            ->and($e->getMessage())->toContain('its CFF outlines cannot be read: '.$why);
    }
})->with([
    'a stack past 48' => [cffOf(TinyCff::cs(...[...array_fill(0, 49, 1), 'endchar'])), 0, 'a charstring has more than 48 values on its stack.'],
    'subroutines 11 deep' => [cffWith(fn (TinyCff $c) => $c->localSubrs = subrChain(11), TinyCff::cs(0, 0, 'rmoveto', -107, 'callsubr', 'endchar')), 0, 'subroutines nest more than 10 deep.'],
    'a subroutine not in the font' => [cffOf(TinyCff::cs(5, 'callgsubr', 'endchar')), 0, 'global subroutine 112 is not in the font.'],
    'an operator Type 2 does not have' => [cffOf(TinyCff::cs(['raw', "\x09"], 'endchar')), 0, 'a charstring has operator 9, which Type 2 does not have.'],
    'random' => [cffOf(TinyCff::cs('random', 'endchar')), 0, "a charstring asks for 'random', which draws a glyph differently each time."],
    'endchar with 3 values' => [cffOf(TinyCff::cs(1, 2, 3, 'endchar')), 0, 'endchar takes 0 or 4 values, got 3.'],
    'a hintmask past the charstring' => [cffOf(TinyCff::cs(0, 5, 'hstemhm', 'hintmask')), 0, 'a hintmask runs past its charstring.'],
    'a number cut short' => [cffOf(TinyCff::cs(['raw', "\x1C\x01"])), 0, 'a charstring is cut short in a number.'],
    'an FDSelect naming a Font DICT it lacks' => [cffWith(function (TinyCff $c): void {
        $c->fonts = [[]];
        $c->select = [0, 4];
    }, TinyCff::cs('endchar'), TinyCff::cs('endchar')), 1, 'its FDSelect gives glyph 1 Font DICT 4; it has 1.'],
    'accents in a CID-keyed font' => [cffWith(function (TinyCff $c): void {
        $c->fonts = [[]];
        $c->select = [0];
    }, TinyCff::cs(0, 0, 65, 194, 'endchar')), 0, "glyph 0 uses endchar's accent form in a CID-keyed font."],
    'a division by 0' => [cffOf(TinyCff::cs(1, 0, 'div', 'endchar')), 0, 'a charstring divides by 0.'],
    'index past the bottom of the stack' => [cffOf(TinyCff::cs(5, 6, 2, 'index', 'endchar')), 0, 'index reaches past the stack.'],
    'a FontMatrix that flattens the plane' => [cffWith(fn (TinyCff $c) => $c->fontMatrix = [0.001, 0.0, 0.001, 0.0, 0.0, 0.0], TinyCff::cs('endchar')), 0, 'a FontMatrix flattens the plane.'],
]);

/** $bytes changed by $change, which must change them. */
function cffBroken(string $bytes, Closure $change): string
{
    $changed = $change($bytes);
    if ($changed === $bytes || $changed === null) {
        throw new LogicException('the change did not apply');
    }

    return $changed;
}

it('refuses a CFF table it cannot open by TYPOG_CFF, and still reads the rest of the face', function (string $table, string $why): void {
    $font = new TinyFont();
    $font->simple(600, []);
    $font->cff = true;
    $font->cffTable = $table;
    $face = typogFace($font, 'tiny.otf');

    expect($face->vertical())->toBe([800.0, -200.0, 90.0]);
    try {
        $face->outline(0);
        $this->fail('an outline was given');
    } catch (TypogException $e) {
        expect($e->getCode())->toBe(TYPOG_CFF)->and($e->getMessage())->toContain('its CFF outlines cannot be read: '.$why);
    }
})->with([
    'major version 2' => [cffWith(fn (TinyCff $c) => $c->major = 2, TinyCff::cs('endchar'))->bytes(), 'it is not a CFF table of major version 1.'],
    'a Private entry with no values' => [cffBroken(cffOf(TinyCff::cs('endchar'))->bytes(), fn (string $b) => preg_replace('/\x1D....\x1D....\x12/s', "\x8B\x8B\x8B\x8B\x0D\x8B\x8B\x8B\x8B\x0D\x12", $b, 1)), 'its Private DICT lies outside the table.'],
    'a table cut short' => [substr(cffOf(TinyCff::cs('endchar'))->bytes(), 0, 30), 'an INDEX at 19 reaches past the table.'],
]);

it('refuses a glyph past the face\'s count before reading CFF', function (): void {
    expect(fn () => cffFace(cffOf(TinyCff::cs('endchar')))->outline(5))->toThrow(TypogException::class, 'there is no glyph 5');
});
