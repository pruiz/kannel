/*
 * bb_smsc.c: SMSC wrapper
 *
 * handles start/restart/shutdown/suspend/die operations of the
 * SMS center connections
 *
 * Kalle Marjola <rpr@wapit.com> 2000 for project Kannel
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
#include "smsc.h"
#include "numhash.h"

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
extern List *incoming_wdp;
extern List *incoming_sms;
extern List *outgoing_sms;

extern Counter *incoming_sms_counter;
extern Counter *outgoing_sms_counter;

extern List *flow_threads;
extern List *suspended;
extern List *isolated;

/* our own thingies */

static volatile sig_atomic_t smsc_running;
static List *smsc_list;
static char *unified_prefix;

static Numhash *black_list;
static Numhash *white_list;

typedef struct _smsc {
    List 	*outgoing_list;
    long	receiver;
    SMSCenter 	*smsc;
} Smsc;


/*---------------------------------------------
 * receiver thingies
 */
static void sms_receiver(void *arg)
{
    Msg *msg;
    Smsc *conn = arg;
    int ret;

    debug("bb.thread", 0, "START: sms_receiver");
    list_add_producer(flow_threads);
    list_add_producer(incoming_sms);

    /* remove messages from SMSC until it is closed */
    while(bb_status != BB_DEAD && bb_status != BB_SHUTDOWN) {

	list_consume(isolated);	/* block here if suspended/isolated */

	ret = smsc_get_message(conn->smsc, &msg);
	if (ret == -1)
	    break;

	if (ret == 1) {
	    /* XXX do we want to normalize receiver? it is like
	     *     1234 anyway... */

	    normalize_number(unified_prefix, &(msg->smart_sms.sender));
	    if (white_list &&
		numhash_find_number(white_list, msg->smart_sms.sender) < 1)
	    {
		info(0, "Number <%s> is not in white-list, message discarded",
		     octstr_get_cstr(msg->smart_sms.sender));
		msg_destroy(msg);
		continue;
	    }
	    if (black_list &&
		numhash_find_number(black_list, msg->smart_sms.sender) == 1)
	    {
		info(0, "Number <%s> is in black-list, message discarded",
		     octstr_get_cstr(msg->smart_sms.sender));
		msg_destroy(msg);
		continue;
	    }
	    list_produce(incoming_sms, msg);

	    counter_increase(incoming_sms_counter);
	    debug("bb.sms", 0, "smsc: new message received");
	}
	else
	    sleep(1);
    }    
    list_remove_producer(incoming_sms);
    debug("bb.thread", 0, "EXIT: sms_receiver");
    list_remove_producer(flow_threads);
}


/*---------------------------------------------
 * sender thingies
 */
static void sms_sender(void *arg)
{
    Msg *msg;
    Smsc *conn = arg;
    int ret;

    debug("bb.thread", 0, "START: sms_sender");
    list_add_producer(flow_threads);
    
    while(bb_status != BB_DEAD) {

	list_consume(suspended);	/* block here if suspended */

	if ((msg = list_consume(conn->outgoing_list)) == NULL)
	    break;

	debug("bb.sms", 0, "sms_sender: sending message");
	
	ret = smsc_send_message(conn->smsc, msg);
	if (ret == -1) {
	    /* XXX: do not discard! */
	    error(0, "sms_sender: failed, message discarded for now");
	    msg_destroy(msg);
	} else {
	    /* send_message destroys both the tmp and msg */
	    counter_increase(outgoing_sms_counter);
	}
    }
    list_lock(smsc_list);
    list_delete_equal(smsc_list, conn);
    list_unlock(smsc_list);

    if (list_len(smsc_list) == 0)
	list_destroy(smsc_list);

    debug("bb", 0, "sms_sender: done, waiting in join");
    
    gwthread_join(conn->receiver);

    list_destroy(conn->outgoing_list);
    smsc_close(conn->smsc);

    gw_free(conn);
    debug("bb.thread", 0, "EXIT: sms_sender");
    list_remove_producer(flow_threads);
}



/* function to route outgoing SMS'es,
 * use some nice magics to route them to proper SMSC
 */
