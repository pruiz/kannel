/* Define our own version of assert that calls panic(), because the
 * normal assert() prints to stdout which no-one will see. */

#include "log.h"  /* for panic() */

/* If NDEBUG is defined, assert does nothing. */

#ifdef NDEBUG
#define gw_assert(expr) ((void) 0)
#else
#define gw_assert(expr) ((void) ((expr) ? 0 : \
			panic(0, "%s:%d: %s: Assertion `%s' failed.", \
			      __FILE__, __LINE__, __FUNCTION__, #expr), 0))
#endif
