PHP_ARG_ENABLE([typog],
  [whether to enable typog support],
  [AS_HELP_STRING([--enable-typog], [Enable the typog font reading and shaping extension])],
  [no])

if test "$PHP_TYPOG" != "no"; then
  dnl -ffp-contract=off: no fused multiply-add, so a composite glyph's points come out of its
  dnl transform rounded step by step, as the same arithmetic in PHP rounds them.
  PHP_NEW_EXTENSION([typog],
    [src/typog.c src/sfnt.c src/cff.c src/TypogFace.c],
    [$ext_shared],, [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1 -ffp-contract=off])
  PHP_ADD_BUILD_DIR([$ext_builddir/src])
fi
