/* gw_inet.h - implements ipv6 inet_* for systems that don't have them */

#include "config.h"
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "gwlib/gw_inet.h"
#include "gwlib/gwmem.h"

char *gw_inet_ntop(int af, const void *src, char *dst, size_t size)
{
    const char* ba;  /* ByteAddress - allow us to bytewise address the src.*/
    char* dst_tmp;
    int ret;

    if (src == NULL || dst == NULL || size < 1) {
        errno = ENOSPC;
        return NULL;
    }


    switch (af) {
    case AF_INET:
        /* Normal IPv4 address */
        ba = src;

        dst_tmp = gw_malloc(sizeof(INET_ADDRSTRLEN));
        if (dst_tmp == NULL) {
            errno = ENOSPC;
            return NULL;
        }


        ret = snprintf(dst_tmp, sizeof(INET_ADDRSTRLEN), "%u.%u.%u.%u",
                       ba[0], ba[1], ba[2], ba[3]);

        /* If ret > size then we don't have the space to continue.
         * If ret > sizeof(INET_ADDRSTRLEN) then it is stuffed anyway. */
        if (ret > size || ret > sizeof(INET_ADDRSTRLEN)) {
            errno = ENOSPC;
            return NULL;
        }

        strncpy(dst, dst_tmp, size);
        return dst;

        break;
    default:
        /* We don't know how to handle this type of address */
        errno = EAFNOSUPPORT;
        return NULL;
    }
}
