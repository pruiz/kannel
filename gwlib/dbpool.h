/*
 * dbpool.h - database pool functions
 */

#ifndef GWDBPOOL_H
#define GWDBPOOL_H

#if defined(HAVE_MYSQL) || defined(HAVE_SDB) || defined(HAVE_ORACLE)
#define HAVE_DBPOOL 1
#endif

/* supported databases for connection pools */
enum db_type {
	DBPOOL_MYSQL, DBPOOL_SDB, DBPOOL_ORACLE
};


/*
 * The DBPool type. It is opaque: do not touch it except via the functions
 * defined in this header.
 */
typedef struct DBPool DBPool;

/*
 * The DBPoolConn type. It stores the abtracted pointer to a database 
 * specific connection and the pool pointer itself to allow easy
 * re-storage into the pool (also disallowing to insert the conn into an
 * other pool).
 */
 typedef struct {
    void *conn; /* the pointer holding the database specific connection */
    DBPool *pool; /* pointer of the pool where this connection belongs to */
}  DBPoolConn;

typedef struct {
    Octstr *host;
    Octstr *username;
    Octstr *password;
    Octstr *database;
} MySQLConf;

/*
 * TODO Think how to get rid of it and have generic Conf struct
 */
typedef struct {
    Octstr *tnsname;
    Octstr *username;
    Octstr *password;
} OracleConf;

typedef struct {
    Octstr *url;
} SDBConf;

typedef union {
    MySQLConf *mysql;
    SDBConf *sdb;
    OracleConf *oracle;
} DBConf;

/*
 * Create a database pool with #connections of connections. The pool
 * is stored within a queue list.
 * Returns a pointer to the pool object on success or NULL if the
 * creation fails.
 */
DBPool *dbpool_create(enum db_type db_type, DBConf *conf, unsigned int connections);

/*
 * Destroys the database pool. Includes also shutdowning all existing
 * connections within the pool queue.
 */
void dbpool_destroy(DBPool *p);

/*
 * Increase the connection size of the pool by #conn connections.
 * Beware that you can't increase a pool size to more then the initial
 * dbpool_create() call defined and opened the maximum pool connections.
 * Returns how many connections have been additionally created and
 * inserted to the pool.
 */
unsigned int dbpool_increase(DBPool *p, unsigned int conn);

/*
 * Decrease the connection size of the pool by #conn connections.
 * A pool size can only by reduced up to 0. So if the caller specifies
 * to close more connections then there are in the pool, all connections
 * are closed.
 * Returns how many connections have been shutdown and deleted from the
 * pool queue.
 */
unsigned int dbpool_decrease(DBPool *p, unsigned int conn);

/*
 * Return the number of connections that are currently queued in the pool.
 */
long dbpool_conn_count(DBPool *p);

/*
 * Gets and active connection from the pool and returns it.
 * The caller can use it then for queuery operations and has to put it
 * back into the pool via dbpool_conn_produce(conn).
 * If no connection is in pool and DBPool is not in destroying phase then
 * will block until connection is available otherwise returns NULL.
 */
DBPoolConn *dbpool_conn_consume(DBPool *p);

/*
 * Returns a used connection to the pool again.
 * The connection is returned to it's domestic pool for further extraction
 * using dbpool_conn_consume().
 */
void dbpool_conn_produce(DBPoolConn *conn);

int inline dbpool_conn_select(DBPoolConn *conn, const Octstr *sql, List **result);
int inline dbpool_conn_update(DBPoolConn *conn, const Octstr *sql);

/*
 * Perfoms a check of all connections within the pool and tries to
 * re-establish the same ammount of connections if there are broken
 * connections within the pool.
 * (This operation can only be performed if the database allows such
 * operations by its API.)
 * Returns how many connections within the pool have been checked and
 * are still considered active.
 */
unsigned int dbpool_check(DBPool *p);


#endif
