/*
 * timers.h - interface to timers and timer sets.
 *
 * Timers can be set to elapse after a specified number of seconds
 * (the "interval").  They can be stopped before elapsing, and the
 * interval can be changed.
 *
 * An "output list" is defined for each timer.  When it elapses, an
 * event is generated on this list.  The event may be removed from
 * the output list if the timer is destroyed or extended before the
 * event is consumed.
 *
 * The event to use when a timer elapses is provided by the caller.
 * The timer module will "own" it, and be responsible for deallocation.
 * This will be true until the event has been consumed from the output
 * list (at which point it is owned by the consuming thread).
 * While the event is on the output list, it is in a gray area, because
 * the timer module might still take it back.  This won't be a problem
 * as long as you access the event only by consuming it.
 *
 * Timers work best if the thread that manipulates the timer (the
 * "calling thread") is the same thread that consumes the output list.
 * This way, it can be guaranteed that the calling thread will not
 * see a timer elapse after being destroyed, or while being extended,
 * because the elapse event will be deleted during such an operation.
 *
 * The timer_* functions have been renamed to gwtimer_* to avoid
 * a name conflict on Solaris systems.
 */

#ifndef TIMERS_H
#define TIMERS_H

#include "gwlib/gwlib.h"
#include "wap_events.h"

typedef struct Timer Timer;

/*
 * Start up the timer system.
 * Can be called more than once, in which case multiple shutdowns are
 * also required.
 */
void timers_init(void);

/*
 * Stop all timers and shut down the timer system.
 */
void timers_shutdown(void);

/*
 * Create a timer and tell it to use the specified output list when
 * it elapses.  Do not start it yet.  Return the new timer.
 */
Timer *gwtimer_create(List *outputlist);

/*
 * Destroy this timer and free its resources.  Stop it first, if needed.
 */
void gwtimer_destroy(Timer *timer);

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
void gwtimer_start(Timer *timer, int interval, WAPEvent *event);

/*
 * Stop this timer.  If it has already elapsed, try to remove its
 * event from the output list.
 */
void gwtimer_stop(Timer *timer);

#endif
