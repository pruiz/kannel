/* dlr.c
** delivery reports
** generic routines
** Andreas Fink <andreas@fink.org> ,18.8.2001
**
** Changes:
** 2001-12-17: andreas@fink.org:
**     implemented use of mutex to avoid two mysql calls to run at the same time
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

/* #define DLR_TRACE 1 */

/* if we use MySQL for delivery reports, you got 
to define this stuff for your implementation 
you got to add -lmysqlclient to the LIBS line in the makefile too
create the database with this 

CREATE TABLE DLR (
		smsc varchar(40),
		ts varchar(40),
		destination varchar(40),
		service varchar(40),
		url varchar(255),
		mask int(10),
		status int(10)
		)
*/
/* #define	MYSQL_DLR	0 */
/* #define	SQL_DEBUG	1 */

#if (MYSQL_DLR)

#include <mysql/mysql.h>
MYSQL	*connection;
MYSQL	mysql;
#define	DB_HOST		"localhost"
#define	DB_USER		"username"
#define	DB_PASSWORD	"password"

#define	DB_PORT		0
#define	DB_SOCKET	0
#define	DB_CLIENTFLAG	0
#define	DB_NAME		"DLR"
#define	DLR_TABLE	"DLR"
#define DLR_FIELD_SMSC		"smsc"
#define	DLR_FIELD_TS		"ts"
#define	DLR_FIELD_DST		"destination"
#define	DLR_FIELD_SERVICE	"service"
#define	DLR_FIELD_URL		"url"
#define	DLR_FIELD_MASK		"mask"
#define	DLR_FIELD_STATUS	"status"

#else
/* we use memory based DLR */
/* the structure of a delivery report waiting list entry */

typedef struct	dlr_wle
{
   Octstr	*smsc;
   Octstr	*timestamp;	
   Octstr	*destination;
   Octstr	*service;
   Octstr	*url;	
   int		mask;
} dlr_wle;

/* this is the global list where all messages being sent out are being kept track of */
/* this list is looked up once a delivery report comes in */

static	List	*dlr_waiting_list;
void	dlr_destroy(dlr_wle *dlr);

#endif /* else MYSQL_DLR */



Mutex *dlr_mutex;

/* at startup initialize the list */

void	dlr_init()
{
    dlr_mutex = mutex_create();

#if (MYSQL_DLR)
   mysql_init(&mysql);
   connection = mysql_real_connect(&mysql, DB_HOST,DB_USER,DB_PASSWORD,DB_NAME,DB_PORT,DB_SOCKET,DB_CLIENTFLAG);
   if(connection == NULL)
   	error(0,"can not connect to MySQL database for DLR ! Error = %s",mysql_error(&mysql));
#else
    dlr_waiting_list = list_create();
#endif
}


/* at shutdown, destroy the list */
void	dlr_shutdown()
{
#if (MYSQL_DLR)
    mysql_close(connection);
#else
    list_destroy(dlr_waiting_list, (list_item_destructor_t *)dlr_destroy);
#endif
    mutex_destroy(dlr_mutex);
}


#if !(MYSQL_DLR)

/* internal function to allocate a new dlr_wle entry */
/* and intialize it to zero */

dlr_wle *dlr_new()
{
	int i;
	dlr_wle *dlr;

	dlr = gw_malloc(sizeof(dlr_wle));
	if(dlr)
		for(i=0;i<sizeof(dlr_wle);i++)
			((char *)dlr)[i] = 0;
	return dlr;
}


/*internal function to destroy the dlr_wle entry */

#define	O_DELETE(a)	 { if(a) octstr_destroy(a); a = NULL; }

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

#endif


/* add a new entry to the list */
void	dlr_add(char *smsc, char *ts, char *dst, char *service, char *url, int mask)
{
#if (MYSQL_DLR)

    Octstr *sql;
    int	state;

    sql = octstr_format("INSERT INTO %s (%s, %s, %s, %s, %s, %s, %s) VALUES ('%s', '%s', '%s', '%s', '%s', '%d', '%d');",
		DLR_TABLE,
		DLR_FIELD_SMSC,
		DLR_FIELD_TS,
		DLR_FIELD_DST,
		DLR_FIELD_SERVICE,
		DLR_FIELD_URL,
		DLR_FIELD_MASK,
		DLR_FIELD_STATUS,
		smsc,
		ts,
		dst,
		service,
		url,
		mask,
		0);
   
    mutex_lock(dlr_mutex);
   
    state = mysql_query(connection,octstr_get_cstr(sql));
    if(state !=0)
	error(0,"mysql_error %s",mysql_error(connection));
    octstr_destroy(sql);
   
    mutex_unlock(dlr_mutex);

#else
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
#endif
}

/* find an entry in the list. if there is one a message is returned and the entry is removed from the list
otherwhise the message returned is NULL */

