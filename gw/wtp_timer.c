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

/*
 * Wtp timers "queue" (additions at to the end, removals by searching through 
 * whole data structure. Lock is global.
 */

struct Timers {
       WTPTimer *list;        /* pointer to the first timer in the timers 
                                 list */
       Mutex *lock;           /* global mutex for serializing timer 
                                 handling */
};

typedef struct Timers Timers;

static Timers timers =
{
       NULL,
       NULL
};


WTPTimer *wtp_timer_create(void) {
	WTPTimer *timer;
	
	timer = gw_malloc(sizeof(WTPTimer));
        timer->start_time = 0;
	timer->interval = 0;
        timer->event = NULL;
        timer->machine = NULL;
  
        mutex_lock(timers.lock);

        timer->next = timers.list;
	timers.list = timer;

        mutex_unlock(timers.lock);

        debug("wap.wtp.timer", 0, "creating timer %p", (void *) timer);
	return timer;
}


void wtp_timer_destroy(WTPTimer *timer) {
	WTPTimer *t = NULL;
	
        mutex_lock(timers.lock);

	if (timers.list == timer) {
	   timers.list = timer->next;
           debug("wap.wtp.timer", 0, "First item in the list: %p. Destroying it", (void *) timer);

	} else {
	   for (t = timers.list; t != NULL && t != timer; t = t->next){
               debug("wap.wtp.timer", 0, "Going thru timers list. Met timer %p, belonging to machine %p", (void *) timer, (void *) timer->machine);
	       continue;
           }

	   if (t == NULL) {
	      error(0, "Unknown timer, ignored, not stopped.");
              mutex_unlock(timers.lock);
	      return;
	   }

	   assert(t == timer);
           debug("wap.wtp.timer", 0, "destroying timer %p", (void *) t);
	   timer->next = t->next;
	}
        
	gw_free(timer);

        mutex_unlock(timers.lock);
        return;
}


void wtp_timer_start(WTPTimer *timer, long interval, WTPMachine *sm,
     WTPEvent *e) {

     mutex_lock(timers.lock);

     timer->start_time = (long) time(NULL);
     timer->interval = interval;
     timer->machine = sm;
     timer->event = e;
     debug("wap.wtp.timer", 0, "Timer %p started at %ld, duration %ld.", 
	   (void *) timer, timer->start_time, timer->interval);

     mutex_unlock(timers.lock);
}


void wtp_timer_stop(WTPTimer *timer) {
   
     mutex_lock(timers.lock);
     
     timer->interval = 0;
     debug("wap.wtp.timer", 0, "Timer %p stopped at %ld.", (void *) timer,
	    (long) time(NULL));

     mutex_unlock(timers.lock);
}


void wtp_timer_check(void) {

	WTPTimer *timer = NULL,
                 *temp = NULL;
	long now;

	now = (long) time(NULL);
	debug("wap.wtp.timer", 0, "Checking timers at %ld.", now);

        mutex_lock(timers.lock);

        timer = timers.list;

	while ( timer != NULL) {

            debug("wap.wtp.timer", 0, "Going thru timers list. This timer belongs to the machine %p and its timer interval was %ld", (void *) timer->machine, timer->interval);

	    if (timer->interval == 0) {
               debug("wap.wtp.timer", 0, "Timer %p stopped.", 
                    (void *) timer);
               timer = timer->next;
	       continue;
            }

	    if (timer->start_time + timer->interval <= now) {
	       debug("wap.wtp.timer", 0, "Timer %p has elapsed.", 
                    (void *) timer);
               
	       timer->interval = 0;
/*
 * Wtp_handle_event can call wtp_timer_destroy, which would free the memory 
 * allocated to timer. So we must store the pointer to the next element before
 * calling it.
 */
               temp = timer->next;
               mutex_unlock(timers.lock);
               wtp_handle_event(timer->machine, timer->event);
               mutex_lock(timers.lock);
               timer = temp;

	    } else {
	       debug("wap.wtp.timer", 0, "Timer %p has not elapsed.", 
                    (void *) timer);
               timer = timer->next;
            }          
	}

        mutex_unlock(timers.lock);

        return;
}

void wtp_timer_dump(WTPTimer *timer){

     mutex_lock(timers.lock);

     debug("wap.wtp.timer", 0, "Timer dump starts");
     debug("wap.wtp.timer", 0, "Starting time was %ld", timer->start_time);
     debug("wap.wtp.timer", 0, "Checking interval was %ld", timer->interval);
     debug("wap.wtp.timer", 0, "Timer belonged to a machine");
     wtp_machine_dump(timer->machine);
     debug("wap.wtp.timer", 0, "Timer event was");
     wtp_event_dump(timer->event);
     debug("wap.wtp.timer", 0, "Timer dump ends");

     mutex_unlock(timers.lock); 
}

void wtp_timer_init(void){

     timers.lock = mutex_create();
}

