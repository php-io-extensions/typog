<?php

declare(strict_types=1);

namespace Typog\Tests\Support;

/**
 * Writes a small TrueType font, table by table, so a test can state what a face holds and check
 * that the reader finds exactly that. Glyph 0 is the missing glyph, empty. Checksums are left at 0:
 * nothing reads them.
 */
final class TinyFont
{
    public int $unitsPerEm = 1000;

    /** @var array{int, int, int} 'hhea' ascender, descender, line gap */
    public array $hhea = [800, -200, 90];

    /** @var array{int, int, int} OS/2 typographic ascender, descender, line gap */
    public array $typo = [700, -300, 50];

    /** @var array{int, int} OS/2 Windows ascent, descent */
    public array $windows = [900, 250];

    public bool $useTypoMetrics = false;

    public string $family = 'Tiny';

    public ?string $typographicFamily = null;

    public int $weight = 400;

    public int $width = 5;

    public bool $italic = false;

    public bool $oblique = false;

    /** 'glyf' outlines, or a 'CFF ' table and no 'glyf': $cffTable (TinyCff's bytes), or a stand-in. */
    public bool $cff = false;

    public ?string $cffTable = null;

    /** The format 4 map written as a Windows symbol subtable (3, 0) instead of Unicode (3, 1). */
    public bool $symbol = false;

    /** A stand-in 'CFF2' table and no 'glyf'. */
    public bool $cff2 = false;

    /** Outlines only in a table of this tag no reader here draws (Apple's 'hvgl', say): stand-in bytes, and no 'glyf'. */
    public ?string $otherOutlines = null;

    /** @var list<array{string, float, float, float}> 'fvar' axes: tag, minimum, default, maximum */
    public array $axes = [];

    /** @var array<int, list<array{float, float}>> 'avar' segment maps: axis => [from, to] pairs */
    public array $avar = [];

    /** 'avar''s major version, to write a version OpenType does not have. */
    public int $avarVersion = 1;

    /** 'fvar's instanceSize; null writes the spec's 4 * axisCount + 4 */
    public ?int $instanceSize = null;

    /** @var list<list<float>> 'gvar' shared tuples */
    public array $sharedTuples = [];

    /** A raw 'HVAR' table (see hvar()); null for none. */
    public ?string $hvar = null;

    /** A raw 'MVAR' table (see mvar()); null for none. */
    public ?string $mvar = null;

    /** @var array<int, array{list<int>|null, list<array<string, mixed>>}> glyph => shared point numbers, tuples */
    private array $variations = [];

    /** Long metrics in 'hmtx'; glyphs past them repeat the last advance. Null: every glyph. */
    public ?int $longMetrics = null;

    /** @var list<array{int, int|null, string}> advance, left side bearing (null: the glyph's left edge), 'glyf' bytes */
    private array $glyphs = [[500, null, '']];

    /** @var array<int, int> codepoint => glyph, written in format 4 (first plane) */
    private array $format4 = [];

    /** @var array<int, int> codepoint => glyph, written in format 12 */
    private array $format12 = [];

    /** @var list<array{int, list<array{int, int, int}>}> coverage, pairs [left, right, value] */
    private array $kern = [];

    /**
     * A simple glyph. Points are [x, y, on curve], in font units.
     *
     * @param  list<list<array{int, int, bool}>>  $contours
     */
    public function simple(int $advance, array $contours, ?int $bearing = null): int
    {
        $this->glyphs[] = [$advance, $bearing, self::simpleGlyph($contours)];

        return count($this->glyphs) - 1;
    }

    /**
     * A composite glyph. Each component: ['glyph' => n] and either 'x', 'y' (an offset) or 'match'
     * => [point placed so far, point of the component]; optionally 'scale' (one for both axes),
     * 'bytes' (one-byte arguments), 'metrics' (USE_MY_METRICS).
     *
     * @param  list<array<string, mixed>>  $components
     * @param  array{int, int, int, int}  $box  xMin, yMin, xMax, yMax as the header gives them
     */
    public function composite(int $advance, array $components, array $box, ?int $bearing = null): int
    {
        $data = pack('n5', 0xFFFF, ...array_map(fn (int $v) => $v & 0xFFFF, $box));
        foreach ($components as $i => $component) {
            $flags = 0;
            $xy = ! isset($component['match']);
            [$one, $two] = $xy ? [$component['x'] ?? 0, $component['y'] ?? 0] : $component['match'];
            $flags |= $xy ? 0x0002 : 0;
            $flags |= ($component['bytes'] ?? false) ? 0 : 0x0001;
            $flags |= isset($component['scale']) ? 0x0008 : 0;
            $flags |= ($component['metrics'] ?? false) ? 0x0200 : 0;
            $flags |= $i < count($components) - 1 ? 0x0020 : 0;
            $data .= pack('n2', $flags, $component['glyph']);
            $data .= ($flags & 0x0001) !== 0 ? pack('n2', $one & 0xFFFF, $two & 0xFFFF) : pack('C2', $one & 0xFF, $two & 0xFF);
            if (isset($component['scale'])) {
                $data .= pack('n', ((int) round($component['scale'] * 16384)) & 0xFFFF);
            }
        }
        $this->glyphs[] = [$advance, $bearing, $data];

        return count($this->glyphs) - 1;
    }

