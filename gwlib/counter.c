/*
 * gwlib/counter.c - a counter object
 *
 * This file implements the Counter objects declared in counter.h.
 *
 * Lars Wirzenius.
 *
 * Changed the counter type 'long' into 'unsigned long' so it wraps 
 * by itself. Just keep increasing it.
 * Also added a counter_increase_with function.
 * harrie@lisanza.net
 */

#include <limits.h>

#include "gwlib.h"

struct Counter
{
    Mutex *lock;
    unsigned long n;
};

Counter *counter_create(void)
{
    Counter *counter;

    counter = gw_malloc(sizeof(Counter));
    counter->lock = mutex_create();
    counter->n = 0;
    return counter;
}


void counter_destroy(Counter *counter)
{
    mutex_destroy(counter->lock);
    gw_free(counter);
}

unsigned long counter_increase(Counter *counter)
{
    unsigned long ret;

    mutex_lock(counter->lock);
    ret = counter->n;
    ++counter->n;
    mutex_unlock(counter->lock);
    return ret;
}

unsigned long counter_increase_with(Counter *counter, unsigned long value)
{
    unsigned long ret;

    mutex_lock(counter->lock);
    ret = counter->n;
    counter->n += value;
    mutex_unlock(counter->lock);
    return ret;
}

unsigned long counter_value(Counter *counter)
{
    unsigned long ret;

    mutex_lock(counter->lock);
    ret = counter->n;
    mutex_unlock(counter->lock);
    return ret;
}

unsigned long counter_decrease(Counter *counter)
{
    unsigned long ret;

    mutex_lock(counter->lock);
    ret = counter->n;
    if (counter->n > 0)
        --counter->n;
    mutex_unlock(counter->lock);
    return ret;
}

unsigned long counter_set(Counter *counter, unsigned long n)
{
    unsigned long ret;

    mutex_lock(counter->lock);
    ret = counter->n;
    counter->n = n;
    mutex_unlock(counter->lock);
    return ret;
}
