/* 
 * gw/dlr.c
 *
 * Implementation of handling delivery reports (DLRs)
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <tolj@wapme-systems.de>, 22.03.2002
 *
 * Changes:
 * 2001-12-17: andreas@fink.org:
 *     implemented use of mutex to avoid two mysql calls to run at the same time
 * 2002-03-22: tolj@wapme-systems.de:
 *     added more abstraction to fit for several other storage types
 */
 
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <unistd.h>

#include "gwlib/gwlib.h"
#include "smsc_p.h"
#include "sms.h"
#include "dlr.h"

/* 
#define DLR_TRACE 1 
*/

/* 
 * We use memory based DLR
 * the structure of a delivery report waiting list entry 
 */

typedef struct dlr_wle {
   Octstr *smsc;
   Octstr *timestamp;	
   Octstr *destination;
   Octstr *service;
   Octstr *url;	
   int mask;
} dlr_wle;

/* 
 * This is the global list where all messages being sent out are being kept track 
 * of his list is looked up once a delivery report comes in 
 */
static List *dlr_waiting_list;
void dlr_destroy(dlr_wle *dlr);


Mutex *dlr_mutex;
Octstr *dlr_type;

/* 
 * At startup initialize the list, use abstraction to
 * allow to add additional dlr_init_foobar() routines here.
 * 
 * Check 'dlr-storage' directive in core group to see how DLRs are
 * processed.
 */

void dlr_init_mem()
{
    dlr_waiting_list = list_create();
}
   
void dlr_init_mysql(Cfg* cfg)
{
#ifdef DLR_MYSQL
    CfgGroup *grp;
    List *grplist;
    Octstr *mysql_host, *mysql_user, *mysql_pass, *mysql_db, *mysql_id;
    Octstr *p;

    /*
     * check for all mandatory directives that specify the field names 
     * of the used MySQL table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-mysql"))))
        panic(0, "DLR: MySQL: group 'dlr-mysql' is not specified!");

    if (!(mysql_id = cfg_get(grp, octstr_imm("mysql-id"))))
   	    panic(0, "DLR: MySQL: directive 'mysql-id' is not specified!");
    if (!(table = cfg_get(grp, octstr_imm("table"))))
   	    panic(0, "DLR: MySQL: directive 'table' is not specified!");
    if (!(field_smsc = cfg_get(grp, octstr_imm("field-smsc"))))
   	    panic(0, "DLR: MySQL: directive 'field-smsc' is not specified!");
    if (!(field_ts = cfg_get(grp, octstr_imm("field-timestamp"))))
        panic(0, "DLR: MySQL: directive 'field-timestamp' is not specified!");
    if (!(field_dst = cfg_get(grp, octstr_imm("field-destination"))))
   	    panic(0, "DLR: MySQL: directive 'field-destination' is not specified!");
    if (!(field_serv = cfg_get(grp, octstr_imm("field-service"))))
   	    panic(0, "DLR: MySQL: directive 'field-service' is not specified!");
    if (!(field_url = cfg_get(grp, octstr_imm("field-url"))))
   	    panic(0, "DLR: MySQL: directive 'field-url' is not specified!");
    if (!(field_mask = cfg_get(grp, octstr_imm("field-mask"))))
        panic(0, "DLR: MySQL: directive 'field-mask' is not specified!");
    if (!(field_status = cfg_get(grp, octstr_imm("field-status"))))
   	    panic(0, "DLR: MySQL: directive 'field-status' is not specified!");

    /*
     * now grap the required information from the 'mysql-connection' group
     * with the mysql-id we just obtained
     *
     * we have to loop through all available MySQL connection definitions 
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("mysql-connection"));
     while (grplist && (grp = list_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, mysql_id) == 0) {
            goto found;
        }
     }
     panic(0, "DLR: MySQL: connection settings for id '%s' are not specified!", 
           octstr_get_cstr(mysql_id));

found:
    octstr_destroy(p);
    list_destroy(grplist, NULL);

    if (!(mysql_host = cfg_get(grp, octstr_imm("host"))))
   	    panic(0, "DLR: MySQL: directive 'host' is not specified!");
    if (!(mysql_user = cfg_get(grp, octstr_imm("mysql-username"))))
   	    panic(0, "DLR: MySQL: directive 'mysql-username' is not specified!");
    if (!(mysql_pass = cfg_get(grp, octstr_imm("mysql-password"))))
   	    panic(0, "DLR: MySQL: directive 'mysql-password' is not specified!");
    if (!(mysql_db = cfg_get(grp, octstr_imm("database"))))
   	    panic(0, "DLR: MySQL: directive 'database' is not specified!");

    /*
     * ok, ready to connect to MySQL 
     */
    mysql_init(&mysql);
    connection = mysql_real_connect(&mysql, 
   	                octstr_get_cstr(mysql_host), octstr_get_cstr(mysql_user), 
                    octstr_get_cstr(mysql_pass), octstr_get_cstr(mysql_db), 
                    0, NULL, 0);

    /* 
     * XXX should a failing connect throw panic?!
     */
    if (connection == NULL) {
        error(0,"DLR: MySQL: can not connect to database!");
        error(0,"MYSQL: %s", mysql_error(&mysql));
    } else {
        info(0,"Connected to mysql server at %s.", octstr_get_cstr(mysql_host));
        info(0,"MYSQL: server version %s, client version %s.",  
             mysql_get_server_info(&mysql), mysql_get_client_info()); 
        info(0,"MYSQL protocol info: %s.", mysql_get_proto_info(&mysql)); 
    } 

    octstr_destroy(mysql_db);
    octstr_destroy(mysql_host);
    octstr_destroy(mysql_user);
    octstr_destroy(mysql_pass);
    octstr_destroy(mysql_id);
#endif
}

