---
type: Runbook
title: Build, install, test
description: Scratch build with gen_stub, the two installers, Pest, guard malloc.
resource: install-macos.sh
tags: [build, linux, macos, pest]
status: draft
generated: { by: claude-opus/5.5, at: 2026-11-16T20:00:00Z }
sources:
  - id: mac
    resource: install-macos.sh
    title: install-macos.sh
  - id: debian
    resource: install-debian-trixie.sh
    title: install-debian-trixie.sh
---

# Overview

No system library needed: php-dev and a C compiler.

Dev loop: copy `config.m4 php_typog.h src stubs` to a scratch dir, `phpize`, `php build/gen_stub.php stubs`, `./configure --enable-typog --with-php-config=… CFLAGS="-g -O2 -Wall -Wextra"`, `make`. Copy `stubs/*_arginfo.h` back after a stub edit. Run Pest with `-d extension=<scratch>/modules/typog.so -d memory_limit=128M`. No warning from `src/` is acceptable; the linker's notes about `-undefined suppress` and `-single_module` on macOS are PHP's own.

Memory check on macOS: `DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib` before the same Pest command.

macOS (`install-macos.sh`): builds in a temp copy for php@8.4 and php@8.4-zts, ad-hoc signs, writes `30-typog.ini`.[^mac]

Linux (`install-debian-trixie.sh`): builds in place, installs `typog.so`, writes `30-typog.ini`, removes build output.[^debian]

`config.m4` passes `-ffp-contract=off`: without it the compiler may fuse a composite glyph's `a * x + c * y` into one rounding, and its points differ in the last bit from the same arithmetic in PHP.

[^mac]: Same script as ext-comrec's, names changed.
[^debian]: Same script as ext-comrec's, names changed.
