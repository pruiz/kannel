/*
 * dlr_mysql.c
 *
 * Implementation of handling delivery reports (DLRs)
 * for MySql database
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <tolj@wapme-systems.de>, 22.03.2002
 * Alexander Malysh <a.malysh@centrium.de> 2003
*/

#include "gwlib/gwlib.h"
#include "dlr_p.h"


#ifdef DLR_MYSQL
#include <mysql/mysql.h>
static MYSQL *connection;
static MYSQL mysql;

/*
 * Database fields, which we are use.
 */
static struct dlr_db_fields *fields = NULL;

/*
 * Mutex to protec access to mysql.
 */
static Mutex *dlr_mutex = NULL;


static void dlr_mysql_shutdown()
{
    mysql_close(connection);
    dlr_db_fields_destroy(fields);
    mutex_destroy(dlr_mutex);
}

static void dlr_mysql_add(struct dlr_entry *entry)
{
    Octstr *sql;
    int	state;

    sql = octstr_format("INSERT INTO %s (%s, %s, %s, %s, %s, %s, %s, %s, %s) VALUES "
                        "('%s', '%s', '%s', '%s', '%s', '%s', '%d', '%s', '%d');",
                        octstr_get_cstr(fields->table), octstr_get_cstr(fields->field_smsc),
                        octstr_get_cstr(fields->field_ts),
                        octstr_get_cstr(fields->field_src), octstr_get_cstr(fields->field_dst),
                        octstr_get_cstr(fields->field_serv), octstr_get_cstr(fields->field_url),
                        octstr_get_cstr(fields->field_mask), octstr_get_cstr(fields->field_boxc),
                        octstr_get_cstr(fields->field_status),
                        octstr_get_cstr(entry->smsc), octstr_get_cstr(entry->timestamp), octstr_get_cstr(entry->source),
                        octstr_get_cstr(entry->destination), octstr_get_cstr(entry->service), octstr_get_cstr(entry->url),
                        entry->mask, octstr_get_cstr(entry->boxc_id), 0);

#if defined(DLR_TRACE)
     debug("dlr.mysql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    mutex_lock(dlr_mutex);

    state = mysql_query(connection, octstr_get_cstr(sql));
    if (state != 0)
        error(0, "MYSQL: %s", mysql_error(connection));

    mutex_unlock(dlr_mutex);
    octstr_destroy(sql);
    dlr_entry_destroy(entry);
}

static struct dlr_entry* dlr_mysql_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    struct dlr_entry *res = NULL;
    Octstr *sql;
    int	state;
    MYSQL_RES *result;
    MYSQL_ROW row;

    sql = octstr_format("SELECT %s, %s, %s, %s, %s, %s FROM %s WHERE %s='%s' AND %s='%s';",
                        octstr_get_cstr(fields->field_mask), octstr_get_cstr(fields->field_serv),
                        octstr_get_cstr(fields->field_url), octstr_get_cstr(fields->field_src),
                        octstr_get_cstr(fields->field_dst), octstr_get_cstr(fields->field_boxc),
                        octstr_get_cstr(fields->table), octstr_get_cstr(fields->field_smsc),
                        octstr_get_cstr(smsc), octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

#if defined(DLR_TRACE)
    debug("dlr.mysql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    mutex_lock(dlr_mutex);
    state = mysql_query(connection, octstr_get_cstr(sql));
    octstr_destroy(sql);
    if (state != 0) {
        error(0, "MYSQL: %s", mysql_error(connection));
        mutex_unlock(dlr_mutex);
        return NULL;
    }
    result = mysql_store_result(connection);
    mutex_unlock(dlr_mutex);
    if (mysql_num_rows(result) < 1) {
        debug("dlr.mysql", 0, "no rows found");
        mysql_free_result(result);
        return NULL;
    }
    row = mysql_fetch_row(result);
    if (!row) {
        debug("dlr.mysql", 0, "rows found but could not load them");
        mysql_free_result(result);
        return NULL;
    }

    debug("dlr.mysql", 0, "Found entry, row[0]=%s, row[1]=%s, row[2]=%s, row[3]=%s, row[4]=%s row[5]=%s",
          row[0], row[1], row[2], (row[3] ? row[3] : "NULL"), (row[4] ? row[4] : "NULL"), (row[5] ? row[5] : "NULL"));

    res = dlr_entry_create();
    gw_assert(res != NULL);
    res->mask = atoi(row[0]);
    res->service = octstr_create(row[1]);
    res->url = octstr_create(row[2]);
    res->source = row[3] ? octstr_create(row[3]) : octstr_create("");
    res->destination = row[4] ? octstr_create(row[4]) : octstr_create("");
    res->boxc_id = row[5] ? octstr_create(row[5]) : octstr_create("");

    mysql_free_result(result);

    return res;
}

static void dlr_mysql_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql;
    int	state;

    debug("dlr.mysql", 0, "removing DLR from database");
    sql = octstr_format("DELETE FROM %s WHERE %s='%s' AND %s='%s' LIMIT 1;",
                        octstr_get_cstr(fields->table), octstr_get_cstr(fields->field_smsc),
                        octstr_get_cstr(smsc), octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

#if defined(DLR_TRACE)
    debug("dlr.mysql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    mutex_lock(dlr_mutex);
    state = mysql_query(connection, octstr_get_cstr(sql));
    octstr_destroy(sql);
    if (state != 0) {
        error(0, "MYSQL: %s", mysql_error(connection));
    }
    mutex_unlock(dlr_mutex);
}

static void dlr_mysql_update(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status)
{
    Octstr *sql;
    int	state;

    debug("dlr.mysql", 0, "updating DLR status in database");
    sql = octstr_format("UPDATE %s SET %s=%d WHERE %s='%s' AND %s='%s' LIMIT 1;",
                        octstr_get_cstr(fields->table),
                        octstr_get_cstr(fields->field_status), status,
                        octstr_get_cstr(fields->field_smsc), octstr_get_cstr(smsc),
                        octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

#if defined(DLR_TRACE)
    debug("dlr.mysql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    mutex_lock(dlr_mutex);
    state = mysql_query(connection, octstr_get_cstr(sql));
    octstr_destroy(sql);
    if (state != 0) {
        error(0, "MYSQL: %s", mysql_error(connection));
    }
    mutex_unlock(dlr_mutex);
}


static long dlr_mysql_messages(void)
{
    Octstr *sql;
    int	state;
    long res;
    MYSQL_RES *result;
    MYSQL_ROW row;

    sql = octstr_format("SELECT count(*) FROM %s;", octstr_get_cstr(fields->table));
#if defined(DLR_TRACE)
    debug("dlr.mysql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    mutex_lock(dlr_mutex);
    state = mysql_query(connection, octstr_get_cstr(sql));
    octstr_destroy(sql);
    if (state != 0) {
        error(0, "MYSQL: %s", mysql_error(connection));
        mutex_unlock(dlr_mutex);
        return -1;
    }
    result = mysql_store_result(connection);
    mutex_unlock(dlr_mutex);
    if (mysql_num_rows(result) < 1) {
        debug("dlr.mysql", 0, "Could not get count of DLR table");
        mysql_free_result(result);
        return 0;
    }
    row = mysql_fetch_row(result);
    if (row == NULL) {
        debug("dlr.mysql", 0, "rows found but could not load them");
        mysql_free_result(result);
        return 0;
    }
    res = atol(row[0]);
    mysql_free_result(result);
    return res;
}

static void dlr_mysql_flush(void)
{
        Octstr *sql;
        int	state;
        MYSQL_RES *result;

        sql = octstr_format("DELETE FROM %s;", octstr_get_cstr(fields->table));
        mutex_lock(dlr_mutex);
        state = mysql_query(connection, octstr_get_cstr(sql));
        octstr_destroy(sql);
        if (state != 0) {
            error(0, "MYSQL: %s", mysql_error(connection));
        }
        result = mysql_store_result(connection);
        mutex_unlock(dlr_mutex);
        mysql_free_result(result);
}

static struct dlr_storage  handles = {
    .type = "mysql",
    .dlr_add = dlr_mysql_add,
    .dlr_get = dlr_mysql_get,
    .dlr_update = dlr_mysql_update,
    .dlr_remove = dlr_mysql_remove,
    .dlr_shutdown = dlr_mysql_shutdown,
    .dlr_messages = dlr_mysql_messages,
    .dlr_flush = dlr_mysql_flush
};

struct dlr_storage *dlr_init_mysql(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *mysql_host, *mysql_user, *mysql_pass, *mysql_db, *mysql_id;
    Octstr *p = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the used MySQL table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))))
        panic(0, "DLR: MySQL: group 'dlr-db' is not specified!");

    if (!(mysql_id = cfg_get(grp, octstr_imm("id"))))
   	    panic(0, "DLR: MySQL: directive 'id' is not specified!");

    fields = dlr_db_fields_create(grp);
    gw_assert(fields != NULL);

    /*
     * now grap the required information from the 'mysql-connection' group
     * with the mysql-id we just obtained
     *
     * we have to loop through all available MySQL connection definitions
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("mysql-connection"));
     while (grplist && (grp = list_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, mysql_id) == 0) {
            goto found;
        }
        if (p != NULL) octstr_destroy(p);
     }
     panic(0, "DLR: MySQL: connection settings for id '%s' are not specified!",
           octstr_get_cstr(mysql_id));

found:
    octstr_destroy(p);
    list_destroy(grplist, NULL);

    if (!(mysql_host = cfg_get(grp, octstr_imm("host"))))
   	    panic(0, "DLR: MySQL: directive 'host' is not specified!");
    if (!(mysql_user = cfg_get(grp, octstr_imm("mysql-username"))))
   	    panic(0, "DLR: MySQL: directive 'mysql-username' is not specified!");
    if (!(mysql_pass = cfg_get(grp, octstr_imm("mysql-password"))))
   	    panic(0, "DLR: MySQL: directive 'mysql-password' is not specified!");
    if (!(mysql_db = cfg_get(grp, octstr_imm("database"))))
   	    panic(0, "DLR: MySQL: directive 'database' is not specified!");

    /*
     * ok, ready to connect to MySQL
     */
    mysql_init(&mysql);
    connection = mysql_real_connect(&mysql,
   	                octstr_get_cstr(mysql_host), octstr_get_cstr(mysql_user),
                    octstr_get_cstr(mysql_pass), octstr_get_cstr(mysql_db),
                    0, NULL, 0);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (connection == NULL) {
        error(0,"DLR: MySQL: can not connect to database!");
        panic(0,"MYSQL: %s", mysql_error(&mysql));
    } else {
        info(0,"Connected to mysql server at %s.", octstr_get_cstr(mysql_host));
        info(0,"MYSQL: server version %s, client version %s.",
             mysql_get_server_info(&mysql), mysql_get_client_info());
    }

    dlr_mutex = mutex_create();

    octstr_destroy(mysql_db);
    octstr_destroy(mysql_host);
    octstr_destroy(mysql_user);
    octstr_destroy(mysql_pass);
    octstr_destroy(mysql_id);

    return &handles;
}
#else
/*
 * Return NULL , so we point dlr-core that we were
 * not compiled in.
 */
struct dlr_storage *dlr_init_mysql(Cfg* cfg)
{
    return NULL;
}
#endif /* DLR_MYSQL */

