/*
 * gwlib/counter.c - a counter object
 *
 * This file implements the Counter objects declared in counter.h.
 *
 * Lars Wirzenius.
 */

#include <limits.h>

#include "gwlib.h"

struct Counter
{
    Mutex *lock;
    long n;
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

long counter_increase(Counter *counter)
{
    long ret;

    mutex_lock(counter->lock);
    ret = counter->n;
    if (counter->n == LONG_MAX)
        counter->n = 0;
    else
        ++counter->n;
    mutex_unlock(counter->lock);
    return ret;
}

long counter_value(Counter *counter)
{
    long ret;

    mutex_lock(counter->lock);
    ret = counter->n;
    mutex_unlock(counter->lock);
    return ret;
}

long counter_decrease(Counter *counter)
{
    long ret;

    mutex_lock(counter->lock);
    ret = counter->n;
    if (counter->n > 0)
        --counter->n;
    mutex_unlock(counter->lock);
    return ret;
}

long counter_set(Counter *counter, long n)
{
    long ret;

    mutex_lock(counter->lock);
    ret = counter->n;
    counter->n = n;
    mutex_unlock(counter->lock);
    return ret;
}
