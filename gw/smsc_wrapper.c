/*
 * SMSC Connection wrapper
 *
 * Interface to old SMS center implementations
 *
 * Kalle Marjola 2000
 */

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"

#include "smsc.h"
#include "smsc_p.h"


typedef struct smsc_wrapper {
    SMSCenter	*smsc;
    List	*outgoing_queue;
    List	*stopped;	/* list-trick for suspend/isolate */ 
    long     	receiver_thread;
    long	sender_thread;
} SmscWrapper;



static int reconnect(SMSCConn *conn)
{
    SmscWrapper *wrap = conn->data;
    int ret;
    int wait = 1;

    /* disable double-reconnect
     * NOTE: it is still possible that we do oduble-connect if
     *   first thread gets through this if-statement and then
     *   execution switches to another thread.. this can be avoided
     *   via double-mutex system, but I do not feel it is worth it,
     *   maybe later --rpr
     */
    if (conn->status == SMSCCONN_RECONNECTING) {
	mutex_lock(conn->flow_mutex);	/* wait here */
	mutex_unlock(conn->flow_mutex);
	return 0;
    }
    mutex_lock(conn->flow_mutex);

    debug("bb.sms", 0, "smsc_wrapper <%s>: reconnect started",
	  octstr_get_cstr(conn->name));

    conn->status = SMSCCONN_RECONNECTING;
    

    while(conn->why_killed == SMSCCONN_ALIVE) {
	ret = smsc_reopen(wrap->smsc);
	if (ret == 0) {
	    conn->status = SMSCCONN_ACTIVE;
	    conn->connect_time = time(NULL);
	    break;
	}
	else if (ret == -2) {
	    error(0, "Re-open of %s failed permanently",
		  octstr_get_cstr(conn->name));
	    conn->status = SMSCCONN_DISCONNECTED;
	    mutex_unlock(conn->flow_mutex);
	    return -1;	/* permanent failure */
	}
	else {
	    error(0, "Re-open to <%s> failed, retrying after %d minutes...",
		  octstr_get_cstr(conn->name), wait);
	    gwthread_sleep(wait*60.0);

	    wait = wait > 10 ? 10 : wait * 2 + 1;
	}
    }
    mutex_unlock(conn->flow_mutex);
    return 0;
}


static Msg *sms_receive(SMSCConn *conn)
{
    SmscWrapper *wrap = conn->data;
    int ret;
    Msg *newmsg = NULL;

    if (smscenter_pending_smsmessage(wrap->smsc) == 1) {

        ret = smscenter_receive_msg(wrap->smsc, &newmsg);
        if (ret == 1) {

            /* if any smsc_id available, use it */
            newmsg->sms.smsc_id = octstr_duplicate(conn->id);

	    return newmsg;
        } else if (ret == 0) { /* "NEVER" happens */
            warning(0, "SMSC %s: Pending message returned '1', "
                    "but nothing to receive!", octstr_get_cstr(conn->name));
            msg_destroy(newmsg);
            return NULL;
        } else {
            msg_destroy(newmsg);
	    if (reconnect(conn) == -1)
		smscconn_shutdown(conn, 0);
	    return NULL;
        }
    }
    return NULL;
}


static void wrapper_receiver(void *arg)
{
    Msg 	*msg;
    SMSCConn 	*conn = arg;
    SmscWrapper *wrap = conn->data;
    /* SmscWrapper *wrap = conn->data; ** non-used */
    double 	sleep = 0.0001;
    
    
    /* remove messages from SMSC until we are killed */
    while(conn->why_killed == SMSCCONN_ALIVE) {

        list_consume(wrap->stopped); /* block here if suspended/isolated */

	msg = sms_receive(conn);
	if (msg) {
            debug("bb.sms", 0, "smscconn (%s): new message received",
		  octstr_get_cstr(conn->name));
            sleep = 0.0001;
	    counter_increase(conn->received);
	    bb_smscconn_receive(conn, msg);
        }
        else {
	    /* note that this implementations means that we sleep even
	     * when we fail connection.. but time is very short, anyway
	     */
            gwthread_sleep(sleep);
            /* gradually sleep longer and longer times until something starts to
             * happen - this of course reduces response time, but that's better than
             * extensive CPU usage when it is not used
             */
            sleep *= 2;
            if (sleep >= 2.0)
                sleep = 1.999999;
        }
    }
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;

    /* this thread is joined at sender */
}



static int sms_send(SMSCConn *conn, Msg *msg)
{
    SmscWrapper *wrap = conn->data;
    int ret;

    debug("bb.sms", 0, "smscconn_sender (%s): sending message",
	  octstr_get_cstr(conn->name));
        
    ret = smscenter_submit_msg(wrap->smsc, msg);
    if (ret == -1) {
	counter_increase(conn->failed);
	bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_REJECTED);

	if (reconnect(conn) == -1)
	    smscconn_shutdown(conn, 0);
        return -1;
    } else {
	counter_increase(conn->sent);
	bb_smscconn_sent(conn, msg);
        return 0;
    }
}


