/*
 *
 * wshash.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999 Markku Rossi, etc.
 *		 All rights reserved.
 *
 * A simple string to `void *' mapping.
 *
 */

#ifndef WSHASH_H
#define WSHASH_H

/********************* Types and definitions ****************************/

typedef struct WsHashRec *WsHashPtr;

typedef void (*WsHashItemDestructor)(void *item, void *context);

/********************* Prototypes for global functions ******************/

WsHashPtr ws_hash_create(WsHashItemDestructor destructor, void *contex);

void ws_hash_destroy(WsHashPtr hash);

/* The possible old data, stored for the name `name', will be freed
   with the destructor function. */
WsBool ws_hash_put(WsHashPtr hash, const char *name, void *data);

void *ws_hash_get(WsHashPtr hash, const char *name);

/* Clear the hash and free all individual items with the destructor
   function. */
void ws_hash_clear(WsHashPtr hash);

#endif /* not WSHASH_H */