void dlr_init(Cfg* cfg)
{
    CfgGroup *grp;

    /* create the DLR mutex */
    dlr_mutex = mutex_create();

    /* check which DLR storage type we are using */
    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    dlr_type = cfg_get(grp, octstr_imm("dlr-storage"));

    /* 
     * assume we are using internal memory in case no directive
     * has been specified, warn the user anyway
     */

    if (dlr_type == NULL) {
        dlr_type = octstr_imm("internal");
        warning(0, "DLR: using default 'internal' for storage type.");
    }

    /* call the sub-init routine */
    if (octstr_compare(dlr_type, octstr_imm("mysql")) == 0) {
#ifdef DLR_MYSQL
        dlr_init_mysql(cfg);
#else
   	    panic(0, "DLR: storage type defined as '%s', but no MySQL support build in!", 
              octstr_get_cstr(dlr_type));
#endif        
    } else 
    if (octstr_compare(dlr_type, octstr_imm("internal")) == 0) {
        dlr_init_mem();

    /*
     * add aditional types here
     */

    } else {
   	    panic(0, "DLR: storage type '%s' is not supported!", octstr_get_cstr(dlr_type));
    }

}



/* 
 * At stutdown destroy the list, use abstraction to
 * allow to add additional dlr_shutdown_foobar() routines here.
 * 
 * Check 'dlr-storage' directive in core group to see how DLRs are
 * processed.
 */

void dlr_shutdown_mem()
{
    list_destroy(dlr_waiting_list, (list_item_destructor_t *)dlr_destroy);
}

void dlr_shutdown_mysql()
{
#ifdef DLR_MYSQL
    mysql_close(connection);
    if (!mysql_errno(connection)) 
        error(0,"MYSQL: %s", mysql_error(connection));
#endif
}

void dlr_shutdown()
{
    /* call the sub-init routine */
    if (octstr_compare(dlr_type, octstr_imm("mysql")) == 0) {
        dlr_shutdown_mysql();
    } else 
    if (octstr_compare(dlr_type, octstr_imm("internal")) == 0) {
        dlr_shutdown_mem();

    /*
     * add aditional types here
     */

    } else {
   	    panic(0, "DLR: storage type '%s' is not supported!", octstr_get_cstr(dlr_type));
    }

    /* destroy the DLR mutex */
    mutex_destroy(dlr_mutex);
}



/* 
 * internal function to allocate a new dlr_wle entry
 * and intialize it to zero 
 */

dlr_wle *dlr_new()
{
	int i;
	dlr_wle *dlr;

	dlr = gw_malloc(sizeof(dlr_wle));
	if (dlr)
        for(i=0;i<sizeof(dlr_wle);i++)
			((char *)dlr)[i] = 0;
	return dlr;
}


/*
 * internal function to destroy the dlr_wle entry 
 */

void dlr_destroy(dlr_wle *dlr)
{
	O_DELETE (dlr->smsc);
	O_DELETE (dlr->timestamp);
	O_DELETE (dlr->destination);
	O_DELETE (dlr->service);
	O_DELETE (dlr->url);
	dlr->mask = 0;
	gw_free(dlr);
}


