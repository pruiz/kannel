/*
 *
 * wshash.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * A mapping from null-terminated strings to `void *' pointers.
 *
 */

#include <wsint.h>
#include <wshash.h>

/********************* Types and definitions ****************************/

/* The size of the hash table. */
#define WS_HASH_TABLE_SIZE	256

/* A hash item. */
struct WsHashItemRec
{
  struct WsHashItemRec *next;
  char *name;
  void *data;
};

typedef struct WsHashItemRec WsHashItem;

/* The hash object. */
struct WsHashRec
{
  WsHashItem *items[WS_HASH_TABLE_SIZE];
  WsHashItemDestructor destructor;
  void *destructor_context;
};

/********************* Prototypes for static functions ******************/

/* Hash function to count the hash value of string `string'.  */
static size_t count_hash(const char *string);

/********************* Global functions *********************************/

WsHashPtr
ws_hash_create(WsHashItemDestructor destructor, void *context)
{
  WsHashPtr hash = ws_calloc(1, sizeof(*hash));

  if (hash)
    {
      hash->destructor = destructor;
      hash->destructor_context = context;
    }

  return hash;
}


void
ws_hash_destroy(WsHashPtr hash)
{
  if (hash == NULL)
    return;

  ws_hash_clear(hash);
  ws_free(hash);
}


WsBool
ws_hash_put(WsHashPtr hash, const char *name, void *data)
{
  WsHashItem *i;
  size_t h = count_hash(name);

  for (i = hash->items[h]; i; i = i->next)
    if (strcmp(i->name, name) == 0)
      {
	/* Found it. */

	/* Destroy the old item */
	if (hash->destructor)
	  (*hash->destructor)(i->data, hash->destructor_context);

	i->data = data;

	return WS_FALSE;
      }

  /* Must create a new mapping. */
  i = ws_calloc(1, sizeof(*i));

  if (i == NULL)
    return WS_FALSE;

  i->name = ws_strdup(name);
  if (i->name == NULL)
    {
      ws_free(i);
      return WS_FALSE;
    }

  i->data = data;

  /* Link it to our hash. */
  i->next = hash->items[h];
  hash->items[h] = i;

  return WS_TRUE;
}


void *
ws_hash_get(WsHashPtr hash, const char *name)
{
  WsHashItem *i;
  size_t h = count_hash(name);

  for (i = hash->items[h]; i; i = i->next)
    if (strcmp(i->name, name) == 0)
      return i->data;

  return NULL;
}


void
ws_hash_clear(WsHashPtr hash)
{
  WsHashItem *i, *n;
  size_t j;

  for (j = 0; j < WS_HASH_TABLE_SIZE; j++)
    {
      for (i = hash->items[j]; i; i = n)
	{
	  n = i->next;
	  if (hash->destructor)
	    (*hash->destructor)(i->data, hash->destructor_context);

	  ws_free(i->name);
	  ws_free(i);
	}
      hash->items[j] = NULL;
    }
}

/********************* Static functions *********************************/

static size_t
count_hash(const char *string)
{
  size_t val = 0;
  int i;

  for (i = 0; string[i]; i++)
    {
      val <<= 3;
      val ^= string[i];
      val ^= (val & 0xff00) >> 5;
      val ^= (val & 0xff0000) >> 16;
    }

  return val % WS_HASH_TABLE_SIZE;
}
