#ifndef SMS_MSG_H
#define SMS_MSG_H


#include <stdio.h>
#include <time.h>

#include "gwlib.h"

/* NOTE: This is depricated thing, should not be used by anyone anymore! */


/*
 * A data structure holding one SMS message. `sender' and `receiver'
 * are strings containing the phone numbers of the sender and
 * receiver. `text' is an octet string containing the contents of the
 * message.
 *
 * The `sender' and `receiver' fields semantics are 
 *  "[00][c|cc|ccc][nnnnnnnnnn]" where 
 *    - [c|cc|ccc] is the country code
 *    - [nnnnnnnnnn] is the max 10(???) digit phone number
 * 
 * An SMS message can contain User Data Headers (see GSM 03.40,
 * 9.2.3.23 and 9.2.3.24, page 55 in the 5.3.0/July 1996 version).
 * These allow, for example, WAP protocols to run over SMS. The
 * headers are prepended to the text part of the SMS. `has_udh' is
 * 0 if the message doesn't contain UDH, and 1 if it does. The headers
 * are prepended to `text'.
 *
 * EMI requires that the Message-Type is set correctly, therefore we
 * need a flag for binary messages
 *
 * `time' is the time the SMS message was sent, or received by the SMSC,
 * or (if the SMSC doesn't tell us that) when it was received from the
 * SMSC.
 *
 * Note that this data structure is not opaque: users may reference
 * the fields directly.
 */
typedef struct {
	char *sender;
	char *receiver;
	Octstr *text;
	int has_udh;
	int is_binary;
	time_t time;
        int id;		/* new: used by the SMS BOX */
} SMSMessage;


/* 
 * Allocate one SMSMessage in dynamic memory.
 * Return pointer to it, or NULL if the operation failed.
 */
SMSMessage *smsmessage_construct(char *sender, char *receiver, Octstr *text);


/*
 * Add a new User Data Header to the message. The new header will come
 * after the headers that have already been added, just before the
 * actual text of the message.
 *
 * `id' is the identifier for the header. `data' is the contents.
 * It is the caller's responsibility to make sure the headers (including
 * their length and other data) and the text of the message doesn't
 * exceed the length limit of an SMS message (160 7-bit characters or
 * 140 8-bit octets).
 *
 * XXX the length should be checked and an error returned if too long
 */
void smsmessage_add_udh(SMSMessage *sms, int id, Octstr *data);


/*
 * Free dynamic memory for one SMSMesssage. 
 * The message may no longer be used after it has been freed. 
 */
void smsmessage_destruct(SMSMessage *sms);



#endif
