/*
 * test_octstr_format.c - simple testing of octstr_format()
 */

#include "gwlib/gwlib.h"

int main(void) {
	Octstr *os, *os2, *os3;

	gwlib_init();

	os = octstr_format("hello, %% %-5.*s, <%*s>, %-5d + %05d = %d, -%5.2f", 
			   3, "world", 3, "", 1, 2, 3, 3.1415927);
	octstr_dump(os, 0);
	
	os2 = octstr_format("<%S>", os);
	octstr_dump(os2, 0);
	
	octstr_format_append(os2, "yeehaa!");
	octstr_dump(os2, 0);
	
	os3 = octstr_format("NULL=%p &os=%p", (void *) NULL, (void *) &os);
	octstr_dump(os3, 0);
	
	octstr_destroy(os);
	octstr_destroy(os2);
	octstr_destroy(os3);
	
	gwlib_shutdown();
	
	return 0;
}
