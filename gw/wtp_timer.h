/*
 * wtp_timer.h - timers for WTP
 *
 * The WTP layer of the WAP protocol stack uses timers. A typical use
 * scenario is when it sends a packet to the phone and waits for a response
 * packet. It sends the packet and then starts a timer. If the timer elapses
 * before the response packet arrives, WTP assumes that the first packet
 * was lost and re-sends it. If the responses arrives in time, WTP stops
 * the timer. Since WTP is implemented as a state machine, if the timer
 * elapses, it needs to send an event to the relevant WTP state machine.
 * The machine and the event are given to the timer when it is started;
 * if the timer elapses, is uses the wtp_handle_event function to send
 * the event to the state machine.
 *
 * The checking of whether timers have elapsed is done by wtp_timer_check
 * (see below). It knows all timers that exist (the functions for creating
 * and destroying them keep a list of all timers), and checks each of them
 * in turn. It is meant that a separate thread calls wtp_timer_check at
 * suitable intervals.
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef WTP_TIMER_H
#define WTP_TIMER_H

#if 0
typedef int WTPMachine;
typedef int WTPEvent;
#else
#include "wtp.h"
#endif

/*
 * The timer itself is defined in wtp_timer.c. It can only be accessed
 * via the functions declared in this header.
 */
typedef struct WTPTimer WTPTimer;


/*
 * Create and initialize a WTPTimer object.
 */
WTPTimer *wtp_timer_create(void);


/*
 * Destroy a WTPTimer object. It is implicitly stopped as well.
 */
void wtp_timer_destroy(WTPTimer *timer);


/*
 * Start the timer.
 */
void wtp_timer_start(WTPTimer *timer, long interval, WTPMachine *sm, 
	WTPEvent *e);


/*
 * Stop the timer.
 */
void wtp_timer_stop(WTPTimer *timer);


/*
 * Check all timers and see whether they have elapsed, and if they have,
 * send the event. (The timer is then stopped, of course.)
 */
void wtp_timer_check(void);
	

#endif
