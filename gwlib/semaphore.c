/*
 * semaphore.c - implementation of semaphores
 *
 * Lars Wirzenius
 */


#include "gwlib/gwlib.h"


struct Semaphore {
    List *list;
};


Semaphore *semaphore_create(long n)
{
    Semaphore *semaphore;
    static char item;
    
    semaphore = gw_malloc(sizeof(*semaphore));
    semaphore->list = list_create();
    list_add_producer(semaphore->list);
    while (n-- > 0)
	list_produce(semaphore->list, &item);
    return semaphore;
}


void semaphore_destroy(Semaphore *semaphore)
{
    if (semaphore != NULL) {
	list_destroy(semaphore->list, NULL);
	gw_free(semaphore);
    }
}


void semaphore_up(Semaphore *semaphore)
{
    static char item;

    list_produce(semaphore->list, &item);
}


void semaphore_down(Semaphore *semaphore)
{
    list_consume(semaphore->list);
}
