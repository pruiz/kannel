/*
 * list.c - generic dynamic list
 *
 * This module implements the generic list. See list.h for an explanation
 * of how to use the list.
 *
 * The list is implemented as an array, a starting index into the array,
 * and an integer giving the length of the list. The list's element i is
 * not necessarily at array element i, but instead it is found at element
 *
 *	(start + i) % len
 *
 * This is because we need to make it fast to use the list as a queue,
 * meaning that adding elements to the end and removing them from the
 * beginning must be very fast. Insertions into the middle of the list
 * need not be fast, however. It would be possible to implement the list
 * with a linked list, of course, but this would cause many more memory
 * allocations: every time an item is added to the list, a new node would
 * need to be allocated, and when it is removed, it would need to be freed.
 * Using an array lets us reduce the number of allocations. It also lets
 * us access an arbitrary element in constant time, which is especially
 * useful since it lets us simplify the list API by not adding iterators
 * or an explicit list item type.
 *
 * If insertions and deletions into the middle of the list become common,
 * it would be more efficient to use a buffer gap implementation, but
 * there's no point in doing that until the need arises.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

#include <string.h>
#include <unistd.h>

#include "gwassert.h"
#include "config.h"
#include "list.h"
#include "log.h"
#include "gwmem.h"
#include "thread.h"


struct List {
	void **tab;
	long tab_size;
	long start;
	long len;
	Mutex *single_operation_lock;
	Mutex *permanent_lock;
	pthread_cond_t nonempty;
	long num_producers;
};

#define INDEX(list, i)	(((list)->start + i) % (list)->tab_size)
#define GET(list, i)	((list)->tab[INDEX(list, i)])



static void lock(List *list);
static void unlock(List *list);
static void make_bigger(List *list, long items);
static void delete_items_from_list(List *list, long pos, long count);


List *list_create(void) {
	List *list;

	list = gw_malloc(sizeof(List));
	list->tab = NULL;
	list->tab_size = 0;
	list->start = 0;
	list->len = 0;
	list->single_operation_lock = mutex_create();
	list->permanent_lock = mutex_create();
	pthread_cond_init(&list->nonempty, NULL);
	list->num_producers = 0;
	return list;
}


void list_destroy(List *list) {
	if (list == NULL)
		return;

	mutex_lock(list->permanent_lock);
	mutex_destroy(list->permanent_lock);
	mutex_destroy(list->single_operation_lock);
	pthread_cond_destroy(&list->nonempty);
	gw_free(list->tab);
	gw_free(list);
}


long list_len(List *list) {
	return list->len;
}


void list_append(List *list, void *item) {
	lock(list);
	make_bigger(list, 1);
	list->tab[INDEX(list, list->len)] = item;
	++list->len;
	pthread_cond_signal(&list->nonempty);
	unlock(list);
}


void list_insert(List *list, long pos, void *item) {
	long i;

	lock(list);
	gw_assert(pos >= 0);
	gw_assert(pos <= list->len);

	make_bigger(list, 1);
	for (i = list->len; i > pos; --i)
		list->tab[(list->start + i) % list->tab_size] = 
			list->tab[(list->start + i - 1) % list->tab_size];
	list->tab[(list->start + pos) % list->tab_size] = item;
	++list->len;
	pthread_cond_signal(&list->nonempty);
	unlock(list);
}


void list_delete(List *list, long pos, long count) {
	lock(list);
	delete_items_from_list(list, pos, count);
	unlock(list);
}


void list_delete_all(List *list, void *pat, list_item_matches_t *cmp) {
	long i;

	lock(list);
	
	/* XXX this could be made more efficient by noticing
	   consecutive items to be removed, but leave that for later.
	   --liw */
	i = 0;
	while (i < list->len) {
		if (cmp(GET(list, i), pat))
			delete_items_from_list(list, i, 1);
		else
			++i;
	}
	unlock(list);
}


void list_delete_equal(List *list, void *item) {
	long i;

	lock(list);
	
	/* XXX this could be made more efficient by noticing
	   consecutive items to be removed, but leave that for later.
	   --liw */
	i = 0;
	while (i < list->len) {
		if (GET(list, i) == item)
			delete_items_from_list(list, i, 1);
		else
			++i;
	}
	unlock(list);
}