    public function map(int $codepoint, int $glyph, int $format = 4): self
    {
        if ($format === 4) {
            $this->format4[$codepoint] = $glyph;
        } else {
            $this->format12[$codepoint] = $glyph;
        }

        return $this;
    }

    /**
     * A format 0 'kern' subtable, horizontal; $override sets the bit that makes its values replace
     * those before it.
     *
     * @param  list<array{int, int, int}>  $pairs  [left, right, value]
     */
    public function kern(array $pairs, bool $override = false): self
    {
        $this->kern[] = [0x0001 | ($override ? 0x0008 : 0), $pairs];

        return $this;
    }

    /**
     * A glyph's 'gvar' variation data. Each tuple: 'peak' (embedded, one value an axis) or 'shared'
     * (an index into $sharedTuples); 'start' and 'end' for an intermediate region; 'points' for
     * private point numbers ('all' for every point); 'deltas', one [dx, dy] a point it names. A tuple
     * without 'points' takes the shared point numbers, or every point when there are none.
     *
     * @param  list<array<string, mixed>>  $tuples
     * @param  list<int>|null  $shared  shared point numbers
     */
    public function vary(int $glyph, array $tuples, ?array $shared = null): self
    {
        $this->variations[$glyph] = [$shared, $tuples];

        return $this;
    }

    /**
     * An ItemVariationStore. Regions: one [start, peak, end] an axis. Subtables: 'regions' (indexes),
     * 'rows' (one delta a region), 'words' (how many leading columns are words), 'long' (words are
     * 32-bit, the rest 16-bit).
     *
     * @param  list<list<array{float, float, float}>>  $regions
     * @param  list<array{regions: list<int>, rows: list<list<int>>, words?: int, long?: bool}>  $subtables
     */
    public static function store(array $regions, array $subtables): string
    {
        $axes = $regions === [] ? 0 : count($regions[0]);
        $list = pack('n2', $axes, count($regions));
        foreach ($regions as $region) {
            foreach ($region as $coordinates) {
                $list .= pack('n3', ...array_map(fn (float $v) => self::f2dot14($v), $coordinates));
            }
        }
        $datas = [];
        foreach ($subtables as $subtable) {
            $words = $subtable['words'] ?? 0;
            $long = $subtable['long'] ?? false;
            $data = pack('n3', count($subtable['rows']), $words | ($long ? 0x8000 : 0), count($subtable['regions']))
                .pack('n*', ...$subtable['regions']);
            foreach ($subtable['rows'] as $row) {
                foreach ($row as $k => $delta) {
                    $data .= $k < $words
                        ? ($long ? pack('N', $delta & 0xFFFFFFFF) : pack('n', $delta & 0xFFFF))
                        : ($long ? pack('n', $delta & 0xFFFF) : pack('C', $delta & 0xFF));
                }
            }
            $datas[] = $data;
        }
        $header = 8 + 4 * count($datas);
        $store = pack('nNn', 1, $header, count($datas));
        $at = $header + strlen($list);
        $body = '';
        foreach ($datas as $data) {
            $store .= pack('N', $at + strlen($body));
            $body .= $data;
        }

        return $store.$list.$body;
    }

    /**
     * An 'HVAR' table over a store, with an advance map (glyph => [outer, inner], written as a
     * format 0 DeltaSetIndexMap) or none (the glyph is the inner index).
     *
     * @param  list<array{int, int}>|null  $map
     */
    public static function hvarTable(string $store, ?array $map = null): string
    {
        $mapBytes = '';
        if (! is_null($map)) {
            $mapBytes = pack('CCn', 0, 0x17, count($map));   // two-byte entries, 8 inner bits
            foreach ($map as [$outer, $inner]) {
                $mapBytes .= pack('n', ($outer << 8) | $inner);
            }
        }

        return pack('n2N4', 1, 0, 20, $mapBytes === '' ? 0 : 20 + strlen($store), 0, 0).$store.$mapBytes;
    }

