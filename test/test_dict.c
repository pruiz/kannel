/*
 * test_dict.c - test Dict objects
 *
 * Lars Wirzenius
 */


#include "gwlib/gwlib.h"


int main(void)
{
    Dict *dict;
    Octstr *foo, *bar;
    
    gwlib_init();
    
    foo = octstr_create_immutable("foo");
    bar = octstr_create_immutable("bar");
    
    dict = dict_create(10, NULL);
    dict_put(dict, foo, bar);
    info(0, "foo gives %s", octstr_get_cstr(dict_get(dict, foo)));
    dict_destroy(dict);
    gwlib_shutdown();
    return 0;
}
