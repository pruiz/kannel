/*
 * test_octstr.c - simple testing of octstr functions
 */

#include <string.h>

#include "gwlib/gwlib.h"

static int signof(int n)
{
    if (n < 0)
    	return -1;
    if (n == 0)
    	return 0;
    return 1;
}

static void check_comparisons(void) 
{
    static const char *tab[] = {
	"",
	"a",
	"ab",
	"abc",
	"abcåäö",
	"ABCÅÄÖ",
    };
    static const int n = sizeof(tab) / sizeof(tab[0]);
    int i, j;
    int sign_str, sign_oct;
    Octstr *os1, *os2;
    
    for (i = 0; i < n; ++i) {
	os1 = octstr_create(tab[i]);
        for (j = 0; j < n; ++j) {
	    os2 = octstr_create(tab[j]);

	    sign_str = signof(strcmp(tab[i], tab[j]));
	    sign_oct = signof(octstr_compare(os1, os2));
	    if (sign_str != sign_oct)
	    	panic(0, "strcmp (%d) and octstr_compare (%d) differ for "
		      "`%s' and `%s'", sign_str, sign_oct, tab[i], tab[j]);

	    sign_str = signof(strcasecmp(tab[i], tab[j]));
	    sign_oct = signof(octstr_case_compare(os1, os2));
	    if (sign_str != sign_oct)
	    	panic(0, "strcasecmp (%d) and octstr_case_compare (%d) "
		      "differ for `%s' and `%s'", sign_str, sign_oct,
		      tab[i], tab[j]);
	    
	    octstr_destroy(os2);
	}
	octstr_destroy(os1);
    }
}


int main(void)
{
    gwlib_init();
    check_comparisons();
    gwlib_shutdown();
    return 0;
}
