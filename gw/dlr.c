/* dlr.c
** delivery reports
** generic routines
** Andreas Fink <andreas@fink.org> ,18.8.2001
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

#ifndef DLR_TRACE
#define DLR_TRACE 0
#endif


/* the structure of a delivery report waiting list entry */

typedef struct	dlr_wle
{
   Octstr	*smsc;
   Octstr	*timestamp;	
   Octstr	*destination;
   Octstr	*keyword;
   Octstr	*id;	
   int		mask;
} dlr_wle;

void dump_dlr(dlr_wle *dlr);

/* this is the global list where all messages being sent out are being kept track of */
/* this list is looked up once a delivery report comes in */

static	List	*dlr_waiting_list;

void	dlr_destroy(dlr_wle *dlr);


/* at startup initialize the list */

void	dlr_init()
{
	dlr_waiting_list = list_create();
}


/* at shutdown, destroy the list */
void	dlr_shutdown()
{
	list_destroy(dlr_waiting_list, (list_item_destructor_t *)dlr_destroy);
}


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
	O_DELETE (dlr->keyword);
	O_DELETE (dlr->id);
	dlr->mask = 0;
	gw_free(dlr);
}


/* add a new entry to the list */
void	dlr_add(char *smsc, char *ts, char *dst, char *keyword, char *id, int mask)
{
   dlr_wle	*dlr;
	
   info(0,"Adding to DLR list smsc=%s, ts=%s, dst=%s, keyword=%s, id=%s mask=%d",smsc,ts,dst,keyword,id,mask);
   if ((mask & DLR_SUCCESS) | (mask & DLR_FAIL))
   {
	dlr = dlr_new(); 
	
	dlr->smsc = octstr_create(smsc);
	dlr->timestamp = octstr_create(ts);
	dlr->destination = octstr_create(dst);
	dlr->keyword = octstr_create(keyword); /* octstr_imm(keyword); */
	dlr->id = octstr_create(id);
	dlr->mask = mask;
	list_append(dlr_waiting_list,dlr);
	info(0,"appended");
   }
   else
   	info(0,"ignored");
}

/* find an entry in the list. if there is one a message is returned and the entry is removed from the list
otherwhise the message returned is NULL */

Msg *dlr_find(char *smsc, char *ts, char *dst, int typ)
{
    long i;
    long len;
    dlr_wle *dlr;
    Octstr *text;
    Msg *msg;
    int dlr_mask;
    
    info(0,"Looking for DLR smsc=%s, ts=%s, dst=%s, type=%d",smsc,ts,dst,typ);
    len = list_len(dlr_waiting_list);
    for(i=0;i<len;i++)
    {
	dlr = list_get(dlr_waiting_list,i);
	info(0,"checking entry %d in list",(int)i);
	dump_dlr(dlr);
	
	if(    (strcmp(octstr_get_cstr(dlr->smsc),smsc) == 0) &&
	       (strcmp(octstr_get_cstr(dlr->timestamp),ts) == 0))
	{
	   /* lets save the keyword and dump the rest of the entry */
	   info(0,"DLR found!");

	   text = octstr_duplicate(dlr->keyword);	   
	   if(typ == DLR_SUCCESS)
                 octstr_append_cstr(text," 1 ");
           else
                 octstr_append_cstr(text," 2 ");
           octstr_append(text,dlr->id);
           octstr_append_cstr(text," ");
           octstr_append_cstr(text,smsc);

	   dlr_mask = dlr->mask;
	   list_delete(dlr_waiting_list,i,1);
	   dlr_destroy(dlr);

	   if((typ & dlr_mask) > 0)
	   {
	       	/* its an entry we are interested in */
	      info(0,"creating DLR message");
	      msg = msg_create(sms);
 	      msg->sms.smsc_id = octstr_create(smsc);
    	      msg->sms.sender  = octstr_create(dst);
              msg->sms.receiver = octstr_create("000");
	      msg->sms.msgdata = text;
	      info(0,"DLR = %s",octstr_get_cstr(text));
              return msg;
	   }
	   else
	   {
	        info(0,"ignoring DLR message because of mask");
	   	/* ok that was a status report but we where not interested in having it */
	   	octstr_destroy(text);
		return NULL;
	   }
	}
    }
    info(0,"DLR not found!");
   /* we couldnt find a matching entry */
    return NULL;
}

void dump_dlr(dlr_wle *dlr)
{
    if(!dlr)
    {
    	info(0,"dlr == NULL");
    	return;
    }

    if(!dlr->smsc)
	info(0,"dlr->smsc = NULL");
    else
    	info(0,"dlr->smsc = '%s'",octstr_get_cstr(dlr->smsc));
 
     if(!dlr->timestamp)
	info(0,"dlr->timestamp = NULL");
    else
    	info(0,"dlr->timestamp = '%s'",octstr_get_cstr(dlr->timestamp));
   	
 
     if(!dlr->destination)
	info(0,"dlr->destination = NULL");
    else
    	info(0,"dlr->destination = '%s'",octstr_get_cstr(dlr->destination));
   	
 
     if(!dlr->keyword)
	info(0,"dlr->keyword = NULL");
    else
    	info(0,"dlr->keyword = '%s'",octstr_get_cstr(dlr->keyword));
   	
 
     if(!dlr->id)
	info(0,"dlr->id = NULL");
    else
    	info(0,"dlr->id = '%s'",octstr_get_cstr(dlr->id));
    info(0,"dlr->mask = %d", dlr->mask);

}
