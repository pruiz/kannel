/*
 * dbpool_mysql.c - implement MySQL operations for generic database connection pool
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 *      2003 Initial version.
 * Alexander Malysh <a.malysh@centrium.de>
 *      2003 Made dbpool more generic.
 */

#ifdef HAVE_MYSQL
#include <mysql.h>

static void* mysql_open_conn(const DBConf *db_conf)
{
    MYSQL *mysql = NULL;
    MySQLConf *conf = db_conf->mysql; /* make compiler happy */

    /* sanity check */
    if (conf == NULL)
        return NULL;

     /* pre-allocate */
    mysql = gw_malloc(sizeof(MYSQL));
    gw_assert(mysql != NULL);

    /* initialize mysql structures */
    if (!mysql_init(mysql)) {
        error(0, "MYSQL: init failed!");
        error(0, "MYSQL: %s", mysql_error(mysql));
        goto failed;
    }

    if (!mysql_real_connect(mysql, octstr_get_cstr(conf->host),
                            octstr_get_cstr(conf->username),
                            octstr_get_cstr(conf->password),
                            octstr_get_cstr(conf->database), 0, NULL, 0)) {
        error(0, "MYSQL: can not connect to database!");
        error(0, "MYSQL: %s", mysql_error(mysql));
        goto failed;
    }

    info(0,"MYSQL: Connected to server at %s.", octstr_get_cstr(conf->host));
    info(0, "MYSQL: server version %s, client version %s.",
                  mysql_get_server_info(mysql), mysql_get_client_info());

    return mysql;

failed:
    if (mysql != NULL) gw_free(mysql);
    return NULL;
}

static void mysql_close_conn(void *conn)
{
    if (conn == NULL)
        return;

    mysql_close((MYSQL*) conn);
    gw_free(conn);
}

static int mysql_check_conn(void *conn)
{
    if (conn == NULL)
        return -1;

    if (mysql_ping((MYSQL*) conn)) {
        error(0, "MYSQL: database check failed!");
        error(0, "MYSQL: %s", mysql_error(conn));
        return -1;
    }

    return 0;
}

static void mysql_conf_destroy(DBConf *db_conf)
{
    MySQLConf *conf = db_conf->mysql;

    octstr_destroy(conf->host);
    octstr_destroy(conf->username);
    octstr_destroy(conf->password);
    octstr_destroy(conf->database);

    gw_free(conf);
    gw_free(db_conf);
}

static struct db_ops mysql_ops = {
    .open = mysql_open_conn,
    .close = mysql_close_conn,
    .check = mysql_check_conn,
    .conf_destroy = mysql_conf_destroy
};

#endif /* HAVE_MYSQL */

