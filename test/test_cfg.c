#include "gwlib/gwlib.h"

int main(int argc, char **argv)
{
    Cfg *cfg;
    int ret;
    Octstr *name;
    int i;
    
    gwlib_init();

    for (i = 1; i < argc; ++i) {
        name = octstr_create(argv[i]);
        cfg = cfg_create(name);
        octstr_destroy(name);
        ret = cfg_read(cfg);
        info(0, "cfg_read returned %d", ret);
        if (ret == 0)
            cfg_dump(cfg);
        cfg_destroy(cfg);
    }
    
    info(0, "Shutting down.");
    gwlib_shutdown();

    return (ret == 0 ? 0 : 1);
}
