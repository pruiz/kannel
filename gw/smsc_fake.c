/*
 * smsc_fake.c - implement interface to fakesmsc.c
 *
 * Lars Wirzenius for WapIT Ltd.
 */


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "gwlib/gwlib.h"
#include "smsc.h"
#include "smsc_p.h"

/* This smsc stores the last-read line in smsc->private as an Octstr. */

int fake_open(SMSCenter *smsc, ConfigGroup *grp)
{
    gw_assert(smsc->type == SMSC_TYPE_FAKE);

    if (smsc->hostname == NULL) {
	error(0, "smsc_fake config: 'host' field missing.");
	return -1;
    }

    if (smsc->port == NULL) {
	error(0, "smsc_fake config: 'port' field missing.");
	return -1;
    }

    smsc->conn = conn_open_tcp(smsc->hostname, smsc->port);
    if (smsc->conn == NULL)
	return -1;

    sprintf(smsc->name, "FAKE:%s:%d", smsc->hostname, smsc->port);
    info(0, "Fake open successfully done");

    return 0;
}


int fake_reopen(SMSCenter *smsc)
{
    if (smsc->conn == NULL) {
        info(0, "trying to close already closed fake, ignoring");
    } else {
        conn_destroy(smsc->conn);
        smsc->conn = conn_open_tcp(smsc->hostname, smsc->port);
        if (smsc->conn == NULL)
  	    return -1;
    }

    return 0;
}


int fake_close(SMSCenter *smsc)
{
    if (smsc->conn == NULL) {
        info(0, "trying to close already closed fake, ignoring");
        return 0;
    }
    conn_destroy(smsc->conn);
    smsc->conn = NULL;
    return 0;
}


int fake_pending_smsmessage(SMSCenter *smsc)
{
    if (smsc->conn == NULL)
	return 0;

    if (smsc->private != NULL)
	return 1;

    smsc->private = conn_read_line(smsc->conn);

    if (smsc->private != NULL)
	return 1;

    if (conn_eof(smsc->conn))
	return 1;  /* next call to receive will signal EOF */

    return 0;
}


int fake_submit_msg(SMSCenter *smsc, Msg *msg)
{
    if (msg_type(msg) == smart_sms) {
	Octstr *line;
	int ret;

	line = octstr_format("%S %S %S\n",
		msg->smart_sms.sender,
		msg->smart_sms.receiver,
		msg->smart_sms.msgdata);
	ret = conn_write(smsc->conn, line);
	octstr_destroy(line);
	if (ret < 0)
	    return -1;
    }
    return 0;
}


int fake_receive_msg(SMSCenter *smsc, Msg **msg)
{
    Octstr *sender, *receiver, *text;
    Octstr *line;
    long pos1, pos2;

    if (smsc->private == NULL)
	return 0;
    line = smsc->private;

    pos1 = octstr_search_char(line, ' ', 0);
    if (pos1 < 0)
	pos1 = pos2 = octstr_len(line);
    else {
	pos2 = octstr_search_char(line, ' ', pos1 + 1);
	if (pos2 < 0)
		pos2 = octstr_len(line);
    }

   *msg = msg_create(smart_sms);
   (*msg)->smart_sms.sender = octstr_copy(line, 0, pos1);
   (*msg)->smart_sms.receiver = octstr_copy(line, pos1 + 1, pos2 - pos1 - 1);
   (*msg)->smart_sms.text = octstr_copy(line, pos2 + 1, octstr_len(line));

    octstr_destroy(line);
    return 1;
}
