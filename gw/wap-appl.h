/*
 * gw/wap-appl.h - wapbox application layer declarations
 *
 * The application layer's outside interface consists of three functions:
 *
 *	wap_appl_init()
 *		This initializes and starts the application layer thread.
 *
 *	wap_appl_dispatch(event)
 *		This adds a new event to the application layer event
 *		queue.
 *
 *	wap_appl_shutdown()
 *		This shuts down the application layer thread.
 *
 * The application layer is a thread that reads events from its event
 * queue, fetches the corresponding URLs and feeds back events to the
 * WSP layer.
 *
 * Lars Wirzenius
 */


#ifndef WAP_APPL_H
#define WAP_APPL_H

#include "wap-events.h"

void wap_appl_init(void);
void wap_appl_shutdown(void);
void wap_appl_dispatch(WAPEvent *event);

#endif
