/*
 * gwassert.h - assertions macros that report via log files
 *
 * Define our own version of assert that calls panic(), because the
 * normal assert() prints to stdout which no-one will see.
 *
 * We also define a gw_assert_place macro so that we can easily use it
 * data structure consistency checking function and report the place where
 * the consistency checking function was called.
 *
 * Richard Braakman
 * Lars Wirzenius
 */

#include "log.h"  /* for panic() */

/* The normal assert() does nothing if NDEBUG is defined.  We honor both
 * NDEBUG and our own NO_GWASSERT.  If NDEBUG is defined, we always turn
 * on NO_GWASSERT, so that user code does not have to check for them
 * separately. */

#if defined(NDEBUG) && !defined(NO_GWASSERT)
#define NO_GWASSERT
#endif

#ifdef NO_GWASSERT
#define gw_assert(expr) ((void) 0)
#define gw_assert_place(expr, file, lineno, func) ((void) 0)
#else
#define gw_assert(expr) \
	((void) ((expr) ? 0 : \
		  (panic(0, "%s:%ld: %s: Assertion `%s' failed.", \
			__FILE__, (long) __LINE__, __func__, #expr), 0)))
#define gw_assert_place(expr, file, lineno, func) \
	((void) ((expr) ? 0 : \
		  (panic(0, "%s:%ld: %s: Assertion `%s' failed. " \
		           "(Called from %s:%ld:%s.)", \
			      __FILE__, (long) __LINE__, __func__, \
			      #expr, (file), (long) (lineno), (func)), 0)))
#endif