/*
 * external functions
 */

void dlr_add_mem(char *smsc, char *ts, char *dst, char *service, char *url, int mask)
{
   dlr_wle	*dlr;
	
   if (mask & 0x1F) 
   {
	dlr = dlr_new(); 
	
	dlr->smsc = octstr_create(smsc);
	dlr->timestamp = octstr_create(ts);
	dlr->destination = octstr_create(dst);
	dlr->service = octstr_create(service); 
	dlr->url = octstr_create(url);
	dlr->mask = mask;
	list_append(dlr_waiting_list,dlr);
   }
}

void dlr_add_mysql(char *smsc, char *ts, char *dst, char *service, char *url, int mask)
{
#ifdef DLR_MYSQL
    Octstr *sql;
    int	state;

    sql = octstr_format("INSERT INTO %s (%s, %s, %s, %s, %s, %s, %s) VALUES "
                        "('%s', '%s', '%s', '%s', '%s', '%d', '%d');",
		                table, field_smsc, field_ts, field_dst, field_serv,
                        field_url, field_mask, field_status,
                        smsc, ts, dst, service,	url, mask, 0);

    mutex_lock(dlr_mutex);
  
    state = mysql_query(connection, octstr_get_cstr(sql));
    if (state != 0)
        error(0, "MYSQL: %s", mysql_error(connection));
    
    octstr_destroy(sql);
    mutex_unlock(dlr_mutex);
#endif
}

void dlr_add(char *smsc, char *ts, char *dst, char *keyword, char *id, int mask)
{
    if (octstr_compare(dlr_type, octstr_imm("internal")) == 0) {
        dlr_add_mem(smsc, ts, dst, keyword, id, mask);
    } else 
    if (octstr_compare(dlr_type, octstr_imm("mysql")) == 0) {
        dlr_add_mysql(smsc, ts, dst, keyword, id, mask);

    /*
     * add aditional types here
     */

    } else {
   	    panic(0, "DLR: storage type '%s' is not supported!", octstr_get_cstr(dlr_type));
    }
}


Msg *dlr_find_mem(char *smsc, char *ts, char *dst, int typ)
{
    long i;
    long len;
    dlr_wle *dlr;
    Octstr *text;
    Msg *msg;
    int dlr_mask;
    
    debug("dlr.dlr", 0, "Looking for DLR smsc=%s, ts=%s, dst=%s, type=%d", smsc, ts, dst, typ);
    len = list_len(dlr_waiting_list);
    for (i=0; i < len; i++) {
        dlr = list_get(dlr_waiting_list, i);
	
        if((strcmp(octstr_get_cstr(dlr->smsc),smsc) == 0) &&
	       (strcmp(octstr_get_cstr(dlr->timestamp),ts) == 0)) {

            /* lets save the service and dump the rest of the entry */
            text = octstr_len(dlr->url) ? octstr_duplicate(dlr->url) : octstr_create("");	   

            dlr_mask = dlr->mask;

            if( (typ & dlr_mask) > 0) {
                /* its an entry we are interested in */
                msg = msg_create(sms);
                msg->sms.service = octstr_duplicate(dlr->service);
                msg->sms.dlr_mask = typ;
                msg->sms.sms_type = report;
                msg->sms.smsc_id = octstr_create(smsc);
                msg->sms.sender = octstr_create(dst);
                msg->sms.receiver = octstr_create("000");
                msg->sms.msgdata = text;
                time(&msg->sms.time);
                debug("dlr.dlr", 0, "created DLR message: %s", octstr_get_cstr(msg->sms.msgdata));
            } else {
                debug("dlr.dlr", 0, "ignoring DLR message because of mask");
                /* ok that was a status report but we where not interested in having it */
                octstr_destroy(text);
                msg=NULL;
            }
            if ((typ & DLR_BUFFERED) && ((dlr_mask & DLR_SUCCESS) || (dlr_mask & DLR_FAIL))) {
                info(0, "dlr not destroyed, still waiting for other delivery report"); 
            } else {
                list_delete(dlr_waiting_list, i, 1);
                dlr_destroy(dlr);
            }

            return msg;
        }
    }
    debug("dlr.dlr", 0, "DLR not found!");
    /* we couldnt find a matching entry */
    return NULL;
}

