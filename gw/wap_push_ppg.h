/*
 * Push PPG main module header
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#ifndef WAP_PUSH_PPG_H
#define WAP_PUSH_PPG_H

#include "wap/wap_events.h"
#include "wap/wap.h"
#include "wap/wap_addr.h"

void wap_push_ppg_init(wap_dispatch_func_t *ota_dispatch);
void wap_push_ppg_shutdown(void);
void wap_push_ppg_dispatch_event(WAPEvent *e);

/*
 * Check do we have established a session with an initiator for this push.
 * Initiators are identified by their address tuple (ppg main module does not
 * know wsp sessions until told. 
 */
int wap_push_ppg_have_push_session_for(WAPAddrTuple *tuple);

/*
 * Now iniator are identified by their session id. This function is used after
 * the session is established.
 */
int wap_push_ppg_have_push_session_for_sid(long sid);

#endif
