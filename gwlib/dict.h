/*
 * dict.h - lookup data structure using octet strings as keys
 *
 * A Dict is an abstract data structure that stores values, represented as
 * void pointers, and uses octet strings (Octstr) as keys. You can think
 * of it as an array indexed by octet strings.
 *
 * Lars Wirzenius
 */

#ifndef DICT_H
#define DICT_H

typedef struct Dict Dict;


/*
 * Create a Dict. `size_hint' gives an indication of how many different
 * keys will be in the Dict at the same time, at most. This is used for
 * performance optimization; things will work fine, though somewhat
 * slower, even if it the number is exceeded. `destroy_value' is a pointer
 * to a function that is called whenever a value stored in the Dict needs
 * to be destroyed. If `destroy_value' is NULL, then values are not
 * destroyed by the Dict, they are just discarded.
 */
Dict *dict_create(long size_hint, void (*destroy_value)(void *));


/*
 * Destroy a Dict and all values in it.
 */
void dict_destroy(Dict *dict);


/*
 * Put a new value into a Dict. If the same key existed already, the
 * old value is destroyed. If `value' is NULL, the old value is destroyed
 * and the key is removed from the Dict.
 */
void dict_put(Dict *dict, Octstr *key, void *value);


/*
 * Look up a value in a Dict. If there is no value corresponding to a 
 * key, return NULL, otherwise return the value. The value will not
 * be removed from the Dict.
 */
void *dict_get(Dict *dict, Octstr *key);


/*
 * Remove a value from a Dict without destroying it.
 */
void *dict_remove(Dict *dict, Octstr *key);


#endif
