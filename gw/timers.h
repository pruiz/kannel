/*
 * timers.h - interface to timers and timer sets.
 *
 * Timers can be set to elapse after a specified number of seconds
 * (the "interval").  They can be stopped before elapsing, and the
 * interval can be changed.
 *
 * Timers are associated with timer sets.  Each set uses a thread
 * and has a single internal lock.  An "output list" is defined for
 * each timer set.  When a timer elapses, an event is generated on
 * this list.  The event may be removed from this list if the timer
 * is destroyed or extended before the event is consumed.
 *
 * The event to use when a timer elapses is provided by the caller.
 * The timer module will "own" it, and be responsible for deallocation.
 * This will be true until the event has been consumed from the output
 * list (at which point it is owned by the consuming thread).
 * While the event is on the output list, it is in a gray area, because
 * the timer module might still take it back.  This won't be a problem
 * as long as you access the event only by consuming it.
 *
 * Timers work best if the thread that manipulates the timers (the
 * "calling thread") is the same thread that consumes the output list.
 * This way, it can be guaranteed that the calling thread will not
 * see a timer elapse after being destroyed, or while being extended,
 * because the elapse event will be deleted during such an operation.
 */

#ifndef TIMERS_H
#define TIMERS_H

typedef struct Timerset Timerset;
typedef struct Timer Timer;

/*
 * Create a new timer set and return it.  Make it use the specified list
 * to report timer elapse events.
 */
Timerset *timerset_create(List *outputlist);

/*
 * Destroy a timer set and free its resources.  Stop all timers
 * associated with it, but do not destroy them.
 */
void timerset_destroy(Timerset *set);

/*
 * Create a timer and associate it with the specified timer set.
 * Do not start it yet.  Return the new timer.
 */
Timer *timer_create(Timerset *set);

/*
 * Destroy this timer and free its resources.  Stop it first, if needed.
 */
void timer_destroy(Timer *timer);

/*
 * Make the timer elapse after 'interval' seconds, at which time it
 * will push event 'event' on the output list defined for its timer set.
 * - If the timer was already running, these parameters will override
 *   its old settings.
 * - If the timer has already elapsed, try to remove its event from
 *   the output list.
 * If this is not the first time the timer was started, the event
 * pointer is allowed to be NULL.  In that case the event pointer
 * from the previous call to timer_start for this timer is re-used.
 * NOTE: Each timer must have a unique event pointer.  The caller must
 * create the event, and passes control of it to the timer module with
 * this call.
 */
void timer_start(Timer *timer, int interval, WAPEvent *event);

/*
 * Stop this timer.  If it has already elapsed, try to remove its
 * event from the output list.
 */
void timer_stop(Timer *timer);

#endif
