/*
 * bearerbox.h
 *
 * General typedefs and functions for bearerbox
 */

#include "gwlib/gwlib.h"
#include "msg.h"

/* general bearerbox state */

enum {
    BB_RUNNING = 0,
    BB_ISOLATED = 1,	/* do not receive new messgaes from UDP/SMSC */
    BB_SUSPENDED = 2,	/* do not transfer any messages */
    BB_SHUTDOWN = 3,
    BB_DEAD = 4
};


/*---------------------------------------------------------------
 * Module interface to core bearerbox
 *
 * Modules implement one or more of the following interfaces:
 *
 * XXX_start(Config *config) - start the module
 * XXX_restart(Config *config) - restart the module, according to new config
 * XXX_shutdown() - start the avalanche - started from UDP/SMSC
 * XXX_die() - final cleanup
 *
 * XXX_addwdp() - only for SMSC/UDP: add a new WDP message to outgoing system
 */


/*---------------
 * bb_boxc.c (SMS and WAPBOX connections)
 */

int smsbox_start(Config *config);
int smsbox_restart(Config *config);

int wapbox_start(Config *config);

Octstr *boxc_status(void);

/*---------------
 * bb_udp.c (UDP receiver/sender)
 */

int udp_start(Config *config);
/* int udp_restart(Config *config); */
int udp_shutdown(void);
int udp_die(void);	/* called when router dies */

/* add outgoing WDP. If fails, return -1 and msg is untouched, so
 * caller must think of new uses for it */
int udp_addwdp(Msg *msg);



/*---------------
 * bb_smsc.c (SMS Center connections)
 */

int smsc_start(Config *config);
int smsc_restart(Config *config);
int smsc_shutdown(void);
int smsc_die(void);	/* called when router dies */

/* as udp_addwdp() */
int smsc_addwdp(Msg *msg);



/*---------------
 * bb_http.c (HTTP Admin)
 */

int httpadmin_start(Config *config);
/* int http_restart(Config *config); */


/*----------------------------------------------------------------
 * Core bearerbox public functions;
 * used only via HTTP adminstration
 */

int bb_shutdown(void);
int bb_isolate(void);
int bb_suspend(void);
int bb_resume(void);
int bb_restart(void);

/* return string of current status */
Octstr *bb_print_status(void);
