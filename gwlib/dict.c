/*
 * dict.c - lookup data structure using octet strings as keys
 *
 * The Dict is implemented as a simple hash table. In the future, it 
 * might be interesting to use a trie instead.
 *
 * Lars Wirzenius, based on code by Tuomas Luttinen
 */


#include "gwlib.h"


/*
 * The hash table stores key/value -pairs in a List.
 */

typedef struct Item Item;
struct Item {
    Octstr *key;
    void *value;
};


static Item *item_create(Octstr *key, void *value)
{
    Item *item;
    
    item = gw_malloc(sizeof(*item));
    item->key = octstr_duplicate(key);
    item->value = value;
    return item;
}

static void item_destroy(void *item)
{
    Item *p;
    
    p = item;
    octstr_destroy(p->key);
    gw_free(p);
}


static int item_has_key(void *item, void *key)
{
    return octstr_compare(key, ((Item *) item)->key) == 0;
}


/*
 * The dictionary itself is a very simple hash table.
 */

struct Dict {
    List **tab;
    long size;
    void (*destroy_value)(void *);
    Mutex *lock;
};


static void lock(Dict *dict)
{
    mutex_lock(dict->lock);
}


static void unlock(Dict *dict)
{
    mutex_unlock(dict->lock);
}


static long key_to_index(Dict *dict, Octstr *key)
{
    return octstr_hash_key(key) % dict->size;
}


/*
 * And finally, the public functions.
 */


Dict *dict_create(long size_hint, void (*destroy_value)(void *))
{
    Dict *dict;
    long i;
    
    dict = gw_malloc(sizeof(*dict));

    /*
     * Hash tables tend to work well until they are fill to about 50%.
     */
    dict->size = size_hint * 2;

    dict->tab = gw_malloc(sizeof(dict->tab[0]) * dict->size);
    for (i = 0; i < dict->size; ++i)
    	dict->tab[i] = list_create();
    dict->lock = mutex_create();
    dict->destroy_value = destroy_value;
    
    return dict;
}


void dict_destroy(Dict *dict)
{
    long i;
    Item *p;
    
    for (i = 0; i < dict->size; ++i) {
	while ((p = list_extract_first(dict->tab[i])) != NULL) {
	    if (dict->destroy_value != NULL)
	    	dict->destroy_value(p->value);
	    item_destroy(p);
	}
	list_destroy(dict->tab[i], NULL);
    }
    mutex_destroy(dict->lock);
    gw_free(dict->tab);
    gw_free(dict);
}


void dict_put(Dict *dict, Octstr *key, void *value)
{
    long i;
    Item *p;

    lock(dict);
    i = key_to_index(dict, key);
    p = list_search(dict->tab[i], key, item_has_key);
    if (p == NULL) {
    	p = item_create(key, value);
	list_append(dict->tab[i], p);
    } else {
	if (dict->destroy_value != NULL)
	    dict->destroy_value(p->value);
	p->value = value;
    }
    unlock(dict);
}


void *dict_get(Dict *dict, Octstr *key)
{
    long i;
    Item *p;
    void *value;

    lock(dict);
    i = key_to_index(dict, key);
    p = list_search(dict->tab[i], key, item_has_key);
    if (p == NULL)
    	value = NULL;
    else
    	value = p->value;
    unlock(dict);
    return value;
}


void *dict_remove(Dict *dict, Octstr *key)
{
    long i;
    Item *p;
    void *value;
    List *list;

    lock(dict);
    i = key_to_index(dict, key);
    list = list_extract_matching(dict->tab[i], key, item_has_key);
    gw_assert(list == NULL || list_len(list) == 1);
    if (list == NULL)
    	value = NULL;
    else {
	p = list_get(list, 0);
	list_destroy(list, NULL);
    	value = p->value;
	item_destroy(p);
    }
    unlock(dict);
    return value;
}
