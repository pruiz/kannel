/*
 * gwlib/counter.h - a counter object
 *
 * Kannel has monotonously growing counters in some places, and these need
 * to work even when several threads use them. This header defines the
 * type Counter that provides such a counter. The counter is a long, and
 * if it reaches LONG_MAX, it wraps around to zero (_NOT_ LONG_MIN).
 *
 * Lars Wirzenius.
 *
 * Changed the counter type 'long' into 'unsigned long' so it wraps
 * by itself. Just keep increasing it.
 * Also added a counter_increase_with function.
 * harrie@lisanza.net
 */


#ifndef COUNTER_H
#define COUNTER_H

typedef struct Counter Counter;

/* create a new counter object. PANIC if fails */
Counter *counter_create(void);

/* destroy it */
void counter_destroy(Counter *counter);

/* return the current value of the counter and increase counter by one */
unsigned long counter_increase(Counter *counter);

/* return the current value of the counter and increase counter by value */
unsigned long counter_increase_with(Counter *counter, unsigned long value);

/* return the current value of the counter */
unsigned long counter_value(Counter *counter);

/* return the current value of the counter and decrease counter by one */
unsigned long counter_decrease(Counter *counter);

/* return the current value of the counter and set it to the supplied value */
unsigned long counter_set(Counter *, unsigned long);

#endif
