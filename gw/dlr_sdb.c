/*
 * dlr_sdb.c
 *
 * Implementation of handling delivery reports (DLRs)
 * for LibSDB.
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <tolj@wapme-systems.de>, 22.03.2002
 * Alexander Malysh <a.malysh@centrium.de> 2003
*/

#include "gwlib/gwlib.h"
#include "dlr_p.h"

#ifdef DLR_SDB
#include <sdb.h>
static char *connection;

/*
 * Database fields, which we are use.
 */
static struct dlr_db_fields *fields = NULL;

/*
 * Mutex to protec access to database.
 */
static Mutex *dlr_mutex = NULL;


static void dlr_sdb_shutdown()
{
    sdb_close(connection);
    dlr_db_fields_destroy(fields);
    mutex_destroy(dlr_mutex);
}

static void dlr_add_sdb(struct dlr_entry *dlr)
{
    Octstr *sql;
    int	state;

    sql = octstr_format("INSERT INTO %s (%s, %s, %s, %s, %s, %s, %s, %s, %s) VALUES "
                        "('%s', '%s', '%s', '%s', '%s', '%s', '%d', '%s', '%d')",
		                octstr_get_cstr(fields->table), octstr_get_cstr(fields->field_smsc),
                        octstr_get_cstr(fields->field_ts),
                        octstr_get_cstr(fields->field_src), octstr_get_cstr(fields->field_dst),
                        octstr_get_cstr(fields->field_serv), octstr_get_cstr(fields->field_url),
                        octstr_get_cstr(fields->field_mask), octstr_get_cstr(fields->field_boxc),
                        octstr_get_cstr(fields->field_status),
                        octstr_get_cstr(dlr->smsc), octstr_get_cstr(dlr->timestamp),
                        octstr_get_cstr(dlr->source), octstr_get_cstr(dlr->destination),
                        octstr_get_cstr(dlr->service), octstr_get_cstr(dlr->url), mask,
                        octstr_get_cstr(dlr->boxc_id), 0);

    mutex_lock(dlr_mutex);
    state = sdb_query(connection, octstr_get_cstr(sql), NULL, NULL);
    mutex_unlock(dlr_mutex);
    if (state == -1)
        error(0, "SDB: error in inserting DLR");

    octstr_destroy(sql);
    dlr_entry_destroy(dlr);
}

static int sdb_callback_add(int n, char **p, void *row)
{
	if (!n) {
        debug("dlr.sdb", 0, "no rows found");
        return 0;
    }

    /* strip string into words */
    row = octstr_split(octstr_imm(p[0]), octstr_imm(" "));
    if (list_len(row) != 6) {
        debug("dlr.sdb", 0, "Row has wrong length %ld", list_len(row));
        return 0;
    }

	return 0;
}

static int sdb_callback_msgs(int n, char **p, void *row)
{}

