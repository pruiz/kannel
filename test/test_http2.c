/*
 * test_http2.c - a simple program to test the http2 library
 *
 * Lars Wirzenius
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "gwlib/gwlib.h"
#include "gwlib/http2.h"

#define MAX_THREADS 1024

static Counter *counter = NULL;
static long max_requests = 1;
static char **urls = NULL;
static int num_urls = 0;

static void client_thread(void *arg) {
	int ret;
	Octstr *url, *final_url, *replyb, *os;
	List *replyh;
	long i, succeeded, failed;
	
	succeeded = 0;
	failed = 0;
	while ((i = counter_increase(counter)) < max_requests) {
		if ((i % 1000) == 0)
			info(0, "Starting fetch %ld", i);
		url = octstr_create(urls[i % num_urls]);
		ret = http2_get_real(url, NULL, &final_url, &replyh, &replyb);
		if (ret == -1) {
			++failed;
			error(0, "http2_get failed");
		} else {
			++succeeded;
			debug("", 0, "Reply headers:");
			while ((os = list_extract_first(replyh)) 
			       != NULL) {
				octstr_dump(os, 1);
				octstr_destroy(os);
			}
			list_destroy(replyh);
			octstr_print(stdout, replyb);
			octstr_destroy(replyb);
			octstr_destroy(url);
			octstr_destroy(final_url);
		}
	}
	info(0, "This thread: %ld succeeded, %ld failed.", succeeded, failed);
}

static void help(void) {
	info(0, "Usage: test_http2 [-r repeats] url ...\n"
		"where -r means the number of times the fetches should be\n"
		"repeated.");
}

int main(int argc, char **argv) {
	int i, opt, num_threads;
	Octstr *os, *proxy;
	List *exceptions;
	long proxy_port;
	char *p;
	long threads[MAX_THREADS];
	time_t start, end;
	double run_time;
	
	gwlib_init();

	proxy = NULL;
	proxy_port = -1;
	exceptions = list_create();
	num_threads = 0;

	while ((opt = getopt(argc, argv, "hv:r:p:P:e:t:")) != EOF) {
		switch (opt) {
		case 'v':
			set_output_level(atoi(optarg));
			break;

		case 'r':
			max_requests = atoi(optarg);
			break;

		case 't':
			num_threads = atoi(optarg);
			if (num_threads > MAX_THREADS)
				num_threads = MAX_THREADS;
			break;

		case 'h':
			help();
			exit(0);
		
		case 'p':
			proxy = octstr_create(optarg);
			break;
		
		case 'P':
			proxy_port = atoi(optarg);
			break;
		
		case 'e':
			p = strtok(optarg, ":");
			while (p != NULL) {
				list_append(exceptions, octstr_create(p));
				p = strtok(NULL, ":");
			}
			break;

		case '?':
		default:
			error(0, "Invalid option %c", opt);
			help();
			panic(0, "Stopping.");
		}
	}

	if (proxy != NULL && proxy_port > 0)
		http2_use_proxy(proxy, proxy_port, exceptions);
	octstr_destroy(proxy);
	while ((os = list_extract_first(exceptions)) != NULL)
		octstr_destroy(os);
	list_destroy(exceptions);
	
	counter = counter_create();
	urls = argv + optind;
	num_urls = argc - optind;
	
	time(&start);
	if (num_threads == 0)
		client_thread(NULL);
	else {
		for (i = 0; i < num_threads; ++i)
			threads[i] = gwthread_create(client_thread, NULL);
		for (i = 0; i < num_threads; ++i)
			gwthread_join(threads[i]);
	}
	time(&end);

	counter_destroy(counter);
	
	run_time = difftime(end, start);
	info(0, "%ld requests in %f seconds, %f requests/s.",
		max_requests, run_time, max_requests / run_time);
	
	gwlib_shutdown();

	return 0;
}
