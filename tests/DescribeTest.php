<?php

declare(strict_types=1);

use Typog\Tests\Support\TinyFont;

/* describe(): a file's faces as a catalog matches them. */

it('describes each face by family, weight, slant and width, the typographic family first', function (): void {
    $plain = new TinyFont();
    $plain->family = 'Plain';
    $grouped = new TinyFont();
    $grouped->family = 'Grouped Black';
    $grouped->typographicFamily = 'Grouped';
    $grouped->weight = 900;
    $grouped->italic = true;
    $grouped->width = 3;
    $leaning = new TinyFont();
    $leaning->family = 'Leaning';
    $leaning->oblique = true;
    $path = TinyFont::collection(fontDirectory().'/three.ttc', $plain, $grouped, $leaning);

    expect(TypogFace::describe($path))->toBe([
        [0, 'Plain', 400, TYPOG_UPRIGHT, 5, [], 'glyf'],
        [1, 'Grouped', 900, TYPOG_ITALIC, 3, [], 'glyf'],
        [2, 'Leaning', 400, TYPOG_OBLIQUE, 5, [], 'glyf'],
    ]);
});

it('refuses a collection that claims more faces than its file has room for, at 32 bytes a face', function (): void {
    $path = TinyFont::collection(fontDirectory().'/claims.ttc', new TinyFont(), new TinyFont());
    $room = intdiv(filesize($path), 32);
    $claim = static function (int $faces) use ($path): void {
        $bytes = (string) file_get_contents($path);
        file_put_contents($path, substr_replace($bytes, pack('N', $faces), 8, 4));
    };

    $claim($room);
    $described = TypogFace::describe($path, true);
    $claim($room + 1);

    expect(array_slice($described, 0, 2))->toBe([[0, 'Tiny', 400, TYPOG_UPRIGHT, 5, [], 'glyf'], [1, 'Tiny', 400, TYPOG_UPRIGHT, 5, [], 'glyf']])
        ->and(fn () => TypogFace::describe($path, true))->toThrow(TypogException::class, sprintf('claims %d faces', $room + 1))
        ->and(fn () => new TypogFace($path, 0))->toThrow(TypogException::class, sprintf('claims %d faces', $room + 1));
});

it('leaves out a face it cannot read when asked to, and refuses it otherwise', function (): void {
    $named = new TinyFont();
    $nameless = new TinyFont();
    $nameless->family = ' ';
    $path = TinyFont::collection(fontDirectory().'/two.ttc', $named, $nameless);

    expect(TypogFace::describe($path, true))->toBe([[0, 'Tiny', 400, TYPOG_UPRIGHT, 5, [], 'glyf']])
        ->and(fn () => TypogFace::describe($path))->toThrow(TypogException::class, 'has no family name');
});

it('names each face\'s outline table: glyf, CFF, CFF2, or none for outlines it does not read', function (): void {
    $glyf = new TinyFont();
    $cff = new TinyFont();
    $cff->cff = true;
    $cff2 = new TinyFont();
    $cff2->cff2 = true;
    $other = new TinyFont();
    $other->otherOutlines = 'hvgl';
    $path = TinyFont::collection(fontDirectory().'/four.ttc', $glyf, $cff, $cff2, $other);

    expect(array_column(TypogFace::describe($path), 6))->toBe(['glyf', 'CFF ', 'CFF2', '']);
});
