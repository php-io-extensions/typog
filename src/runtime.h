/*
 * What the class source shares with the module: the Zend object that holds a
 * face, the class entries, and how a reader's error becomes a TypogException.
 */
#ifndef PHPTG_RUNTIME_H
#define PHPTG_RUNTIME_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "php.h"
#include "php_typog.h"
#include "sfnt.h"

typedef struct {
	tg_face face;
	bool opened;   /* the constructor finished */
	zend_object std;
} phptg_face;

static zend_always_inline phptg_face *phptg_face_from(zend_object *obj)
{
	return (phptg_face *) ((char *) obj - XtOffsetOf(phptg_face, std));
}

extern zend_class_entry *phptg_ce_TypogFace;
extern zend_class_entry *phptg_ce_TypogException;

void phptg_register_TypogFace(void);
void phptg_register_TypogException(void);

/* Throws TypogException with the reader's code and message. */
void phptg_throw(int code, const char *message);

#endif
