/*
 *
 * wsalloc.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Memory allocation routines.  These are simple stub functions to fix
 * some brain damages, found from some system's default allocators.
 *
 */

#include "wsint.h"

#if !WS_MEM_DEBUG

/********************* Global functions *********************************/

void *
ws_malloc(size_t size)
{
  return malloc(size);
}


void *
ws_calloc(size_t num, size_t size)
{
  return calloc(num, size);
}


void *
ws_realloc(void *ptr, size_t size)
{
  if (size == 0)
    {
      if (ptr)
	free(ptr);

      return NULL;
    }

  if (ptr == NULL)
    return malloc(size);

  return realloc(ptr, size);
}


void *
ws_memdup(const void *ptr, size_t size)
{
  unsigned char *data = ws_malloc(size + 1);

  if (data == NULL)
    return NULL;

  memcpy(data, ptr, size);
  data[size] = '\0';

  return data;
}


void *
ws_strdup(const char *str)
{
  size_t len;
  void *s;

  if (str == NULL)
    return NULL;

  len = strlen(str);
  s = ws_malloc(len + 1);

  if (s == NULL)
    return NULL;

  memcpy(s, str, len + 1);

  return s;
}


void
ws_free(void *ptr)
{
  if (ptr)
    free(ptr);
}

#else /* WS_MEM_DEBUG */

/********************* Memory debugging routines ************************/

#define SIZE(_size) (sizeof(WsMemBlockHdr) + (_size))

#define MAGIC 0xfe01fa77

struct WsMemBlockHdrRec
{
  unsigned long magic;
  struct WsMemBlockHdrRec *next;
  struct WsMemBlockHdrRec *prev;
  size_t size;
  const char *file;
  int line;
};

typedef struct WsMemBlockHdrRec WsMemBlockHdr;

/* A linked list of currently allocated blocks. */
WsMemBlockHdr *blocks = NULL;

/* How many blocks are currently allocated. */
unsigned int num_blocks = 0;

/* The maximum amount of blocks used. */
unsigned int max_num_blocks = 0;

/* How many (user) bytes of memory the currently allocated blocks
   use. */
size_t balance = 0;

/* The maximum amount of memory used. */
size_t max_balance = 0;

/* The alloc sequence number. */
unsigned int alloc_number = 0;

/* How many allocations are successful. */
unsigned int num_successful_allocs = -1;


static void
add_block(WsMemBlockHdr *b, size_t size, const char *file, int line)
{
  b->magic = MAGIC;

  b->next = blocks;
  b->prev = NULL;

  if (blocks)
    blocks->prev = b;

  blocks = b;

  b->size = size;
  b->file = file;
  b->line = line;

  num_blocks++;
  balance += size;

  if (balance > max_balance)
    max_balance = balance;

  if (num_blocks> max_num_blocks)
    max_num_blocks = num_blocks;
}


static void
remove_block(WsMemBlockHdr *b)
{
  if (b->magic != MAGIC)
    ws_fatal("remove_block(): invalid magic\n");

  if (b->next)
    b->next->prev = b->prev;
  if (b->prev)
    b->prev->next = b->next;
  else
    blocks = b->next;

  balance -= b->size;
  num_blocks--;

  memset(b, 0xfe, SIZE(b->size));
}


void *
ws_malloc_i(size_t size, const char *file, int line)
{
  WsMemBlockHdr *b;

  if (alloc_number++ >= num_successful_allocs)
    return NULL;

  b = malloc(SIZE(size));

  if (b == NULL)
    return NULL;

  add_block(b, size, file, line);

  return b + 1;
}


void *
ws_calloc_i(size_t num, size_t size, const char *file, int line)
{
  void *p = ws_malloc_i(num * size, file, line);

  if (p)
    memset(p, 0, num * size);

  return p;
}


void *
ws_realloc_i(void *ptr, size_t size, const char *file, int line)
{
  WsMemBlockHdr *b = ((WsMemBlockHdr *) ptr) - 1;
  void *n;

  if (ptr == NULL)
    return ws_malloc_i(size, file, line);

  if (b->size >= size)
    /* We can use the old block. */
    return ptr;

  /* Allocate a bigger block. */
  n = ws_malloc_i(size, file, line);
  if (n == NULL)
    return NULL;

  memcpy(n, ptr, b->size);

  /* Free old block. */
  remove_block(b);
  free(b);

  return n;
}


void *
ws_memdup_i(const void *ptr, size_t size, const char *file, int line)
{
  void *p = ws_malloc_i(size + 1, file, line);

  if (p)
    {
      unsigned char *cp = (unsigned char *) p;

      memcpy(p, ptr, size);
      cp[size] = '\0';
    }

  return p;
}


void *
ws_strdup_i(const char *str, const char *file, int line)
{
  return ws_memdup_i(str, strlen(str), file, line);
}


void
ws_free_i(void *ptr)
{
  WsMemBlockHdr *b = ((WsMemBlockHdr *) ptr) - 1;

  if (ptr == NULL)
    return;

  remove_block(b);
  free(b);
}


int
ws_has_leaks(void)
{
  return num_blocks || balance;
}


void
ws_dump_blocks(void)
{
  WsMemBlockHdr *b;

  fprintf(stderr, "ws: maximum memory usage: %u blocks, %ld bytes\n",
	  max_num_blocks, (long) max_balance);
  fprintf(stderr, "ws: number of allocs: %u\n", alloc_number);

  if (num_blocks || balance)
    {
      fprintf(stderr, "ws: memory leaks: %u blocks, %ld bytes:\n",
	      num_blocks, (long) balance);

      for (b = blocks; b; b = b->next)
	fprintf(stderr, "%s:%d: %ld\n", b->file, b->line, (long) b->size);
    }
}


void
ws_clear_leaks(unsigned int num_successful_allocs_)
{
  alloc_number = 0;
  num_successful_allocs = num_successful_allocs_;
  blocks = NULL;
}

#endif /* WS_MEM_DEBUG */
