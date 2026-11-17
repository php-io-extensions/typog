#ifndef PHP_TYPOG_H
#define PHP_TYPOG_H

extern zend_module_entry typog_module_entry;
#define phpext_typog_ptr &typog_module_entry

#define PHP_TYPOG_VERSION "0.11.0"

#if defined(ZTS) && defined(COMPILE_DL_TYPOG)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif
