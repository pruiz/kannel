/*
 * protected.c - thread-safe versions of standard library functions
 *
 * Lars Wirzenius
 */

#include <locale.h>

#include "gwlib.h"


/*
 * Undefine the accident protectors.
 */
#undef localtime
#undef gmtime
#undef rand
#undef gethostbyname


enum {
    LOCALTIME,
    GMTIME,
    RAND,
    GETHOSTBYNAME,
    NUM_LOCKS
};


static Mutex locks[NUM_LOCKS];


static void lock(int which)
{
    mutex_lock(&locks[which]);
}


static void unlock(int which)
{
    mutex_unlock(&locks[which]);
}


void gwlib_protected_init(void)
{
    int i;

    for (i = 0; i < NUM_LOCKS; ++i)
        mutex_init_static(&locks[i]);
}


void gwlib_protected_shutdown(void)
{
    int i;

    for (i = 0; i < NUM_LOCKS; ++i)
        mutex_destroy(&locks[i]);
}


struct tm gw_localtime(time_t t)
{
    struct tm tm;

    lock(LOCALTIME);
    tm = *localtime(&t);
    unlock(LOCALTIME);
    return tm;
}


struct tm gw_gmtime(time_t t)
{
    struct tm tm;

    lock(GMTIME);
    tm = *gmtime(&t);
    unlock(GMTIME);
    return tm;
}


int gw_rand(void)
{
    int ret;

    lock(RAND);
    ret = rand();
    unlock(RAND);
    return ret;
}


int gw_gethostbyname(struct hostent *ent, const char *name)
{
    int ret;
    struct hostent *p;

    lock(GETHOSTBYNAME);
    p = gethostbyname(name);
    if (p == NULL)
        ret = -1;
    else {
        ret = 0;
        *ent = *p;
    }
    unlock(GETHOSTBYNAME);
    return ret;
}
