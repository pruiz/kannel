/*
 * heartbeat.c - thread for sending heartbeat Msgs to bearerbox
 */

#include <signal.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "heartbeat.h"

/*
 * Each running heartbeat gets one of these.  They are collected in
 * the heartbeats List.
 */
struct hb_info {
    hb_send_func_t *send_func;
    double freq;
    hb_load_func_t *load_func;
    long thread;
    volatile sig_atomic_t running;
};

/* List of struct hb_info. */
static List *heartbeats;

/*
 * Look for a hb_info in a list, by thread number.
 */
static int find_hb(void *item, void *pattern)
{
    long *threadnrp;
    struct hb_info *info;

    info = item;
    threadnrp = pattern;

    return info->thread == *threadnrp;
}

static void heartbeat_thread(void *arg)
{
    struct hb_info *info;
    time_t last_hb;

    info = arg;
    last_hb = 0;

    while (info->running) {
        Msg *msg;

        gwthread_sleep(info->freq);

        /*
         * Because the sleep can be interrupted, we might end up sending
         * heartbeats faster than the configured heartbeat frequency.
         * This is not bad unless we send them way too fast.  Make sure
         * our frequency is not more than twice the configured one.
         */
        if (difftime(last_hb, time(NULL)) < info->freq / 2)
            continue;

        msg = msg_create(heartbeat);
        msg->heartbeat.load = info->load_func();
        info->send_func(msg);
        last_hb = time(NULL);
    }
}

long heartbeat_start(hb_send_func_t *send_func, double freq,
                     hb_load_func_t *load_func)
{
    struct hb_info *info;

    info = gw_malloc(sizeof(*info));
    info->send_func = send_func;
    info->freq = freq;
    info->load_func = load_func;
    info->running = 1;
    info->thread = gwthread_create(heartbeat_thread, info);
    if (info->thread >= 0) {
	if (heartbeats == NULL)
	    heartbeats = list_create();
	list_append(heartbeats, info);
        return info->thread;
    } else {
        gw_free(info);
        return -1;
    }
}

void heartbeat_stop(long hb_thread)
{
    List *matching_info;
    struct hb_info *info;

    matching_info = list_extract_matching(heartbeats, &hb_thread, find_hb);
    if (matching_info == NULL) {
        warning(0, "Could not stop heartbeat %ld: not found.", hb_thread);
	return;
    }
    gw_assert(list_len(matching_info) == 1);
    info = list_extract_first(matching_info);
    list_destroy(matching_info, NULL);
 
    info->running = 0;
    gwthread_wakeup(hb_thread);
    gwthread_join(hb_thread);

    gw_free(info);
    if (list_len(heartbeats) == 0) {
	list_destroy(heartbeats, NULL);
	heartbeats = NULL;
    }
}
