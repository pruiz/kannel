#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "gwlib/gwlib.h"


#ifdef MUTEX_STATS
Mutex *mutex_make_measured(Mutex *mutex, unsigned char *filename, int lineno)
{
    mutex->filename = filename;
    mutex->lineno = lineno;
    mutex->locks = 0;
    mutex->collisions = 0;
    return mutex;
}
#endif

Mutex *mutex_create_real(void)
{
    Mutex *mutex;

    mutex = gw_malloc(sizeof(Mutex));
    pthread_mutex_init(&mutex->mutex, NULL);
    mutex->owner = -1;
    mutex->dynamic = 1;
    return mutex;
}

Mutex *mutex_init_static_real(Mutex *mutex)
{
    pthread_mutex_init(&mutex->mutex, NULL);
    mutex->owner = -1;
    mutex->dynamic = 0;
    return mutex;
}

void mutex_destroy(Mutex *mutex)
{
    if (mutex == NULL)
        return;

#ifdef MUTEX_STATS
    if (mutex->locks > 0 || mutex->collisions > 0) {
        info(0, "Mutex %s:%d: %ld locks, %ld collisions.",
             mutex->filename, mutex->lineno,
             mutex->locks, mutex->collisions);
    }
#endif

    pthread_mutex_destroy(&mutex->mutex);
    if (mutex->dynamic == 0)
        return;
    gw_free(mutex);
}


void mutex_lock_real(Mutex *mutex, char *file, int line)
{
    int ret;

    gw_assert(mutex != NULL);

#ifdef MUTEX_STATS
    ret = pthread_mutex_trylock(&mutex->mutex);
    if (ret != 0) {
        ret = pthread_mutex_lock(&mutex->mutex);
        mutex->collisions++;
    }
    mutex->locks++;
#else
    ret = pthread_mutex_lock(&mutex->mutex);
#endif
    if (ret != 0)
        panic(ret, "mutex_lock: Mutex failure! called from %s at line %d",file,line);
    if (mutex->owner == gwthread_self())
        panic(0, "mutex_lock: Managed to lock the mutex twice! called from %s at line %d",file,line);
    mutex->owner = gwthread_self();
}

int mutex_unlock_real(Mutex *mutex, char *file, int line)
{
     int ret;
    
    if(mutex == NULL)
    {
       error(0,"trying to unlock a NULL mutex from %s at line %d",file,line);
       return -1;
    }
     gw_assert(mutex != NULL);
     mutex->owner = -1;
     ret = pthread_mutex_unlock(&mutex->mutex);
     if (ret != 0)
        error(ret, "mutex_unlock: Mutex failure! called from file %s at line %d",file,line);
     return ret;
}

