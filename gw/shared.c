/*
 * shared.c - some utility routines shared by all Kannel boxes
 *
 * Lars Wirzenius
 */

#include <sys/utsname.h>
#include <xmlversion.h>

#include "gwlib/gwlib.h"

#include "shared.h"

void report_versions(const char *boxname)
{
    struct utsname u;

    uname(&u);

    debug("gwlib.gwlib", 0, "Kannel %s, version `%s' starting up.", 
    	  boxname, VERSION);
    debug("gwlib.gwlib", 0, "System: %s, release %s, version %s, machine %s", 
    	  u.sysname, u.release, u.version, u.machine);
#ifdef LIBXML_VERSION_STRING
    debug("gwlib.gwlib", 0, "Libxml version %s", LIBXML_VERSION_STRING);
#endif
}
