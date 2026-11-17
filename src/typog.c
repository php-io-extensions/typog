/*
 * typog: OpenType and TrueType fonts read in C. TypogFace opens one face of a
 * file and answers its metrics, character map, advances, outlines and 'kern'
 * pairs, shapes text one glyph a character, and describes a file's faces for a
 * catalog. No system library, no framework names.
 */

#include "runtime.h"
#include "ext/standard/info.h"
#include "zend_exceptions.h"
#include "../stubs/typog_arginfo.h"
#include "../stubs/TypogException_arginfo.h"

zend_class_entry *phptg_ce_TypogException;

void phptg_register_TypogException(void)
{
	phptg_ce_TypogException = register_class_TypogException(zend_ce_exception);
}

void phptg_throw(int code, const char *message)
{
	zend_throw_exception(phptg_ce_TypogException, message, code);
}

PHP_MINIT_FUNCTION(typog)
{
	(void) type;

	register_typog_symbols(module_number);

	phptg_register_TypogException();
	phptg_register_TypogFace();

	return SUCCESS;
}

PHP_RINIT_FUNCTION(typog)
{
	(void) type;
	(void) module_number;
#if defined(COMPILE_DL_TYPOG) && defined(ZTS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	return SUCCESS;
}

PHP_MINFO_FUNCTION(typog)
{
	(void) zend_module;

	php_info_print_table_start();
	php_info_print_table_row(2, "typog support", "enabled");
	php_info_print_table_row(2, "Version", PHP_TYPOG_VERSION);
	php_info_print_table_end();
}

zend_module_entry typog_module_entry = {
	STANDARD_MODULE_HEADER,
	"typog",
	NULL,
	PHP_MINIT(typog),
	NULL,
	PHP_RINIT(typog),
	NULL,
	PHP_MINFO(typog),
	PHP_TYPOG_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_TYPOG
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(typog)
#endif
