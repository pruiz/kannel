/*
 * This is a simple malloc()-wrapper. It does not return NULLs but
 * instead panics.
 *
 * Kalle 'rpr' Marjola 1999
 */

#ifndef _GWMEM_H
#define _GWMEM_H

#include <stdlib.h>

/* these functions are as equivalent stdlib functions, except
 *
 * 1) they may include wrappers for debugging
 * 2) if memory allocation fails, PANIC is called
 *
 * So they work 'always', so need to check the return value.
 */

void *gw_malloc(size_t size);
void *gw_realloc(void *ptr, size_t size);
void  gw_free(void *ptr);

#endif
