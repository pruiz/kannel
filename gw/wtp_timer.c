/*
 * wtp_timer.c - timers for WTP
 *
 * This file implements the timers declard in wtp_timer.h. It is very
 * straightforward for now. In the future, it would be a good idea to
 * keep the timers in a priority queue sorted by ascending end time,
 * so that it is not necessary to traverse the entire list of timers
 * every time of wtp_timer_check is called.
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include "wtp_timer.h"
#include "gwlib.h"


struct WTPTimer {
	struct WTPTimer *next;
	long start_time;
	long interval;
	WTPMachine *machine;
	WTPEvent *event;
};


static WTPTimer *list = NULL;


WTPTimer *wtp_timer_create(void) {
	WTPTimer *timer;
	
	timer = malloc(sizeof(WTPTimer));
	if (timer == NULL) {
		error(errno, "Out of memory creating timer.");
		return NULL;
	}

	timer->interval = 0;

	timer->next = list;
	list = timer;

	return timer;
}


void wtp_timer_destroy(WTPTimer *timer) {
	WTPTimer *t;
	
	if (list == timer)
		list = timer->next;
	else {
		for (t = list; t != NULL && t->next != timer; t = t->next)
			continue;
		if (t == NULL) {
			error(0, "Unknown timer, ignored, not stopped.");
			return;
		}
		assert(t->next == timer);
		t->next = timer->next;
	}

	free(timer);
}


void wtp_timer_start(WTPTimer *timer, long interval, WTPMachine *sm,
WTPEvent *e) {
	timer->start_time = (long) time(NULL);
	timer->interval = interval;
	timer->machine = sm;
	timer->event = e;
	debug(0, "Timer %p started at %ld, duration %ld.", 
		(void *) timer, timer->start_time, timer->interval);
}


void wtp_timer_stop(WTPTimer *timer) {
	timer->interval = 0;
	debug(0, "Timer %p stopped at %ld.",
		(void *) timer, (long) time(NULL));
}


void wtp_timer_check(void) {
	WTPTimer *t;
	long now;

	now = (long) time(NULL);
	debug(0, "Checking timers at %ld.", now);
	for (t = list; t != NULL; t = t->next) {
		if (t->interval == 0)
			continue;
		if (t->start_time + t->interval <= now) {
			debug(0, "Timer %p has elapsed.", (void *) t);
			t->interval = 0;
		} else
			debug(0, "Timer %p has not elapsed.", (void *) t);
	}
}
