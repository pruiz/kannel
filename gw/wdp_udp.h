/*
 * wdp_udp.h - declarations for WDP over UDP
 */


#ifndef WDP_UDP_H
#define WDP_UDP_H

#include "gwlib/gwlib.h"
#include "bb_msg.h"

typedef struct {
	Octstr *addr;
	int fd;
} WDP_UDPBearer;


/*
 * the following functions are similar to SMSCenter
 */

/*
 * Open a UDP port and start listening it for packets. Return NULL if this
 * fails.
 */
WDP_UDPBearer *wdp_udp_open(ConfigGroup *grp);

/*
 * Close the UDP port. Return 0 for OK, -1 for failure.
 */
int wdp_udp_close(WDP_UDPBearer *udp);

/*
 * Get next message from UDP port and return it. If there was no message or
 * something went wrong, return NULL:
 */
RQueueItem *wdp_udp_get_message(WDP_UDPBearer *udp);

/*
 * Send a message as a UDP packet. Return 0 for OK, -1 for failure.
 */
int wdp_udp_send_message(WDP_UDPBearer *udp, RQueueItem *msg);

/*
 * Check whether this particular instance should handle this Msg.
 *
 * Return:
 *  1 if this instance handles this message
 *  0 if this instance doesn't handle this message
 * -1 on failure
 */
int wdp_udp_is_to_us(WDP_UDPBearer *udp, Msg *msg);


#endif
