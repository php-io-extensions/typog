# Agent guidance — php-io-extensions/typog

1. **Read [`.okf/index.md`](.okf/index.md) first** before changing the API, the C, or packaging. Open only the concepts the change touches.
2. **Fonts in, numbers out, nothing drawn.** `TypogFace` reads one face of an OpenType or TrueType file: metrics, the character map, advances, TrueType and CFF outlines as path steps, `kern` pairs; shapes text one glyph a character; describes a file's faces for a catalog. No pixels, no windows, no glyph cache, no dependency, no framework names. See [`.okf/api/face.md`](.okf/api/face.md).
3. **Plain C on the Zend API.** No FFI, no system library. `src/sfnt.{h,c}` is plain C with no Zend in it (the reader), `src/cff.{h,c}` the same for CFF charstrings; `src/TypogFace.c` is the class; `src/typog.c` is the module and the exception.
4. **A font file is bytes this project did not write.** Every read is checked against what the file and the table hold before it is made: a short or broken file is a `TypogException` with its `TYPOG_*` code, never a read past the end, a warning or a wrong number. A new read goes through `inside()` (or `read_at()` for the file) first.
5. **The rules are a contract.** Vertical metrics, the character map's subtable order and symbol remap, the phantom point (a `USE_MY_METRICS` component's, as FreeType does), the implied on-curve points, `kern` folding (sum, override replaces) and the shaping arithmetic are written down in [`.okf/api/face.md`](.okf/api/face.md). A change to one changes every caller's numbers: it needs its reason and a test working the expected values by hand.
6. **One flag stays in `config.m4`.** `-ffp-contract=off`: without it the compiler fuses a composite's `a * x + c * y` into one rounding and its points move by a bit.
7. **Every method is written out.** One `PHP_METHOD` a method, matching its stub. No macro that stamps out methods.
8. **Refuse, don't guess.** What the file cannot answer is a `TypogException` with its code; an argument out of range or text that is not UTF-8 is a `ValueError` naming the argument; a call on an unopened face is an `Error`.
9. **NTS and ZTS.** No module globals. Every change builds with `-Wall -Wextra` without a warning from `src/` and passes Pest under NTS and ZTS on macOS and Linux, and under guard malloc on macOS.
10. **Tests are Pest v4 and write their own fonts** (`tests/Support/TinyFont.php`): the values a test expects are the ones it wrote. No font file lives in git.
11. **Durable facts go in `.okf`.** Update the matching concept, bump its `generated.at`, append `.okf/log.md`. The bundle documents the package, never a session.
12. **Version**: `PHP_TYPOG_VERSION` moves only past a published tag; tags drive Packagist.
