/*
 *
 * wshash.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999 Markku Rossi, etc.
 *		 All rights reserved.
 *
 * A simple string to `void *' mapping.
 *
 */

#include <wsint.h>
#include <wshash.h>

/* XXX the current implementation is a stupid linear linked list.
   This must be fixed.  But, the interface is ok. */

/********************* Types and definitions ****************************/

struct WsHashItemRec
{
  struct WsHashItemRec *next;
  char *name;
  void *data;
};

typedef struct WsHashItemRec WsHashItem;

struct WsHashRec
{
  WsHashItem *items;
  WsHashItemDestructor destructor;
  void *destructor_context;
};

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

  for (i = hash->items; i; i = i->next)
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
  i->next = hash->items;
  hash->items = i;

  return WS_TRUE;
}


void *
ws_hash_get(WsHashPtr hash, const char *name)
{
  WsHashItem *i;

  for (i = hash->items; i; i = i->next)
    if (strcmp(i->name, name) == 0)
      return i->data;

  return NULL;
}


void
ws_hash_clear(WsHashPtr hash)
{
  WsHashItem *i, *n;

  for (i = hash->items; i; i = n)
    {
      n = i->next;
      if (hash->destructor)
	(*hash->destructor)(i->data, hash->destructor_context);

      ws_free(i->name);
      ws_free(i);
    }

  hash->items = NULL;
}
