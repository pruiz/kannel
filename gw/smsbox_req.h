/*
 * smsbox_req.h - fulfill sms requests from users
 */

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

extern List *smsbox_requests;

/*
 * Initialization routine. MUST be called first
 *
 * 'translations' are already depacked URLTranslations
 * 'sms_max_length' is max length of one message (160 normally)
 * 'global_sender' is backup sender number, which can be NULL.
 *  String is strdupped
 * 'accept_str' is string of accepted characters in 'to' field in
 *  send-sms request. If NULL, defaults to "0123456789 +-"
 */

void smsbox_req_init(URLTranslationList *translations,
		    Config *config,
		    int sms_max_length,
		    char *global_sender,
		    char *accept_str);

/*
 * Shut down the request module, must be called last.
 */
void smsbox_req_shutdown(void);

/*
 * return the total number of request threads currently running
 */
long smsbox_req_count(void);


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
char *smsbox_req_sendsms(List *cgivars, char *client_ip);

/*
 * handle sendota request. Note that this does NOT start a new thread, but
 * instead must be called from appropriate HTTP-thread
 *
 * Returns 'answer' string (which shall NOT be freed by the caller)
 */
char *smsbox_req_sendota(List *cgivars, char *client_ip);

#endif