static void sms_router(void *arg)
{
    Msg *msg;
    Smsc *si, *backup;
    char *number;
    int i, s;

    debug("bb.thread", 0, "START: sms_router");
    list_add_producer(flow_threads);

    while(bb_status != BB_DEAD) {

	if ((msg = list_consume(outgoing_sms)) == NULL)
	    break;

	gw_assert(msg_type(msg) == smart_sms);

	if (list_len(smsc_list) == 0) {
	    warning(0, "No SMSCes to receive message, discarding it!");
	    msg_destroy(msg);
	    continue;
	}
	/* XXX we normalize the receiver - if set, but do we want
	 *     to normalize the sender, too?
	 */
	normalize_number(unified_prefix, &(msg->smart_sms.receiver));
	    
	/* select in which list to add this
	 * start - from random SMSC, as they are all 'equal'
	 */

	list_lock(smsc_list);

	s = rand() % list_len(smsc_list);
	number = octstr_get_cstr(msg->smart_sms.receiver);
	backup = NULL;
	
	for (i=0; i < list_len(smsc_list); i++) {
	    si = list_get(smsc_list,  (i+s) % list_len(smsc_list));

	    if (smsc_denied(si->smsc, number)==1)
		continue;

	    if (smsc_preferred(si->smsc, number)==1) {
		debug("bb", 0, "sms_router: adding message to preferred <%s>",
		      smsc_name(si->smsc));
		list_produce(si->outgoing_list, msg);
		goto found;
	    }
	    if (backup == NULL)
		backup = si;
	}
	if (backup) {
	    debug("bb", 0, "sms_router: adding message to <%s>",
		  smsc_name(backup->smsc));
	    list_produce(backup->outgoing_list, msg);
	}
	else {
	    warning(0, "Cannot find SMSC for message to <%s>, discarded.", number);
	    msg_destroy(msg);
	}
    found:
	list_unlock(smsc_list);
    }
    smsc_die();

    debug("bb.thread", 0, "EXIT: sms_router");
    list_remove_producer(flow_threads);
}



static Smsc *create_new_smsc(ConfigGroup *grp)
{
    Smsc *si;

    si = gw_malloc(sizeof(Smsc));
    
    if ((si->smsc = smsc_open(grp)) == NULL) {
	gw_free(si);
	return NULL;
    }
    si->outgoing_list = list_create();
    list_add_producer(si->outgoing_list);
    
    si->receiver = gwthread_create(sms_receiver, si);
    if (si->receiver == -1)
	goto error;

    if (gwthread_create(sms_sender, si) == -1)
	goto error;

    return si;

error:
    /* XXX should be own sms_destroy -thingy? */

    error(0, "Failed to start a new SMSC thingy");
    
    list_destroy(si->outgoing_list);
    smsc_close(si->smsc);
    gw_free(si);
    return NULL;
}



/*-------------------------------------------------------------
 * public functions
 *
 */

int smsc_start(Config *config)
{
    ConfigGroup *grp;
    Smsc *si;
    char *ls;
    
    if (smsc_running) return -1;

    smsc_list = list_create();

    grp = config_find_first_group(config, "group", "core");
    unified_prefix = config_get(grp, "unified-prefix");

    white_list = black_list = NULL;
    if ((ls = config_get(grp, "white-list")) != NULL)
	white_list = numhash_create(ls);
    if ((ls = config_get(grp, "black-list")) != NULL)
	black_list = numhash_create(ls);

    grp = config_find_first_group(config, "group", "smsc");
    while(grp != NULL) {
	si = create_new_smsc(grp);
	if (si == NULL)
	    panic(0, "Cannot start with SMSC connection failing");
	
	list_append(smsc_list, si);
	grp = config_find_next_group(grp, "group", "smsc");
    }
    if (gwthread_create(sms_router, NULL) == -1)
	panic(0, "Failed to start a new thread for SMS routing");
    
    list_add_producer(incoming_sms);
    list_add_producer(incoming_wdp);
    smsc_running = 1;
    return 0;
}


/*
 * this function receives an WDP message, and puts into
 * WDP disassembly unit list... in the future!
 */
int smsc_addwdp(Msg *msg)
{
    if (!smsc_running) return -1;
    
    return -1;
}

int smsc_shutdown(void)
{
    if (!smsc_running) return -1;

    /* start avalanche by removing producers from lists */

    /* XXX shouldn'w we be sure that all smsces have closed their
     * receive thingies? Is this guaranteed by setting bb_status
     * to shutdown before calling these?
     */
    list_remove_producer(incoming_sms);
    list_remove_producer(incoming_wdp);
    return 0;
}


int smsc_die(void)
{
    Smsc *si;
    int i;
    
    if (!smsc_running) return -1;

    /*
     * remove producers from all outgoing lists.
     */

    debug("bb.sms", 0, "smsc_die: removing producers from smsc-lists");
    
    list_lock(smsc_list);
    
    for(i=0; i < list_len(smsc_list); i++) {
	si = list_get(smsc_list, i);
	list_remove_producer(si->outgoing_list);
    }
    list_unlock(smsc_list);

    /* XXX hopefully these are not used at this stage; at least they
     *    should NOT be used, receivers should have exited already
     */
    numhash_destroy(white_list);
    numhash_destroy(black_list);
    
    smsc_running = 0;
    return 0;
}

