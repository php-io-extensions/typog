/* This is a generated file, edit the .stub.php file instead.
 * Stub hash: 25d57d20cb5022f86fbcd01af1a1f556667191bb */

ZEND_BEGIN_ARG_INFO_EX(arginfo_class_TypogFace___construct, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, path, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, index, IS_LONG, 0, "0")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, variations, IS_ARRAY, 0, "[]")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypogFace_describe, 0, 1, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, path, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, skipBroken, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypogFace_path, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypogFace_index, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

#define arginfo_class_TypogFace_unitsPerEm arginfo_class_TypogFace_index

#define arginfo_class_TypogFace_glyphCount arginfo_class_TypogFace_index

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypogFace_vertical, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypogFace_glyph, 0, 1, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, codepoint, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypogFace_advance, 0, 1, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, glyph, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypogFace_outline, 0, 1, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, glyph, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypogFace_kerning, 0, 2, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, left, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, right, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypogFace_shape, 0, 2, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, text, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, size, IS_DOUBLE, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_class_TypogFace_close, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_METHOD(TypogFace, __construct);
ZEND_METHOD(TypogFace, describe);
ZEND_METHOD(TypogFace, path);
ZEND_METHOD(TypogFace, index);
ZEND_METHOD(TypogFace, unitsPerEm);
ZEND_METHOD(TypogFace, glyphCount);
ZEND_METHOD(TypogFace, vertical);
ZEND_METHOD(TypogFace, glyph);
ZEND_METHOD(TypogFace, advance);
ZEND_METHOD(TypogFace, outline);
ZEND_METHOD(TypogFace, kerning);
ZEND_METHOD(TypogFace, shape);
ZEND_METHOD(TypogFace, close);

static const zend_function_entry class_TypogFace_methods[] = {
	ZEND_ME(TypogFace, __construct, arginfo_class_TypogFace___construct, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, describe, arginfo_class_TypogFace_describe, ZEND_ACC_PUBLIC|ZEND_ACC_STATIC)
	ZEND_ME(TypogFace, path, arginfo_class_TypogFace_path, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, index, arginfo_class_TypogFace_index, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, unitsPerEm, arginfo_class_TypogFace_unitsPerEm, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, glyphCount, arginfo_class_TypogFace_glyphCount, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, vertical, arginfo_class_TypogFace_vertical, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, glyph, arginfo_class_TypogFace_glyph, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, advance, arginfo_class_TypogFace_advance, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, outline, arginfo_class_TypogFace_outline, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, kerning, arginfo_class_TypogFace_kerning, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, shape, arginfo_class_TypogFace_shape, ZEND_ACC_PUBLIC)
	ZEND_ME(TypogFace, close, arginfo_class_TypogFace_close, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static zend_class_entry *register_class_TypogFace(void)
{
	zend_class_entry ce, *class_entry;

	INIT_CLASS_ENTRY(ce, "TypogFace", class_TypogFace_methods);
	class_entry = zend_register_internal_class_with_flags(&ce, NULL, ZEND_ACC_FINAL|ZEND_ACC_NO_DYNAMIC_PROPERTIES|ZEND_ACC_NOT_SERIALIZABLE);

	return class_entry;
}