    /**
     * An 'MVAR' table over a store.
     *
     * @param  array<string, array{int, int}>  $records  value tag => [outer, inner]
     */
    public static function mvarTable(string $store, array $records): string
    {
        ksort($records, SORT_STRING);
        $table = pack('n6', 1, 0, 0, 8, count($records), 12 + 8 * count($records));
        foreach ($records as $tag => [$outer, $inner]) {
            $table .= $tag.pack('n2', $outer, $inner);
        }

        return $table.$store;
    }

    public function write(string $path): string
    {
        file_put_contents($path, $this->bytes());

        return $path;
    }

    /** A collection of faces, in order. */
    public static function collection(string $path, self ...$faces): string
    {
        $fonts = array_map(fn (self $face) => $face->bytes(), $faces);
        $header = 12 + 4 * count($fonts);
        $offsets = [];
        $at = $header;
        foreach ($fonts as $font) {
            $offsets[] = $at;
            $at += strlen($font);
        }
        // each face's table offsets are from the start of the file: rewrite them
        $body = '';
        foreach ($fonts as $i => $font) {
            $tables = unpack('n', $font, 4)[1];
            for ($t = 0; $t < $tables; $t++) {
                $record = 12 + 16 * $t + 8;
                $font = substr_replace($font, pack('N', unpack('N', $font, $record)[1] + $offsets[$i]), $record, 4);
            }
            $body .= $font;
        }
        file_put_contents($path, 'ttcf'.pack('n2N', 1, 0, count($fonts)).pack('N*', ...$offsets).$body);

        return $path;
    }

    public function bytes(): string
    {
        $glyf = '';
        $loca = [];
        foreach ($this->glyphs as [, , $data]) {
            $loca[] = strlen($glyf);
            $glyf .= $data.str_repeat("\0", strlen($data) % 2);
        }
        $loca[] = strlen($glyf);

        $long = $this->longMetrics ?? count($this->glyphs);
        $hmtx = '';
        foreach ($this->glyphs as $g => [$advance, $bearing, $data]) {
            $bearing ??= $data === '' ? 0 : unpack('n', $data, 2)[1];
            $hmtx .= $g < $long ? pack('n2', $advance, $bearing & 0xFFFF) : pack('n', $bearing & 0xFFFF);
        }

        $tables = [
            'head' => pack('NNNNnn', 0x00010000, 0, 0, 0x5F0F3CF5, 0, $this->unitsPerEm).str_repeat("\0", 16)
                .pack('n4', 0, 0, 0, 0).pack('nnn', ($this->italic ? 0x02 : 0) | ($this->weight >= 700 ? 0x01 : 0), 8, 2).pack('nn', 1, 0),
            'hhea' => pack('N', 0x00010000).pack('n3', ...array_map(fn (int $v) => $v & 0xFFFF, $this->hhea)).str_repeat("\0", 24).pack('n', $long),
            'maxp' => pack('Nn', 0x00005000, count($this->glyphs)),
            'OS/2' => $this->os2(),
            'hmtx' => $hmtx,
            'cmap' => $this->cmap(),
            'name' => $this->name(),
        ];
        if ($this->cff) {
            $tables['CFF '] = $this->cffTable ?? "\x01\x00\x04\x01";
        } elseif ($this->cff2) {
            $tables['CFF2'] = "\x02\x00\x05\x00\x00";
        } elseif (! is_null($this->otherOutlines)) {
            $tables[$this->otherOutlines] = "\x00\x01\x00\x00";
        } else {
            $tables['loca'] = pack('N*', ...$loca);
            $tables['glyf'] = $glyf;
        }
        if ($this->kern !== []) {
            $tables['kern'] = $this->kernTable();
        }
        if ($this->axes !== []) {
            $tables['fvar'] = $this->fvarTable();
        }
        if ($this->avar !== []) {
            $tables['avar'] = $this->avarTable();
        }
        if ($this->variations !== []) {
            $tables['gvar'] = $this->gvarTable();
        }
        if (! is_null($this->hvar)) {
            $tables['HVAR'] = $this->hvar;
        }
        if (! is_null($this->mvar)) {
            $tables['MVAR'] = $this->mvar;
        }

        $count = count($tables);
        $directory = '';
        $data = '';
        $at = 12 + 16 * $count;
        foreach ($tables as $tag => $table) {
            $directory .= $tag.pack('N3', 0, $at + strlen($data), strlen($table));
            $data .= $table.str_repeat("\0", (4 - strlen($table) % 4) % 4);
        }

        return pack('Nn4', 0x00010000, $count, 0, 0, 0).$directory.$data;
    }