void *list_get(List *list, long pos) {
	void *item;

	lock(list);
	gw_assert(pos >= 0);
	gw_assert(pos < list->len);
	item = GET(list, pos);
	unlock(list);
	return item;
}


void *list_extract_first(List *list) {
	void *item;
	
	/*gw_assert(list);*/
	lock(list);
	if (list->len == 0)
		item = NULL;
	else {
		item = GET(list, 0);
		list->start = (list->start + 1) % list->tab_size;
		list->len -= 1;
	}
	unlock(list);
	return item;
}


List *list_extract_all(List *list, void *pat, list_item_matches_t *cmp) {
	List *new_list;
	long i;

	new_list = list_create();
	lock(list);
	i = 0;
	while (i < list->len) {
		if (cmp(GET(list, i), pat)) {
			list_append(new_list, GET(list, i));
			delete_items_from_list(list, i, 1);
		} else
			++i;
	}
	unlock(list);

	if (list_len(new_list) == 0) {
		list_destroy(new_list);
		return NULL;
	}
	return new_list;
}


void list_lock(List *list) {
	gw_assert(list);
	mutex_lock(list->permanent_lock);
}


void list_unlock(List *list) {
	gw_assert(list);
	mutex_unlock(list->permanent_lock);
}


int list_wait_until_nonempty(List *list) {
	int ret;

	lock(list);
	while (list->len == 0 && list->num_producers > 0) {
		pthread_cond_wait(&list->nonempty, 
				  &list->single_operation_lock->mutex);
	}
	if (list->len > 0)
		ret = 1;
	else
		ret = -1;
	unlock(list);
	return ret;
}


void list_add_producer(List *list) {
	lock(list);
	++list->num_producers;
	unlock(list);
}


int list_producer_count(List *list) {
        int ret;
	lock(list);
	ret = list->num_producers;
	unlock(list);
	return ret;
}


void list_remove_producer(List *list) {
	lock(list);
	gw_assert(list->num_producers > 0);
	--list->num_producers;
	pthread_cond_broadcast(&list->nonempty);
	unlock(list);
}


void list_produce(List *list, void *item) {
	list_append(list, item);
}


void *list_consume(List *list) {
	void *item;

	lock(list);
	while (list->len == 0 && list->num_producers > 0) {
		pthread_cond_wait(&list->nonempty, 
				  &list->single_operation_lock->mutex);
	}
	if (list->len > 0) {
		item = GET(list, 0);
		list->start = (list->start + 1) % list->tab_size;
		list->len -= 1;
	} else {
		item = NULL;
	}
	unlock(list);
	return item;
}



void *list_search(List *list, void *pattern, int (*cmp)(void *, void *)) {
	void *item;
	long i;
	
	lock(list);
	item = NULL;
	for (i = 0; i < list->len; ++i) {
		item = GET(list, i);
		if (cmp(item, pattern))
			break;
	}
	if (i == list->len)
		item = NULL;
	unlock(list);
	
	return item;
}



List *list_search_all(List *list, void *pattern, int (*cmp)(void *, void *)) {
	List *new_list;
	void *item;
	long i;
	
	new_list = list_create();

	lock(list);
	item = NULL;
	for (i = 0; i < list->len; ++i) {
		item = GET(list, i);
		if (cmp(item, pattern))
			list_append(new_list, item);
	}
	unlock(list);
	
	if (list_len(new_list) == 0) {
		list_destroy(new_list);
		new_list = NULL;
	}
	
	return new_list;
}


/*
 * list_cat - 'catenate' two lists. Destroy the latter list.
 */
List *list_cat(List *list1, List *list2)
{
    void *item;
    
    while((item = list_extract_first(list2)) != NULL)
	list_append(list1, item);

    list_destroy(list2);
    
    return list1;    
}


/*************************************************************************/

static void lock(List *list) {
	gw_assert(list);
	mutex_lock(list->single_operation_lock);
}

static void unlock(List *list) {
	gw_assert(list);
	mutex_unlock(list->single_operation_lock);
}


