<?php

declare(strict_types=1);

namespace Typog\Tests\Support;

/**
 * Writes a 'CFF ' table (Adobe TN #5176) so a test can state what each glyph's Type 2 charstring
 * holds and check the reader draws exactly that: name-keyed with one Private DICT, or CID-keyed with
 * a Font DICT a group of glyphs, chosen by FDSelect format 0 or 3. Offsets in DICTs are written as
 * five-byte integers, so a DICT's length never depends on where things land.
 *
 * Charstrings are written with cs(): numbers become operands (an int in its shortest form, a float
 * as 16.16 fixed), strings name operators; ['short', n] forces the three-byte form (28),
 * ['two', n] the two-byte forms (247..254), ['byte', n] the one-byte form, ['fixed', x] form 255.
 */
final class TinyCff
{
    /** @var list<string> charstrings, glyph by glyph */
    public array $glyphs = [];

    /** @var list<string> */
    public array $globalSubrs = [];

    /** @var list<string> the Private DICT's subrs, for a name-keyed font */
    public array $localSubrs = [];

    /** @var list<list<string>>|null CID-keyed: each Font DICT's local subrs */
    public ?array $fonts = null;

    /** @var list<int> each glyph's Font DICT, for a CID-keyed font */
    public array $select = [];

    /** FDSelect format, 0 or 3. */
    public int $selectFormat = 3;

    /** @var list<float>|null the Top DICT's FontMatrix; null leaves it out */
    public ?array $fontMatrix = null;

    /** @var array<int, list<float>> Font DICT => its FontMatrix, for a CID-keyed font; one not named has none */
    public array $fdMatrices = [];

    /** @var list<int>|null SIDs of glyphs 1.., written as a format 0 charset; null leaves the charset ISOAdobe */
    public ?array $charset = null;

    /** Major version written in the header. */
    public int $major = 1;

    private const array OPERATORS = [
        'hstem' => [1], 'vstem' => [3], 'vmoveto' => [4], 'rlineto' => [5], 'hlineto' => [6], 'vlineto' => [7],
        'rrcurveto' => [8], 'callsubr' => [10], 'return' => [11], 'endchar' => [14], 'hstemhm' => [18],
        'hintmask' => [19], 'cntrmask' => [20], 'rmoveto' => [21], 'hmoveto' => [22], 'vstemhm' => [23],
        'rcurveline' => [24], 'rlinecurve' => [25], 'vvcurveto' => [26], 'hhcurveto' => [27], 'callgsubr' => [29],
        'vhcurveto' => [30], 'hvcurveto' => [31],
        'and' => [12, 3], 'or' => [12, 4], 'not' => [12, 5], 'abs' => [12, 9], 'add' => [12, 10], 'sub' => [12, 11],
        'div' => [12, 12], 'neg' => [12, 14], 'eq' => [12, 15], 'drop' => [12, 18], 'put' => [12, 20], 'get' => [12, 21],
        'ifelse' => [12, 22], 'random' => [12, 23], 'mul' => [12, 24], 'sqrt' => [12, 26], 'dup' => [12, 27],
        'exch' => [12, 28], 'index' => [12, 29], 'roll' => [12, 30],
        'hflex' => [12, 34], 'flex' => [12, 35], 'hflex1' => [12, 36], 'flex1' => [12, 37],
    ];

    /** A Type 2 charstring: numbers as operands, names as operators, raw bytes as ['raw', bytes]. */
    public static function cs(mixed ...$tokens): string
    {
        $out = '';
        foreach ($tokens as $token) {
            if (is_string($token)) {
                $out .= pack('C*', ...(self::OPERATORS[$token] ?? throw new \LogicException("No operator '{$token}'.")));
            } elseif (is_int($token)) {
                $out .= self::number($token);
            } elseif (is_float($token)) {
                $out .= self::fixed($token);
            } else {
                [$form, $v] = $token;
                $out .= match ($form) {
                    'short' => "\x1C".pack('n', $v & 0xFFFF),
                    'two' => self::number($v, true),
                    'byte' => chr($v + 139),
                    'fixed' => self::fixed($v),
                    'raw' => $v,
                };
            }
        }

        return $out;
    }

    private static function number(int $v, bool $two = false): string
    {
        if (! $two && $v >= -107 && $v <= 107) {
            return chr($v + 139);
        }
        if ($v >= 108 && $v <= 1131) {
            $v -= 108;

            return chr(247 + intdiv($v, 256)).chr($v % 256);
        }
        if ($v <= -108 && $v >= -1131) {
            $v = -$v - 108;

            return chr(251 + intdiv($v, 256)).chr($v % 256);
        }
        if ($two) {
            throw new \LogicException("{$v} has no two-byte form.");
        }

        return "\x1C".pack('n', $v & 0xFFFF);
    }

    private static function fixed(float $v): string
    {
        return "\xFF".pack('N', ((int) round($v * 65536)) & 0xFFFFFFFF);
    }