    /** @param  list<list<array{int, int, bool}>>  $contours */
    private static function simpleGlyph(array $contours): string
    {
        if ($contours === []) {
            return '';   // a glyph that draws nothing has no data
        }
        $points = array_merge(...$contours);
        $xs = array_column($points, 0);
        $ys = array_column($points, 1);
        $ends = [];
        $end = -1;
        foreach ($contours as $contour) {
            $ends[] = $end += count($contour);
        }
        $flags = [];
        $xBytes = '';
        $yBytes = '';
        [$px, $py] = [0, 0];
        foreach ($points as [$x, $y, $on]) {
            [$xFlag, $xMore] = self::coordinate($x - $px, 0x02, 0x10);
            [$yFlag, $yMore] = self::coordinate($y - $py, 0x04, 0x20);
            $flags[] = ($on ? 0x01 : 0) | $xFlag | $yFlag;
            $xBytes .= $xMore;
            $yBytes .= $yMore;
            [$px, $py] = [$x, $y];
        }
        // runs of the same flag written once, with REPEAT_FLAG and a count
        $packed = '';
        for ($i = 0, $n = count($flags); $i < $n;) {
            $run = 1;
            while ($i + $run < $n && $flags[$i + $run] === $flags[$i] && $run < 256) {
                $run++;
            }
            $packed .= $run > 1 ? chr($flags[$i] | 0x08).chr($run - 1) : chr($flags[$i]);
            $i += $run;
        }

        return pack('n5', count($contours), min($xs) & 0xFFFF, min($ys) & 0xFFFF, max($xs) & 0xFFFF, max($ys) & 0xFFFF)
            .pack('n*', ...$ends).pack('n', 0).$packed.$xBytes.$yBytes;
    }

    /**
     * One coordinate's delta: none when 0 (the same flag), a byte and a sign in the flags when short,
     * else a signed word.
     *
     * @return array{int, string}
     */
    private static function coordinate(int $delta, int $short, int $same): array
    {
        return match (true) {
            $delta === 0 => [$same, ''],
            abs($delta) < 256 => [$short | ($delta > 0 ? $same : 0), chr(abs($delta))],
            default => [0, pack('n', $delta & 0xFFFF)],
        };
    }

    private function os2(): string
    {
        $selection = ($this->italic ? 0x0001 : 0) | ($this->useTypoMetrics ? 0x0080 : 0) | ($this->oblique ? 0x0200 : 0);

        return pack('n', 4).pack('n', 500).pack('n2', $this->weight, $this->width).str_repeat("\0", 54)
            .pack('n', $selection).pack('n2', 0x20, 0xFFFF)
            .pack('n3', ...array_map(fn (int $v) => $v & 0xFFFF, $this->typo)).pack('n2', ...$this->windows)
            .str_repeat("\0", 18);
    }

    private function cmap(): string
    {
        $subtables = [];
        if ($this->format12 !== []) {
            $subtables[] = [3, 10, $this->format12Table()];
        }
        if ($this->format4 !== []) {
            $subtables[] = [3, $this->symbol ? 0 : 1, $this->format4Table()];
        }
        $header = pack('n2', 0, count($subtables));
        $at = 4 + 8 * count($subtables);
        $body = '';
        foreach ($subtables as [$platform, $encoding, $table]) {
            $header .= pack('n2N', $platform, $encoding, $at + strlen($body));
            $body .= $table;
        }

        return $header.$body;
    }

    /** Format 12: one group a run of consecutive codepoints with consecutive glyphs. */
    private function format12Table(): string
    {
        ksort($this->format12);
        $groups = [];
        foreach ($this->format12 as $codepoint => $glyph) {
            $last = array_key_last($groups);
            if (! is_null($last) && $groups[$last][1] === $codepoint - 1 && $groups[$last][2] + ($codepoint - $groups[$last][0]) === $glyph) {
                $groups[$last][1] = $codepoint;
            } else {
                $groups[] = [$codepoint, $codepoint, $glyph];
            }
        }
        $body = '';
        foreach ($groups as $group) {
            $body .= pack('N3', ...$group);
        }

        return pack('nnNNN', 12, 0, 16 + strlen($body), 0, count($groups)).$body;
    }

