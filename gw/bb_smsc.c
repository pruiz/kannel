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
#include "new_bb.h"
#include "smsc.h"


/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
extern List *incoming_wdp;
extern List *incoming_sms;
extern List *outgoing_sms;

/* our own thingies */

static volatile sig_atomic_t smsc_running;
static List *smsc_list;


typedef struct _smsc {
    List 	*outgoing_list;
    pthread_t 	receiver;
    SMSCenter 	*smsc;
} Smsc;


/*---------------------------------------------
 * receiver thingies
 */
static void *sms_receiver(void *arg)
{
    Msg *msg;
    Smsc *conn = arg;
    int ret;

    list_add_producer(incoming_sms);

    /* remove messages from SMSC until it is closed */
    while(bb_status != BB_DEAD && bb_status != BB_SHUTDOWN) {

	// if (bb_status == bb_suspended)
        // wait_for_status_change(&bb_status, bb_suspended);

	// XXX mutexes etc are needed, I think?
/*
 * XXX have to think about the new SMSCenter interface...
 *  
 *	if (smscenter_pending_smsmessage(conn->smsc) == 1) {
 *	    ret = smscenter_receive_msg(conn->smsc, new);
 *
 *	     if (ret == 1)
 *		list_produce(incoming_sms, msg);
 *	    else
 *		warning(0, "problems with smscenters... FIX FIX");
 *	} else
 *	    sleep(1);
 */
    }    
    list_remove_producer(incoming_sms);
    return NULL;
}


/*---------------------------------------------
 * sender thingies
 */
static void *sms_sender(void *arg)
{
    Msg *msg;
    Smsc *conn = arg;
    int ret;
    
    while(bb_status != BB_DEAD) {

	if ((msg = list_consume(conn->outgoing_list)) == NULL)
	    break;

	/* XXX note that last argument! */
	/* and the second argument */

	// ret = smsc_send_message(conn->smsc, msg, NULL);
	
	// msg_destroy(msg); implicit destroy?
    }
    if (pthread_join(conn->receiver, NULL) != 0)
	error(0, "Join failed in sms_sender");

    list_destroy(conn->outgoing_list);
    smsc_close(conn->smsc);

    gw_free(conn);
    return NULL;
}



/* function to route outgoing SMS'es,
 * use some nice magics to route them to proper SMSC
 */
static void *sms_router(void *arg)
{
    Msg *msg;
    Smsc *si;
    int i;

    while(bb_status != BB_DEAD) {

	if ((msg = list_consume(outgoing_sms)) == NULL)
	    break;

	gw_assert(msg_type(msg) == smart_sms);
	
	list_lock(smsc_list);
	/* select in which list to add this */

	for (i=0; i < list_len(smsc_list); i++) {
	    si = list_get(smsc_list, i);

	    /* here we _should_ have some good routing system */
	    {
		list_produce(si->outgoing_list, msg);
		break;
	    }
	}
	list_unlock(smsc_list);
    }
    smsc_die();

    return NULL;
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
    
    if ((int)(si->receiver = start_thread(0, sms_receiver, si, 0)) == -1)
	goto error;

    if ((int)start_thread(0, sms_sender, si, 0) == -1)
	goto error;

    return si;

error:
    /* XXX should be own sms_destroy -thingy? */
    
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
    
    if (smsc_running) return -1;

    smsc_list = list_create();
    
    grp = config_find_first_group(config, "group", "smsc");
    while(grp != NULL) {
	si = create_new_smsc(grp);
	if (si == NULL)
	    panic(0, "Cannot start with SMSC connection failing");
	
	list_append(smsc_list, si);
	grp = config_find_next_group(grp, "group", "smsc");
    }
    if ((int)(start_thread(0, sms_router, NULL, 0)) == -1)
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
    
    if (!smsc_running) return -1;

    /*
     * remove producers from all outgoing lists.
     */

    debug("bb.sms", 0, "smsc_die: removing producers from smsc-lists");
    
    while((si = list_consume(smsc_list)) != NULL) {
	list_remove_producer(si->outgoing_list);
    }
    list_destroy(smsc_list);
    
    smsc_running = 0;
    return 0;
}


