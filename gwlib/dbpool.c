/*
 * dbpool.c - implement database connection pool
 */

#include "gwlib.h"

#include "dbpool.h"

#ifdef HAVE_MYSQL
#include <mysql.h>
#endif

#ifdef HAVE_DBPOOL

typedef struct {
    Octstr *host;
    Octstr *username;
    Octstr *password;
    Octstr *database;
} MySQLConf;

typedef struct {
    Octstr *url;
} SDBConf;


struct DBPool
{
    List *pool; /* queue representing the pool */
    enum dbpool_type type;
    unsigned int max_size;
    void *conf; /* the database type specific configuration block */
};

/* Increase pool size by #conn connections. */
unsigned int dbpool_increase_mysql(DBPool *p, unsigned int c)
{
    unsigned int i, n = 0;
    long len;
    MySQLConf *conf;

    gw_assert(p->conf != NULL);

    conf = p->conf;

    list_lock(p->pool);

    /* ensure we don't increase more items than the max_size border */
    c = (len = list_len(p->pool)) + c > p->max_size ? p->max_size - len : c;

    for (i = 0; i < c; i++) {
        MYSQL *mysql;
        DBPoolConn *pc;

        /* pre-allocate */
        mysql = gw_malloc(sizeof(MYSQL));
        pc = gw_malloc(sizeof(DBPoolConn));
        
        /* assign pool connection */
        pc->conn = mysql;
        pc->pool = p;
                                   
        if (!mysql_init(pc->conn)) {
            error(0, "MYSQL: init failed!");
            error(0, "MYSQL: %s", mysql_error(pc->conn));
        }

        if (!mysql_real_connect(mysql, octstr_get_cstr(conf->host), 
                                  octstr_get_cstr(conf->username),
                                  octstr_get_cstr(conf->password),
                                  octstr_get_cstr(conf->database), 0, NULL, 0)) {
            error(0, "MYSQL: can not connect to database!");
            error(0, "MYSQL: %s", mysql_error(pc->conn));
        } else {
       
            /* drop the connection to the pool */
            list_produce(p->pool, pc);
            n++;

            info(0,"Connected to mysql server at %s.", octstr_get_cstr(conf->host));
            debug("gwlib.dbpool", 0, "MYSQL: server version %s, client version %s.",  
                  mysql_get_server_info(pc->conn), mysql_get_client_info()); 
        }
    }
    list_unlock(p->pool);

    return n;
}


/* Initialize the mysql database config block */
static void dbpool_startup_mysql(DBPool *p, void *data)
{
    list_add_producer(p->pool);
    p->conf = data;
}


static void dbpool_conn_destroy(DBPoolConn *conn)
{
    gw_assert(conn != NULL && conn->conn != NULL);
    mysql_close(conn->conn);
    gw_free(conn->conn);
    gw_free(conn);
}


/*************************************************************************
 * public functions
 */

DBPool *dbpool_create(enum dbpool_type type, void *data, unsigned int connections) 
{
    DBPool *p;

    p = gw_malloc(sizeof(DBPool));
    p->pool = list_create();
    p->type = type;
    p->max_size = connections;
    p->conf = NULL;

    dbpool_startup_mysql(p, data);
    dbpool_increase_mysql(p, connections);

    return p;
}

void dbpool_destroy(DBPool *p)
{
    gw_assert(p != NULL && p->pool != NULL);

    list_destroy(p->pool, (void*) dbpool_conn_destroy);

    gw_free(p->conf);
    gw_free(p);
}


unsigned int dbpool_increase(DBPool *p, unsigned int conn) 
{
    return dbpool_increase_mysql(p, conn);
}
    

unsigned int dbpool_decrease(DBPool *p, unsigned int c)
{
    long len;
    unsigned int i;

    gw_assert(p != NULL && p->pool != NULL);

    list_lock(p->pool);

    /* Ensure we don't decrease more items then ammount in the queue */
    c = (len = list_len(p->pool)) < c ? len : c;

    /* 
     * Ensure we don't try to decrease more then available in pool,
     * because this would block while list_consume().
     */
    for (i = 0; i < c; i++) {
        DBPoolConn *pc;
        
        pc = list_consume(p->pool);

        /* close connections and destroy pool connection */
        dbpool_conn_destroy(pc);
    }
    list_unlock(p->pool);

    return c;
}


long dbpool_conn_count(DBPool *p)
{
    gw_assert(p->pool != NULL);

    return list_len(p->pool);
}


void *dbpool_conn_consume(DBPool *p)
{
    DBPoolConn *pc;

    /* garantee that you deliver a valid connection to the caller */
    while ((pc = list_consume(p->pool)) != NULL) {

        /* 
         * XXX check that the connection is still existing.
         * Is this a performance bottle-neck?!
         */
        if (!pc->conn || mysql_ping(pc->conn) != 0) {
            /* something was wrong, drop the connection */
            dbpool_conn_destroy(pc);
        } else {
            break;
        }
    }

    return (pc->conn != NULL ? pc : NULL);
}


void dbpool_conn_produce(DBPoolConn *pc)
{
    list_produce(pc->pool->pool, pc);
}


unsigned int dbpool_check(DBPool *p)
{
    long i, len, n = 0;

    gw_assert(p != NULL && p->pool != NULL);

    list_lock(p->pool);
    
    len = list_len(p->pool);
    for (i = 0; i < len; i++) {
        DBPoolConn *pconn;
        
        pconn = list_get(p->pool, i);
        if (mysql_ping(pconn->conn) != 0) {
            /* something was wrong, drop the connection */
            list_delete(p->pool, i, 1);
            dbpool_conn_destroy(pconn);
        } else {
            n++;
        }
    }
    list_unlock(p->pool);

    return n;
}
        

#endif /* HAVE_DBPOOL */
