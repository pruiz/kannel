/*
 * check_list.c - check that gwlib/list.c works
 */
 
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"

#define NUM_PRODUCERS (4)
#define NUM_CONSUMERS (4)
#define NUM_ITEMS_PER_PRODUCER (1*1000)

static long producers[NUM_PRODUCERS];
static long consumers[NUM_CONSUMERS];
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


static void producer(void *arg) {
	List *list;
	long i, index;
	long id;

	list = arg;

	id = gwthread_self();
	index = producer_index_start(id);
	list_add_producer(list);
	for (i = 0; i < NUM_ITEMS_PER_PRODUCER; ++i, ++index)
		list_produce(list, new_item(id, i, index));
	list_remove_producer(list);
}

static void consumer(void *arg) {
	List *list;
	long i;
	Item *item;
	
	list = arg;
	i = 0;
	for (;;) {
		item = list_consume(list);
		if (item == NULL)
			break;
		received[item->index] = 1;
		gw_free(item);
		++i;
	}
}


static void init_received(void) {
	memset(received, 0, sizeof(received));
}

static void check_received(void) {
	long p, n, index;
	int errors;
	
	errors = 0;
	for (p = 0; p < NUM_PRODUCERS; ++p) {
		for (n = 0; n < NUM_ITEMS_PER_PRODUCER; ++n) {
			index = p * NUM_ITEMS_PER_PRODUCER + n;
			if (!received[index]) {
				error(0, "Not received: producer=%lu "
				         "item=%ld index=%ld", 
					 (unsigned long) producers[p], 
					 n, index);
				errors = 1;
			}
		}
	}
	
	if (errors)
		panic(0, "Not all messages were received.");
}


static void main_for_producer_and_consumer(void) {
	List *list;
	int i;
	Item *item;
	
	list = list_create();
	init_received();
	
	for (i = 0; i < NUM_PRODUCERS; ++i)
		producers[i] = gwthread_create(producer, list);
	for (i = 0; i < NUM_CONSUMERS; ++i)
		consumers[i] = gwthread_create(consumer, list);
	
	for (i = 0; i < NUM_PRODUCERS; ++i)
		gwthread_join(producers[i]);
	for (i = 0; i < NUM_CONSUMERS; ++i)
		gwthread_join(consumers[i]);

	while (list_len(list) > 0) {
		item = list_get(list, 0);
		list_delete(list, 0, 1);
		warning(0, "main: %ld %ld %ld", (long) item->producer, 
				item->num, item->index);
	}
	
	check_received();
}


static int compare_cstr(void *item, void *pat) {
	return strcmp(item, pat) == 0;
}


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
}


int main(void) {
	gwlib_init();
	set_output_level(INFO);
	main_for_list_add_and_delete();
	main_for_extract();
	main_for_producer_and_consumer();
	close_all_logfiles();
	return 0;
}
