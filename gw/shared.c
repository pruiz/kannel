/*
 * shared.c - some utility routines shared by all Kannel boxes
 *
 * Lars Wirzenius
 */

#include <sys/utsname.h>
#include <xmlversion.h>

#include "gwlib/gwlib.h"
#include "shared.h"


enum program_status program_status = starting_up;


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


/***********************************************************************
 * Communication with the bearerbox.
 */


static Connection *bb_conn;


void connect_to_bearerbox(Octstr *host, int port)
{
    bb_conn = conn_open_tcp(host, port);
    if (bb_conn == NULL)
    	panic(0, "Couldn't connect to the bearerbox.");
    info(0, "Connected to bearerbox at %s port %d.",
	 octstr_get_cstr(host), port);
}


void close_connection_to_bearerbox(void)
{
    conn_destroy(bb_conn);
    bb_conn = NULL;
}


void write_to_bearerbox(Msg *pmsg)
{
    Octstr *pack;
    int ret;

    pack = msg_pack(pmsg);
    ret = conn_write_withlen(bb_conn, pack);

    info(0, "Message sent to bearerbox, receiver <%s>",
         octstr_get_cstr(pmsg->sms.receiver));
   
    msg_destroy(pmsg);
    octstr_destroy(pack);
}


Msg *read_from_bearerbox(void)
{
    int ret;
    Octstr *pack;
    Msg *msg;

    pack = NULL;
    while (program_status != shutting_down) {
	pack = conn_read_withlen(bb_conn);
	gw_claim_area(pack);
	if (pack != NULL)
	    break;
	if (conn_eof(bb_conn)) {
	    info(0, "Connection closed by the bearerbox");
	    return NULL;
	}

	ret = conn_wait(bb_conn, -1.0);
	if (ret < 0) {
	    error(0, "Connection to bearerbox broke.");
	    return NULL;
	}
    }
    
    if (pack == NULL)
    	return NULL;

    msg = msg_unpack(pack);
    octstr_destroy(pack);

    if (msg == NULL)
	error(0, "Failed to unpack data!");
    return msg;
}
