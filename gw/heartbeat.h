/*
 * heartbeat.h - thread for sending heartbeat Msgs to bearerbox
 */

#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include "gwlib/gwlib.h"
#include "msg.h"

/*
 * Signature for a function that returns the current load value.
 */
typedef long hb_load_func_t(void);

/*
 * Signature for a function that takes the heartbeat msg and does
 * something with it.
 */
typedef void hb_send_func_t(Msg *hb);

/* 
 * Start a thread that produces Msgs of type heartbeat on the msgs list.
 * The speed is approximately one per freq seconds.
 * The function load_func will be called to determine what should be
 * filled in for the load parameter.
 * Return the thread number.  Return -1 if the operation failed.
 */
long heartbeat_start(hb_send_func_t *send_func, double freq,
                     hb_load_func_t *load_func);

/*
 * Stop the indicated heartbeat thread.
 */
void heartbeat_stop(long hb_thread);

#endif
