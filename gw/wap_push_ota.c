/*
 * Wap_push_ota.c: push OTA implementation
 *
 * By Aarno Syvänen for Wapit Ltd
 */

#include "wap_push_ota.h"
#include "gwlib/gwlib.h"
 
void wap_push_ota_dispatch_event(WAPEvent *e)
{
    debug("wap.wsp", 0, "OTA not yet supported, dropping an event: \n");
    wap_event_dump(e);
}