static struct dlr_entry*  dlr_sdb_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql;
    int	state;
    List *row;
    struct dlr_entry *res = NULL;

    sql = octstr_format("SELECT %s, %s, %s, %s, %s FROM %s WHERE %s='%s' AND %s='%s'",
                        octstr_get_cstr(fields->field_mask), octstr_get_cstr(fields->field_serv),
                        octstr_get_cstr(fields->field_url), octstr_get_cstr(fields->field_src),
                        octstr_get_cstr(fields->field_dst), octstr_get_cstr(fields->field_boxc),
                        octstr_get_cstr(fields->table),
                        octstr_get_cstr(fields->field_smsc), octstr_get_cstr(smsc),
                        octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

#if defined(DLR_TRACE)
     debug("dlr.sdb", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    mutex_lock(dlr_mutex);
    state = sdb_query(connection, octstr_get_cstr(sql), sdb_callback_add, row);
    mutex_unlock(dlr_mutex);
    octstr_destroy(sql);
    if (state == -1) {
        error(0, "SDB: error in finding DLR");
        return NULL;
    }

    debug("dlr.sdb", 0, "Found entry, row[0]=%s, row[1]=%s, row[2]=%s, row[3]=%s row[4]=%s row[5]=%s",
          octstr_get_cstr(list_get(row, 0)),
          octstr_get_cstr(list_get(row, 1)),
          octstr_get_cstr(list_get(row, 2)),
          octstr_get_cstr(list_get(row, 3)),
          octstr_get_cstr(list_get(row, 4)),
          octstr_get_cstr(list_get(row, 5)));

    res = dlr_entry_create();
    gw_assert(res);
    res->dlr_mask = atoi(octstr_get_cstr(list_get(row, 0)));
    res->dlr_service = octstr_duplicate(list_get(row, 1));
    res->dlr_url = octstr_duplicate(list_get(row, 2));
    res->source = octstr_duplicate(list_get(row, 3));
    res->destination = octstr_duplicate(list_get(row, 4));
    res->boxc_id = octstr_duplicate(list_get(row, 5));

    list_destroy(row, octstr_destroy_item);

    return res;
}

static void  dlr_sdb_update(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql;
    int	state;

    debug("dlr.sdb", 0, "updating DLR status in database");
    sql = octstr_format("UPDATE %s SET %s=%d WHERE %s='%s' AND %s='%s'",
                        octstr_get_cstr(fields->table),
                        octstr_get_cstr(fields->field_status), typ,
                        octstr_get_cstr(fields->field_smsc), octstr_get_cstr(smsc),
                        octstr_get_cstr(field_ts), octstr_get_cstr(ts));

#if defined(DLR_TRACE)
     debug("dlr.sdb", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    mutex_lock(dlr_mutex);
    state = sdb_query(connection, octstr_get_cstr(sql), NULL, NULL);
    mutex_unlock(dlr_mutex);
    octstr_destroy(sql);
    if (state == -1) {
        error(0, "SDB: error in updating DLR");
    }
}

static void  dlr_sdb_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql;
    int	state;

    debug("dlr.sdb", 0, "removing DLR from database");
    sql = octstr_format("DELETE FROM %s WHERE %s='%s' AND %s='%s' LIMIT 1",
                        octstr_get_cstr(fields->table),
                        octstr_get_cstr(fields->field_smsc), octstr_get_cstr(smsc),
                        octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

#if defined(DLR_TRACE)
     debug("dlr.sdb", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    mutex_lock(dlr_mutex);
    state = sdb_query(connection, octstr_get_cstr(sql), NULL, NULL);
    mutex_unlock(dlr_mutex);
    octstr_destroy(sql);
    if (state == -1)
        error(0, "SDB: error in deleting DLR");
}

static long dlr_sdb_messages(void)
{
    Octstr *sql;
    int	state;
    long res;

     /*
      * XXXX select * ... is not efficient.
     * Please use here SELECT count(*) ... , see mysql for example.
     */
    sql = octstr_format("SELECT * FROM %s", octstr_get_cstr(table));

#if defined(DLR_TRACE)
    debug("dlr.sdb", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    mutex_lock(dlr_mutex);
    state = sdb_query(connection, octstr_get_cstr(sql), sdb_callback_msgs, NULL);
    octstr_destroy(sql);
    if (state == -1) {
        error(0, "SDB: error in selecting ammount of waiting DLRs");
        mutex_unlock(dlr_mutex);
        return -1;
    }
    res = (long) state;
    mutex_unlock(dlr_mutex);

    return res;
}

static void dlr_sdb_flush(void)
{
    Octstr *sql;
    int	state;

    sql = octstr_format("DELETE FROM %s", octstr_get_cstr(table));

#if defined(DLR_TRACE)
     debug("dlr.sdb", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    mutex_lock(dlr_mutex);
    state = sdb_query(connection, octstr_get_cstr(sql), NULL, NULL);
    octstr_destroy(sql);
    if (state == -1) {
        error(0, "SDB: error in flusing DLR table");
    }
    mutex_unlock(dlr_mutex);
}


static struct dlr_storage  handles = {
    .type = "sdb",
    .dlr_add = dlr_sdb_add,
    .dlr_get = dlr_sdb_get,
    .dlr_update = dlr_sdb_update,
    .dlr_remove = dlr_sdb_remove,
    .dlr_shutdown = dlr_sdb_shutdown,
    .dlr_messages = dlr_sdb_messages,
    .dlr_flush = dlr_sdb_flush
};

static struct dlr_storage *dlr_init_sdb(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *sdb_url, *sdb_id;
    Octstr *p = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the used table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))))
        panic(0, "DLR: SDB: group 'dlr-db' is not specified!");

    if (!(sdb_id = cfg_get(grp, octstr_imm("id"))))
   	    panic(0, "DLR: SDB: directive 'id' is not specified!");

    fields = dlr_db_fields_create(grp);
    gw_assert(fields != NULL);
    dlr_mutex = mutex_create();

    /*
     * now grap the required information from the 'mysql-connection' group
     * with the sdb-id we just obtained
     *
     * we have to loop through all available SDB connection definitions
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("sdb-connection"));
     while (grplist && (grp = list_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, sdb_id) == 0) {
            goto found;
        }
     }
     panic(0, "DLR: SDB: connection settings for id '%s' are not specified!",
           octstr_get_cstr(sdb_id));

found:
    octstr_destroy(p);
    list_destroy(grplist, NULL);

    if (!(sdb_url = cfg_get(grp, octstr_imm("url"))))
   	    panic(0, "DLR: SDB: directive 'url' is not specified!");

    /*
     * ok, ready to connect
     */
    info(0,"Connecting to sdb resource <%s>.", octstr_get_cstr(sdb_url));
    connection = sdb_open(octstr_get_cstr(sdb_url));
    if (connection == NULL)
        panic(0, "Could not connect to database");

    octstr_destroy(sdb_url);

    return &handles;
}
#else
/*
 * Return NULL , so we point dlr-core that we were
 * not compiled in.
 */
struct dlr_storage *dlr_init_sdb(Cfg* cfg)
{
    return NULL;
}
#endif /* DLR_SDB */
