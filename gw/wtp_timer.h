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

typedef struct WTPTimer WTPTimer;

#include "wtp.h"

struct WTPTimer {
	struct WTPTimer *next;
	long start_time;
	long interval;
	WTPMachine *machine;
	WTPEvent *event;
};

/*
 * Initialize timers data structure. This function MUST be called before others.
 */
void wtp_timer_init(void);

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

/*
 * Print all fields of a timer, using the project debugging function
 */
void wtp_timer_dump(WTPTimer *timer);

#endif




