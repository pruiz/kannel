/*
 * protected.c - thread-safe versions of standard library functions
 *
 * Lars Wirzenius
 */

#include <locale.h>
#include <errno.h>

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

#ifndef HAVE_LOCALTIME_R
    lock(LOCALTIME);
    tm = *localtime(&t);
    unlock(LOCALTIME);
#else
    localtime_r(&t, &tm);
#endif

    return tm;
}


struct tm gw_gmtime(time_t t)
{
    struct tm tm;

#ifndef HAVE_GMTIME_R
    lock(GMTIME);
    tm = *gmtime(&t);
    unlock(GMTIME);
#else
    gmtime_r(&t, &tm);
#endif

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

#if HAVE_FUNC_GETHOSTBYNAME_R_6
/* linux version */
int gw_gethostbyname(struct hostent *ent, const char *name, char **buff)
{
    struct hostent *tmphp, hp;
    int herr, res;
    size_t bufflen;

    tmphp = NULL; /* for compiler please */

    bufflen = 1024;
    *buff = (char*) gw_malloc(bufflen);
    while ((res=gethostbyname_r(name, &hp,*buff, bufflen, &tmphp, &herr)) == ERANGE) {
        /* enlarge the buffer */
	bufflen *= 2;
	*buff = (char*) gw_realloc(*buff, bufflen);
    }

    if (res != 0 || tmphp == NULL) {
        error(herr, "Error while gw_gethostbyname occurs.");
        res = -1;
    }
    else {
        *ent = hp;
    }

    return res;
}
#elif HAVE_FUNC_GETHOSTBYNAME_R_5
/* solaris */
int gw_gethostbyname(struct hostent *ent, const char *name, char **buff)
{
    int herr = 0;
    size_t bufflen = 1024;
    int res = 0;
    struct hostent *tmphp = NULL;

    *buff = gw_malloc(bufflen);

    while ((tmphp = gethostbyname_r(name, ent, *buff, bufflen,&herr)) == NULL && (errno == ERANGE)) {
        /* Enlarge the buffer. */
        bufflen *= 2;
        *buff = (char *) gw_realloc(*buff, bufflen);
    }

    if (tmphp == NULL || herr != 0) {
        error(herr, "Error while gw_gethostbyname occurs.");
        res = -1;
    }

    return res;
}
/* not yet implemented, no machine for testing (alex) */
/* #elif HAVE_FUNC_GETHOSTBYNAME_R_3 */
#else
/*
 * Hmm, we doesn't have a gethostbyname_r , it's bad...
 * Here we must perform a "deep-copy" of a hostent struct returned
 * from gethostbyname.
 * Note: bellow code based on curl.
 */
int gw_gethostbyname(struct hostent *ent, const char *name, char **buff)
{
    int len, i;
    struct hostent *p;
    /* Allocate enough memory to hold the full name information structs and
     * everything. OSF1 is known to require at least 8872 bytes. The buffer
     * required for storing all possible aliases and IP numbers is according to
     * Stevens' Unix Network Programming 2nd editor, p. 304: 8192 bytes!
     */
    size_t bufflen = 9000;
    char *bufptr, *str;

    lock(GETHOSTBYNAME);

    p = gethostbyname(name);
    if (p == NULL) {
        unlock(GETHOSTBYNAME);
        *buff = NULL;
        return -1;
    }

    *ent = *p;
    /* alloc mem */
    bufptr = *buff = gw_malloc(bufflen);
    ent->h_name = bufptr;
    /* copy h_name into buff */
    len = strlen(p->h_name) + 1;
    strncpy(bufptr, p->h_name, len);
    bufptr += len;

  /* we align on even 64bit boundaries for safety */
#define MEMALIGN(x) ((x)+(8-(((unsigned long)(x))&0x7)))

    /* This must be aligned properly to work on many CPU architectures! */
    bufptr = MEMALIGN(bufptr);

    ent->h_aliases = (char**)bufptr;

    /* Figure out how many aliases there are */
    for (i = 0; p->h_aliases[i] != NULL; ++i)
        ;

    /* Reserve room for the array */
    bufptr += (i + 1) * sizeof(char*);

    /* Clone all known aliases */
    for(i = 0; (str = p->h_aliases[i]); i++) {
        len = strlen(str) + 1;
        strncpy(bufptr, str, len);
        ent->h_aliases[i] = bufptr;
        bufptr += len;
    }
    /* Terminate the alias list with a NULL */
    ent->h_aliases[i] = NULL;

    ent->h_addrtype = p->h_addrtype;
    ent->h_length = p->h_length;

    /* align it for (at least) 32bit accesses */
    bufptr = MEMALIGN(bufptr);

    ent->h_addr_list = (char**)bufptr;

    /* Figure out how many addresses there are */
    for (i = 0; p->h_addr_list[i] != NULL; ++i)
        ;

    /* Reserve room for the array */
    bufptr += (i + 1) * sizeof(char*);

    i = 0;
    len = p->h_length;
    str = p->h_addr_list[i];
    while (str != NULL) {
        memcpy(bufptr, str, len);
        ent->h_addr_list[i] = bufptr;
        bufptr += len;
        str = p->h_addr_list[++i];
    }
    ent->h_addr_list[i] = NULL;

#undef MEMALIGN

    unlock(GETHOSTBYNAME);

    return 0;
}
#endif
