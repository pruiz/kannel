/*
 * dlr_oracle.c - Oracle dlr storage implementation.
 *
 * Author: Alexander Malysh <a.malysh@centrium.de>, (C) 2003
 *
 * Copyright: See COPYING file that comes with this distribution
 */

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "dlr_p.h"


#ifdef HAVE_ORACLE

/*
 * Our connection pool to oracle.
 */
static DBPool *pool = NULL;

/*
 * Database fields, which we are use.
 */
static struct dlr_db_fields *fields = NULL;


static long dlr_messages_oracle()
{
    List *result, *row;
    Octstr *sql;
    DBPoolConn *conn;
    long msgs = -1;

    conn = dbpool_conn_consume(pool);
    if (conn == NULL)
        return -1;

    sql = octstr_format("SELECT count(*) FROM %s", octstr_get_cstr(fields->table));
#if defined(DLR_TRACE)
    debug("dlr.oracle", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    if (dbpool_conn_select(conn, sql, &result) != 0) {
        octstr_destroy(sql);
        dbpool_conn_produce(conn);
        return -1;
    }
    dbpool_conn_produce(conn);
    octstr_destroy(sql);

    if (list_len(result) > 0) {
        row = list_extract_first(result);
        msgs = strtol(octstr_get_cstr(list_get(row,0)), NULL, 10);
        list_destroy(row, octstr_destroy_item);
    }
    list_destroy(result, NULL);

    return msgs;
}

static void dlr_shutdown_oracle()
{
    dbpool_destroy(pool);
    dlr_db_fields_destroy(fields);
}

static void dlr_add_oracle(struct dlr_entry *entry)
{
    Octstr *sql;
    DBPoolConn *pconn;

    debug("dlr.oracle", 0, "adding DLR entry into database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL) {
        dlr_entry_destroy(entry);
        return;
    }

    sql = octstr_format("INSERT INTO %s (%s, %s, %s, %s, %s, %s, %s, %s, %s) VALUES "
                        "('%s', '%s', '%s', '%s', '%s', '%s', '%d', '%s', '%d')",
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
    debug("dlr.oracle", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if (dbpool_conn_update(pconn, sql) == -1)
        error(0, "DLR: ORACLE: Error while adding dlr entry for DST<%s>", octstr_get_cstr(entry->destination));

    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
    dlr_entry_destroy(entry);
}

static void dlr_remove_oracle(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql;
    DBPoolConn *pconn;

    debug("dlr.oracle", 0, "removing DLR from database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL)
        return;

    sql = octstr_format("DELETE FROM %s WHERE %s='%s' AND %s='%s' AND ROWNUM < 2",
                        octstr_get_cstr(fields->table), octstr_get_cstr(fields->field_smsc),
                        octstr_get_cstr(smsc), octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

#if defined(DLR_TRACE)
    debug("dlr.oracle", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    if (dbpool_conn_update(pconn, sql) == -1)
        error(0, "DLR: ORACLE: Error while removing dlr entry for DST<%s>", octstr_get_cstr(dst));

    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
}

static struct dlr_entry* dlr_get_oracle(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *sql;
    DBPoolConn *pconn;
    List *result = NULL, *row;
    struct dlr_entry *res = NULL;

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) /* should not happens, but sure is sure */
        return NULL;

    sql = octstr_format("SELECT %s, %s, %s, %s, %s, %s FROM %s WHERE %s='%s' AND %s='%s' AND ROWNUM < 2",
                        octstr_get_cstr(fields->field_mask), octstr_get_cstr(fields->field_serv),
                        octstr_get_cstr(fields->field_url), octstr_get_cstr(fields->field_src),
                        octstr_get_cstr(fields->field_dst), octstr_get_cstr(fields->field_boxc),
                        octstr_get_cstr(fields->table), octstr_get_cstr(fields->field_smsc),
                        octstr_get_cstr(smsc), octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

#if defined(DLR_TRACE)
    debug("dlr.oracle", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if (dbpool_conn_select(pconn, sql, &result) != 0) {
        octstr_destroy(sql);
        dbpool_conn_produce(pconn);
        return NULL;
    }
    octstr_destroy(sql);
    dbpool_conn_produce(pconn);

#define LO2CSTR(r, i) octstr_get_cstr(list_get(r, i))

    if (list_len(result) > 0) {
        row = list_extract_first(result);
        res = dlr_entry_create();
        gw_assert(res != NULL);
        res->mask = atoi(LO2CSTR(row,0));
        res->service = octstr_create(LO2CSTR(row, 1));
        res->url = octstr_create(LO2CSTR(row,2));
        res->source = octstr_create(LO2CSTR(row, 3));
        res->destination = octstr_create(LO2CSTR(row, 4));
        res->boxc_id = octstr_create(LO2CSTR(row, 5));
        list_destroy(row, octstr_destroy_item);
    }
    list_destroy(result, NULL);

#undef LO2CSTR

    return res;
}

static void dlr_update_oracle(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status)
{
    Octstr *sql;
    DBPoolConn *pconn;

    debug("dlr.oracle", 0, "updating DLR status in database");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL)
        return;

    sql = octstr_format("UPDATE %s SET %s=%d WHERE %s='%s' AND %s='%s' ROWNUM < 2",
                        octstr_get_cstr(fields->table),
                        octstr_get_cstr(fields->field_status), status,
                        octstr_get_cstr(fields->field_smsc), octstr_get_cstr(smsc),
                        octstr_get_cstr(fields->field_ts), octstr_get_cstr(ts));

#if defined(DLR_TRACE)
    debug("dlr.oracle", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if (dbpool_conn_update(pconn, sql) == -1)
        error(0, "DLR: ORACLE: Error while updating dlr entry for DST<%s>", octstr_get_cstr(dst));

    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
}

static struct dlr_storage handles = {
    .type = "oracle",
    .dlr_messages = dlr_messages_oracle,
    .dlr_shutdown = dlr_shutdown_oracle,
    .dlr_add = dlr_add_oracle,
    .dlr_get = dlr_get_oracle,
    .dlr_remove = dlr_remove_oracle,
    .dlr_update = dlr_update_oracle
};

struct dlr_storage *dlr_init_oracle(Cfg *cfg)
{
    CfgGroup *grp;
    List *grplist;
    long pool_size;
    DBConf *db_conf = NULL;
    Octstr *id, *username, *password, *tnsname;
    int found;

    if ((grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))) == NULL)
        panic(0, "DLR: ORACLE: group 'dlr-db' is not specified!");

    if (!(id = cfg_get(grp, octstr_imm("id"))))
   	panic(0, "DLR: ORACLE: directive 'id' is not specified!");

    /* initialize database fields */
    fields = dlr_db_fields_create(grp);
    gw_assert(fields != NULL);

    grplist = cfg_get_multi_group(cfg, octstr_imm("oracle-connection"));
    found = 0;
    while (grplist && (grp = list_extract_first(grplist)) != NULL) {
        Octstr *p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, id) == 0) {
            found = 1;
        }
        if (p != NULL) octstr_destroy(p);
        if (found == 1) break;
    }
    list_destroy(grplist, NULL);

    if (found == 0)
        panic(0, "DLR: ORACLE: connection settings for id '%s' are not specified!",
           octstr_get_cstr(id));

    username = cfg_get(grp, octstr_imm("username"));
    password = cfg_get(grp, octstr_imm("password"));
    tnsname = cfg_get(grp, octstr_imm("tnsname"));
    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1)
        pool_size = 1;

    if (username == NULL || password == NULL || tnsname == NULL)
        panic(0, "DLR: ORACLE: connection settings missing for id '%s', please"
                      " check you configuration.",octstr_get_cstr(id));

    /* ok we are ready to create dbpool */
    db_conf = gw_malloc(sizeof(*db_conf));
    db_conf->oracle = gw_malloc(sizeof(OracleConf));

    db_conf->oracle->username = username;
    db_conf->oracle->password = password;
    db_conf->oracle->tnsname = tnsname;

    pool = dbpool_create(DBPOOL_ORACLE, db_conf, pool_size);
    gw_assert(pool != NULL);

    if (dbpool_conn_count(pool) == 0)
        panic(0, "DLR: ORACLE: Couldnot establish oracle connection(s).");

    octstr_destroy(id);

    return &handles;
}
#else
/* no oracle support build in */
struct dlr_storage *dlr_init_oracle(Cfg *cfg)
{
    return NULL;
}
#endif /* HAVE_ORACLE */
