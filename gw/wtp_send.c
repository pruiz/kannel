/*
 *wtp_send.h - WTP message module implementation
 *
 *By Aarno Syvänen for WapIT Ltd.
 */

#include "wtp_send.h"

/*
 * Packs wdp datagram having result PDU as user data. Fetches SDU from WTP 
 * event, address four-tuple and machine state information (are we resending
 * the packet) from WTP machine. Handles all errors by itself.
 */

void wtp_send_result(WTPMachine *machine, WTPEvent *event){

}
