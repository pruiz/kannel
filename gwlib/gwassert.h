/* Define our own version of assert that calls panic(), because the
 * normal assert() prints to stdout which no-one will see. */

#include "log.h"  /* for panic() */

/* If NDEBUG is defined, assert does nothing. */

#ifdef NDEBUG
#define gw_assert(expr) ((void) 0)
#define gw_assert_place(expr, file, line) ((void) 0)
#else
#define gw_assert(expr) \
	gw_assert_place(expr, __FILE__, __LINE__, __FUNCTION__)
#define gw_assert_place(expr, file, lineno, func) ((void) ((expr) ? 0 : \
			panic(0, "%s:%ld: %s: Assertion `%s' failed.", \
			      (file), (long) (lineno), (func), #expr), 0))
#endif
