/*
 * test_octstr_immutables.c - simple testing of octstr_imm()
 */

#include "gwlib/gwlib.h"

int main(void) {
	Octstr *os;

	gwlib_init();
	
	os = octstr_imm("foo");

	/* 
	 * Note: don't destroy this, check that the log file has no
	 * memory leaks.
	 */
	
	octstr_dump(os, 0);
	
	gwlib_shutdown();

	return 0;
}
