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

#ifndef GWMEM_H
#define GWMEM_H


void gw_native_init(void);
void gw_native_check_leaks(void);
void *gw_native_malloc(size_t size);
void *gw_native_realloc(void *ptr, size_t size);
void gw_native_free(void *ptr);
char *gw_native_strdup(const char *str);
void gw_native_shutdown(void);


void gw_check_init_mem(int slow_flag);
void gw_check_check_leaks(void);
void *gw_check_malloc(size_t size, 
	const char *filename, long line, const char *function);
void *gw_check_realloc(void *p, size_t size, 
	const char *filename, long line, const char *function);
void  gw_check_free(void *p, 
	const char *filename, long line, const char *function);
char *gw_check_strdup(const char *str, 
	const char *filename, long line, const char *function);
int gw_check_is_allocated(void *p);
long gw_check_area_size(void *p);
void *gw_check_claim_area(void *p,
	const char *filename, long line, const char *function);
void gw_check_shutdown(void);


/*
 * "slow" == "checking" with a small variation.
 */
#if USE_GWMEM_SLOW
#define USE_GWMEM_CHECK 1
#endif


#if USE_GWMEM_NATIVE

/*
 * The `native' wrapper.
 */

#define gw_init_mem()
#define gw_check_leaks()
#define gw_malloc(size) (gw_native_malloc(size))
#define gw_realloc(ptr, size) (gw_native_realloc(ptr, size))
#define gw_free(ptr) (gw_native_free(ptr))
#define gw_strdup(str) (gw_native_strdup(str))
#define gw_assert_allocated(ptr, file, line, function)
#define gw_claim_area(ptr) (ptr)
#define gwmem_shutdown()

#elif USE_GWMEM_CHECK

/*
 * The `check' wrapper.
 */

#ifdef USE_GWMEM_SLOW
#define gw_init_mem() (gw_check_init_mem(1))
#else
#define gw_init_mem() (gw_check_init_mem(0))
#endif

#define gw_check_leaks() (gw_check_check_leaks())
#define gw_malloc(size) \
	(gw_check_malloc(size, __FILE__, __LINE__, __func__))
#define gw_realloc(ptr, size) \
	(gw_check_realloc(ptr, size, __FILE__, __LINE__, __func__))
#define gw_free(ptr) \
	(gw_check_free(ptr, __FILE__, __LINE__, __func__))
#define gw_strdup(str) \
	(gw_check_strdup(str, __FILE__, __LINE__, __func__))
#define gw_assert_allocated(ptr, file, line, function) \
	(gw_assert_place(gw_check_is_allocated(ptr), file, line, function))
#define gw_claim_area(ptr) \
	(gw_check_claim_area(ptr, __FILE__, __LINE__, __func__))
#define gwmem_shutdown() (gw_check_shutdown())

#else

/*
 * Unknown wrapper. Oops.
 */
#error "Unknown malloc wrapper."


#endif


/*
 * Make sure no-one uses the unwrapped functions by mistake.
 */

#define malloc(n)	do_not_call_malloc_directly
#define calloc(a, b)	do_not_use_calloc
#define realloc(p, n)	do_not_call_realloc_directly
#define free(p)	    	do_not_call_free_directly


#endif
