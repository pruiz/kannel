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

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;
extern List *incoming_wdp;
extern List *incoming_sms;
extern List *outgoing_sms;

/* our own thingies */

static volatile sig_atomic_t smsc_running;
static List *smsc_list;


typedef struct _smsc {
    List *outgoing_list;
    pthread_t receiver;
} Smsc;




/*-------------------------------------------------------------
 * public functions
 *
 */

int smsc_start(Config *config)
{
    if (smsc_running) return -1;

    smsc_running = 1;
    return 0;
}


/*
 * this function receives an WDP message and adds it to
 * corresponding outgoing_list.
 */
int smsc_addwdp(Msg *msg)
{
    if (!smsc_running) return -1;
    
    return -1;
}

int smsc_shutdown(void)
{
    list_remove_producer(incoming_sms);
    list_remove_producer(incoming_wdp);
    return 0;
}


int smsc_die(void)
{
    if (!smsc_running) return -1;
    
    smsc_running = 0;
    return 0;
}


