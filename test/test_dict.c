/*
 * test_dict.c - test Dict objects
 *
 * Lars Wirzenius
 */


#include "gwlib/gwlib.h"

#define HUGE_SIZE 200000

int main(void)
{
    Dict *dict;
    Octstr *foo, *bar;
    unsigned long i;
     
    gwlib_init();
    
    foo = octstr_imm("foo");
    bar = octstr_imm("bar");
    
    debug("",0,"Dict simple test.");
    dict = dict_create(10, NULL);
    dict_put(dict, foo, bar);
    info(0, "foo gives %s", octstr_get_cstr(dict_get(dict, foo)));
    if (dict_key_count(dict) == 1)
        info(0, "there is but one foo.");
    else
        error(0, "key count is %ld, should be 1.", dict_key_count(dict));
    dict_destroy(dict);

    debug("",0,"Dict extended/huge test.");
    dict = dict_create(HUGE_SIZE, (void (*)(void *))octstr_destroy);
    for (i = 1; i <= HUGE_SIZE; i++) {
        unsigned long key, val;
        Octstr *okey, *oval;
        key = gw_rand();
        val = gw_rand();
        okey = octstr_format("%ld", key);
        oval = octstr_format("%ld", val);
        dict_put(dict, okey, oval);
    }
    gwthread_sleep(5); /* give hash table some time */
    if (dict_key_count(dict) == HUGE_SIZE)
        info(0, "ok, got %d entries in the dictionary.", HUGE_SIZE);
    else
        error(0, "key count is %ld, should be %d.", dict_key_count(dict), HUGE_SIZE);
    dict_destroy(dict);

    gwlib_shutdown();
    return 0;
}
