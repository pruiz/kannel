/*
 * radius_acct.h - RADIUS accounting proxy thread declarations
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#ifndef RADIUS_ACCT_H
#define RADIUS_ACCT_H


/*
 * Start the RADIUS accounting proxy accoding to the given configuration
 * group provided by the caller.
 */
void radius_acct_init(CfgGroup *grp);

/*
 * Stop the RADIUS accounting proxy and destroy all depended structures
 * and hash tables.
 */
void radius_acct_shutdown(void);

/*
 * Provides the mapping from client IP to client MSISDN from inside the 
 * RADIUS accounting mapping tables. If the client IP is not found or NULL
 * has been given as argument, returns NULL.
 */
Octstr *radius_acct_get_msisdn(Octstr *client_ip);


#endif
