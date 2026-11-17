# Security Policy

## Supported versions

ext-typog is pre-1.0. No 0.x release receives security fixes or advisories; fixes land in the
next release line. Security support starts with 1.0.

| Version | Security fixes |
|---------|----------------|
| < 1.0   | No             |

## Reporting a vulnerability

Please don't open a public issue for a security problem.

Report it privately through GitHub: the **Report a vulnerability** button on the
[php-io-extensions/typog](https://github.com/php-io-extensions/typog) repository's **Security**
tab. If that isn't available, email **info@projectsaturnstudios.com**.

Include what you found, the affected version, your platform and PHP build (NTS or ZTS), the font
file or the calls that trigger it, and steps to reproduce. Reports are read by the maintainers and
answered as soon as we can.

## What the extension touches

ext-typog is plain C on the Zend API. It links no system library and opens no socket or device.

- **Files.** It opens the font file a `TypogFace` is made with, read-only, and reads it with
  `pread`. It writes nothing. `close()` closes it; dropping the object does too.
- **Untrusted bytes.** A font file is input the extension did not write. Every offset and length
  in it is checked against the size of the table or file it points into before anything is read
  there; a file that is short, or whose tables contradict each other, is refused with a
  `TypogException`. Composite glyphs nest at most 8 deep. The suite runs under guard malloc on
  macOS, and the reader was fuzzed with randomly corrupted font files under guard malloc.
- **Memory.** Tables are read whole into memory the face owns, except the glyph table, which is
  read a glyph at a time. A table is as large as the file says, up to the file's size.
- **Text.** `shape()` reads UTF-8 and refuses invalid sequences, overlong forms and surrogates
  with a `ValueError` naming the byte.
