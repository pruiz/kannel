/*
 * check_ipcheck.c - check the is_allowed_ip function
 *
 * Lars Wirzenius
 */


#include "gwlib/gwlib.h"


int main(void)
{
    Octstr *ip;
    Octstr *allowed;
    Octstr *denied;
    int result;
    int i;
    static struct {
	char *allowed;
	char *denied;
	char *ip;
	int should_be_allowed;
    } tab[] = {
	{ "127.0.0.1", "", "127.0.0.1", 1 },
	{ "127.0.0.1", "", "127.0.0.2", 1 },
	{ "127.0.0.1", "*.*.*.*", "127.0.0.1", 1 },
	{ "127.0.0.1", "*.*.*.*", "1.2.3.4", 0 },
	{ "127.0.0.1", "127.0.0.*", "1.2.3.4", 1 },
	{ "127.0.0.1", "127.0.0.*", "127.0.0.2", 0 },
    };
    
    gwlib_init();
    log_set_output_level(GW_INFO);
        
    for (i = 0; (size_t) i < sizeof(tab) / sizeof(tab[0]); ++i) {
	allowed = octstr_imm(tab[i].allowed);
	denied = octstr_imm(tab[i].denied);
	ip = octstr_imm(tab[i].ip);
	result = is_allowed_ip(allowed, denied, ip);
	if (!!result != !!tab[i].should_be_allowed) {
	    panic(0, "is_allowed_ip did not work for "
	    	     "allowed=<%s> denied=<%s> ip=<%s>, "
		     "returned %d should be %d",
		     octstr_get_cstr(allowed),
		     octstr_get_cstr(denied),
		     octstr_get_cstr(ip),
		     result,
		     tab[i].should_be_allowed);
	}
    }

    gwlib_shutdown();
    return 0;
}
