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

#include <time.h>

void gwlib_protected_init(void);
void gwlib_protected_shutdown(void);
struct tm gw_localtime(time_t t);
struct tm gw_gmtime(time_t t);
int gw_rand(void);

#endif
