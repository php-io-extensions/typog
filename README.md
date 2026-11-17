<div align="center" id="header">

<h1>ext-typog</h1>

<h3>OpenType and TrueType fonts read in C, for PHP 8.4+</h3>

[![CI](https://img.shields.io/github/actions/workflow/status/php-io-extensions/typog/ci.yml?style=flat-square&label=CI)](https://github.com/php-io-extensions/typog/actions/workflows/ci.yml)
[![Packagist](https://img.shields.io/packagist/v/php-io-extensions/typog?style=flat-square)](https://packagist.org/packages/php-io-extensions/typog)
[![PHP](https://img.shields.io/packagist/dependency-v/php-io-extensions/typog/php?style=flat-square&label=PHP)](https://packagist.org/packages/php-io-extensions/typog)
[![License](https://img.shields.io/github/license/php-io-extensions/typog?style=flat-square)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-linux_%7C_macos-blue?style=flat-square&logo=linux&logoColor=white)](#requirements)
[![OKF](https://img.shields.io/badge/OKF-0.2-6f42c1?style=flat-square&label=knowledge%20bundle)](.okf/index.md)
[![PIE](https://img.shields.io/badge/PIE-installable-4f5b93?style=flat-square&logo=php&logoColor=white)](https://github.com/php/pie)

</div>

Laying out text means reading font files: a face's metrics, which glyph each character takes, how wide each glyph is, its outline, and which pairs of glyphs move closer. Done in PHP, that is a loop of `unpack()` calls over binary tables for every face and every glyph.

<b>ext-typog</b> reads them in C. `TypogFace` opens one face of an OpenType or TrueType file and answers its metrics, character map, advances, TrueType outlines and `kern` pairs, shapes text one glyph a character, and describes a file's faces for a font catalog. A variable font is read at the instance its variations name (`fvar`, `avar`, `gvar`, `HVAR`, `MVAR`). It draws nothing and has no dependency.

## Basic usage

```php
$face = new TypogFace('/System/Library/Fonts/Helvetica.ttc', 0);

$face->unitsPerEm();            // 2048
$face->vertical();              // [ascender, descender, lineGap] in font units

$bold = new TypogFace('/Library/Fonts/SF-Pro.ttf', 0, ['wght' => 700.0, 'wdth' => 90.0]);   // a variable font's instance
$glyph = $face->glyph(ord('g'));
$face->advance($glyph);         // font units
$face->outline($glyph);         // [['M', x, y], ['Q', cx, cy, x, y], ..., ['Z']], y up

[$glyphs, $offsets, $advances] = $face->shape('AVATAR', 16.0);   // advances at 16 units an em, kerned

TypogFace::describe('/System/Library/Fonts/Helvetica.ttc');
// [[0, 'Helvetica', 400, TYPOG_UPRIGHT, 5, []], [1, 'Helvetica', 700, TYPOG_UPRIGHT, 5, []], ...]; the last is fvar's axes
```

A file it cannot read is a `TypogException` whose code says why:

```php
try {
    (new TypogFace($path))->outline($glyph);
} catch (TypogException $e) {
    $e->getCode() === TYPOG_CFF;          // CFF2 outlines, which ext-typog does not read, or a CFF table it cannot read
    $e->getCode() === TYPOG_NO_OUTLINES;  // no 'glyf', 'CFF ' or 'CFF2' table: bitmaps only, or outlines such as 'hvgl'
}
```

## Requirements

* PHP 8.4 or later, NTS or ZTS
* Linux or macOS
* A C compiler and the PHP development headers. No system library.

## Installation

### With PHP PIE (recommended)

```bash
pie install php-io-extensions/typog
```

### Manually (install script)

```bash
./install-macos.sh            # Homebrew php@8.4 and php@8.4-zts
./install-debian-trixie.sh    # Debian trixie, Raspberry Pi OS
```

### Manually (phpize)

```bash
phpize
./configure --enable-typog
make
sudo make install
```

Then add `extension=typog` to your `php.ini`.

### Verify

```bash
php --ri typog
```

## API

See [the face concept](.okf/api/face.md) for every method and the rules it reads a face by: vertical metrics, the character map's subtable order, outlines and their origin, kerning, shaping and describing.

What it does not do: GSUB, GPOS and Apple's AAT tables (no ligatures, contextual forms or GPOS kerning), CFF2 outlines, hinting, colour glyphs, variable font instances.

## Testing

```bash
composer install
php -d extension=modules/typog.so -d memory_limit=128M vendor/bin/pest
```

The tests write every font they read, so each expects exactly the values it wrote.

## License

MIT. See [LICENSE](LICENSE).
