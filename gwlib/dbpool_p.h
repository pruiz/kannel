/*
 * dbpool_p.h - Database pool private header.
 *
 * Author: Alexander Malysh <a.malysh@centrium.de>, (C) 2003
 *
 * Copyright: See COPYING file that comes with this distribution
 */

#ifndef DBPOOL_P_H
#define DBPOOL_P_H 1


 struct db_ops {
    /*
     * Open db connection with given config params.
     * Config params are specificaly for each database type.
     * return NULL if error occurs ; established connection's pointer otherwise
     */
    void* (*open) (const DBConf *conf);
    /*
     * close given connection.
     */
    void (*close) (void *conn);
    /*
     * check if given connection still alive,
     * return -1 if not or error occurs ; 0 if all was fine
     * NOTE: this function is optional
     */
    int (*check) (void *conn);
    /*
     * Destroy specificaly configuration struct.
     */
    void (*conf_destroy) (DBConf *conf);
    /*
     * Database specific select.
     * Note: Result will be stored as follows:
     *           result is the list of rows each row will be stored also as list each column is stored as Octstr.
     * If someone has better idea please tell me ...
     *
     * @params conn - database specific connection; sql - sql statement ; result - result will be saved here
     * @return 0 if all was fine ; -1 otherwise
     */
    int (*select) (void *conn, const Octstr *sql, List **result);
    /*
     * Database specific update/insert/delete.
     * @params conn - database specific connection ; sql - sql statement
     * @return #rows processed ; -1 if a error occurs
     */
    int (*update) (void *conn, const Octstr *sql);
};

struct DBPool
{
    List *pool; /* queue representing the pool */
    unsigned int max_size; /* max #connections */
    unsigned int curr_size; /* current #connections */
    DBConf *conf; /* the database type specific configuration block */
    struct db_ops *db_ops; /* the database operations callbacks */
    enum db_type db_type; /* the type of database */
};


#endif