/*
 * Make the array bigger. It might be more efficient to make the size
 * bigger than what is explicitly requested.
 *
 * Assume list has been locked for a single operation already.
 */
static void make_bigger(List *list, long items) {
	long old_size, new_size;
	long len_at_beginning, len_at_end;

	if (list->len + items <= list->tab_size)
		return;

	old_size = list->tab_size;
	new_size = old_size + items;
	list->tab = gw_realloc(list->tab, new_size * sizeof(void *));
	list->tab_size = new_size;

	/*
	 * Now, one of the following situations is in effect
	 * (* is used, empty is unused element):
	 *
	 * Case 1: Used area did not wrap. No action is necessary.
	 * 
	 *			   old_size              new_size
	 *			   v                     v
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * | |*|*|*|*|*|*| | | | | | | | | | | | | | | | |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *   ^           ^
	 *   start       start+len
	 * 
	 * Case 2: Used area wrapped, but the part at the beginning
	 * of the array fits into the new area. Action: move part
	 * from beginning to new area.
	 * 
	 *			   old_size              new_size
	 *			   v                     v
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |*|*| | | | | | | |*|*|*| | | | | | | | | | | |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *     ^             ^
	 *     start+len     start
	 * 
	 * Case 3: Used area wrapped, and the part at the beginning
	 * of the array does not fit into the new area. Action: move
	 * as much as will fit from beginning to new area and move
	 * the rest to the beginning.
	 * 
	 *				      old_size   new_size
	 *					     v   v
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 * |*|*|*|*|*|*|*|*|*| | | | | | | | |*|*|*|*| | |
	 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	 *		     ^               ^
	 *		     start+len       start
	 */		

	gw_assert(list->start < old_size || (list->start == 0 && old_size == 0));
	if (list->start + list->len > old_size) {
		len_at_end = old_size - list->start;
		len_at_beginning = list->len - len_at_end;
		if (len_at_beginning <= new_size - old_size) {
			/* This is Case 2. */
			memmove(list->tab + old_size,
				list->tab,
				len_at_beginning * sizeof(void *));
		} else {
			/* This is Case 3. */
			memmove(list->tab + old_size,
				list->tab,
				(new_size - old_size) * sizeof(void *));
			memmove(list->tab,
			        list->tab + (new_size - old_size),
				(len_at_beginning - (new_size - old_size))
					* sizeof(void *));
		}
	}
}


/*
 * Remove items `pos' through `pos+count-1' from list. Assume list has
 * been locked by caller already.
 */
static void delete_items_from_list(List *list, long pos, long count) {
	long i, from, to;

	gw_assert(pos >= 0);
	gw_assert(pos < list->len);
	gw_assert(count >= 0);
	gw_assert(pos + count <= list->len);

	/*
	 * There are four cases:
	 *
	 * Case 1: Deletion at beginning of list. Just move start
	 * marker forwards (wrapping it at end of array). No need
	 * to move any items.
	 *
	 * Case 2: Deletion at end of list. Just shorten the length
	 * of the list. No need to move any items.
	 *
	 * Case 3: Deletion in the middle so that the list does not
	 * wrap in the array. Move remaining items at end of list
	 * to the place of the deletion.
	 *
	 * Case 4: Deletion in the middle so that the list does indeed
	 * wrap in the array. Move as many remaining items at the end
	 * of the list as will fit to the end of the array, then move
	 * the rest to the beginning of the array.
	 */
	if (pos == 0) {
		list->start = (list->start + count) % list->tab_size;
		list->len -= count;
	} else if (pos + count == list->len) {
		list->len -= count;
	} else if (list->start + list->len < list->tab_size) {
		memmove(list->tab + list->start + pos,
		        list->tab + list->start + pos + count,
			(list->len - pos - count) * sizeof(void *));
		list->len -= count;
	} else {
		/*
		 * This is not especially efficient, but it's simple and
		 * works. Faster methods would have to take more special
		 * cases into account. 
		 */
		for (i = 0; i < list->len - count - pos; ++i) {
			from =  INDEX(list, pos + i + count);
			to = INDEX(list, pos + i);
			list->tab[to] = list->tab[from];
		}
		list->len -= count;
	}
}

