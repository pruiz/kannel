/*
 * smsc.c - implement interface to SMS centers as defined by smsc.h
 *
 * Lars Wirzenius and Kalle Marjola for WapIT Ltd.
 */

/* NOTE: private functions (only for smsc_* use) are named smscenter_*,
 * public functions (used by gateway) are named smsc_*
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
#include "smsc.h"
#include "sms_msg.h"
#include "smsc_p.h"
#include "config.h"
#include "bb_msg.h"

/*
 * Maximum number of characters for read_into_buffer to read at a time.
 */
#define MAX_READ_INTO_BUFFER	(1024)


static int smscenter_lock(SMSCenter *smsc);
static int smscenter_unlock(SMSCenter *smsc);

/*--------------------------------------------------------------------
 * TODO: WAP WDP functions!
 */


/*--------------------------------------------------------------------
 * smscenter functions
 */



SMSCenter *smscenter_construct(void) {
	SMSCenter *smsc;
	static int next_id = 1;

	smsc = malloc(sizeof(SMSCenter));
	if (smsc == NULL)
		goto error;

	smsc->type = SMSC_TYPE_DELETED;
	smsc->dial_prefix[0] = '\0';
	smsc->alt_charset = 0;

#if HAVE_THREADS
	pthread_mutex_init(&smsc->mutex, NULL);
#else
	smsc->mutex = 0;
#endif

	sprintf(smsc->name, "Unknown SMSC");
	smsc->id = next_id++;

	/* FAKE */
	smsc->hostname = NULL;
	smsc->port = -1;
	smsc->socket = -1;

	/* CIMD */
	smsc->cimd_hostname = NULL;
	smsc->cimd_port = -1;
	smsc->cimd_username = NULL;
	smsc->cimd_password = NULL;

	/* EMI */
	smsc->emi_phonenum = NULL;
	smsc->emi_serialdevice = NULL;
	smsc->emi_username = NULL;
	smsc->emi_password = NULL;

	/* EMI IP */
	smsc->emi_hostname = NULL;
	smsc->emi_port = -1;

	/* Memory */
	smsc->buflen = 0;
	smsc->bufsize = 10*1024;
	smsc->buffer = malloc(smsc->bufsize);
	if (smsc->buffer == NULL)
		goto error;
	bzero(smsc->buffer, smsc->bufsize);

	return smsc;

error:
	error(errno, "smscenter_construct: memory allocation failed");
	smscenter_destruct(smsc);	
	return NULL;
}


void smscenter_destruct(SMSCenter *smsc) {
	if(smsc == NULL)
		return;

	/* FAKE */
	free(smsc->hostname);

	/* CIMD */
	free(smsc->cimd_hostname);
	free(smsc->cimd_username);
	free(smsc->cimd_password);

	/* EMI */
	free(smsc->emi_phonenum);
	free(smsc->emi_serialdevice);
	free(smsc->emi_username);
	free(smsc->emi_password);

	/* EMI IP */
	free(smsc->emi_hostname);

	/* Memory */
	free(smsc->buffer);
	free(smsc);
}




int smscenter_submit_smsmessage(SMSCenter *smsc, SMSMessage *msg) {
	if (smscenter_lock(smsc) == -1)
		return -1;

	switch (smsc->type) {
	case SMSC_TYPE_FAKE:
		if (fake_submit_smsmessage(smsc->socket, msg) == -1)
			goto error;
		break;

	case SMSC_TYPE_CIMD:
		if(cimd_submit_smsmessage(smsc, msg) == -1)
			goto error;
		break;

	case SMSC_TYPE_EMI:
	case SMSC_TYPE_EMI_IP:
		if(emi_submit_smsmessage(smsc, msg) == -1)
			goto error;
		break;

	case SMSC_TYPE_SMPP_IP:
		if(smpp_submit_smsmessage(smsc, msg) == -1)
			goto error;
		break;

	default:
		goto error;
	}

	smscenter_unlock(smsc);
	return 0;

error:
	smscenter_unlock(smsc);
	return -1;
}


