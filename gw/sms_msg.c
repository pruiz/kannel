/*
 */

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include "wapitlib.h"
#include "sms_msg.h"


/*--------------------------------------------------------------------
 * SMS message functions
 */


SMSMessage *smsmessage_construct(char *sender, char *receiver, Octstr *text) {
	SMSMessage *sms;
	
	if (text == NULL) {
		error(0, "smsmessage_construct: text is NULL");
		return NULL;
	}

	sms = malloc(sizeof(SMSMessage));
	if (sms == NULL)
		goto error;

	sms->sender = strdup(sender);
	sms->receiver = strdup(receiver);
	if (sms->sender == NULL || sms->receiver == NULL)
		goto error;
	
	sms->text = text;
	sms->has_udh = 0;
	sms->is_binary = 0;
	sms->time = (time_t) 0;
	return sms;

error:
	error(errno, "strdup failed");
	smsmessage_destruct(sms);
	return NULL;
}


int smsmessage_add_udh(SMSMessage *sms, int id, Octstr *data) {
	Octstr *temp, *temp2;
	char buf[2];
	unsigned char len;

	temp = NULL;
	temp2 = NULL;

	/* Prepend the length byte for the total length of the headers,
	   if the message doesn't already have one. */
	if (!sms->has_udh) {
		temp = octstr_create_from_data("\0", 1);
		if (temp == NULL)
			goto error;
		if (octstr_insert(sms->text, temp, 0) == -1)
			goto error;
		sms->has_udh = 1;
		octstr_destroy(temp);
	}
	
	buf[0] = id;
	buf[1] = (unsigned char) octstr_len(data);
	temp = octstr_create_from_data(buf, 2);
	if (temp == NULL)
		goto error;
	debug(0, "temp:");
	octstr_dump(temp);
	debug(0, "data:");
	octstr_dump(data);
	temp2 = octstr_cat(temp, data);
	debug(0, "temp2:");
	octstr_dump(temp2);
	if (temp2 == NULL)
		goto error;
	len = octstr_get_char(sms->text, 0);
	if (octstr_insert(sms->text, temp2, 1 + len) == -1)
		goto error;
	octstr_set_char(sms->text, 0, len + octstr_len(temp2));
	
	octstr_destroy(temp);
	octstr_destroy(temp2);
	return 0;

error:
	error(errno, "Out of memory");
	octstr_destroy(temp);
	octstr_destroy(temp2);
	return -1;
}



void smsmessage_destruct(SMSMessage *sms) {
	if(sms != NULL) {
		free(sms->sender);
		free(sms->receiver);
		free(sms);
	}
}