    public function bytes(): string
    {
        $cid = ! is_null($this->fonts);
        $header = pack('C4', $this->major, 0, 4, 4);
        $name = self::index(['Tiny']);
        $strings = self::index([]);
        $global = self::index($this->globalSubrs);
        $charStrings = self::index($this->glyphs);

        // the Top DICT has a fixed length: work it out with every offset 0, then again with them
        $top = fn (array $at) => self::index([$this->topDict($at, $cid)]);
        $start = strlen($header) + strlen($name) + strlen($top(['cs' => 0, 'charset' => 0, 'private' => [0, 0], 'fdarray' => 0, 'fdselect' => 0])) + strlen($strings) + strlen($global);

        $at = ['cs' => $start];
        $body = $charStrings;
        $charset = '';
        if (! is_null($this->charset)) {
            $at['charset'] = $start + strlen($body);
            $charset = pack('C', 0).pack('n*', ...$this->charset);
            $body .= $charset;
        }
        if ($cid) {
            $at['fdselect'] = $start + strlen($body);
            $body .= $this->fdSelect();
            // each Font DICT points at its Private DICT, which sits after the FDArray
            $fdCount = count($this->fonts);
            $fdDict = fn (int $fd, int $size, int $offset) => (isset($this->fdMatrices[$fd]) ? self::matrix($this->fdMatrices[$fd]) : '').self::int($size).self::int($offset).chr(18);
            $fdArrayLength = strlen(self::index(array_map(fn (int $fd) => $fdDict($fd, 0, 0), range(0, $fdCount - 1))));
            $at['fdarray'] = $start + strlen($body);
            $privateAt = $at['fdarray'] + $fdArrayLength;
            $dicts = [];
            $privates = '';
            foreach ($this->fonts as $fd => $subrs) {
                [$private, $size] = self::private($subrs);
                $dicts[] = $fdDict($fd, $size, $privateAt + strlen($privates));
                $privates .= $private;
            }
            $body .= self::index($dicts).$privates;
        } else {
            [$private, $size] = self::private($this->localSubrs);
            $at['private'] = [$size, $start + strlen($body)];
            $body .= $private;
        }

        return $header.$name.$top($at + ['charset' => 0, 'private' => [0, 0], 'fdarray' => 0, 'fdselect' => 0]).$strings.$global.$body;
    }

    private function topDict(array $at, bool $cid): string
    {
        $dict = '';
        if ($cid) {
            $dict .= self::int(0).self::int(0).self::int(0)."\x0C\x1E";      // ROS
        }
        if (! is_null($this->fontMatrix)) {
            $dict .= self::matrix($this->fontMatrix);
        }
        $dict .= self::int($at['cs'])."\x11";
        if (! is_null($this->charset)) {
            $dict .= self::int($at['charset'])."\x0F";
        }
        if ($cid) {
            $dict .= self::int($at['fdarray'])."\x0C\x24".self::int($at['fdselect'])."\x0C\x25";
        } else {
            $dict .= self::int($at['private'][0]).self::int($at['private'][1])."\x12";
        }

        return $dict;
    }

    /**
     * A Private DICT and, after it, its subrs.
     *
     * @param  list<string>  $subrs
     * @return array{string, int} the bytes and the DICT's own size
     */
    private static function private(array $subrs): array
    {
        if ($subrs === []) {
            return ['', 0];
        }
        $dict = self::int(6)."\x13";    // Subrs, right after this six-byte DICT

        return [$dict.self::index($subrs), strlen($dict)];
    }

    private function fdSelect(): string
    {
        if ($this->selectFormat === 0) {
            return pack('C', 0).pack('C*', ...$this->select);
        }
        $ranges = [];
        foreach ($this->select as $glyph => $fd) {
            if ($ranges === [] || $ranges[array_key_last($ranges)][1] !== $fd) {
                $ranges[] = [$glyph, $fd];
            }
        }
        $bytes = pack('Cn', 3, count($ranges));
        foreach ($ranges as [$first, $fd]) {
            $bytes .= pack('nC', $first, $fd);
        }

        return $bytes.pack('n', count($this->select));
    }

    /** @param list<string> $items */
    public static function index(array $items): string
    {
        if ($items === []) {
            return pack('n', 0);
        }
        $offsets = [1];
        foreach ($items as $item) {
            $offsets[] = end($offsets) + strlen($item);
        }

        return pack('nC', count($items), 4).pack('N*', ...$offsets).implode('', $items);
    }

    private static function int(int $v): string
    {
        return "\x1D".pack('N', $v & 0xFFFFFFFF);
    }

    /** @param list<float> $m */
    private static function matrix(array $m): string
    {
        return implode('', array_map(self::real(...), $m))."\x0C\x07";
    }

    private static function real(float $v): string
    {
        $text = rtrim(rtrim(sprintf('%.10F', $v), '0'), '.');
        $text = $text === '' || $text === '-' ? '0' : $text;
        $nibbles = [];
        foreach (str_split($text) as $c) {
            $nibbles[] = match ($c) { '.' => 0xA, '-' => 0xE, default => (int) $c };
        }
        $nibbles[] = 0xF;
        if (count($nibbles) % 2 === 1) {
            $nibbles[] = 0xF;
        }
        $bytes = "\x1E";
        for ($i = 0; $i < count($nibbles); $i += 2) {
            $bytes .= chr(($nibbles[$i] << 4) | $nibbles[$i + 1]);
        }

        return $bytes;
    }
}
