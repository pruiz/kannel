/*
 *
 * wshash.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * A mapping from null-terminated strings to `void *' pointers.
 *
 */

#ifndef WSHASH_H
#define WSHASH_H

/********************* Types and definitions ****************************/

/* A hash handle. */
typedef struct WsHashRec *WsHashPtr;

/* A callback function of this type is called to free the data item
   `item' when the hash is destroyed, or a new mapping is set for the
   key of the item `item'.  The argument `context' is a user specified
   context data for the function. */
typedef void (*WsHashItemDestructor)(void *item, void *context);

/********************* Prototypes for global functions ******************/

/* Create a new hash table.  The argument `destructor' is a destructor
   function that is called once for each deleted item.  The argument
   `context' is passed as context data to the destructor function.
   The argument `destructor' can be NULL in which case the mapped
   items are not freed.  The function returns NULL if the creation
   failed (out of memory). */
WsHashPtr ws_hash_create(WsHashItemDestructor destructor, void *contex);

/* Destroy the hash `hash' and free all resources it has allocated.
   If the hash has a destructor function, it is called once for each
   mapped item. */
void ws_hash_destroy(WsHashPtr hash);

/* Add a mapping from the name `name' to the data `data'.  The
   function takes a copy of the name `name' but the data `data' is
   stored as-is.  The possible old data, stored for the name `name',
   will be freed with the destructor function.  The function returns
   WS_TRUE if the operatio was successful or WS_FALSE otherwise. */
WsBool ws_hash_put(WsHashPtr hash, const char *name, void *data);

/* Get the mapping of the name `name' from the hash `hash'. */
void *ws_hash_get(WsHashPtr hash, const char *name);

/* Clear the hash and free all individual items with the destructor
   function.  After this call, the hash `hash' does not contain any
   mappings. */
void ws_hash_clear(WsHashPtr hash);

#endif /* not WSHASH_H */
