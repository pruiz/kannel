#include <unistd.h>

#include "gwlib/gwlib.h"

static void thread1(void *arg) {
	debug("test", 0, "Sleeping");
	gwthread_sleep(600);
	debug("test", 0, "Woke up");
}

int main(void) {
	gwlib_init();
	gwthread_create(thread1, NULL);
	sleep(1);
	gwthread_wakeup_all();
	gwthread_join_all();
	gwlib_shutdown();
    	return 0;
}