    /**
     * Format 4: one segment a run of consecutive codepoints, mapped by a delta when its glyphs are
     * consecutive and through the glyph array when they are not, then the closing 0xFFFF segment.
     */
    private function format4Table(): string
    {
        ksort($this->format4);
        $runs = [];
        foreach ($this->format4 as $codepoint => $glyph) {
            $last = array_key_last($runs);
            if (! is_null($last) && array_key_last($runs[$last]) === $codepoint - 1) {
                $runs[$last][$codepoint] = $glyph;
            } else {
                $runs[] = [$codepoint => $glyph];
            }
        }
        $runs[] = [0xFFFF => 0];
        $segments = count($runs);
        [$ends, $starts, $deltas, $ranges, $array] = ['', '', '', [], []];
        foreach ($runs as $s => $run) {
            $first = array_key_first($run);
            $ends .= pack('n', array_key_last($run));
            $starts .= pack('n', $first);
            $consecutive = array_values($run) === range($run[$first], $run[$first] + count($run) - 1);
            if ($consecutive || $first === 0xFFFF) {
                $deltas .= pack('n', ($run[$first] - $first) & 0xFFFF);
                $ranges[$s] = null;
            } else {
                $deltas .= pack('n', 0);
                $ranges[$s] = count($array);
                array_push($array, ...array_values($run));
            }
        }
        $rangeBytes = '';
        foreach ($ranges as $s => $index) {
            // bytes from this idRangeOffset entry to the glyph array entry it points at
            $rangeBytes .= pack('n', is_null($index) ? 0 : 2 * ($segments - $s) + 2 * $index);
        }
        $body = $ends.pack('n', 0).$starts.$deltas.$rangeBytes.pack('n*', ...$array);

        return pack('n7', 4, 14 + strlen($body), 0, 2 * $segments, 0, 0, 0).$body;
    }

    private function name(): string
    {
        $records = [[1, $this->family], [2, $this->italic ? 'Italic' : 'Regular']];
        if (! is_null($this->typographicFamily)) {
            $records[] = [16, $this->typographicFamily];
        }
        $strings = '';
        $table = '';
        foreach ($records as [$id, $text]) {
            $utf16 = mb_convert_encoding($text, 'UTF-16BE', 'UTF-8');
            $table .= pack('n6', 3, 1, 0x409, $id, strlen($utf16), strlen($strings));
            $strings .= $utf16;
        }

        return pack('n3', 0, count($records), 6 + 12 * count($records)).$table.$strings;
    }

    private function fvarTable(): string
    {
        $table = pack('n6', 1, 0, 16, 2, count($this->axes), 20).pack('n2', 0, $this->instanceSize ?? 4 * count($this->axes) + 4);
        foreach ($this->axes as $a => [$tag, $min, $default, $max]) {
            $table .= $tag.pack('N3', ...array_map(fn (float $v) => ((int) round($v * 65536)) & 0xFFFFFFFF, [$min, $default, $max]))
                .pack('n2', 0, 256 + $a);
        }

        return $table;
    }

    private function avarTable(): string
    {
        $table = pack('n4', $this->avarVersion, 0, 0, count($this->axes));
        foreach (array_keys($this->axes) as $a) {
            $maps = $this->avar[$a] ?? [];
            $table .= pack('n', count($maps));
            foreach ($maps as [$from, $to]) {
                $table .= pack('n2', self::f2dot14($from), self::f2dot14($to));
            }
        }

        return $table;
    }

    private function gvarTable(): string
    {
        $axes = count($this->axes);
        $shared = '';
        foreach ($this->sharedTuples as $tuple) {
            $shared .= pack('n*', ...array_map(fn (float $v) => self::f2dot14($v), $tuple));
        }
        $datas = [];
        foreach (array_keys($this->glyphs) as $g) {
            $datas[] = isset($this->variations[$g]) ? $this->glyphVariationData(...$this->variations[$g]) : '';
        }
        $offsets = [0];
        $array = '';
        foreach ($datas as $data) {
            $array .= $data.str_repeat("\0", strlen($data) % 2);
            $offsets[] = strlen($array);
        }
        $offsetBytes = pack('N*', ...$offsets);
        $sharedAt = 20 + strlen($offsetBytes);

        return pack('n4NnnN', 1, 0, $axes, count($this->sharedTuples), $sharedAt, count($this->glyphs), 1, $sharedAt + strlen($shared))
            .$offsetBytes.$shared.$array;
    }

