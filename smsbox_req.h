#ifndef _SMSBOX_REQ_H
#define _SMSBOX_REQ_H

/*
 * This is the SMS Service requester handler
 *
 * It can be used as a part of an independent SMS Box, or in
 * Bearebox thread SMS Box
 */

#include "urltrans.h"
#include "cgi.h"

/*
 * Initialization routine. MUST be called first
 *
 * 'translations' are already depacked URLTranslations
 * 'sms_max_length' is max length of one message (160 normally)
 * 'global-sender' is backup sender number. String is strdupped
 * 'sender' is function pointer to function that does the actual sending,
 *     that is either uses a socket (to bearer box) or appends into reply
 *     queue (bearerbox thread smsbox)
 *
 * Return -1 on error, 0 if Ok.
 */

int smsbox_req_init(URLTranslationList *translations,
		    int sms_max_length,
		    char *global_sender,
		    int (*sender) (Msg *msg));

/*
 * return the total number of request threads currently running
 */
int smsbox_req_count(void);


/*
 * handle one MO request. Arg is Msg *smg, and is void so that this can be
 * run directly with 'start_thread'
 */
void *smsbox_req_thread(void *arg);


/*
 * handle sendsms request. Note that this does NOT start a new thread, but
 * instead must be called from appropriate HTTP-thread
 *
 * Returns 'asnwer' string
 */
char *smsbox_req_sendsms(CGIArg *list);


#endif
