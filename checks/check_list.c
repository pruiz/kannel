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

struct producer_info {
    List *list;
    long start_index;
    long id;
};


static char received[NUM_PRODUCERS * NUM_ITEMS_PER_PRODUCER];


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
	long i, index;
	long id;
	struct producer_info *info;

	info = arg;

	id = gwthread_self();
	index = info->start_index;
	for (i = 0; i < NUM_ITEMS_PER_PRODUCER; ++i, ++index)
		list_produce(info->list, new_item(id, i, index));
	list_remove_producer(info->list);
}

static void consumer(void *arg) {
	List *list;
	Item *item;
	
	list = arg;
	for (;;) {
		item = list_consume(list);
		if (item == NULL)
			break;
		received[item->index] = 1;
		gw_free(item);
	}
}


static void init_received(void) {
	memset(received, 0, sizeof(received));
}


static void main_for_producer_and_consumer(void) {
	List *list;
	int i;
	Item *item;
	struct producer_info tab[NUM_PRODUCERS];
	long p, n, index;
	int errors;
	
	list = list_create();
	init_received();
	
	for (i = 0; i < NUM_PRODUCERS; ++i) {
	    	tab[i].list = list;
		tab[i].start_index = i * NUM_ITEMS_PER_PRODUCER;
	    	list_add_producer(list);
		tab[i].id = gwthread_create(producer, tab + i);
	}
	for (i = 0; i < NUM_CONSUMERS; ++i)
		gwthread_create(consumer, list);
	
    	gwthread_join_every(producer);
    	gwthread_join_every(consumer);

	while (list_len(list) > 0) {
		item = list_get(list, 0);
		list_delete(list, 0, 1);
		warning(0, "main: %ld %ld %ld", (long) item->producer, 
				item->num, item->index);
	}
	
	errors = 0;
	for (p = 0; p < NUM_PRODUCERS; ++p) {
		for (n = 0; n < NUM_ITEMS_PER_PRODUCER; ++n) {
			index = p * NUM_ITEMS_PER_PRODUCER + n;
			if (!received[index]) {
				error(0, "Not received: producer=%ld "
				         "item=%ld index=%ld", 
					 tab[p].id, n, index);
				errors = 1;
			}
		}
	}
	
	if (errors)
		panic(0, "Not all messages were received.");
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
	list_delete_matching(list, items[0], compare_cstr);
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
	
	list_destroy(list, NULL);
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
		extracted = list_extract_matching(list, items[j], 
					compare_cstr);
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
		list_destroy(extracted, NULL);
	}
	
	if (list_len(list) != 0)
		panic(0, "list is not empty after extracting everything");
	
	list_destroy(list, NULL);
}


int main(void) {
	gwlib_init();
	set_output_level(GW_INFO);
	main_for_list_add_and_delete();
	main_for_extract();
	main_for_producer_and_consumer();
	close_all_logfiles();
	return 0;
}
