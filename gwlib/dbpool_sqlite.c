/*
 * dbpool_sqlite.c - implement SQLite operations for generic database connection pool
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#ifdef HAVE_SQLITE
#include <sqlite.h>

static void *sqlite_open_conn(const DBConf *db_conf)
{
    sqlite *db = NULL;
    SQLiteConf *conf = db_conf->sqlite; /* make compiler happy */
    char *errmsg = 0;

    /* sanity check */
    if (conf == NULL)
        return NULL;

    if ((db = sqlite_open(octstr_get_cstr(conf->file), 0, &errmsg)) == NULL) {
        error(0, "SQLite: can not open database file `%s'!", 
              octstr_get_cstr(conf->file));
        error(0, "SQLite: %s", errmsg);
        goto failed;
    }

    info(0, "SQLite: Opened database file `%s'.", octstr_get_cstr(conf->file));
    info(0, "SQLite: library version %s.", sqlite_version);

    return db;

failed:
    return NULL;
}

static void sqlite_close_conn(void *conn)
{
    if (conn == NULL)
        return;

    sqlite_close((sqlite*) conn);
    gw_free(conn);
}

static int sqlite_check_conn(void *conn)
{
    if (conn == NULL)
        return -1;

    /* XXX there is no such construct in SQLite?! */

    return 0;
}

static void sqlite_conf_destroy(DBConf *db_conf)
{
    SQLiteConf *conf = db_conf->sqlite;

    octstr_destroy(conf->file);

    gw_free(conf);
    gw_free(db_conf);
}

static struct db_ops sqlite_ops = {
    .open = sqlite_open_conn,
    .close = sqlite_close_conn,
    .check = sqlite_check_conn,
    .conf_destroy = sqlite_conf_destroy
};

#endif /* HAVE_SQLITE */

