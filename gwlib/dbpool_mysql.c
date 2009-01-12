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
 * dbpool_mysql.c - implement MySQL operations for generic database connection pool
 *
 * Stipe Tolj <stolj@wapme.de>
 *      2003 Initial version.
 * Alexander Malysh <a.malysh@centrium.de>
 *      2003 Made dbpool more generic.
 */

#ifdef HAVE_MYSQL
#include <mysql.h>


static void *mysql_open_conn(const DBConf *db_conf)
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
                            octstr_get_cstr(conf->database), 
                            conf->port, NULL, 0)) {
        error(0, "MYSQL: can not connect to database!");
        error(0, "MYSQL: %s", mysql_error(mysql));
        goto failed;
    }

    info(0, "MYSQL: Connected to server at %s.", octstr_get_cstr(conf->host));
    info(0, "MYSQL: server version %s, client version %s.",
           mysql_get_server_info(mysql), mysql_get_client_info());

    return mysql;

failed:
    if (mysql != NULL) 
        gw_free(mysql);
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

