
#include "gwlib.h"
#include "gwmem.h"
#include "thread.h"
#include <stdlib.h>
#include <errno.h>

void *gw_malloc(size_t size)
{
    void *ptr;

    ptr = malloc(size);
    if (ptr == NULL)
	panic(errno, "Memory allocation of %d bytes failed", size);

    return ptr;
}


void *gw_realloc(void *ptr, size_t size)
{
    void *new_ptr;

    new_ptr = realloc(ptr, size);
    if (new_ptr == NULL)
	panic(errno, "Memory re-allocation of %d bytes failed", size);
    
    return new_ptr;
}


void  gw_free(void *ptr)
{
    free(ptr);
}


