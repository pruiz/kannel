/*
 * test_octstr_format.c - simple testing of octstr_format()
 */

#include "gwlib/gwlib.h"

int main(void) {
	Octstr *os, *os2;

	gwlib_init();

	os = octstr_format("hello, %% %-5.*s, %-5d + %05d = %d, -%5.2f", 
			   3, "world", 1, 2, 3, 3.1415927);
	octstr_dump(os, 0);
	
	os2 = octstr_format("<%S>", os);
	octstr_dump(os2, 0);
	
	octstr_format_append(os2, "yeehaa!");
	octstr_dump(os2, 0);
	
	octstr_destroy(os);
	octstr_destroy(os2);
	
	gwlib_shutdown();
	
	return 0;
}