    /**
     * @param  list<int>|null  $shared
     * @param  list<array<string, mixed>>  $tuples
     */
    private function glyphVariationData(?array $shared, array $tuples): string
    {
        $headers = '';
        $serialized = is_null($shared) ? '' : self::packedPoints($shared);
        foreach ($tuples as $tuple) {
            $index = 0;
            $peak = '';
            if (isset($tuple['peak'])) {
                $index |= 0x8000;
                $peak = pack('n*', ...array_map(fn (float $v) => self::f2dot14($v), $tuple['peak']));
            } else {
                $index |= $tuple['shared'];
            }
            $region = '';
            if (isset($tuple['start'])) {
                $index |= 0x4000;
                $region = pack('n*', ...array_map(fn (float $v) => self::f2dot14($v), [...$tuple['start'], ...$tuple['end']]));
            }
            $data = '';
            if (array_key_exists('points', $tuple)) {
                $index |= 0x2000;
                $data .= self::packedPoints($tuple['points'] === 'all' ? [] : $tuple['points']);
            }
            $data .= $tuple['raw'] ?? self::packedDeltas(array_column($tuple['deltas'], 0)).self::packedDeltas(array_column($tuple['deltas'], 1));
            $headers .= pack('n2', strlen($data), $index).$peak.$region;
            $serialized .= $data;
        }
        $count = count($tuples) | (is_null($shared) ? 0 : 0x8000);

        return pack('n2', $count, 4 + strlen($headers)).$headers.$serialized;
    }

    /**
     * Packed point numbers: the count, then runs of differences, a run in words when any of its
     * differences needs one. An empty list is "every point".
     *
     * @param  list<int>  $points
     */
    private static function packedPoints(array $points): string
    {
        $count = count($points);
        $bytes = $count < 128 ? chr($count) : pack('n', 0x8000 | $count);
        $differences = [];
        $last = 0;
        foreach ($points as $point) {
            $differences[] = $point - $last;
            $last = $point;
        }
        for ($i = 0; $i < $count;) {
            $words = $differences[$i] > 255;
            $run = [];
            while ($i < $count && count($run) < 128 && ($differences[$i] > 255) === $words) {
                $run[] = $differences[$i++];
            }
            $bytes .= chr((count($run) - 1) | ($words ? 0x80 : 0)).($words ? pack('n*', ...$run) : pack('C*', ...$run));
        }

        return $bytes;
    }

    /**
     * Packed deltas: runs of zeros, of bytes and of words.
     *
     * @param  list<int>  $deltas
     */
    private static function packedDeltas(array $deltas): string
    {
        $bytes = '';
        $kind = fn (int $d) => $d === 0 ? 'zero' : ($d >= -128 && $d <= 127 ? 'byte' : 'word');
        for ($i = 0, $n = count($deltas); $i < $n;) {
            $type = $kind($deltas[$i]);
            $run = [];
            while ($i < $n && count($run) < 64 && $kind($deltas[$i]) === $type) {
                $run[] = $deltas[$i++];
            }
            $bytes .= match ($type) {
                'zero' => chr(0x80 | (count($run) - 1)),
                'byte' => chr(count($run) - 1).pack('C*', ...array_map(fn (int $d) => $d & 0xFF, $run)),
                'word' => chr(0x40 | (count($run) - 1)).pack('n*', ...array_map(fn (int $d) => $d & 0xFFFF, $run)),
            };
        }

        return $bytes;
    }

    private static function f2dot14(float $value): int
    {
        return ((int) round($value * 16384)) & 0xFFFF;
    }

    private function kernTable(): string
    {
        $table = pack('n2', 0, count($this->kern));
        foreach ($this->kern as [$coverage, $pairs]) {
            usort($pairs, fn (array $a, array $b) => [$a[0], $a[1]] <=> [$b[0], $b[1]]);
            $body = pack('n4', count($pairs), 0, 0, 0);
            foreach ($pairs as [$left, $right, $value]) {
                $body .= pack('n3', $left, $right, $value & 0xFFFF);
            }
            $table .= pack('n3', 0, 6 + strlen($body), $coverage).$body;
        }

        return $table;
    }
}