Msg *dlr_find_mysql(char *smsc, char *ts, char *dst, int typ)
{
#ifdef DLR_MYSQL
    Octstr *sql;
    int	state;
    MYSQL_RES *result;
    MYSQL_ROW row;
    int dlr_mask;
    Octstr *dlr_service;
    Octstr *dlr_url;
    Msg	*msg = NULL;
    
    sql = octstr_format("SELECT %s, %s, %s FROM %s WHERE %s='%s' AND %s='%s';",
                        field_mask, field_serv, field_url, table, field_smsc,
                        smsc, field_ts, ts);

    mutex_lock(dlr_mutex);
    
    state = mysql_query(connection, octstr_get_cstr(sql));
    octstr_destroy(sql);
    if (state != 0) {
        error(0, "MYSQL: %s", mysql_error(connection));
        mutex_unlock(dlr_mutex);
        return NULL;
    }
    result = mysql_store_result(connection);
    if (mysql_num_rows(result) < 1) {
        debug("dlr.mysql", 0, "no rows found");
        mysql_free_result(result);
        mutex_unlock(dlr_mutex);
        return NULL;
    }
    row = mysql_fetch_row(result);
    if (!row) {
        debug("dlr.mysql", 0, "rows found but could not load them");
        mysql_free_result(result);
    	mutex_unlock(dlr_mutex);
        return NULL;
    }
    
    debug("dlr.mysql", 0, "Found entry, row[0]=%s, row[1]=%s, row[2]=%s", row[0], row[1], row[2]);
    dlr_mask = atoi(row[0]);
    dlr_service = octstr_create(row[1]);
    dlr_url = octstr_create(row[2]);
    mysql_free_result(result);
    
    mutex_unlock(dlr_mutex);
    
    sql = octstr_format("UPDATE %s SET %s=%d WHERE %s='%s' AND %s='%s';",
                        table, field_status, typ, field_smsc, smsc, 
                       	field_ts, ts);
    
    mutex_lock(dlr_mutex);
    
    state = mysql_query(connection, octstr_get_cstr(sql));
    octstr_destroy(sql);
    if (state != 0) {
        error(0, "MYSQL: %s", mysql_error(connection));
        mutex_unlock(dlr_mutex);
        return NULL;
    }

    mutex_unlock(dlr_mutex);

    if ((typ & dlr_mask)) {
        /* its an entry we are interested in */
        msg = msg_create(sms);
        msg->sms.service = octstr_duplicate(dlr_service);
        msg->sms.dlr_mask = typ;
        msg->sms.sms_type = report;
        msg->sms.smsc_id = octstr_create(smsc);
    	msg->sms.sender = octstr_create(dst);
        msg->sms.receiver = octstr_create("000");
        msg->sms.msgdata = octstr_duplicate(dlr_url);
        time(&msg->sms.time);
        debug("dlr.dlr", 0, "created DLR message: %s", octstr_get_cstr(msg->sms.msgdata));
    } else {
        debug("dlr.dlr", 0, "ignoring DLR message because of mask");
    }
 
    if ((typ & DLR_BUFFERED) && ((dlr_mask & DLR_SUCCESS) || (dlr_mask & DLR_FAIL))) {
        debug("dlr.mysql", 0, "dlr not deleted because we wait on more reports");
    } else {
        debug("dlr.mysql", 0, "removing DLR from database");
        sql = octstr_format("DELETE FROM %s WHERE %s='%s' AND %s='%s';",
                            table, field_smsc, smsc, field_ts, ts);
        
        mutex_lock(dlr_mutex);

        state = mysql_query(connection, octstr_get_cstr(sql));
        octstr_destroy(sql);
        if (state != 0) {
            error(0, "MYSQL: %s", mysql_error(connection));
        }

        mutex_unlock(dlr_mutex);
    }

    octstr_destroy(dlr_service);
    octstr_destroy(dlr_url);

    return msg;
#endif
}

Msg *dlr_find(char *smsc, char *ts, char *dst, int typ)
{
    Msg	*msg = NULL;

    if (octstr_compare(dlr_type, octstr_imm("internal")) == 0) {
        msg = dlr_find_mem(smsc, ts, dst, typ);
    } else 
    if (octstr_compare(dlr_type, octstr_imm("mysql")) == 0) {
        msg = dlr_find_mysql(smsc, ts, dst, typ);

    /*
     * add aditional types here
     */

    } else {
   	    panic(0, "DLR: storage type '%s' is not supported!", octstr_get_cstr(dlr_type));
    }
    return msg;
}




