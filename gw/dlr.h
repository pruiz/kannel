/*
 * gwlib/dlr.h
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

/*
 * MySQL specific global things
 */
#ifdef DLR_MYSQL
#include <mysql/mysql.h>
MYSQL *connection;
MYSQL mysql;
Octstr *table;
Octstr *field_smsc, *field_ts, *field_dst, *field_serv;
Octstr *field_url, *field_mask, *field_status;
#endif

/* macros */
#define	O_DELETE(a)	 { if (a) octstr_destroy(a); a = NULL; }

/* DLR initialization routine (abstracted) */
void dlr_init(Cfg *cfg);

/* DLR shutdown routine (abstracted) */
void dlr_shutdown();

/* 
 * Add a new entry to the list 
 */
void dlr_add(char *smsc, char *ts, char *dst, char *keyword, char *id, int mask);

/* 
 * Find an entry in the list. If there is one a message is returned and 
 * the entry is removed from the list otherwhise the message returned is NULL 
 */
Msg* dlr_find(char *smsc, char *ts, char *dst, int type);

void dlr_save(const char *filename);
void dlr_load(const char *filename);

#endif /* DLR_H */

