/*
 * test_octstr_immutables.c - simple testing of octstr_imm()
 */

#include <stdio.h>   

#include "gwlib/gwlib.h"

int main(int argc, char **argv) 
{
    Octstr *os;

    gwlib_init();
	
    if (optind >= argc) {
        os = octstr_imm("foo");
    } else {
        os = octstr_imm(argv[optind]);
    }

    /* 
     * Note: don't destroy this, check that the log file has no
     * memory leaks.
     */
	
    octstr_dump(os, 0);
	
    gwlib_shutdown();

    return 0;
}

