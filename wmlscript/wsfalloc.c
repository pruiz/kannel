/*
 *
 * wsfalloc.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Fast memory allocation routines.
 *
 */

#include <wsint.h>

/********************* Global functions *********************************/

WsFastMalloc *
ws_f_create(size_t block_size)
{
  WsFastMalloc *pool = ws_calloc(1, sizeof(WsFastMalloc));

  if (pool == NULL)
    return NULL;

  pool->block_size = block_size;

  return pool;
}


void
ws_f_destroy(WsFastMalloc *pool)
{
  WsFastMallocBlock *b, *bnext;

  if (pool == NULL)
    return;

#if 0
  fprintf(stderr, "ws_f_destroy(): user_bytes_allocated=%u\n",
	  pool->user_bytes_allocated);
#endif

  for (b = pool->blocks; b; b = bnext)
    {
      bnext = b->next;
      ws_free(b);
    }
  ws_free(pool);
}


void *
ws_f_malloc(WsFastMalloc *pool, size_t size)
{
  unsigned char *result;

  if (pool->size < size)
    {
      size_t alloc_size;
      WsFastMallocBlock *b;

      /* Must allocate a fresh block. */
      alloc_size = pool->block_size;
      if (alloc_size < size)
	alloc_size = size;

      /* Allocate the block and remember to add the header size. */
      b = ws_malloc(alloc_size + sizeof(WsFastMallocBlock));

      if (b == NULL)
	/* No memory available. */
	return NULL;

      /* Add this block to the memory pool. */
      b->next = pool->blocks;
      pool->blocks = b;

      pool->ptr = ((unsigned char *) b) + sizeof(WsFastMallocBlock);
      pool->size = alloc_size;
    }

  /* Now we can allocate `size' bytes of data from this pool. */

  result = pool->ptr;

  pool->ptr += size;
  pool->size -= size;

  pool->user_bytes_allocated += size;

  return result;
}


void *
ws_f_calloc(WsFastMalloc *pool, size_t num, size_t size)
{
  void *p = ws_f_malloc(pool, num * size);

  if (p == NULL)
    return p;

  memset(p, 0, num * size);

  return p;
}


void *
ws_f_memdup(WsFastMalloc *pool, const void *ptr, size_t size)
{
  unsigned char *d = ws_f_malloc(pool, size + 1);

  if (d == NULL)
    return NULL;

  memcpy(d, ptr, size);
  d[size] = '\0';

  return d;
}


void *
ws_f_strdup(WsFastMalloc *pool, const char *str)
{
  size_t len;
  char *s;

  if (str == NULL)
    return NULL;

  len = strlen(str) + 1;
  s = ws_f_malloc(pool, len);

  if (s == NULL)
    return NULL;

  memcpy(s, str, len);

  return s;
}
