/*
 * This is a simple malloc()-wrapper. It does not return NULLs but
 * instead panics. It also introduces mutex wrappers
 *
 * Kalle 'rpr' Marjola 1999
 */

#ifndef _GWMEM_H
#define _GWMEM_H

/* these functions are as equivalent stdlib functions, except
 *
 * 1) they may include wrappers for debugging
 * 2) if memory allocation fails, PANIC is called
 *
 * So they work 'always', no need to check the return value.
 */

void *gw_malloc_real(size_t size, const char *filename, long line);
void *gw_realloc_real(void *ptr, size_t size, const char *filename, long line);
void  gw_free_real(void *ptr, const char *filename, long line);
char *gw_strdup_real(const char *str, const char *filename, long line);

void gw_init_mem(void);
void gw_check_leaks(void);


#define gw_malloc(size) \
	(gw_malloc_real(size, __FILE__, __LINE__))
#define gw_realloc(ptr, size) \
	(gw_realloc_real(ptr, size, __FILE__, __LINE__))
#define gw_free(ptr) \
	(gw_free_real(ptr, __FILE__, __LINE__))
#define gw_strdup(str) \
	(gw_strdup_real(str, __FILE__, __LINE__))


#endif
