/*
 * gw/dlr_mem.c
 *
 * Implementation of handling delivery reports (DLRs)
 * in memory
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <tolj@wapme-systems.de>, 22.03.2002
 * Alexander Malysh <a.malysh@centrium.de> 2003
 */

#include "gwlib/gwlib.h"
#include "dlr_p.h"

/*
 * This is the global list where all messages being sent out are being kept track
 * of his list is looked up once a delivery report comes in
 */
static List *dlr_waiting_list;

/*
 * Destroy dlr_waiting_list.
 */
static void dlr_mem_shutdown()
{
    list_destroy(dlr_waiting_list, (list_item_destructor_t *)dlr_entry_destroy);
}

/*
 * Get count of dlr messages waiting.
 */
static long dlr_mem_messages(void)
{
    return list_len(dlr_waiting_list);
}

static void dlr_mem_flush(void)
{
    long i;
    long len;

    len = list_len(dlr_waiting_list);
    for (i=0; i < len; i++)
        list_delete(dlr_waiting_list, i, 1);
}

/*
 * add struct dlr_entry to list
 */
static void dlr_mem_add(struct dlr_entry *dlr)
{
    list_append(dlr_waiting_list,dlr);
}

/*
 * Private compare function
 * Return 0 if entry match and 1 if not.
 */
static int dlr_mem_entry_match(struct dlr_entry *dlr, const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    /* XXX: check destination addr too, because e.g. for UCP is not enough to check only
     *          smsc and timestamp (timestamp is even without milliseconds)
     */
    if(octstr_compare(dlr->smsc,smsc) == 0 && octstr_compare(dlr->timestamp,ts) == 0)
        return 0;

    return 1;
}

/*
 * Find matching entry and return copy of it, otherwise NULL
 */
static struct dlr_entry *dlr_mem_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    long i;
    long len;
    struct dlr_entry *dlr = NULL, *ret = NULL;

    list_lock(dlr_waiting_list);
    len = list_len(dlr_waiting_list);
    for (i=0; i < len; i++) {
        dlr = list_get(dlr_waiting_list, i);

        if (dlr_mem_entry_match(dlr, smsc, ts, dst) == 0) {
            ret = dlr_entry_duplicate(dlr);
            break;
        }
    }
    list_unlock(dlr_waiting_list);

    /* we couldnt find a matching entry */
    return ret;
}

/*
 * Remove matching entry
 */
static void dlr_mem_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    long i;
    long len;
    struct dlr_entry *dlr = NULL;

    list_lock(dlr_waiting_list);
    len = list_len(dlr_waiting_list);
    for (i=0; i < len; i++) {
        dlr = list_get(dlr_waiting_list, i);

        if (dlr_mem_entry_match(dlr, smsc, ts, dst) == 0) {
            list_delete(dlr_waiting_list, i, 1);
            break;
        }
    }
    list_unlock(dlr_waiting_list);
}

static struct dlr_storage  handles = {
    .type = "internal",
    .dlr_add = dlr_mem_add,
    .dlr_get = dlr_mem_get,
    .dlr_remove = dlr_mem_remove,
    .dlr_shutdown = dlr_mem_shutdown,
    .dlr_messages = dlr_mem_messages,
    .dlr_flush = dlr_mem_flush
};

/*
 * Initialize dlr_waiting_list and return out storage handles.
 */
struct dlr_storage *dlr_init_mem(Cfg *cfg)
{
    dlr_waiting_list = list_create();

    return &handles;
}
