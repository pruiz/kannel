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
 * 2002-08-04: tolj@wapme-systems.de:
 *     added simple database library (sdb) support
 */
 
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <unistd.h>

#include "gwlib/gwlib.h"
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
   Octstr *source;
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
static void dlr_destroy(dlr_wle *dlr);
static void dlr_init_mem(void);
#ifdef DLR_MYSQL
static void dlr_init_mysql(Cfg* cfg);
#endif
#ifdef DLR_SDB
static void dlr_init_sdb(Cfg* cfg);
#endif
static void dlr_shutdown_mem(void);
static void dlr_shutdown_mysql(void);
static dlr_wle *dlr_new(void);

/* 
 * At startup initialize the list, use abstraction to
 * allow to add additional dlr_init_foobar() routines here.
 * 
 * Check 'dlr-storage' directive in core group to see how DLRs are
 * processed.
 */

static void dlr_init_mem()
{
    dlr_waiting_list = list_create();
}


/*
 * Load all configuration directives that are common for all database
 * types that use the 'dlr-db' group to define which attributes are 
 * used in the table
 */
#ifdef DLR_DB
static void dlr_db_init(CfgGroup *grp)
{
    if (!(table = cfg_get(grp, octstr_imm("table"))))
   	    panic(0, "DLR: DB: directive 'table' is not specified!");
    if (!(field_smsc = cfg_get(grp, octstr_imm("field-smsc"))))
   	    panic(0, "DLR: DB: directive 'field-smsc' is not specified!");
    if (!(field_ts = cfg_get(grp, octstr_imm("field-timestamp"))))
        panic(0, "DLR: DB: directive 'field-timestamp' is not specified!");
    if (!(field_src = cfg_get(grp, octstr_imm("field-source"))))
   	    panic(0, "DLR: DB: directive 'field-source' is not specified!");
    if (!(field_dst = cfg_get(grp, octstr_imm("field-destination"))))
   	    panic(0, "DLR: DB: directive 'field-destination' is not specified!");
    if (!(field_serv = cfg_get(grp, octstr_imm("field-service"))))
   	    panic(0, "DLR: DB: directive 'field-service' is not specified!");
    if (!(field_url = cfg_get(grp, octstr_imm("field-url"))))
   	    panic(0, "DLR: DB: directive 'field-url' is not specified!");
    if (!(field_mask = cfg_get(grp, octstr_imm("field-mask"))))
        panic(0, "DLR: DB: directive 'field-mask' is not specified!");
    if (!(field_status = cfg_get(grp, octstr_imm("field-status"))))
   	    panic(0, "DLR: DB: directive 'field-status' is not specified!");
}
#endif
   
