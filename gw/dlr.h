/*
 * gw/dlr.h
 *
 * Implementation of handling delivery reports (DLRs)
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <tolj@wapme-systems.de>, 22.03.2002
 */

#ifndef	DLR_H
#define	DLR_H 1

#define	DLR_SUCCESS         0x01
#define	DLR_FAIL            0x02
#define	DLR_BUFFERED        0x04
#define	DLR_SMSC_SUCCESS    0x08
#define	DLR_SMSC_FAIL       0x10

#if defined(DLR_MYSQL) || defined(DLR_SDB)
#define DLR_DB 1
#endif

Mutex *dlr_mutex;
Octstr *dlr_type;

/*
 * DB specific global things
 */
#ifdef DLR_DB
#ifdef DLR_MYSQL
#include <mysql/mysql.h>
MYSQL *connection;
MYSQL mysql;
#endif
#ifdef DLR_SDB
#include <sdb.h>
char *connection;
#endif
Octstr *table;
Octstr *field_smsc, *field_ts, *field_src, *field_dst, *field_serv;
Octstr *field_url, *field_mask, *field_status, *field_boxc;
#endif

/* macros */
#define	O_DELETE(a)	 { if (a) octstr_destroy(a); a = NULL; }

/* DLR initialization routine (abstracted) */
void dlr_init(Cfg *cfg);

/* DLR shutdown routine (abstracted) */
void dlr_shutdown(void);

/* 
 * Add a new entry to the list 
 */
void dlr_add(char *smsc, char *ts, char *src, char *dst, 
             char *keyword, char *id, int mask, char *boxc);

/* 
 * Find an entry in the list. If there is one a message is returned and 
 * the entry is removed from the list otherwhise the message returned is NULL 
 */
Msg* dlr_find(char *smsc, char *ts, char *dst, int type);

void dlr_save(const char *filename);
void dlr_load(const char *filename);

/* return the number of DLR messages in the current waiting queue */
long dlr_messages(void);

/* 
 * Flush all DLR messages in the current waiting queue.
 * Beware to take bearerbox to suspended state before doing this.
 */
void dlr_flush(void);


#endif /* DLR_H */

