/*
 * SMSC Connection interface to bearerbox - callback functions
 *
 * These functions are called by SMSC Connection functions
 * when they have finished their tasks
 *
 * Kalle Marjola 2000 for project Kannel
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>


#include "gwlib/gwlib.h"
#include "msg.h"
#include "bearerbox.h"

#include "bb_smscconn_cb.h"

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
extern List *incoming_wdp;
extern List *incoming_sms;
extern List *outgoing_sms;

extern Counter *incoming_sms_counter;
extern Counter *outgoing_sms_counter;

extern List *flow_threads;




void bb_smscconn_ready(SMSCConn *conn)
{
    list_add_producer(flow_threads);
    list_add_producer(incoming_sms);
}


void bb_smscconn_killed(int reason)
{
    list_remove_producer(incoming_sms);
    list_remove_producer(flow_threads);
}


void bb_smscconn_sent(SMSCConn *conn, Msg *sms)
{
    counter_increase(outgoing_sms_counter);
    msg_destroy(sms);
}


void bb_smscconn_send_failed(SMSCConn *conn, Msg *sms, int reason)
{
    switch (reason) {

    case SMSCCONN_FAILED_SHUTDOWN:
	list_produce(outgoing_sms, sms);
	break;
    default:
	/* XXX yell etc. here */
	msg_destroy(sms);
    }
}    


int bb_smscconn_receive(SMSCConn *conn, Msg *sms)
{
#if 0
    normalize_number(unified_prefix, &(sms->sms.sender));
    if (white_list &&
	numhash_find_number(white_list, sms->sms.sender) < 1) {
	info(0, "Number <%s> is not in white-list, message discarded",
	     octstr_get_cstr(sms->sms.sender));
	msg_destroy(sms);
        return -1;
    }
    if (black_list &&
	numhash_find_number(black_list, sms->sms.sender) == 1) {
	info(0, "Number <%s> is in black-list, message discarded",
	     octstr_get_cstr(sms->sms.sender));
	msg_destroy(sms);
	return -1;
    }

    alog("Received a message - SMSC:%s sender:%s msg: '%s'",
	 smscconn_name(conn),
	 octstr_get_cstr(sms->sms.sender),
	 octstr_get_cstr(sms->sms.msgdata));
#endif    

    list_produce(incoming_sms, sms);
    counter_increase(incoming_sms_counter);

    return 0;
}

