/* gw_inet.h - define ipv6 inet_* for systems that don't have them */


#include "gwlib/gwlib.h"
#include "config.h"

#ifndef GW_INET_H
#define GW_INET_H
/* Since we are completing the header rather than supplying it in
 * its entirety, include it and work from there. 
 */
#include <arpa/inet.h>

/* If we don't have suppert for IPv6 */
#ifndef INET6_ADDRSTRLEN


/* Don't try to feed these functions IPv6, just stick to IPv4 */
#define GW_DONT_USE_IPV6

#define  INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46 /* You don't get to use this, but define it
* so compilers won't barf.                  */


#define inet_ntop(af, src, dst, size) gw_inet_ntop(af, src, dst, size)

/* Convert address from network format to presentation format,
* implement as gw_inet_ntop in case the system actually does provide
* an inet_ntop.  */
char *gw_inet_ntop(int af, const void *src, char *dst, size_t size);

#endif  /* INET6_ADDRSTRLEN */


#endif