int smscenter_receive_smsmessage(SMSCenter *smsc, SMSMessage **msg) {
	int ret, retval;
	char *new;

	if (smscenter_lock(smsc) == -1)
		return -1;

	switch (smsc->type) {

	case SMSC_TYPE_FAKE:
		ret = fake_receive_smsmessage(smsc, msg);
		if (ret == -1)
			goto error;
		break;

	case SMSC_TYPE_CIMD:
		ret = cimd_receive_smsmessage(smsc, msg);
		if (ret == -1)
			goto error;
		break;
	
	case SMSC_TYPE_EMI:
	case SMSC_TYPE_EMI_IP:
		ret = emi_receive_smsmessage(smsc, msg);
		if (ret == -1)
			goto error;
		break;

	case SMSC_TYPE_SMPP_IP:
		ret = smpp_receive_smsmessage(smsc, msg);
		if (ret == -1)
			goto error;
		break;

	default:
		goto error;

	}

	smscenter_unlock(smsc);
	
	/* Fix the time if the SMSC didn't tell us it. */
	if (ret == 1 && (*msg)->time == 0)
		time(&(*msg)->time);

	/* Make sure the sender number starts with smsc->dial_prefix,
	   if it doesn't, but should. */
	retval = normalize_number(smsc->dial_prefix, (*msg)->sender, &new);
	if (new == NULL) {
		error(0, "Couldn't normalize phone number. Dropping message.");
		goto error;
	}
	free((*msg)->sender);
	(*msg)->sender = new;
	
	return ret;

error:
	smscenter_unlock(smsc);
	return -1;
}


int smscenter_pending_smsmessage(SMSCenter *smsc) {
	int ret;

	if (smscenter_lock(smsc) == -1)
		return -1;

	switch (smsc->type) {
	case SMSC_TYPE_FAKE:
		ret = fake_pending_smsmessage(smsc);
		if (ret == -1)
			goto error;
		break;
	
	case SMSC_TYPE_CIMD:
		ret = cimd_pending_smsmessage(smsc);
		if (ret == -1)
			goto error;
		break;

	case SMSC_TYPE_EMI:
	case SMSC_TYPE_EMI_IP:
		ret = emi_pending_smsmessage(smsc);
		if (ret == -1)
			goto error;
		break;

	case SMSC_TYPE_SMPP_IP:
		ret = smpp_pending_smsmessage(smsc);
		if (ret == -1)
			goto error;
		break;

	default:
		goto error;
	}

	smscenter_unlock(smsc);
	return ret;

error:
	error(0, "smscenter_pending_smsmessage is failing");
	smscenter_unlock(smsc);
	return -1;
}


int smscenter_read_into_buffer(SMSCenter *smsc) {
	char *p;
	int ret, result;
	fd_set read_fd;
	struct timeval tv, tvinit;
	size_t bytes_read;

	tvinit.tv_sec = 0;
	tvinit.tv_usec = 1000;

	bytes_read = 0;
	result = 0;
	for (;;) {
		FD_ZERO(&read_fd);
		FD_SET(smsc->socket, &read_fd);
		tv = tvinit;
		ret = select(smsc->socket + 1, &read_fd, NULL, NULL, &tv);
		if (ret == -1) {
			error(errno, "Error doing select for socket");
			goto error;
		} else if (ret == 0)
			goto got_data;

		if (smsc->buflen == smsc->bufsize) {
			p = realloc(smsc->buffer, smsc->bufsize * 2);
			if (p == NULL) {
				error(errno, 
					"Couldn't allocate read buffer");
				goto error;
			}
			smsc->buffer = p;
			smsc->bufsize *= 2;
		}

		ret = read(smsc->socket,
			   smsc->buffer + smsc->buflen,
			   1);
		if (ret == -1) {
			error(errno, "Reading from `%s' port `%d' failed.", 
				smsc->hostname, smsc->port);
			goto error;
		}
		if (ret == 0)
			goto eof;
		smsc->buflen += ret;
		bytes_read += ret;
		if (bytes_read >= MAX_READ_INTO_BUFFER)
			break;
	}

eof:
	ret = 0;
	goto unblock;

got_data:
	ret = 1;
	goto unblock;

error:
	ret = -1;
	goto unblock;

unblock:
	return ret;
}


