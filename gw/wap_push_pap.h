/*
 * wap_push_pap.h: Header of PPG interface to PI
 *
 * By Aarno Syvänen for Wapit Ltd
 */

#ifndef WAP_PUSH_PAP_H
#define WAP_PUSH_PAP_H

#include "wap/wap_events.h"
#include "wap/wap.h"

void wap_push_pap_init(wap_dispatch_func_t *ppg_dispatch);
void wap_push_pap_shutdown(void);
void wap_push_pap_dispatch_event(WAPEvent *e);

#endif
