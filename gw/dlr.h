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


/* DLR initialization routine (abstracted) */
void dlr_init(Cfg *cfg);

/* DLR shutdown routine (abstracted) */
void dlr_shutdown(void);

/* 
 * Add a new entry to the list 
 */
void dlr_add(const Octstr *smsc, const Octstr *ts, const Msg *msg);

/* 
 * Find an entry in the list. If there is one a message is returned and 
 * the entry is removed from the list otherwhise the message returned is NULL 
 */
Msg* dlr_find(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int type);

/* return the number of DLR messages in the current waiting queue */
long dlr_messages(void);

/* 
 * Flush all DLR messages in the current waiting queue.
 * Beware to take bearerbox to suspended state before doing this.
 */
void dlr_flush(void);

/*
 * Return type of dlr storage
 */
const char* dlr_type();

/*
 * Yet not used functions.
 */
void dlr_save(const char *filename);
void dlr_load(const char *filename);

#endif /* DLR_H */