void smscenter_remove_from_buffer(SMSCenter *smsc, size_t n) {
	memmove(smsc->buffer, smsc->buffer + n, smsc->buflen - n);
	smsc->buflen -= n;
}


/*
 * Lock an SMSCenter. Return -1 for error, 0 for OK. 
 */
static int smscenter_lock(SMSCenter *smsc) {
	int ret;

	if (smsc->type == SMSC_TYPE_DELETED)
		return -1;

#if HAVE_THREADS
	ret = pthread_mutex_lock(&smsc->mutex);
	
#else
	if (smsc->mutex)
		ret = -1;
	else {
		ret = 0;
		smsc->mutex = 1;
	}
#endif

	if (ret != 0) {
		error(ret, "Couldn't lock the SMSCenter data structure");
		return -1;
	}

	return 0;
}


/*
 * Unlock an SMSCenter. Return -1 for error, 0 for OK.
 */
static int smscenter_unlock(SMSCenter *smsc) {
	int ret;

#if HAVE_THREADS
	ret = pthread_mutex_unlock(&smsc->mutex);
#else
	if (smsc->mutex) {
		ret = 0;
		smsc->mutex = 0;
	} else
		ret = -1;
#endif

	if (ret != 0) {
		error(ret, "Couldn't unlock SMSC data structure");
		return -1;
	}

	return 0;

}


/*
 * Normalize a phone number. `dial_prefixes' is a list of prefix of
 * the following format:
 *
 *	"0035850,050;0035840,040"
 *
 * The alternatives are separated by commas. The first one is the
 * official one; if the phone number begins with any of the others,
 * it is replaced with the official one.
 *
 * If there is several 'official' numbers, each entry is separated with ';'
 *
 * The new string is returned as a 'new', dynamically allocated string,
 * even if it hasn't been modified. The caller must free it.
 *
 * return value -1 on error, 0 if no match found, 1 if match found
 */
int normalize_number(char *dial_prefixes, char *number, char **new) {
	char *official, *p, *tmps;
	char copy[DIAL_PREFIX_MAX_LEN];
	char tmp[DIAL_PREFIX_MAX_LEN];

	if (dial_prefixes == NULL || dial_prefixes[0] == '\0') {
	        *new = malloc(strlen(number)+1);
		if (*new == NULL) {
		    error(errno, "Out of memory normalizing phone number.");
		    return -1;
		}
		strcpy(*new, number);
		return 0;
	}
	*new = malloc(strlen(dial_prefixes) + strlen(number) + 1);
	if (*new == NULL) {
		error(errno, "Out of memory normalizing phone number.");
		return -1;
	}
	

	strcpy(tmp, dial_prefixes);
	tmps = tmp;

	while (tmps != NULL && (p = strtok(tmps, ";")) != NULL) {
	    strcpy(copy, tmps);
	    tmps += strlen(copy)+1;	/* store next... */
	
	    official = strtok(copy, ",");

	    /* Begins with official prefix? */	
	    if (strncmp(number, official, strlen(official)) == 0) {
		strcpy(*new, number);
		return 1;
	    }

	    /* Begins with one of the alternatives? */
	    p = strtok(NULL, ",");
	    while (p != NULL) {
		if (strncmp(number, p, strlen(p)) == 0) {
			sprintf(*new, "%s%s", official, number + strlen(p));
			return 1;
		}
		p = strtok(NULL, ",");
	    }
	}
	/* Use as is. */
	strcpy(*new, number);
	return 0;
}


/*------------------------------------------------------------------------
 * Public SMSC functions
 */


