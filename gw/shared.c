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
			 "Hostname %s, IP %s.\n"
			 "Libxml version %s.\n"
                         "Using %s malloc.\n",
			 boxname, VERSION,
			 u.sysname, u.release, u.version, u.machine,
			 octstr_get_cstr(get_official_name()),
			 octstr_get_cstr(get_official_ip()),
			 LIBXML_VERSION_STRING,
                         octstr_get_cstr(gwmem_type()));
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

    pack = msg_pack(pmsg);
    if (conn_write_withlen(bb_conn, pack) == -1)
    	error(0, "Couldn't write Msg to bearerbox.");

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
	if (conn_read_error(bb_conn)) {
	    info(0, "Error reading from bearerbox, disconnecting");
	    return NULL;
	}
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

/*****************************************************************************
 *
 * Functions handling OSI dates
 *
 * Function validates an OSI date. Return unmodified octet string date when it
 * is valid, NULL otherwise.
 */

Octstr *parse_date(Octstr *date)
{
    long date_value;

    if (octstr_get_char(date, 4) != '-')
        goto error;
    if (octstr_get_char(date, 7) != '-')
        goto error;
    if (octstr_get_char(date, 10) != 'T')
        goto error;
    if (octstr_get_char(date, 13) != ':')
        goto error;
    if (octstr_get_char(date, 16) != ':')
        goto error;
    if (octstr_get_char(date, 19) != 'Z')
        goto error;

    if (octstr_parse_long(&date_value, date, 0, 10) < 0)
        goto error;
    if (octstr_parse_long(&date_value, date, 5, 10) < 0)
        goto error;
    if (date_value < 1 || date_value > 12)
        goto error;
    if (octstr_parse_long(&date_value, date, 8, 10) < 0)
        goto error;
    if (date_value < 1 || date_value > 31)
        goto error;
    if (octstr_parse_long(&date_value, date, 11, 10) < 0)
        goto error;
    if (date_value < 0 || date_value > 23)
        goto error;
    if (octstr_parse_long(&date_value, date, 14, 10) < 0)
        goto error;
    if (date_value < 0 || date_value > 59)
        goto error;
    if (date_value < 0 || date_value > 59)
        goto error;
    if (octstr_parse_long(&date_value, date, 17, 10) < 0)
        goto error;

    return date;

error:
    warning(0, "parse_date: not an ISO date");
    return NULL;
}





