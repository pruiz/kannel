/*
 * gw/dlr_p.h
 *
 * Implementation of handling delivery reports (DLRs)
 * These are private header.
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <tolj@wapme-systems.de>, 22.03.2002
 * Alexander Malysh <amalysh@centrium.de>
*/

#ifndef	DLR_P_H
#define	DLR_P_H 1

#define DLR_TRACE 1

/*
 * The structure of a delivery report  entry.
 */
struct dlr_entry {
   Octstr *smsc;
   Octstr *timestamp;
   Octstr *source;
   Octstr *destination;
   Octstr *service;
   Octstr *url;
   Octstr *boxc_id;
   int mask;
};

/*
 * Create struct dlr_entry and initialize it to zero
 */
struct dlr_entry *dlr_entry_create();

/*
 * Destroy struct dlr_entry
 */
void dlr_entry_destroy(struct dlr_entry *dlr);

/*
 * Duplicate dlr entry
 */
struct dlr_entry *dlr_entry_duplicate(const struct dlr_entry *dlr);

/* 
 * Callback functions to hanlde specifical dlr storage type 
 */
struct dlr_storage {
    /*
     * Type of storage. Used for status reguest.
     */
    const char* type;
    /*
     * Add dlr entry into storage.
     * NOTE: this function is responsible to destroy struct dlr_entry
     */
    void (*dlr_add) (struct dlr_entry *entry);
    /*
     * Find and return struct dlr_entry. If entry not found return NULL.
     * NOTE: Caller will detroy struct dlr_entry
     */
    struct dlr_entry* (*dlr_get) (const Octstr *smsc, const Octstr *ts, const Octstr *dst);
    /*
     * Remove matching dlr entry from storage
     */
    void (*dlr_remove) (const Octstr *smsc, const Octstr *ts, const Octstr *dst);
    /*
     * Update dlr entry status field if any.
     */
    void (*dlr_update) (const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status);
    /*
     * Return count dlr entries in storage.
     */
    long (*dlr_messages) (void);
    /*
     * Flush storage
     */
    void (*dlr_flush) (void);
    /*
     * Shutdown storage
     */
    void (*dlr_shutdown) (void);
};

/*
 * Will be used by DB based storage types.
 * We have helper init function also.
 */
struct dlr_db_fields {
    Octstr *table;
    Octstr *field_smsc;
    Octstr *field_ts;
    Octstr *field_src;
    Octstr *field_dst;
    Octstr *field_serv;
    Octstr *field_url;
    Octstr *field_mask;
    Octstr *field_status;
    Octstr *field_boxc;
};

struct dlr_db_fields *dlr_db_fields_create(CfgGroup *grp);
void dlr_db_fields_destroy(struct dlr_db_fields *fields);

/*
 * Storages we have already. This will gone in future
 * if we have module API implemented.
 */
struct dlr_storage *dlr_init_mem(Cfg *cfg);
struct dlr_storage *dlr_init_mysql(Cfg *cfg);
struct dlr_storage *dlr_init_sdb(Cfg *cfg);

#endif /* DLR_P_H */