SMSCenter *smsc_open(ConfigGroup *grp) {
	SMSCenter *smsc;
        char *type, *host, *port, *username, *password, *phone, *device;
        char *dial_prefix, *route_prefix;
        char *backup_port;      /* EMI IP */
        char *alt_chars;
        char *smpp_system_id, *smpp_system_type, *smpp_address_range;

        int typeno, portno, backportno;


        type = config_get(grp, "smsc");
        host = config_get(grp, "host");
        port = config_get(grp, "port");
        backup_port = config_get(grp, "backup-port");
        username = config_get(grp, "username");
        password = config_get(grp, "password");
        phone = config_get(grp, "phone");
        device = config_get(grp, "device");
        dial_prefix = config_get(grp, "dial-prefix");
        route_prefix = config_get(grp, "route-prefix");
        alt_chars = config_get(grp, "alt-charset");

        smpp_system_id = config_get(grp, "system-id");
        smpp_system_type = config_get(grp, "system-type");
        smpp_address_range = config_get(grp, "address-range");

	portno = (port != NULL ? atoi(port) : 0);
	backportno = (backup_port != NULL ? atoi(backup_port) : 0);

	smsc = NULL;

	if (strcmp(type, "fake") == 0) typeno = SMSC_TYPE_FAKE;
	else if (strcmp(type, "cimd") == 0) typeno = SMSC_TYPE_CIMD;
	else if (strcmp(type, "emi") == 0) typeno = SMSC_TYPE_EMI;
	else if (strcmp(type, "emi_ip") == 0) typeno = SMSC_TYPE_EMI_IP;
	else if (strcmp(type, "smpp") == 0) typeno = SMSC_TYPE_SMPP_IP;
	else {
	    error(0, "Unknown SMSC type '%s'", type);
	    return NULL;
	}
	switch (typeno) {
	case SMSC_TYPE_FAKE:
	    if (host == NULL || portno == 0)
		error(0, "'host' or 'port' invalid in 'fake' record.");
	    else {
		smsc = fake_open(host, portno);
		break;
	    }

	case SMSC_TYPE_CIMD:
	    if (host == NULL || portno == 0 || username == NULL ||
		password == NULL)
		error(0, "Required field missing for CIMD center.");
	    else
		smsc = cimd_open(host, portno, username, password);
	    break;

	case SMSC_TYPE_EMI:
	    if (phone == NULL || device == NULL || username == NULL ||
		password == NULL)
		error(0, "Required field missing for EMI center.");
	    else
		smsc = emi_open(phone, device, username, password);
	    break;

	case SMSC_TYPE_EMI_IP:
	    if (host == NULL || port == NULL || username == NULL ||
		password == NULL)
		error(0, "Required field missing for EMI IP center.");
            else
		smsc = emi_open_ip(host, portno, username, password,
				   backportno);
	    break;

	case SMSC_TYPE_SMPP_IP:
	    if (host == NULL || port == NULL || 
	    	smpp_system_id == NULL || password == NULL)
		error(0, "Required field missing for SMPP center.");
	    else
		smsc = smpp_open(host, portno, smpp_system_id,
				 password, smpp_system_type,
				 smpp_address_range);
	    break;

	default:		/* Unknown SMSC type */
		break;
	}
	if (smsc != NULL) {
	    smsc->alt_charset = (alt_chars != NULL ? atoi(alt_chars) : 0);

	    sprintf(smsc->dial_prefix, "%.*s",
		    (int) sizeof(smsc->dial_prefix),
		    (dial_prefix == NULL) ? "" : dial_prefix);

	    sprintf(smsc->route_prefix, "%.*s",
		    (int) sizeof(smsc->route_prefix),
		    (route_prefix == NULL) ? "" : route_prefix);

	    info(0, "Opened a new SMSC type %d", typeno);
	}
	
	return smsc;
}



int smsc_reopen(SMSCenter *smsc) {

	switch (smsc->type) {
	case SMSC_TYPE_FAKE:
	    return fake_reopen(smsc);
	case SMSC_TYPE_CIMD:
	    return cimd_reopen(smsc);
	case SMSC_TYPE_EMI_IP:
	    return emi_reopen_ip(smsc);
	case SMSC_TYPE_EMI:
	    return emi_reopen(smsc);
	case SMSC_TYPE_SMPP_IP:
	default:		/* Unknown SMSC type */
	    return -2;		/* no use */
	}
}



char *smsc_name(SMSCenter *smsc) {
    return smsc->name;
}


int smsc_receiver(SMSCenter *smsc, char *number)
{
    char *p, *b;

    p = smsc->route_prefix;

    while(*p != '\0') {
	b = number;
	for(b = number; *b != '\0'; b++, p++) {
	    if (*p == ';' || *p == '\0') {
		return 1;
	    }
	    if (*p != *b) break;
	}
	while(*p != '\0' && *p != ';')
	    p++;
	while(*p == ';') p++;
    }
    if (strstr(smsc->route_prefix, "default") != NULL)
	return 2;		/* default */

    return 0;
}


