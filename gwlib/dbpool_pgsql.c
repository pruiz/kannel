/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2004 Kannel Group  
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
 * dbpool_pgsql.c - implement PostgreSQL operations for generic database connection pool
 *
 * modeled after dbpool_mysql.c 
 * Martiin Atukunda <matlads@myrealbox.com>
 */

#ifdef HAVE_PGSQL
#include <libpq-fe.h>


#define add1(str, value) \
    if (value != NULL && octstr_len(value) > 0) { \
        tmp = octstr_format(str, value); \
        octstr_append(cs, tmp); \
        octstr_destroy(tmp); \
    }


static void* pgsql_open_conn(const DBConf *db_conf)
{
    PGconn *conn = NULL;
    PgSQLConf *conf = db_conf->pgsql; /* make compiler happy */
    Octstr *tmp, *cs;


    /* sanity check */
    if (conf == NULL)
        return NULL;

    cs = octstr_create("");
    add1(" host=%S", conf->pghost);
    //add1(" port=%S", conf->pgport);
    add1(" user=%S", conf->login);
    add1(" password=%S", conf->password);
    add1(" dbname=%S", conf->dbName);

#if 0
    /* This is very bad to show password in the log file */
    info(0, "PGSQL: Using connection string: %s.", octstr_get_cstr(cs));
#endif

    conn = PQconnectdb(octstr_get_cstr(cs));

    octstr_destroy(cs);
    if (conn == NULL)
        goto failed;

    gw_assert(conn != NULL);

    if (PQstatus(conn) == CONNECTION_BAD) {
        error(0, "PGSQL: connection to database %s failed!", octstr_get_cstr(conf->dbName)); 
	panic(0, "PGSQL: %s", PQerrorMessage(conn));
        goto failed;
    }

    info(0, "PGSQL: Connected to server at %s.", octstr_get_cstr(conf->pghost));

    return conn;

failed:
    PQfinish(conn);
    return NULL;
}


static void pgsql_close_conn(void *conn)
{
    if (conn == NULL)
        return;

    PQfinish(conn);
    return;
}


static int pgsql_check_conn(void *conn)
{
    if (conn == NULL)
        return -1;
	
    if (PQstatus(conn) == CONNECTION_BAD) {    
        error(0, "PGSQL: database check failed!");
        error(0, "PGSQL: %s", PQerrorMessage(conn));
        return -1;
    }	

    return 0;
}


static void pgsql_conf_destroy(DBConf *db_conf)
{
    PgSQLConf *conf = db_conf->pgsql;

    octstr_destroy(conf->pghost);
    octstr_destroy(conf->login);
    octstr_destroy(conf->password);
    octstr_destroy(conf->dbName);

    gw_free(conf);
    gw_free(db_conf);
}


static int pgsql_update(void *theconn, const Octstr *sql, List *binds)
{
    int	rows;
    PGresult *res = NULL;
    PGconn *conn = (PGconn*) theconn;

    res = PQexec(conn, octstr_get_cstr(sql));
    if (res == NULL)
        return -1;

    switch(PQresultStatus(res)) {
    case PGRES_BAD_RESPONSE:
    case PGRES_NONFATAL_ERROR:
    case PGRES_FATAL_ERROR:
	error(0, "PGSQL: %s", octstr_get_cstr(sql));
	error(0, "PGSQL: %s", PQresultErrorMessage(res));
	PQclear(res);
        return -1;
    default: /* for compiler please */
        break;
    }
    rows = atoi(PQcmdTuples(res));
    PQclear(res);

    return rows;
}


static int pgsql_select(void *theconn, const Octstr *sql, List *binds, List **list)
{
    int	nTuples, nFields, row_loop, field_loop;
    PGresult *res = NULL;
    List *fields;
    PGconn *conn = (PGconn*) theconn;

    gw_assert(list != NULL);
    *list = NULL;

    res = PQexec(conn, octstr_get_cstr(sql));
    if (res == NULL)
        return -1;

    switch(PQresultStatus(res)) {
    case PGRES_EMPTY_QUERY:
    case PGRES_BAD_RESPONSE:
    case PGRES_NONFATAL_ERROR:
    case PGRES_FATAL_ERROR:
        error(0, "PGSQL: %s", PQresultErrorMessage(res));
	PQclear(res);
        return -1;
    default: /* for compiler please */
        break;
    }

    nTuples = PQntuples(res);
    nFields = PQnfields(res);
    *list = list_create();
    for (row_loop = 0; row_loop < nTuples; row_loop++) {
	fields = list_create();
    	for (field_loop = 0; field_loop < nFields; field_loop++) {
            if (PQgetisnull(res, row_loop, field_loop))
                list_produce(fields, octstr_create(""));
            else 
	        list_produce(fields, octstr_create(PQgetvalue(res, row_loop, field_loop)));
	}
	list_produce(*list, fields);
    }
    PQclear(res);

    return 0;
}


static struct db_ops pgsql_ops = {
    .open = pgsql_open_conn,
    .close = pgsql_close_conn,
    .check = pgsql_check_conn,
    .conf_destroy = pgsql_conf_destroy,
    .update = pgsql_update,
    .select = pgsql_select
};

#endif /* HAVE_PGSQL */

