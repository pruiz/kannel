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
    Octstr *os;
    
    os = version_report_string(boxname);
    debug("gwlib.gwlib", 0, "%s", octstr_get_cstr(os));
    octstr_destroy(os);
}


Octstr *version_report_string(const char *boxname)
{
    struct utsname u;

    uname(&u);
    return octstr_format("Kannel %s version `%s'.\n"
    	    	    	 "System %s, release %s, version %s, machine %s.\n"
			 "Libxml version %s.\n",
			 boxname, VERSION, u.sysname, u.release, u.version, 
			 u.machine, LIBXML_VERSION_STRING);
}