int smsc_close(SMSCenter *smsc) {
	int errors = 0;

	if (smsc == NULL)
	    return 0;
	
	if (smscenter_lock(smsc) == -1)
		return -1;
	
	switch (smsc->type) {
	case SMSC_TYPE_FAKE:	/* Our own fake SMSC */
		if (fake_close(smsc) == -1)
			errors = 1;
		break;

	case SMSC_TYPE_CIMD:
		if (cimd_close(smsc) == -1)
			errors = 1;
		break;

	case SMSC_TYPE_EMI:
		if (emi_close(smsc) == -1)
			errors = 1;
		break;

	case SMSC_TYPE_EMI_IP:
		if (emi_close_ip(smsc) == -1)
			errors = 1;
		break;

	case SMSC_TYPE_SMPP_IP:
		if (smpp_close(smsc) == -1)
			errors = 1;
		break;

	default:		/* Unknown SMSC type */
		break;
	}

	smsc->type = SMSC_TYPE_DELETED;
	if (smscenter_unlock(smsc) == -1)
		errors = 1;

	if (errors)
		return -1;

	return 0;
}



int smsc_send_message(SMSCenter *smsc, RQueueItem *msg, RQueue *request_queue)
{
    SMSMessage *sms_msg;
    int ret;
    
    if (msg->msg_class == R_MSG_CLASS_WAP) {
	error(0, "SMSC:WAP messages not yet supported, tough");
	return -1;
    }	

    if (msg->msg_type == R_MSG_TYPE_ACK) {
	debug(0, "SMSC:Read ACK [%d] from queue, ignoring.", msg->id);
	ret = 0;
    } else if (msg->msg_type == R_MSG_TYPE_NACK) {
	debug(0, "SMSC:Read NACK [%d] from queue, ignoring.", msg->id);
	ret = 0;
    }  else if (msg->msg_type == R_MSG_TYPE_MT) {

	info(0, "Send SMS Message [%d] to SMSC", msg->id);
	
	sms_msg = smsmessage_construct(msg->sender, msg->receiver, msg->msg);
    
	ret = smscenter_submit_smsmessage(smsc, sms_msg);
	if (ret == -1)
	    /* rebuild connection? */
	    ;
	if (ret < 0)
	    msg->msg_type = R_MSG_TYPE_NACK;
	else
	    msg->msg_type = R_MSG_TYPE_ACK;

	rq_push_msg_ack(request_queue, msg);
	return ret;
    }
    else {
	error(0, "SMSC:Unknown message type '%d' to be sent by SMSC, ignored",
	      msg->msg_type);
	ret = -1;
    }
/* TODO:		smsmessage_destruct(msg->client_data); */
    rqi_delete(msg);

    return ret;
}


RQueueItem *smsc_get_message(SMSCenter *smsc)
{
    SMSMessage *sms_msg;
    RQueueItem *msg;
    char *p;
    int ret;
    

    if (smscenter_pending_smsmessage(smsc) == 1) {

	ret = smscenter_receive_smsmessage(smsc, &sms_msg);
	if (ret < 1) {
	    error(0, "Failed to receive the message, ignore...");
	    /* reopen the connection etc. invisible to other end */
	}

	/* hm, what about ACK/NACK? */

	msg = rqi_new(R_MSG_CLASS_SMS, R_MSG_TYPE_MO);

	/* normalization should be done much better way, have to think
	 * about that */

	normalize_number(smsc->dial_prefix, sms_msg->sender, &p);
	strcpy(msg->sender, p);
	free(p);
	normalize_number(smsc->dial_prefix, sms_msg->receiver, &p);
	strcpy(msg->receiver, p);
	free(p);
	msg->msg = octstr_copy(sms_msg->text, 0, 160);
	
	msg->client_data = sms_msg;	/* keep the data for ACK/NACK */
	
	
	return msg;		/* ok, quite empty one */
    }
    return NULL;
}