Msg *dlr_find(char *smsc, char *ts, char *dst, int typ)
{
#if (MYSQL_DLR)
    Octstr *sql;
    int	state;
    MYSQL_RES		*result;
    MYSQL_ROW		row;
    int dlr_mask;
    Octstr *dlr_service;
    Octstr *dlr_url;
    Msg	*msg = NULL;
    
    sql = octstr_format("SELECT %s, %s, %s FROM %s WHERE %s='%s' AND %s='%s';",
	DLR_FIELD_MASK,
	DLR_FIELD_SERVICE,
	DLR_FIELD_URL,
	DLR_TABLE,
	DLR_FIELD_SMSC,
	smsc,
	DLR_FIELD_TS,
	ts);

    mutex_lock(dlr_mutex);
    
    state = mysql_query(connection,octstr_get_cstr(sql));
    octstr_destroy(sql);
    if(state !=0)
    {
	error(0,"mysql_error %s",mysql_error(connection));
	mutex_unlock(dlr_mutex);
	return NULL;
    }
    result = mysql_store_result(connection);
    if ( mysql_num_rows(result) < 1)
    {
        debug("dlr.mysql",0,"no rows found");
	mysql_free_result(result);
	mutex_unlock(dlr_mutex);
	return NULL;
    }
    row = mysql_fetch_row(result);
    if(!row)
    {
        debug("dlr.mysql",0,"rows found but coudlnt load them");
	mysql_free_result(result);
    	mutex_unlock(dlr_mutex);
	return NULL;
    }
    
    debug("dlr.mysql",0,"Found entry, row[0]=%s, row[1]=%s, row[2]=%s",row[0],row[1],row[2]);
    dlr_mask = atoi(row[0]);
    dlr_service = octstr_create(row[1]);
    dlr_url = octstr_create(row[2]);
    mysql_free_result(result);
    
    mutex_unlock(dlr_mutex);
  
    
    sql = octstr_format("UPDATE %s SET %s=%d WHERE %s='%s' AND %s='%s';",
    	DLR_TABLE,
    	DLR_FIELD_STATUS,
    	typ,
	DLR_FIELD_SMSC,
	smsc,
	DLR_FIELD_TS,
	ts);
    
    mutex_lock(dlr_mutex);
    
    state = mysql_query(connection,octstr_get_cstr(sql));
    octstr_destroy(sql);
    if(state !=0)
    {
	error(0,"mysql_error %s",mysql_error(connection));
	mutex_unlock(dlr_mutex);
	return NULL;
    }

    mutex_unlock(dlr_mutex);

    if((typ & dlr_mask))
    {
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
    	debug("dlr.dlr",0,"created DLR message: %s",octstr_get_cstr(msg->sms.msgdata));
    }
    else
    {
    	debug("dlr.dlr",0"ignoring DLR message because of mask");
    }
 
    if((typ & DLR_BUFFERED) &&
    	((dlr_mask & DLR_SUCCESS) || (dlr_mask & DLR_FAIL)))
    {
    	debug("dlr.mysql",0,"dlr not deleted because we wait on more reports");
    }
    else
    {
    	debug("dlr.mysql",0,"removing DLR from database");
   	sql = octstr_format("DELETE FROM %s WHERE %s='%s' AND %s='%s';",
	DLR_TABLE,
	DLR_FIELD_SMSC,
	smsc,
	DLR_FIELD_TS,
	ts);

	mutex_lock(dlr_mutex);

	state = mysql_query(connection,octstr_get_cstr(sql));
    	octstr_destroy(sql);
    	if(state !=0)
	{
	    error(0,"mysql_error %s",mysql_error(connection));
	}

	mutex_unlock(dlr_mutex);

    }
    octstr_destroy(dlr_service);
    octstr_destroy(dlr_url);
    return msg;
#else
    long i;
    long len;
    dlr_wle *dlr;
    Octstr *text;
    Msg *msg;
    int dlr_mask;
    
    debug("dlr.dlr",0,"Looking for DLR smsc=%s, ts=%s, dst=%s, type=%d",smsc,ts,dst,typ);
    len = list_len(dlr_waiting_list);
    for(i=0;i<len;i++)
    {
	dlr = list_get(dlr_waiting_list,i);
	
	if(    (strcmp(octstr_get_cstr(dlr->smsc),smsc) == 0) &&
	       (strcmp(octstr_get_cstr(dlr->timestamp),ts) == 0))
	{
	   /* lets save the service and dump the rest of the entry */

	   text = octstr_len(dlr->url) ? octstr_duplicate(dlr->url) : 
		   octstr_create("");	   

	   dlr_mask = dlr->mask;

	   if((typ & dlr_mask) > 0)
	   {
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
    	      debug("dlr.dlr",0,"created DLR message: %s",octstr_get_cstr(msg->sms.msgdata));
	   }
	   else
	   {
    		debug("dlr.dlr",0,"ignoring DLR message because of mask");
	   	/* ok that was a status report but we where not interested in having it */
	   	octstr_destroy(text);
		msg=NULL;
	   }
	    if ((typ & DLR_BUFFERED) && 
	       ((dlr_mask & DLR_SUCCESS) || (dlr_mask & DLR_FAIL))) {
	        info(0,"dlr not destroyed, still waiting for other delivery report"); 
	    } else {
		list_delete(dlr_waiting_list,i,1);
		dlr_destroy(dlr);
	    }

	    return msg;
	}
    }
    debug("dlr.dlr",0,"DLR not found!");
   /* we couldnt find a matching entry */
    return NULL;
#endif

}