#ifdef DLR_MYSQL
static void dlr_init_mysql(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *mysql_host, *mysql_user, *mysql_pass, *mysql_db, *mysql_id;
    Octstr *p = NULL;

    /*
     * check for all mandatory directives that specify the field names 
     * of the used MySQL table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))))
        panic(0, "DLR: MySQL: group 'dlr-db' is not specified!");

    if (!(mysql_id = cfg_get(grp, octstr_imm("id"))))
   	    panic(0, "DLR: MySQL: directive 'id' is not specified!");

    dlr_db_init(grp);

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
    } 

    octstr_destroy(mysql_db);
    octstr_destroy(mysql_host);
    octstr_destroy(mysql_user);
    octstr_destroy(mysql_pass);
    octstr_destroy(mysql_id);
}
#endif /* DLR_MYSQL */

#ifdef DLR_SDB
static void dlr_init_sdb(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *sdb_url, *sdb_id;
    Octstr *p = NULL;

    /*
     * check for all mandatory directives that specify the field names 
     * of the used table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))))
        panic(0, "DLR: SDB: group 'dlr-db' is not specified!");

    if (!(sdb_id = cfg_get(grp, octstr_imm("id"))))
   	    panic(0, "DLR: SDB: directive 'id' is not specified!");

    dlr_db_init(grp);

    /*
     * now grap the required information from the 'mysql-connection' group
     * with the sdb-id we just obtained
     *
     * we have to loop through all available SDB connection definitions 
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("sdb-connection"));
     while (grplist && (grp = list_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, sdb_id) == 0) {
            goto found;
        }
     }
     panic(0, "DLR: SDB: connection settings for id '%s' are not specified!", 
           octstr_get_cstr(sdb_id));

found:
    octstr_destroy(p);
    list_destroy(grplist, NULL);

    if (!(sdb_url = cfg_get(grp, octstr_imm("url"))))
   	    panic(0, "DLR: SDB: directive 'url' is not specified!");

    /*
     * ok, ready to connect
     */
    info(0,"Connecting to sdb resource <%s>.", octstr_get_cstr(sdb_url));
    connection = sdb_open(octstr_get_cstr(sdb_url));
    octstr_destroy(sdb_url);
}
#endif /* DLR_SDB */

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
    } else 
    if (octstr_compare(dlr_type, octstr_imm("sdb")) == 0) {
#ifdef DLR_SDB
        dlr_init_sdb(cfg);
#else
   	    panic(0, "DLR: storage type defined as '%s', but no LibSDB support build in!", 
              octstr_get_cstr(dlr_type));
#endif        

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

static void dlr_shutdown_mem()
{
    list_destroy(dlr_waiting_list, (list_item_destructor_t *)dlr_destroy);
}

static void dlr_shutdown_mysql()
{
#ifdef DLR_MYSQL
    mysql_close(connection);
#endif
}

static void dlr_shutdown_sdb()
{
#ifdef DLR_SDB
    sdb_close(connection);
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
    } else 
    if (octstr_compare(dlr_type, octstr_imm("sdb")) == 0) {
        dlr_shutdown_sdb();

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

static dlr_wle *dlr_new()
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

static void dlr_destroy(dlr_wle *dlr)
{
	O_DELETE (dlr->smsc);
	O_DELETE (dlr->timestamp);
	O_DELETE (dlr->source);
	O_DELETE (dlr->destination);
	O_DELETE (dlr->service);
	O_DELETE (dlr->url);
	dlr->mask = 0;
	gw_free(dlr);
}


/*
 * external functions
 */

static void dlr_add_mem(char *smsc, char *ts, char *src, char *dst, 
                        char *service, char *url, int mask)
{
   dlr_wle *dlr;
	
   if (mask & 0x1F) {
        dlr = dlr_new(); 

        debug("dlr.dlr", 0, "Adding DLR smsc=%s, ts=%s, src=%s, dst=%s, mask=%d", 
              smsc, ts, src, dst, mask);
	
        dlr->smsc = octstr_create(smsc);
        dlr->timestamp = octstr_create(ts);
        dlr->source = octstr_create(src);
        dlr->destination = octstr_create(dst);
        dlr->service = octstr_create(service); 
        dlr->url = octstr_create(url);
        dlr->mask = mask;
        list_append(dlr_waiting_list,dlr);
    }
}

static void dlr_add_mysql(char *smsc, char *ts, char *src, char *dst, 
                          char *service, char *url, int mask)
{
#ifdef DLR_MYSQL
    Octstr *sql;
    int	state;

    sql = octstr_format("INSERT INTO %s (%s, %s, %s, %s, %s, %s, %s, %s) VALUES "
                        "('%s', '%s', '%s', '%s', '%s', '%s', '%d', '%d');",
		                octstr_get_cstr(table), octstr_get_cstr(field_smsc), 
                        octstr_get_cstr(field_ts), 
                        octstr_get_cstr(field_src), octstr_get_cstr(field_dst),
                        octstr_get_cstr(field_serv), octstr_get_cstr(field_url), 
                        octstr_get_cstr(field_mask), octstr_get_cstr(field_status),
                        smsc, ts, src, dst, service, url, mask, 0);

    mutex_lock(dlr_mutex);
  
    state = mysql_query(connection, octstr_get_cstr(sql));
    if (state != 0)
        error(0, "MYSQL: %s", mysql_error(connection));
    
    octstr_destroy(sql);
    mutex_unlock(dlr_mutex);
#endif
}

static void dlr_add_sdb(char *smsc, char *ts, char *src, char *dst, 
                        char *service, char *url, int mask)
{
#ifdef DLR_SDB
    Octstr *sql;
    int	state;

    sql = octstr_format("INSERT INTO %s (%s, %s, %s, %s, %s, %s, %s, %s) VALUES "
                        "('%s', '%s', '%s', '%s', '%s', '%s', '%d', '%d')",
		                octstr_get_cstr(table), octstr_get_cstr(field_smsc), 
                        octstr_get_cstr(field_ts), 
                        octstr_get_cstr(field_src), octstr_get_cstr(field_dst), 
                        octstr_get_cstr(field_serv), octstr_get_cstr(field_url), 
                        octstr_get_cstr(field_mask), octstr_get_cstr(field_status),
                        smsc, ts, src, dst, service, url, mask, 0);

    mutex_lock(dlr_mutex);
  
    state = sdb_query(connection, octstr_get_cstr(sql), NULL, NULL);
    if (state == -1)
        error(0, "SDB: error in inserting DLR");
    
    octstr_destroy(sql);
    mutex_unlock(dlr_mutex);
#endif
}

void dlr_add(char *smsc, char *ts, char *src, char *dst, 
             char *keyword, char *id, int mask)
{
    if (octstr_compare(dlr_type, octstr_imm("internal")) == 0) {
        dlr_add_mem(smsc, ts, src, dst, keyword, id, mask);
    } else 
    if (octstr_compare(dlr_type, octstr_imm("mysql")) == 0) {
        dlr_add_mysql(smsc, ts, src, dst, keyword, id, mask);
    } else 
    if (octstr_compare(dlr_type, octstr_imm("sdb")) == 0) {
        dlr_add_sdb(smsc, ts, src, dst, keyword, id, mask);

    /*
     * add aditional types here
     */

    } else {
   	    panic(0, "DLR: storage type '%s' is not supported!", octstr_get_cstr(dlr_type));
    }
}


static Msg *dlr_find_mem(char *smsc, char *ts, char *dst, int typ)
{
    long i;
    long len;
    dlr_wle *dlr;
    Msg *msg;
    int dlr_mask;
    
    debug("dlr.dlr", 0, "Looking for DLR smsc=%s, ts=%s, dst=%s, type=%d", smsc, ts, dst, typ);
    len = list_len(dlr_waiting_list);
    for (i=0; i < len; i++) {
        dlr = list_get(dlr_waiting_list, i);
	
        if((strcmp(octstr_get_cstr(dlr->smsc),smsc) == 0) &&
	       (strcmp(octstr_get_cstr(dlr->timestamp),ts) == 0)) {

            dlr_mask = dlr->mask;

            if ((typ & dlr_mask) > 0) {
                /* its an entry we are interested in */
                msg = msg_create(sms);
                msg->sms.sms_type = report;
                msg->sms.service = octstr_duplicate(dlr->service);
                msg->sms.dlr_mask = typ;
                msg->sms.sms_type = report;
                msg->sms.smsc_id = octstr_create(smsc);
                msg->sms.sender = octstr_duplicate(dlr->destination);
                msg->sms.receiver = octstr_duplicate(dlr->source);

                /* if dlr_url was present, recode it here again */
                msg->sms.dlr_url = octstr_len(dlr->url) ? 
                    octstr_duplicate(dlr->url) : NULL;	

                /* 
                 * insert orginal message to the data segment 
                 * later in the smsc module 
                 */
                msg->sms.msgdata = NULL;

                time(&msg->sms.time);
                debug("dlr.dlr", 0, "created DLR message for URL <%s>", 
                      octstr_get_cstr(msg->sms.dlr_url));
            } else {
                debug("dlr.dlr", 0, "ignoring DLR message because of mask");
                /* ok that was a status report but we where not interested in having it */
                msg = NULL;
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

static Msg *dlr_find_mysql(char *smsc, char *ts, char *dst, int typ)
{
    Msg	*msg = NULL;
#ifdef DLR_MYSQL
    Octstr *sql;
    int	state;
    int dlr_mask;
    Octstr *dlr_service;
    Octstr *dlr_url;
    Octstr *source_addr;
    MYSQL_RES *result;
    MYSQL_ROW row;
    
    sql = octstr_format("SELECT %s, %s, %s, %s FROM %s WHERE %s='%s' AND %s='%s';",
                        octstr_get_cstr(field_mask), octstr_get_cstr(field_serv), 
                        octstr_get_cstr(field_url), octstr_get_cstr(field_source),
                        octstr_get_cstr(table), octstr_get_cstr(field_smsc),
                        smsc, octstr_get_cstr(field_ts), ts);

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
    
    debug("dlr.mysql", 0, "Found entry, row[0]=%s, row[1]=%s, row[2]=%s, row[3]=%s", 
          row[0], row[1], row[2], row[3]);
    dlr_mask = atoi(row[0]);
    dlr_service = octstr_create(row[1]);
    dlr_url = octstr_create(row[2]);
    source_addr = octstr_create(row[3]);
    mysql_free_result(result);
    
    mutex_unlock(dlr_mutex);
    
    sql = octstr_format("UPDATE %s SET %s=%d WHERE %s='%s' AND %s='%s';",
                        octstr_get_cstr(table), octstr_get_cstr(field_status), 
                        typ, octstr_get_cstr(field_smsc), smsc, 
                       	octstr_get_cstr(field_ts), ts);
    
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
        msg->sms.sender = octstr_duplicate(source_addr);
        msg->sms.receiver = octstr_create(dst);

        /* if dlr_url was present, recode it here again */
        msg->sms.dlr_url = octstr_len(dlr_url) ? 
        octstr_duplicate(dlr_url) : NULL;	

        /* 
         * insert orginal message to the data segment 
         * later in the smsc module 
         */
        msg->sms.msgdata = NULL;

        time(&msg->sms.time);
        debug("dlr.dlr", 0, "created DLR message for URL <%s>", 
              octstr_get_cstr(msg->sms.dlr_url));
    } else {
        debug("dlr.dlr", 0, "ignoring DLR message because of mask");
    }
 
    if ((typ & DLR_BUFFERED) && ((dlr_mask & DLR_SUCCESS) || (dlr_mask & DLR_FAIL))) {
        debug("dlr.mysql", 0, "DLR not deleted because we wait on more reports");
    } else {
        debug("dlr.mysql", 0, "removing DLR from database");
        sql = octstr_format("DELETE FROM %s WHERE %s='%s' AND %s='%s' LIMIT 1;",
                            octstr_get_cstr(table), octstr_get_cstr(field_smsc), 
                            smsc, octstr_get_cstr(field_ts), ts);
        
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
    octstr_destroy(source_addr);

#endif
    return msg;
}

#ifdef DLR_SDB
static int sdb_callback_add(int n, char **p, void *row)
{
	if (!n) {
        debug("dlr.sdb", 0, "no rows found");
        return 0;
    }

    /* strip string into words */
    row = octstr_split(octstr_imm(p[0]), octstr_imm(" "));
    if (list_len(row) != 4) {
        debug("dlr.sdb", 0, "Row has wrong length %ld", list_len(row));
        return 0;
    }
           
	return 0;
}

static int sdb_callback_msgs(int n, char **p, void *row)
{}
#endif

static Msg *dlr_find_sdb(char *smsc, char *ts, char *dst, int typ)
{
    Msg	*msg = NULL;
#ifdef DLR_SDB
    Octstr *sql;
    int	state;
    int dlr_mask;
    Octstr *dlr_service;
    Octstr *dlr_url;
    Octstr *source_addr;
    List *row;
    
    sql = octstr_format("SELECT %s, %s, %s, %s FROM %s WHERE %s='%s' AND %s='%s'",
                        octstr_get_cstr(field_mask), octstr_get_cstr(field_serv), 
                        octstr_get_cstr(field_url), octstr_get_cstr(field_source), 
                        octstr_get_cstr(table), octstr_get_cstr(field_smsc),
                        smsc, octstr_get_cstr(field_ts), ts);

    mutex_lock(dlr_mutex);
    
    state = sdb_query(connection, octstr_get_cstr(sql), sdb_callback_add, row);
    octstr_destroy(sql);
    if (state == -1) {
        error(0, "SDB: error in finding DLR");
        mutex_unlock(dlr_mutex);
        return NULL;
    }

    debug("dlr.sdb", 0, "Found entry, row[0]=%s, row[1]=%s, row[2]=%s, row[3]=%s", 
          octstr_get_cstr(list_get(row, 0)), 
          octstr_get_cstr(list_get(row, 1)), 
          octstr_get_cstr(list_get(row, 2)),
          octstr_get_cstr(list_get(row, 3)));

    dlr_mask = atoi(octstr_get_cstr(list_get(row, 0)));
    dlr_service = octstr_duplicate(list_get(row, 1));
    dlr_url = octstr_duplicate(list_get(row, 2));
    source_addr = octstr_duplicate(list_get(row, 3));
    list_destroy(row, octstr_destroy_item);
    
    mutex_unlock(dlr_mutex);
    
    sql = octstr_format("UPDATE %s SET %s=%d WHERE %s='%s' AND %s='%s'",
                        octstr_get_cstr(table), octstr_get_cstr(field_status), 
                        typ, octstr_get_cstr(field_smsc), smsc, 
                       	octstr_get_cstr(field_ts), ts);
    
    mutex_lock(dlr_mutex);
    
    state = sdb_query(connection, octstr_get_cstr(sql), NULL, NULL);
    octstr_destroy(sql);
    if (state == -1) {
        error(0, "SDB: error in updating DLR");
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
    	msg->sms.sender = octstr_duplicate(source_addr);
        msg->sms.receiver = octstr_create(dst);
        
        /* if dlr_url was present, recode it here again */
        msg->sms.dlr_url = octstr_len(dlr_url) ? 
        octstr_duplicate(dlr_url) : NULL;	

        /* 
         * insert orginal message to the data segment 
         * later in the smsc module 
         */
        msg->sms.msgdata = NULL;

        time(&msg->sms.time);
        debug("dlr.dlr", 0, "created DLR message for URL <%s>", 
              octstr_get_cstr(msg->sms.dlr_url));
    } else {
        debug("dlr.dlr", 0, "ignoring DLR message because of mask");
    }
 
    if ((typ & DLR_BUFFERED) && ((dlr_mask & DLR_SUCCESS) || (dlr_mask & DLR_FAIL))) {
        debug("dlr.sdb", 0, "DLR not deleted because we wait on more reports");
    } else {
        debug("dlr.sdb", 0, "removing DLR from database");
        sql = octstr_format("DELETE FROM %s WHERE %s='%s' AND %s='%s' LIMIT 1",
                            octstr_get_cstr(table), octstr_get_cstr(field_smsc), 
                            smsc, octstr_get_cstr(field_ts), ts);
        
        mutex_lock(dlr_mutex);

        state = sdb_query(connection, octstr_get_cstr(sql), NULL, NULL);
        octstr_destroy(sql);
        if (state == -1) {
            error(0, "SDB: error in deleting DLR");
        }

        mutex_unlock(dlr_mutex);
    }

    octstr_destroy(dlr_service);
    octstr_destroy(dlr_url);
    octstr_destroy(source_addr);

#endif
    return msg;
}

Msg *dlr_find(char *smsc, char *ts, char *dst, int typ)
{
    Msg	*msg = NULL;

    if (octstr_compare(dlr_type, octstr_imm("internal")) == 0) {
        msg = dlr_find_mem(smsc, ts, dst, typ);
    } else 
    if (octstr_compare(dlr_type, octstr_imm("mysql")) == 0) {
        msg = dlr_find_mysql(smsc, ts, dst, typ);
    } else 
    if (octstr_compare(dlr_type, octstr_imm("sdb")) == 0) {
        msg = dlr_find_sdb(smsc, ts, dst, typ);

    /*
     * add aditional types here
     */

    } else {
   	    panic(0, "DLR: storage type '%s' is not supported!", octstr_get_cstr(dlr_type));
    }
    return msg;
}


long dlr_messages(void)
{
    if (octstr_compare(dlr_type, octstr_imm("internal")) == 0) {
        return list_len(dlr_waiting_list);
    } else 
    if (octstr_compare(dlr_type, octstr_imm("mysql")) == 0) {
#ifdef DLR_MYSQL
        Octstr *sql;
        int	state;
        long res;
        MYSQL_RES *result;
        
        sql = octstr_format("SELECT * FROM %s;", octstr_get_cstr(table));
        mutex_lock(dlr_mutex);
        state = mysql_query(connection, octstr_get_cstr(sql));
        octstr_destroy(sql);
        if (state != 0) {
            error(0, "MYSQL: %s", mysql_error(connection));
            mutex_unlock(dlr_mutex);
            return -1;
        }
        result = mysql_store_result(connection);
        res = mysql_num_rows(result);
        mysql_free_result(result);
        mutex_unlock(dlr_mutex);
        return res;
#endif
    } else 
    if (octstr_compare(dlr_type, octstr_imm("sdb")) == 0) {
#ifdef DLR_SDB
        Octstr *sql;
        int	state;
        long res;
        
        sql = octstr_format("SELECT * FROM %s", octstr_get_cstr(table));
        mutex_lock(dlr_mutex);
        state = sdb_query(connection, octstr_get_cstr(sql), sdb_callback_msgs, NULL);
        octstr_destroy(sql);
        if (state == -1) {
            error(0, "SDB: error in selecting ammount of waiting DLRs");
            mutex_unlock(dlr_mutex);
            return -1;
        }
        res = (long) state;
        mutex_unlock(dlr_mutex);
        return res;
#endif

    /*
     * add aditional types here
     */

    } else {
   	    panic(0, "DLR: storage type '%s' is not supported!", octstr_get_cstr(dlr_type));
    }
    return -1;
}


void dlr_flush(void)
{
    long i;
    long len;
 
    info(0, "Flushing all %ld queued DLR messages in %s storage", dlr_messages(), 
            octstr_get_cstr(dlr_type));
 
    if (octstr_compare(dlr_type, octstr_imm("internal")) == 0) {
        len = list_len(dlr_waiting_list);
        for (i=0; i < len; i++)
            list_delete(dlr_waiting_list, i, 1);
        
    } else 
    if (octstr_compare(dlr_type, octstr_imm("mysql")) == 0) {
#ifdef DLR_MYSQL
        Octstr *sql;
        int	state;
        MYSQL_RES *result;
        
        sql = octstr_format("DELETE FROM %s;", octstr_get_cstr(table));
        mutex_lock(dlr_mutex);
        state = mysql_query(connection, octstr_get_cstr(sql));
        octstr_destroy(sql);
        if (state != 0) {
            error(0, "MYSQL: %s", mysql_error(connection));
        }
        result = mysql_store_result(connection);
        mysql_free_result(result);
        mutex_unlock(dlr_mutex);
#endif
    } else 
    if (octstr_compare(dlr_type, octstr_imm("sdb")) == 0) {
#ifdef DLR_SDB
        Octstr *sql;
        int	state;
        
        sql = octstr_format("DELETE FROM %s", octstr_get_cstr(table));
        mutex_lock(dlr_mutex);
        state = sdb_query(connection, octstr_get_cstr(sql), NULL, NULL);
        octstr_destroy(sql);
        if (state == -1) {
            error(0, "SDB: error in flusing DLR table");
        }
        mutex_unlock(dlr_mutex);
#endif

    /*
     * add aditional types here
     */

    } else {
   	    panic(0, "DLR: storage type '%s' is not supported!", octstr_get_cstr(dlr_type));
    }
}

