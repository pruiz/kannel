/*
 * test_list.c - test gwlib/list.c
 *
 * Lars Wirzenius <liw@wapit.com>
 */
 
#ifndef TRACE
#define TRACE (100*1000)
#endif

#ifndef THREADS
#define THREADS 1
#endif


#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"

#if THREADS
#define NUM_PRODUCERS (4)
#define NUM_CONSUMERS (4)
#else
#define NUM_PRODUCERS (1)
#define NUM_CONSUMERS (1)
#endif

static long producers[NUM_PRODUCERS];
static long consumers[NUM_CONSUMERS];

#define NUM_ITEMS_PER_PRODUCER (100*1000)


static char received[NUM_PRODUCERS * NUM_ITEMS_PER_PRODUCER];

static int producer_index_start(long producer) {
	int i;
	
	for (i = 0; i < NUM_PRODUCERS; ++i) {
		if (producer == producers[i])
			break;
	}
	if (i >= NUM_PRODUCERS)
		panic(0, "Couldn't find thread.");
	return i * NUM_ITEMS_PER_PRODUCER;
}




typedef struct {
	long producer;
	long num;
	long index;
} Item;


static Item *new_item(long producer, long num, long index) {
	Item *item;
	
	item = gw_malloc(sizeof(Item));
	item->producer = producer;
	item->num = num;
	item->index = index;
	return item;
}


static void *producer(void *arg) {
	List *list;
	long i, id, index;

	list = arg;

	id = (long) pthread_self();
	index = producer_index_start(id);
	info(0, "producer starts at %ld", index);
	list_add_producer(list);
	for (i = 0; i < NUM_ITEMS_PER_PRODUCER; ++i, ++index) {
		list_produce(list, new_item(id, i, index));
#if TRACE
		if ((i % TRACE) == 0)
			info(0, "Put: producer==%ld item=%ld index=%ld", 
				id, i, index);
#endif
	}
	info(0, "producer dies");
	list_remove_producer(list);
	return NULL;
}

static void *consumer(void *arg) {
	List *list;
	long i;
	Item *item;
	
	info(0, "consumer starts");

	list = arg;

	i = 0;
	for (;;) {
		item = list_consume(list);
		if (item == NULL)
			break;
#if TRACE
		if ((i % TRACE) == 0)
			info(0, "Got %ld: producer=%ld item=%ld index=%ld", 
				i, item->producer, item->num, item->index);
#endif
		received[item->index] = 1;
		gw_free(item);
		++i;
	}

	info(0, "consumer dies");
	return NULL;
}


static void init_received(void) {
	memset(received, 0, sizeof(received));
}

static void check_received(void) {
	long p, n, index;
	
	for (p = 0; p < NUM_PRODUCERS; ++p) {
		for (n = 0; n < NUM_ITEMS_PER_PRODUCER; ++n) {
			index = p * NUM_ITEMS_PER_PRODUCER + n;
			if (!received[index]) {
				error(0, "Not received: producer=%ld "
				         "item=%ld index=%ld", 
					 producers[p], n, index);
			}
		}
	}
}


#if THREADS
static void main_for_producer_and_consumer(void) {
	List *list;
	int i;
	void *ret;
	Item *item;
	
	list = list_create();
	init_received();
	
	for (i = 0; i < NUM_PRODUCERS; ++i)
		producers[i] = start_thread(0, producer, list, 0);
	for (i = 0; i < NUM_CONSUMERS; ++i)
		consumers[i] = start_thread(0, consumer, list, 0);
	
	info(0, "main waits for children");
	for (i = 0; i < NUM_PRODUCERS; ++i)
		if (pthread_join(producers[i], &ret) != 0)
			panic(0, "pthread_join failed");
	for (i = 0; i < NUM_CONSUMERS; ++i)
		if (pthread_join(consumers[i], &ret) != 0)
			panic(0, "pthread_join failed");

	while (list_len(list) > 0) {
		item = list_get(list, 0);
		list_delete(list, 0, 1);
		warning(0, "main: %ld %ld %ld", item->producer, item->num,
				item->index);
	}
	info(0, "main ends");
	
	check_received();
	info(0, "main_with_threads done.");
}
#else
static void main_for_producer_and_consumer(void) {
	List *list;
	Item *item;
	long i;
	
	list = list_create();
	init_received();
	for (i = 0; i < NUM_PRODUCERS; ++i) {
		producers[i] = (long) pthread_self();
		producer(list);
	}
	for (i = 0; i < NUM_PRODUCERS; ++i) {
		consumers[i] = (long) pthread_self();
		consumer(list);
	}

	while (list_len(list) > 0) {
		item = list_get(list, 0);
		list_delete(list, 0, 1);
		warning(0, "main: %ld %ld %ld", item->producer, item->num,
				item->index);
	}
	info(0, "main ends");
	
	check_received();
	info(0, "main_without_threads done.");
}
#endif


static int compare_cstr(void *item, void *pat) {
	return strcmp(item, pat) == 0;
}


#if 0
static void dump(List *list) {
	long i;
	
	debug("", 0, "List dump begin (%ld items):", list_len(list));
	for (i = 0; i < list_len(list); ++i)
		debug("", 0, "[%ld] = <%s>", i, (char *) list_get(list, i));
	debug("", 0, "List dump end.");
}
#endif


static void main_for_list_add_and_delete(void) {
	static char *items[] = {
		"one",
		"two",
		"three",
	};
	int num_items = sizeof(items) / sizeof(items[0]);
	int num_repeats = 3;
	int i, j;
	char *p;
	List *list;

	list = list_create();
	
	for (j = 0; j < num_repeats; ++j)
		for (i = 0; i < num_items; ++i)
			list_append(list, items[i]);
	list_delete_all(list, items[0], compare_cstr);
	for (i = 0; i < list_len(list); ++i) {
		p = list_get(list, i);
		if (strcmp(p, items[0]) == 0)
			panic(0, "list contains `%s' after deleting it!",
				items[0]);
	}
	
	for (i = 0; i < num_items; ++i)
		list_delete_equal(list, items[i]);
	if (list_len(list) != 0)
		panic(0, "list is not empty after deleting everything");
	
	list_destroy(list);
	info(0, "list adds and deletes OK in simple case.");
}


static void main_for_extract(void) {
	static char *items[] = {
		"one",
		"two",
		"three",
	};
	int num_items = sizeof(items) / sizeof(items[0]);
	int num_repeats = 3;
	int i, j;
	char *p;
	List *list, *extracted;

	list = list_create();
	
	for (j = 0; j < num_repeats; ++j)
		for (i = 0; i < num_items; ++i)
			list_append(list, items[i]);

	for (j = 0; j < num_items; ++j) {
		extracted = list_extract_all(list, items[j], compare_cstr);
		if (extracted == NULL)
			panic(0, "no extracted elements, should have!");
		for (i = 0; i < list_len(list); ++i) {
			p = list_get(list, i);
			if (strcmp(p, items[j]) == 0)
				panic(0, "list contains `%s' after "
				         "extracting it!",
					items[j]);
		}
		for (i = 0; i < list_len(extracted); ++i) {
			p = list_get(extracted, i);
			if (strcmp(p, items[j]) != 0)
				panic(0, 
				  "extraction returned wrong element!");
		}
		list_destroy(extracted);
	}
	
	if (list_len(list) != 0)
		panic(0, "list is not empty after extracting everything");
	
	list_destroy(list);
	info(0, "list extraction OK in simple case.");
}


int main(void) {
	gw_init_mem();
	main_for_list_add_and_delete();
	main_for_extract();
	main_for_producer_and_consumer();
	return 0;
}
