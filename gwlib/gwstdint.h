#ifndef GW_STDINT_H
#define GW_STDINT_H

/* gwlib/gwstdint.h - replacement, until <stdint.h> is everywhere
 *
 * First, two system headers are included, <sys/types.h> and <stdint.h>.
 * If the latter is missing, it is not included.
 * With this set of headers, autoconf is used to probe for individual
 * types, setting variables like HAVE_TYPE_UINT32_T. Here, if one of
 * them is missing, suitable replacements are defined.
 *
 * Code using one of these types, should include this file to get them.
 *
 */

#include <config.h>

#if HAVE_STDINT_H
#include <stdint.h>
#endif

#if HAVE_TYPE_UINT32_T_IN_INTTYPES_H
#include <inttypes.h>
#endif

#if !HAVE_TYPE_UINT32_T
typedef unsigned long uint32_t;
#endif

#endif
