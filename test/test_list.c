/*
 * test_list.c - test gwlib/list.c
 *
 * Lars Wirzenius <liw@wapit.com>
 */
 
#ifndef TRACE
#define TRACE 100000
#endif

#ifndef THREADS
#define THREADS 1
#endif


#include <unistd.h>
#include <signal.h>
#include "gwlib.h"

#if THREADS
#define NUM_PRODUCERS (4)
#define NUM_CONSUMERS (4)
#else
#define NUM_PRODUCERS (1)
#define NUM_CONSUMERS (1)
#endif

static pthread_t producers[NUM_PRODUCERS];
static pthread_t consumers[NUM_CONSUMERS];

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
			info(0, "Put: %ld, %ld, %ld", id, i, index);
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
			info(0, "Got %ld: %ld, %ld, %ld", i, item->producer, 
					item->num, item->index);
#endif
		received[item->index] = 1;
		gw_free(item);
		++i;
	}

	info(0, "consumer dies");
	return NULL;
}


static void check_received(void) {
	int p, n;
	
	for (p = 0; p < NUM_PRODUCERS; ++p)
		for (n = 0; n < NUM_ITEMS_PER_PRODUCER; ++n)
			if (!received[p*NUM_ITEMS_PER_PRODUCER + n])
				error(0, "Not received: %d %d", p, n);
}


void main_with_threads(void) {
	List *list;
	int i;
	void *ret;
	Item *item;
	
	list = list_create();
	
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
}


void main_without_threads(void) {
	List *list;
	Item *item;
	
	list = list_create();

	producers[0] = (long) pthread_self();
	consumers[0] = (long) pthread_self();
	
	producer(list);
	consumer(list);

	while (list_len(list) > 0) {
		item = list_get(list, 0);
		list_delete(list, 0, 1);
		warning(0, "main: %ld %ld %ld", item->producer, item->num,
				item->index);
	}
	info(0, "main ends");
	
	check_received();
}


int main(void) {
#if THREADS
	main_with_threads();
#else
	main_without_threads();
#endif
	return 0;
}
