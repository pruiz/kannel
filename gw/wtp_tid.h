/*
 * wtp_tid.h - tid verification implementation header
 *
 * By Aarno Syvänen for WapIT Ltd
 */

#ifndef WTP_TID_H
#define WTP_TID_H

#include <math.h>
#include <stdlib.h>

#include "wtp.h"

#define WTP_TID_WINDOW_SIZE (1L << 14)

/*
 * Constants defining the result of tid validation
 */
enum {
     no_cached_tid,
     ok,
     fail
};

/*
 * Tid cache item consists of iniator identifier and cached tid.
 */
struct WTPCached_tid {
       Octstr *source_address;
       long source_port;
       Octstr *destination_address;
       long destination_port;
       long tid;
       struct WTPCached_tid *next;
};

typedef struct WTPCached_tid WTPCached_tid;

/* 
 * Initilize tid cache. MUST be called before calling other functions in this module.
 */

void wtp_tid_cache_init(void);

/*
 * Shut down the tid cache. MUST be called after tid cache isn't used anymore.
 */
void wtp_tid_cache_shutdown(void);

/*
 * Does the tid validation test, by using a simple window mechanism
 *
 * Returns: no_cached_tid, if the peer has no cached last tid, or the result
 * of the test (ok, fail);
 */

int wtp_tid_is_valid(WAPEvent *event, WTPMachine *machine);

/*
 * Changes the tid value belonging to an existing iniator
 */
void wtp_tid_set_by_machine(WTPMachine *machine, long tid);

#endif


