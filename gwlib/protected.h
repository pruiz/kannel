/*
 * protected.h - thread-safe versions of standard library functions
 *
 * The standard (or commonly available) C library functions are not always
 * thread-safe, or re-entrant. This module provides wrappers. The wrappers
 * are not always the most efficient, but the interface is meant to 
 * allow a more efficient version be implemented later.
 *
 * Lars Wirzenius
 */

#ifndef PROTECTED_H
#define PROTECTED_H

#include <netdb.h>
#include <time.h>

void gwlib_protected_init(void);
void gwlib_protected_shutdown(void);
struct tm gw_localtime(time_t t);
struct tm gw_gmtime(time_t t);
int gw_rand(void);
int gw_gethostbyname(struct hostent *ret, const char *name);

/*
 * Make it harder to use these by mistake.
 */
#define localtime(t) do_not_use_localtime_directly
#define gmtime(t) do_not_use_gmtime_directly
#define rand() do_not_use_rand_directly
#define gethostbyname() do_not_use_gethostbyname_directly


#endif
