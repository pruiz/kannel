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
#include "gwlib/dbpool.h"
#include "dlr_p.h"


#ifdef DLR_MYSQL
#include <mysql/mysql.h>

/*
 * Our connection pool to mysql.
 */
static DBPool *pool = NULL;

/*
 * Database fields, which we are use.
 */
static struct dlr_db_fields *fields = NULL;


static void mysql_update(const Octstr *sql)
{
    int	state;
    DBPoolConn *pc;

#if defined(DLR_TRACE)
     debug("dlr.mysql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "MYSQL: Database pool got no connection! DB update failed!");
        return;
    }

    state = mysql_query(pc->conn, octstr_get_cstr(sql));
    if (state != 0)
        error(0, "MYSQL: %s", mysql_error(pc->conn));

    dbpool_conn_produce(pc);
}

static MYSQL_RES* mysql_select(const Octstr *sql)
{
    int	state;
    MYSQL_RES *result = NULL;
    DBPoolConn *pc;

#if defined(DLR_TRACE)
    debug("dlr.mysql", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "MYSQL: Database pool got no connection! DB update failed!");
        return NULL;
    }

    state = mysql_query(pc->conn, octstr_get_cstr(sql));
    if (state != 0) {
        error(0, "MYSQL: %s", mysql_error(pc->conn));
    } else {
        result = mysql_store_result(pc->conn);
    }

    dbpool_conn_produce(pc);

    return result;
}

static void dlr_mysql_shutdown()
{
    dbpool_destroy(pool);
    dlr_db_fields_destroy(fields);
}

static void dlr_mysql_add(struct dlr_entry *entry)
{
    Octstr *sql;

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


    mysql_update(sql);

    octstr_destroy(sql);
    dlr_entry_destroy(entry);
}

static struct dlr_entry* dlr_mysql_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    struct dlr_entry *res = NULL;
    Octstr *sql;
    MYSQL_RES *result;
    MYSQL_ROW row;

    sql = octstr_format("SELECT %s, %s, %s, %s, %s, %s FROM %s WHERE %s='%s' AND %s='%s';",
                        octstr_get_cstr(fields->field_mask), octstr_get_cstr(fields->field_serv),
                        octstr_get_cstr(fields->field_url), octstr_get_cstr(fields->field_src),
                        octstr_get_cstr(fields->field_dst), octstr_get_cstr(fields->field_boxc),
                        octstr_get_cstr(fields->table), octstr_get_cstr(fields->field_smsc),
                        octstr_get_cstr(smsc), octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));


    result = mysql_select(sql);
    octstr_destroy(sql);

    if (result == NULL) {
        return NULL;
    }
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
    res->smsc = octstr_duplicate(smsc);

    mysql_free_result(result);

    return res;
}

static void dlr_mysql_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql;

    debug("dlr.mysql", 0, "removing DLR from database");
    sql = octstr_format("DELETE FROM %s WHERE %s='%s' AND %s='%s' LIMIT 1;",
                        octstr_get_cstr(fields->table), octstr_get_cstr(fields->field_smsc),
                        octstr_get_cstr(smsc), octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));


    mysql_update(sql);

    octstr_destroy(sql);
}

static void dlr_mysql_update(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status)
{
    Octstr *sql;

    debug("dlr.mysql", 0, "updating DLR status in database");
    sql = octstr_format("UPDATE %s SET %s=%d WHERE %s='%s' AND %s='%s' LIMIT 1;",
                        octstr_get_cstr(fields->table),
                        octstr_get_cstr(fields->field_status), status,
                        octstr_get_cstr(fields->field_smsc), octstr_get_cstr(smsc),
                        octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

    mysql_update(sql);

    octstr_destroy(sql);
}


static long dlr_mysql_messages(void)
{
    Octstr *sql;
    long res;
    MYSQL_RES *result;
    MYSQL_ROW row;

    sql = octstr_format("SELECT count(*) FROM %s;", octstr_get_cstr(fields->table));

    result = mysql_select(sql);
    octstr_destroy(sql);

    if (result == NULL) {
        return -1;
    }
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

    sql = octstr_format("DELETE FROM %s;", octstr_get_cstr(fields->table));

    mysql_update(sql);
    octstr_destroy(sql);
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
    long pool_size;
    DBConf *db_conf = NULL;

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

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

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
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->mysql = gw_malloc(sizeof(MySQLConf));
    gw_assert(db_conf->mysql != NULL);

    db_conf->mysql->host = mysql_host;
    db_conf->mysql->username = mysql_user;
    db_conf->mysql->password = mysql_pass;
    db_conf->mysql->database = mysql_db;

    pool = dbpool_create(DBPOOL_MYSQL, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (dbpool_conn_count(pool) == 0)
        panic(0,"DLR: MySQL: database pool has no connections!");

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