static void wrapper_sender(void *arg)
{
    Msg 	*msg;
    SMSCConn 	*conn = arg;
    SmscWrapper *wrap = conn->data;

    /* send messages to SMSC until our putgoing_list is empty and
     * no producer anymore (we are set to shutdown) */
    while(conn->status != SMSCCONN_DEAD) {

	if ((msg = list_consume(wrap->outgoing_queue)) == NULL)
            break;

        if (octstr_search_char(msg->sms.receiver, ' ', 0) != -1) {
            /*
             * multi-send: this should be implemented in corresponding
             *  SMSC protocol, but while we are waiting for that...
             */
            int i;
	    Msg *newmsg;
            /* split from spaces: in future, split with something more sensible,
             * this is dangerous... (as space is url-encoded as '+')
             */
            List *nlist = octstr_split_words(msg->sms.receiver);

            for(i=0; i < list_len(nlist); i++) {

		newmsg = msg_duplicate(msg);
                octstr_destroy(newmsg->sms.receiver);

                newmsg->sms.receiver = list_get(nlist, i);
                sms_send(conn, newmsg);
            }
            list_destroy(nlist, NULL);
            msg_destroy(msg);
        }
        else
	    sms_send(conn,msg);

    }
    /* cleanup, we are now dying */

    debug("bb.sms", 0, "SMSCConn %s sender died, waiting for receiver",
	  octstr_get_cstr(conn->name));
    
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;

    if (conn->is_stopped) {
	list_remove_producer(wrap->stopped);
	conn->is_stopped = 0;
    }

    gwthread_wakeup(wrap->receiver_thread);
    gwthread_join(wrap->receiver_thread);

    /* call 'failed' to all messages still in queue */
    
    mutex_lock(conn->flow_mutex);

    conn->status = SMSCCONN_DEAD;

    while((msg = list_extract_first(wrap->outgoing_queue))!=NULL) {
	bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN);
    }
    list_destroy(wrap->outgoing_queue, NULL);
    list_destroy(wrap->stopped, NULL);
    smsc_close(wrap->smsc);
    gw_free(wrap);
    conn->data = NULL;
    
    mutex_unlock(conn->flow_mutex);

    bb_smscconn_killed();
}



static int wrapper_add_msg(SMSCConn *conn, Msg *sms)
{
    SmscWrapper *wrap = conn->data;
    Msg *copy;

    mutex_lock(conn->flow_mutex);

    copy = msg_duplicate(sms);
    list_produce(wrap->outgoing_queue, copy);

    mutex_unlock(conn->flow_mutex);
    
    return 0;
}


static int wrapper_shutdown(SMSCConn *conn, int finish_sending)
{
    SmscWrapper *wrap = conn->data;

    debug("bb.sms", 0, "Shutting down SMSCConn %s, %s",
	  octstr_get_cstr(conn->name), finish_sending ? "slow" : "instant");
    
    if (finish_sending == 0) {
	Msg *msg; 
	while((msg = list_extract_first(wrap->outgoing_queue))!=NULL) {
	    bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_SHUTDOWN);
	}
    }
    list_remove_producer(wrap->outgoing_queue);
    gwthread_wakeup(wrap->sender_thread);
    gwthread_wakeup(wrap->receiver_thread);
    return 0;
}

static void wrapper_stop(SMSCConn *conn)
{
    SmscWrapper *wrap = conn->data;

    list_add_producer(wrap->stopped);

}

static void wrapper_start(SMSCConn *conn)
{
    SmscWrapper *wrap = conn->data;

    list_remove_producer(wrap->stopped);
}


static long wrapper_queued(SMSCConn *conn)
{
    SmscWrapper *wrap = conn->data;
    long ret = list_len(wrap->outgoing_queue);

    /* use internal queue as load, maybe something else later */
    
    conn->load = ret;
    return ret;
}

int smsc_wrapper_create(SMSCConn *conn, ConfigGroup *cfg)
{
    /* 1. Call smsc_open()
     * 2. create sender/receiver threads
     * 3. fill up the conn
     *
     * XXX open() SHOULD be done in distinct thread, not here!
     */

    SmscWrapper *wrap;

    wrap = gw_malloc(sizeof(SmscWrapper));
    wrap->smsc = NULL;
    conn->data = wrap;
    conn->send_msg = wrapper_add_msg;
    
    if ((wrap->smsc = smsc_open(cfg)) == NULL)
	goto error;

    conn->name = octstr_create(smsc_name(wrap->smsc));
    
    wrap->outgoing_queue = list_create();
    wrap->stopped = list_create();
    list_add_producer(wrap->outgoing_queue);
    
    conn->status = SMSCCONN_ACTIVE;
    conn->connect_time = time(NULL);

    /* XXX here we could fail things... especially if the second one
     *     fails.. so fix this ASAP
     *
     * moreover, open should be in sender/receiver, so that we can continue
     * while tyring to open... maybe move this, or just wait for new
     * implementations of various SMSC protocols
     */
    
    if ((wrap->receiver_thread = gwthread_create(wrapper_receiver, conn))==-1)
	goto error;

    if ((wrap->sender_thread = gwthread_create(wrapper_sender, conn))==-1)
	goto error;

    conn->shutdown = wrapper_shutdown;
    conn->queued = wrapper_queued;
    conn->stop_conn = wrapper_stop;
    conn->start_conn = wrapper_start;
    
    return 0;

error:
    error(0, "Failed to create Smsc wrapper");
    if (wrap->smsc != NULL)
	smsc_close(wrap->smsc);
    gw_free(wrap);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    return -1;
}



