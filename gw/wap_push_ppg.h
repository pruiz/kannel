/*
 * Push PPG header
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#ifndef WAP_PUSH_PPG_H
#define WAP_PUSH_PPG_H

#include "wap/wap_events.h"
#include "wap/wap.h"

void wap_push_ppg_init(wap_dispatch_func_t *ota_dispatch);
void wap_push_ppg_shutdown(void);
void wap_push_ppg_dispatch_event(WAPEvent *e);

#endif
