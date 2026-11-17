<?php

declare(strict_types=1);

/*
 * ext-typog's suite. Every font a test reads it writes itself (Support\TinyFont), so the values a
 * test expects are the ones it wrote.
 */

/** A fresh directory for one test's fonts, removed with them after it. */
function fontDirectory(): string
{
    $directory = sys_get_temp_dir().'/typog-'.getmypid().'-'.bin2hex(random_bytes(4));
    mkdir($directory);
    register_shutdown_function(function () use ($directory): void {
        array_map('unlink', glob($directory.'/*') ?: []);
        @rmdir($directory);
    });

    return $directory;
}

/** The font written into a fresh directory and opened. */
function typogFace(\Typog\Tests\Support\TinyFont $font, string $name = 'face.ttf'): TypogFace
{
    return new TypogFace($font->write(fontDirectory()."/{$name}"));
}
