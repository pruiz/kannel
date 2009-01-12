/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2009 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * dlr_mysql.c
 *
 * Implementation of handling delivery reports (DLRs)
 * for MySql database
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <stolj@wapme.de>, 22.03.2002
 * Alexander Malysh <a.malysh@centrium.de> 2003
*/

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "dlr_p.h"


#ifdef HAVE_MYSQL
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

static struct dlr_storage handles = {
    .type = "mysql",
    .dlr_add = dlr_mysql_add,
    .dlr_get = dlr_mysql_get,
    .dlr_update = dlr_mysql_update,
    .dlr_remove = dlr_mysql_remove,
    .dlr_shutdown = dlr_mysql_shutdown,
    .dlr_messages = dlr_mysql_messages,
    .dlr_flush = dlr_mysql_flush
};

struct dlr_storage *dlr_init_mysql(Cfg *cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *mysql_host, *mysql_user, *mysql_pass, *mysql_db, *mysql_id;
    long mysql_port = 0;
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
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
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
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(mysql_host = cfg_get(grp, octstr_imm("host"))))
   	    panic(0, "DLR: MySQL: directive 'host' is not specified!");
    if (!(mysql_user = cfg_get(grp, octstr_imm("username"))))
   	    panic(0, "DLR: MySQL: directive 'username' is not specified!");
    if (!(mysql_pass = cfg_get(grp, octstr_imm("password"))))
   	    panic(0, "DLR: MySQL: directive 'password' is not specified!");
    if (!(mysql_db = cfg_get(grp, octstr_imm("database"))))
   	    panic(0, "DLR: MySQL: directive 'database' is not specified!");
    cfg_get_integer(&mysql_port, grp, octstr_imm("port"));  /* optional */

    /*
     * ok, ready to connect to MySQL
     */
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->mysql = gw_malloc(sizeof(MySQLConf));
    gw_assert(db_conf->mysql != NULL);

    db_conf->mysql->host = mysql_host;
    db_conf->mysql->port = mysql_port;
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
#endif /* HAVE_MYSQL */

