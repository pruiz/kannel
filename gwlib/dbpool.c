/*
 * dbpool.c - implement generic database connection pool
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 *      2003 Initial version.
 * Alexander Malysh <a.malysh@centrium.de>
 *      2003 Made dbpool more generic.
 */

#include "gwlib.h"
#include "dbpool.h"
#include "dbpool_p.h"


#ifdef HAVE_DBPOOL


#include "dbpool_mysql.c"
#include "dbpool_oracle.c"
#include "dbpool_sqlite.c"


static inline void dbpool_conn_destroy(DBPoolConn *conn)
{
    gw_assert(conn != NULL);

    if (conn->conn != NULL)
        conn->pool->db_ops->close(conn->conn);

    gw_free(conn);
}

/*************************************************************************
 * public functions
 */

DBPool *dbpool_create(enum db_type db_type, DBConf *conf, unsigned int connections)
{
    DBPool *p;

    if (conf == NULL)
        return NULL;

    p = gw_malloc(sizeof(DBPool));
    gw_assert(p != NULL);
    p->pool = list_create();
    list_add_producer(p->pool);
    p->max_size = connections;
    p->curr_size = 0;
    p->conf = conf;
    p->db_type = db_type;

    switch(db_type) {
#ifdef HAVE_MYSQL
        case DBPOOL_MYSQL:
            p->db_ops = &mysql_ops;
            break;
#endif
#ifdef HAVE_ORACLE
        case DBPOOL_ORACLE:
            p->db_ops = &oracle_ops;
            break;
#endif
#ifdef HAVE_SQLITE
        case DBPOOL_SQLITE:
            p->db_ops = &sqlite_ops;
            break;
#endif
        case DBPOOL_SDB:
            panic(0, "DBPOOL for libsdb not yet implemented");
        default:
            panic(0, "Unknown dbpool type defined.");
    }

    /*
     * XXX what is todo here if not all connections
     * where established ???
     */
    dbpool_increase(p, connections);

    return p;
}

void dbpool_destroy(DBPool *p)
{

    if (p == NULL)
        return; /* nothing todo here */

    gw_assert(p->pool != NULL && p->db_ops != NULL);

    list_remove_producer(p->pool);
    list_destroy(p->pool, (void*) dbpool_conn_destroy);

    p->db_ops->conf_destroy(p->conf);
    gw_free(p);
}


unsigned int dbpool_increase(DBPool *p, unsigned int count)
{
    unsigned int i, opened = 0;

    gw_assert(p != NULL && p->conf != NULL && p->db_ops != NULL && p->db_ops->open != NULL);


    /* lock dbpool for updates */
    list_lock(p->pool);

    /* ensure we don't increase more items than the max_size border */
    for (i=0; i < count && p->curr_size < p->max_size; i++) {
        void *conn = p->db_ops->open(p->conf);
        if (conn != NULL) {
            DBPoolConn *pc = gw_malloc(sizeof(DBPoolConn));
            gw_assert(pc != NULL);

            pc->conn = conn;
            pc->pool = p;

            p->curr_size++;
            opened++;
            list_produce(p->pool, pc);
        }
    }

    /* unlock dbpool for updates */
    list_unlock(p->pool);

    return opened;
}


unsigned int dbpool_decrease(DBPool *p, unsigned int c)
{
    unsigned int i;

    gw_assert(p != NULL && p->pool != NULL && p->db_ops != NULL && p->db_ops->close != NULL);

    /* lock dbpool for updates */
    list_lock(p->pool);

    /*
     * Ensure we don't try to decrease more then available in pool.
     */
    for (i = 0; i < c; i++) {
        DBPoolConn *pc;

        /* list_extract_first doesn't block even if no conn here */
        pc = list_extract_first(p->pool);

        /* no conn availible anymore */
        if (pc == NULL)
            break;

        /* close connections and destroy pool connection */
        dbpool_conn_destroy(pc);
        p->curr_size--;
    }

    /* unlock dbpool for updates */
    list_unlock(p->pool);

    return i;
}


long dbpool_conn_count(DBPool *p)
{
    gw_assert(p->pool != NULL);

    return list_len(p->pool);
}


DBPoolConn *dbpool_conn_consume(DBPool *p)
{
    DBPoolConn *pc;

    gw_assert(p != NULL && p->pool != NULL);

    /* check if we have any connection, if no return NULL; otherwise we have deadlock */
    if (p->curr_size < 1)
        panic(0, "DBPOOL: Deadlock detected!!!");


    /* garantee that you deliver a valid connection to the caller */
    while ((pc = list_consume(p->pool)) != NULL) {

        /* 
         * XXX check that the connection is still existing.
         * Is this a performance bottle-neck?!
         */
        if (!pc->conn || (p->db_ops->check && p->db_ops->check(pc->conn) != 0)) {
            /* something was wrong, reinitialize the connection */
            /* lock dbpool for update */
            list_lock(p->pool);
            dbpool_conn_destroy(pc);
            p->curr_size--;
            /* unlock dbpool for update */
            list_unlock(p->pool);
            /*
             * maybe not needed, just try to get next connection, but it
             * can be dangeros if all connections where broken, then we will
             * block here for ever.
             */
            dbpool_increase(p, 1);
        } else {
            break;
        }
    }

    return (pc->conn != NULL ? pc : NULL);
}


void dbpool_conn_produce(DBPoolConn *pc)
{
    gw_assert(pc != NULL && pc->conn != NULL && pc->pool != NULL && pc->pool->pool != NULL);

    list_produce(pc->pool->pool, pc);
}


unsigned int dbpool_check(DBPool *p)
{
    long i, len, n = 0, reinit = 0;

    gw_assert(p != NULL && p->pool != NULL && p->db_ops != NULL);

    /*
     * First check if db_ops->check function pointer is here.
     * NOTE: db_ops->check is optional, so if it is not there, then
     * we have nothing todo and we simple return list length.
     */
    if (p->db_ops->check == NULL)
        return list_len(p->pool);

    list_lock(p->pool);
    len = list_len(p->pool);
    for (i = 0; i < len; i++) {
        DBPoolConn *pconn;

        pconn = list_get(p->pool, i);
        if (p->db_ops->check(pconn->conn) != 0) {
            /* something was wrong, reinitialize the connection */
            list_delete(p->pool, i, 1);
            dbpool_conn_destroy(pconn);
            p->curr_size--;
            reinit++;
            len--;
            i--;
        } else {
            n++;
        }
    }
    list_unlock(p->pool);

    /* reinitialize brocken connections */
    if (reinit > 0)
        n += dbpool_increase(p, reinit);


    return n;
}

int inline dbpool_conn_select(DBPoolConn *conn, const Octstr *sql, List **result)
{
    if (sql == NULL || conn == NULL)
        return -1;

    if (conn->pool->db_ops->select == NULL)
        return -1; /* may be panic here ??? */

    return conn->pool->db_ops->select(conn->conn, sql, result);
}

int inline dbpool_conn_update(DBPoolConn *conn, const Octstr *sql)
{
    if (sql == NULL || conn == NULL)
        return -1;

    if (conn->pool->db_ops->update == NULL)
        return -1; /* may be panic here ??? */

    return conn->pool->db_ops->update(conn->conn, sql);
}
#endif /* HAVE_DBPOOL */
