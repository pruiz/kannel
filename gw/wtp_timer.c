/*
 * wtp_timer.c - timers for WTP
 *
 * This file implements the timers declared in wtp_timer.h. It is very
 * straightforward for now. In the future, it would be a good idea to
 * keep the timers in a priority queue sorted by ascending end time,
 * so that it is not necessary to traverse the entire list of timers
 * every time of wtp_timer_check is called.
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef TRACE
#define TRACE 0
#endif

#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include "wtp_timer.h"
#include "gwlib/gwlib.h"

/* Wtp timers queue, contains all active timers.  The list's lock is
 * used for operations on its elements.  No list operations should be
 * done without explicit list_lock and list_unlock. */
static List *timers;

/* The events triggered by elapsed timers are collected in these
 * structures, so that they can be handled in a separate loop.
 * (That avoids the problem of wtp_handle_event wanting to modify
 * the timers while wtp_timer_check is scanning the list).
 */
struct pending_event {
	WTPMachine *machine;
	WAPEvent *event;
};

WTPTimer *wtp_timer_create(void) {
	WTPTimer *timer;
	
	timer = gw_malloc(sizeof(WTPTimer));
        timer->start_time = 0;
	timer->interval = 0;
        timer->event = NULL;
        timer->machine = NULL;
 
	list_lock(timers);

	list_append(timers, timer); 
#if TRACE
        debug("wap.wtp.timer", 0, "Created timer %p.", (void *) timer);
#endif

	list_unlock(timers);

	return timer;
}


void wtp_timer_destroy(WTPTimer *timer) {
	long len, count;

	if (timer == NULL)
		return;

	list_lock(timers);

	/* Count the number of deleted items by comparing the list
	 * length before and after. */
	len = list_len(timers);

	list_delete_equal(timers, timer);
	count = len - list_len(timers);

	if (count == 1) {
#if TRACE
        	debug("wap.wtp.timer", 0,
			"Destroyed timer %p.", (void *) timer);
#endif
	} else if (count < 1) {
		error(0, "Unknown timer %p, ignored, not stopped.",
			(void *) timer);
		list_unlock(timers);
		return;
	} else if (count > 1) {
		debug("wap.wtp.timer", 0,
			"Destroyed timer %p, occurred %ld times!",
			(void *) timer, count);
	}

	list_unlock(timers);
	
	wap_event_destroy(timer->event);
	gw_free(timer);
}


void wtp_timer_start(WTPTimer *timer, long interval,
			WTPMachine *sm, WAPEvent *e) {

	list_lock(timers);

	timer->start_time = (long) time(NULL);
	timer->interval = interval;
	timer->machine = sm;
	timer->event = e;

#if 0
	debug("wap.wtp.timer", 0, "Timer %p started at %ld, duration %ld.", 
		(void *) timer, timer->start_time, timer->interval);
#endif

	list_unlock(timers);
}


void wtp_timer_stop(WTPTimer *timer) {

	list_lock(timers);

	timer->interval = 0;
	wap_event_destroy(timer->event);
	timer->event = NULL;
#if 0
	debug("wap.wtp.timer", 0, "Timer %p stopped at %ld.", (void *) timer,
		(long) time(NULL));
#endif

	list_unlock(timers);

}


void wtp_timer_check(void) {
	long now;
	long pos, len;
	struct pending_event *eventp;
	List *elapsed;  /* List of pending_event structs */

	now = (long) time(NULL);
	debug("wap.wtp.timer", 0, "Checking timers at %ld.", now);

	elapsed = list_create();

	list_lock(timers);
	len = list_len(timers);

	for (pos = 0; pos < len; pos++) {
		WTPTimer *timer = list_get(timers, pos);

#if 0
		debug("wap.wtp.timer", 0,
			"Going thru timers list. This timer belongs to the "
			"machine %p and its timer interval was %ld",
			(void *) timer->machine, timer->interval);
#endif

		if (timer->interval == 0) {
#if 0
			debug("wap.wtp.timer", 0, "Timer %p stopped.", 
				(void *) timer);
#endif
			continue;
		}

		if (timer->start_time + timer->interval <= now) {
			debug("wap.wtp.timer", 0, "Timer %p has elapsed.", 
				(void *) timer);
               
			timer->interval = 0;

			eventp = gw_malloc(sizeof(*eventp));
			eventp->event = timer->event;
			eventp->machine = timer->machine;
			list_append(elapsed, eventp);
		} else {
#if 0
			debug("wap.wtp.timer", 0, "Timer %p has not elapsed.", 
				(void *) timer);
#endif
		}          
	}

	list_unlock(timers);

	/* This has to be done after the timers list is unlocked, because
	 * wtp_handle_event can modify timers. */
	while ((eventp = list_consume(elapsed))) {
#if 0
		wtp_handle_event(eventp->machine, eventp->event);
#endif
		gw_free(eventp);
	}
	list_destroy(elapsed);
}

void wtp_timer_dump(WTPTimer *timer){

	list_lock(timers);

	debug("wap.wtp.timer", 0, "Timer dump starts.");
	debug("wap.wtp.timer", 0, "Starting time was %ld.", timer->start_time);
	debug("wap.wtp.timer", 0, "Checking interval was %ld.",
					timer->interval);
	debug("wap.wtp.timer", 0, "Timer belonged to a machine: %p", 
			(void *) timer->machine);
	debug("wap.wtp.timer", 0, "Timer event was:");
	wap_event_dump(timer->event);
	debug("wap.wtp.timer", 0, "Timer dump ends.");

	list_unlock(timers);
}

void wtp_timer_init(void) {
	timers = list_create();
}


void wtp_timer_shutdown(void) {
	while (list_len(timers) > 0)
		wtp_timer_destroy(list_extract_first(timers));
	list_destroy(timers);
}
