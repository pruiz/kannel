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


typedef struct _smsc_wrapper {
    SMSCenter	*smsc;
    List	*outgoing_queue;
    long     	receiver_thread;
    long	sender_thread;
} SmscWrapper;



static void wrapper_receiver(void *arg)
{
    Msg 	*msg;
    SMSCConn 	*conn = arg;
    SmscWrapper *wrap = conn->data;
    int 	ret;
    int 	sleep = 100;

#ifdef 0
    
    /* remove messages from SMSC until we are killed */
    while(conn->is_killed = 0) {


        /* XXX not this way list_consume(isolated); /* block here if suspended/isolated */

        ret = smsc_get_message(conn->smsc, &msg);
        if (ret == -1)
            break;

	        if (ret == 1) {
            debug("bb.sms", 0, "smsc: new message received");
            sleep = 100;
        }
        else {
            usleep(sleep);
            /* gradually sleep longer and longer times until something starts to
             * happen - this of course reduces response time, but that's better than
             * extensive CPU usage when it is not used
             */
            sleep *= 2;
            /* Don't let it go over one second, because usleep might not
             * be able to handle that. */
            if (sleep >= 1000000)
                sleep = 999999;
        }
    }    
    list_remove_producer(incoming_sms);
    list_remove_producer(flow_threads);

#endif    
}


static void wrapper_sender(void *arg)
{
    Msg 	*msg;
    SMSCConn 	*conn = arg;
    SmscWrapper *wrap = conn->data;
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

    conn->status = SMSCCONN_ACTIVE;

    if ((wrap->receiver_thread = gwthread_create(wrapper_receiver, conn))==-1)
	goto error;

    if ((wrap->sender_thread = gwthread_create(wrapper_sender, conn))==-1)
	goto error;
    
    return 0;

error:
    error(0, "Failed to create Smsc wrapper");
    if (wrap->smsc != NULL)
	smsc_close(wrap->smsc);
    gw_free(wrap);
    return -1;
}



