/*
 * bearerbox.h
 *
 * General typedefs and functions for bearerbox
 */

#include "gwlib/gwlib.h"
#include "msg.h"

/* general bearerbox state */

enum {
    bb_running = 0,
    bb_suspended = 1,
    bb_shutdown = 2,
    bb_dead = 3
};


/*---------------
 * in bb_boxc.c
 */

int smsbox_start(Config *config);
int smsbox_restart(Config *config);

int wapbox_start(Config *config);

/*---------------
 * in bb_udp.c
 */

int udp_start(Config *config);

/* add outgoing WDP. If fails, return -1 and msg is untouched, so
 * caller must think of new uses for it */

int udp_addwdp(Msg *msg);

/* start the avalanche */
int udp_shutdown(void);

/* shutdown the system, call after everything else is done */
int udp_die(void);


/*---------------
 * in bb_smsc.c
 */

int smsc_start(Config *config);
int smsc_restart(Config *config);

/* as udp_addwdp() */
int smsc_addwdp(Msg *msg);

/* start the avalanche */
int smsc_shutdown(void);

/* shutdown the system, call after everything else is done */
int smsc_die(void);
