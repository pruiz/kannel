/*
 *
 * wsfalloc.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999, 2000 Markku Rossi, etc.
 *		 All rights reserved.
 *
 * Fast memory allocation routines with easy cleanup.
 *
 */

#ifndef WSFALLOC_H
#define WSFALLOC_H

/********************* Types and definitions ****************************/

struct WsFastMallocBlockRec
{
  struct WsFastMallocBlockRec *next;
  /* The data follows immediately here. */
};

typedef struct WsFastMallocBlockRec WsFastMallocBlock;

struct WsFastMallocRec
{
  WsFastMallocBlock *blocks;

  /* The default block size of this pool. */
  size_t block_size;

  /* The number of bytes allocates for user blocks. */
  size_t user_bytes_allocated;

  /* The next allocation can be done from this position. */
  unsigned char *ptr;

  /* And it has this much space. */
  size_t size;
};

typedef struct WsFastMallocRec WsFastMalloc;

/********************* Prototypes for global functions ******************/

/* Create a new fast memory allocator with internal block size of
   `block_size' bytes.  The function returns NULL if the creation
   failed. */
WsFastMalloc *ws_f_create(size_t block_size);

/* Destroy the fast allocator `pool' and free all resources it has
   allocated.  All memory chunks, allocated from this pool will be
   invalidated with this call. */
void ws_f_destroy(WsFastMalloc *pool);

/* Allocate `size' bytes of memory from the pool `pool'.  The function
   returns NULL if the allocation fails. */
void *ws_f_malloc(WsFastMalloc *pool, size_t size);

/* Allocate `num' items of size `size' from the pool `pool'.  The
   returned memory block is initialized with zero.  The function
   returns NULL if the allocation fails. */
void *ws_f_calloc(WsFastMalloc *pool, size_t num, size_t size);

/* Take a copy of the memory buffer `ptr' which has `size' bytes of
   data.  The copy is allocated from the pool `pool'.  The function
   returns NULL if the allocation fails. */
void *ws_f_memdup(WsFastMalloc *pool, const void *ptr, size_t size);

/* Take a copy of the C-string `str'.  The copy is allocated from the
   pool `pool'.  The function returns NULL if the allocation fails. */
void *ws_f_strdup(WsFastMalloc *pool, const char *str);

#endif /* not WSFALLOC_H */
