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

static void help(void) {
	info(0, "Usage: test_http2 [-r repeats] url ...\n"
		"where -r means the number of times the fetches should be\n"
		"repeated.");
}

int main(int argc, char **argv) {
	int i, opt, ret, source;
	Octstr *os, *url, *final_url, *replyb, *type, *charset, *proxy;
	List *replyh, *exceptions;
	long repeats, proxy_port;
	char *p;
	
	gw_init_mem();
	http2_init();

	repeats = 1;
	source = 0;
	proxy = NULL;
	proxy_port = -1;
	exceptions = list_create();

	while ((opt = getopt(argc, argv, "hsr:p:P:e:")) != EOF) {
		switch (opt) {
		case 'r':
			repeats = atoi(optarg);
			break;

		case 's':
			source = 1;
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

	if (source)
		set_output_level(PANIC);

	if (proxy != NULL && proxy_port > 0)
		http2_use_proxy(proxy, proxy_port, exceptions);
	octstr_destroy(proxy);
	while ((os = list_extract_first(exceptions)) != NULL)
		octstr_destroy(os);
	list_destroy(exceptions);

	while (repeats-- > 0) {
		for (i = optind; i < argc; ++i) {
			url = octstr_create(argv[i]);
			ret = http2_get_real(url, NULL, &final_url, 
				&replyh, &replyb);
			if (ret == -1)
				panic(0, "http2_get failed");
			if (source) {
				octstr_print(stdout, replyb);
				while ((os = list_extract_first(replyh)) 
				       != NULL) {
					octstr_destroy(os);
				}
				list_destroy(replyh);
			} else {
				info(0, "http_get2 returned %d", ret);
				info(0, "location=<%s>", 
					octstr_get_cstr(final_url));
				http2_header_get_content_type(replyh, 
						&type, &charset);
				info(0, "type=<%s>", octstr_get_cstr(type));
				info(0, "charset=<%s>", 
					octstr_get_cstr(charset));
				octstr_destroy(type);
				octstr_destroy(charset);
				debug("", 0, "Reply headers:");
				while ((os = list_extract_first(replyh)) 
				       != NULL) {
					octstr_dump(os, 1);
					octstr_destroy(os);
				}
				list_destroy(replyh);
				debug("", 0, "Reply body:");
				octstr_dump(replyb, 1);
			}
			octstr_destroy(replyb);
			octstr_destroy(url);
			octstr_destroy(final_url);
		}
	}
	
	http2_shutdown();
	gw_check_leaks();
	
	return 0;
}
