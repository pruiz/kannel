/*
 * test_dbpool.c - test DBPool objects
 *
 * Stipe Tolj
 */
             
#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"

#ifdef HAVE_MYSQL
#include <mysql.h>
#endif

#ifdef HAVE_DBPOOL

#define MAX_THREADS 1024

typedef struct {
    Octstr *host;
    Octstr *username;
    Octstr *password;
    Octstr *database;
} MySQLConf;

static void help(void) 
{
    info(0, "Usage: test_dbpool [options] ...");
    info(0, "where options are:");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-h hostname");
    info(0, "    hostname to connect to");
    info(0, "-u username");
    info(0, "    username to use for the login credentials");
    info(0, "-p password");
    info(0, "    password to use for the login credentials");
    info(0, "-d database");
    info(0, "    database to connect to");
    info(0, "-s number");
    info(0, "    size of the database connection pool (default: 5)");
    info(0, "-q number");
    info(0, "    run a set of queries on the database connection pool (default: 100)");
    info(0, "-t number");
    info(0, "    how many query cleint threads should be used (default: 1)");
    info(0, "-S string");
    info(0, "    the SQL string that is performed while the queries (default: SHOW STATUS)");
}

/* global variables */
static unsigned long queries = 100;
static Octstr *sql;

static void client_thread(void *arg) 
{    
    unsigned long i, succeeded, failed;
    DBPool *pool = arg;

    succeeded = failed = 0;

    info(0,"Client thread started with %ld queries to perform on pool", queries);

    /* perform random queries on the pool */
    for (i = 1; i <= queries; i++) {
        DBPoolConn *pconn;
        int state;
        MYSQL_RES *result;

        /* provide us with a connection from the pool */
        pconn = dbpool_conn_consume(pool);
        debug("",0,"Query %ld/%ld: mysql thread id %ld obj at %p", 
              i, queries, mysql_thread_id(pconn->conn), (void*) pconn->conn);

        state = mysql_query(pconn->conn, octstr_get_cstr(sql));
        if (state != 0) {
            error(0, "MYSQL: %s", mysql_error(pconn->conn));
            failed++;
        } else {
            succeeded++;
        }
        result = mysql_store_result(pconn->conn);
        mysql_free_result(result);

        /* return the connection to the pool */
        dbpool_conn_produce(pconn);
    }
    info(0, "This thread: %ld succeeded, %ld failed.", succeeded, failed);
}


int main(int argc, char **argv)
{
    DBPool *pool;
    MySQLConf *conf;
    unsigned int pool_size = 5;
    unsigned int num_threads = 1;
    unsigned long i, threads[MAX_THREADS];
    int opt, ret;
    time_t start, end;
    double run_time;

    gwlib_init();

    conf = gw_malloc(sizeof(MySQLConf));
    conf->host = conf->username = conf->password = conf->database = NULL;
    sql = octstr_imm("SHOW STATUS");

    while ((opt = getopt(argc, argv, "v:h:u:p:d:s:q:t:S:")) != EOF) {
        switch (opt) { 
            case 'v':
                log_set_output_level(atoi(optarg));
                break;
	
            case 'h':
                conf->host = octstr_create(optarg);
                break;

            case 'u':
                conf->username = octstr_create(optarg);
                break;

            case 'p':
                conf->password = octstr_create(optarg);
                break;

            case 'd':
                conf->database = octstr_create(optarg);
                break;

            case 'S':
                octstr_destroy(sql);
                sql = octstr_create(optarg);
                break;

            case 's':
                pool_size = atoi(optarg);
                break;

            case 'q':
                queries = atoi(optarg);
                break;

            case 't':
                num_threads = atoi(optarg);
                break;

            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping.");
        }
    }
    
    if (!optind) {
        help();
        exit(0);
    }

    /* check if we have the database connection details */
    if (!conf->host || !conf->username || 
        !conf->password || !conf->database) {
        help();
        panic(0, "Database connection details are not fully provided!");
    }

    /* create */
    info(0,"Creating database pool to `%s' with %d connections.",
          octstr_get_cstr(conf->host), pool_size);
    pool = dbpool_create(DBPOOL_MYSQL, conf, pool_size); 
    debug("",0,"Connections within pool: %ld", dbpool_conn_count(pool));
    
    /* decrease */
    info(0,"Decreasing pool by half of size, which is %d connections", abs(pool_size/2));
    ret = dbpool_decrease(pool, abs(pool_size/2));
    debug("",0,"Decreased by %d connections", ret);
    debug("",0,"Connections within pool: %ld", dbpool_conn_count(pool));

    /* increase */
    info(0,"Increasing pool again by %d connections", pool_size);
    ret = dbpool_increase(pool, pool_size);
    debug("",0,"Increased by %d connections", ret);
    debug("",0,"Connections within pool: %ld", dbpool_conn_count(pool));

    /* queries */
    info(0,"SQL query is `%s'", octstr_get_cstr(sql));
    time(&start);
    if (num_threads == 1) {
        client_thread(pool);
    } else {
        for (i = 0; i < num_threads; ++i)
            threads[i] = gwthread_create(client_thread, pool);
        for (i = 0; i < num_threads; ++i)
            gwthread_join(threads[i]);
    }
    time(&end);
    
    run_time = difftime(end, start);
    info(0, "%ld requests in %f seconds, %f requests/s.",
         (queries * num_threads), run_time, (queries * num_threads) / run_time);

    /* check all active connections */
    debug("",0,"Connections within pool: %ld", dbpool_conn_count(pool));
    info(0,"Checked pool, %d connections still active and ok", dbpool_check(pool));

    info(0,"Destroying pool");
    dbpool_destroy(pool);

    octstr_destroy(sql);
    gwlib_shutdown();

    return 0;
}


#endif /* HAVE_DBPOOL */
