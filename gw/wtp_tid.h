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

#define window_size (long)pow(2,14)

enum {
     no_cache = -1,
     iniatilised = -2,
     not_iniatilised = -3,
     cached = 0
};

enum {
     no_cached_tid,
     ok,
     fail
};

/* 
 * Initilize tid cache. MUST be called before calling other functions in this module.
 */

void wtp_tid_cache_init(void);

/*
 * Does the tid validation test, by using a simple window mechanism
 *
 * Returns: no_cached_tid, if the peer has no cached last tid, or the result
 * of the test (ok, fail);
 */

int wtp_tid_is_valid(WTPEvent *event, WTPMachine *machine);

#endif


