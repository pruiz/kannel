/*
 * gwmem.h
 *
 * This is a simple malloc()-wrapper. It does not return NULLs but
 * instead panics.
 *
 * We have two wrappers. One that just checks for allocation failures and
 * panics if they happen and one that tries to find allocation problems,
 * such as using an area after it has been freed.
 *
 * Kalle Marjola
 * Lars Wirzenius
 */

#ifndef _GWMEM_H
#define _GWMEM_H


#if USE_GWMEM_NATIVE

/*
 * The `native' wrapper.
 */

void gw_native_init(void);
void gw_native_check_leaks(void);
void *gw_native_malloc(size_t size);
void *gw_native_realloc(void *ptr, size_t size);
void *gw_native_free(void *ptr);
char *gw_strdup(const char *str);

#define gw_init_mem()
#define gw_check_leaks()
#define gw_malloc(size) (gw_native_malloc(size))
#define gw_realloc(ptr, size) (gw_native_realloc(ptr, size))
#define gw_free(ptr) (gw_native_free(ptr))
#define gw_strdup(str) (gw_native_strdup(str))
#define gw_assert_allocated(ptr, file, line, function)

#elif USE_GWMEM_CHECK

/*
 * The `check' wrapper.
 */

void gw_check_init_mem(void);
void gw_check_check_leaks(void);
void *gw_check_malloc(size_t size, 
	const char *filename, long line, const char *function);
void *gw_check_realloc(void *ptr, size_t size, 
	const char *filename, long line, const char *function);
void  gw_check_free(void *ptr, 
	const char *filename, long line, const char *function);
char *gw_check_strdup(const char *str, 
	const char *filename, long line, const char *function);
void gw_check_assert_allocated_real(void *ptr, const char *filename, long line,
	const char *function);

#define gw_init_mem() (gw_check_init_mem())
#define gw_check_leaks() (gw_check_check_leaks())
#define gw_malloc(size) \
	(gw_check_malloc(size, __FILE__, __LINE__, __FUNCTION__))
#define gw_realloc(ptr, size) \
	(gw_check_realloc(ptr, size, __FILE__, __LINE__, __FUNCTION__))
#define gw_free(ptr) \
	(gw_check_free(ptr, __FILE__, __LINE__, __FUNCTION__))
#define gw_strdup(str) \
	(gw_check_strdup(str, __FILE__, __LINE__, __FUNCTION__))
#define gw_assert_allocated(ptr, file, line, function) \
	(gw_check_assert_allocated_real(ptr, file, line, function))

#else

/*
 * Unknown wrapper. Oops.
 */
#error "Unknown malloc wrapper."


#endif


#endif
