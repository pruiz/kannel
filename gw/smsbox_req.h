#ifndef _SMSBOX_REQ_H
#define _SMSBOX_REQ_H

/*
 * This is the SMS Service requester handler
 *
 * It can be used as a part of an independent SMS Box, or in
 * bearerbox thread SMS Box
 *
 * Kalle Marjola <rpr@wapit.com>
 */

#include "urltrans.h"
#include "cgi.h"

/*
 * Initialization routine. MUST be called first
 *
 * 'translations' are already depacked URLTranslations
 * 'sms_max_length' is max length of one message (160 normally)
 * 'global-sender' is backup sender number. String is strdupped
 *
 * 'sender' is function pointer to function that does the actual sending,
 *     that is either uses a socket (to bearerbox) or appends into reply
 *     queue (bearerbox thread smsbox). The sender function must free the
 *     message unless it stores it - however, it is now its responsibility.
 *     Sender function must return 0 on success, and -1 on failure
 *
 * Return -1 on error (couldn't strdup global_sender), 0 if Ok.
 */

int smsbox_req_init(URLTranslationList *translations,
		    Config *config,
		    int sms_max_length,
		    char *global_sender,
		    int (*sender) (Msg *msg));

/*
 * return the total number of request threads currently running
 */
int smsbox_req_count(void);


/*
 * handle one MO request. Arg is Msg *msg, and is void so that this can be
 * run directly with 'start_thread'
 */
void smsbox_req_thread(void *arg);


/*
 * handle sendsms request. Note that this does NOT start a new thread, but
 * instead must be called from appropriate HTTP-thread
 *
 * Returns 'answer' string (which shall NOT be freed by the caller)
 */
char *smsbox_req_sendsms(List *cgivars);


#endif
