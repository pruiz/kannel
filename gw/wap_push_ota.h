/*
 * Push OTA header
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#ifndef WAP_PUSH_OTA_H
#define WAP_PUSH_OTA_H

#include "wap/wap_events.h"
#include "wap/wap.h"

#define CURRENT_VERSION 0;
#define CONNECTED_PORT 9201;

/*
 * Type of bearers (see WDP, Appendix C. pp. 78-79). Only one supported
 */
enum {
    GSM_CSD_IPV4 = 0x0A
};

void wap_push_ota_init(wap_dispatch_func_t *wsp_dispatch,
                       wap_dispatch_func_t *wsp_unit_dispatch);
void wap_push_ota_shutdown(void);
void wap_push_ota_dispatch_event(WAPEvent *e);
void wap_push_ota_bb_address_set(Octstr *ba);

#endif
