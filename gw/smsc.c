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

#include "gwlib.h"
#include "smsc.h"
#include "smsc_p.h"
#include "bb_msg.h"

/*
 * Maximum number of characters for read_into_buffer to read at a time.
 */
#define MAX_READ_INTO_BUFFER	(1024)


static void smscenter_lock(SMSCenter *smsc);
static void smscenter_unlock(SMSCenter *smsc);

/*--------------------------------------------------------------------
 * TODO: WAP WDP functions!
 */


/*--------------------------------------------------------------------
 * smscenter functions
 */



SMSCenter *smscenter_construct(void) {
	SMSCenter *smsc;
	static int next_id = 1;

	smsc = gw_malloc(sizeof(SMSCenter));

	smsc->killed = 0;
	smsc->type = SMSC_TYPE_DELETED;
	smsc->dial_prefix = NULL;
	smsc->route_prefix = NULL;
	smsc->alt_charset = 0;

	smsc->mutex = mutex_create();

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
	smsc->buffer = gw_malloc(smsc->bufsize);
	bzero(smsc->buffer, smsc->bufsize);

	return smsc;
}


void smscenter_destruct(SMSCenter *smsc) {
	if(smsc == NULL)
		return;

	/* FAKE */
	gw_free(smsc->hostname);

	/* CIMD */
	gw_free(smsc->cimd_hostname);
	gw_free(smsc->cimd_username);
	gw_free(smsc->cimd_password);

	/* EMI */
	gw_free(smsc->emi_phonenum);
	gw_free(smsc->emi_serialdevice);
	gw_free(smsc->emi_username);
	gw_free(smsc->emi_password);

	/* EMI IP */
	gw_free(smsc->emi_hostname);

	/* Memory */
	gw_free(smsc->buffer);
	gw_free(smsc);
}




int smscenter_submit_msg(SMSCenter *smsc, Msg *msg) {
	smscenter_lock(smsc);

	switch (smsc->type) {
	case SMSC_TYPE_FAKE:
		if (fake_submit_msg(smsc, msg) == -1)
			goto error;
		break;

	case SMSC_TYPE_CIMD:
		if(cimd_submit_msg(smsc, msg) == -1)
			goto error;
		break;

	case SMSC_TYPE_EMI:
	case SMSC_TYPE_EMI_IP:
		if(emi_submit_msg(smsc, msg) == -1)
			goto error;
		break;

	case SMSC_TYPE_SMPP_IP:
		if(smpp_submit_msg(smsc, msg) == -1)
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



int smscenter_receive_msg(SMSCenter *smsc, Msg **msg) {
	int ret;

	smscenter_lock(smsc);

	switch (smsc->type) {

	case SMSC_TYPE_FAKE:
		ret = fake_receive_msg(smsc, msg);
		if (ret == -1)
			goto error;
		break;

	case SMSC_TYPE_CIMD:
		ret = cimd_receive_msg(smsc, msg);
		if (ret == -1)
			goto error;
		break;
	
	case SMSC_TYPE_EMI:
	case SMSC_TYPE_EMI_IP:
		ret = emi_receive_msg(smsc, msg);
		if (ret == -1)
			goto error;
		break;

	case SMSC_TYPE_SMPP_IP:
		ret = smpp_receive_msg(smsc, msg);
		if (ret == -1)
			goto error;
		break;

	default:
		goto error;

	}

	smscenter_unlock(smsc);
	
	/* Fix the time if the SMSC didn't tell us it. */
/*
	if (ret == 1 && (*msg)->time == 0)
		time(&(*msg)->time);
*/

	return ret;

error:
	smscenter_unlock(smsc);
	return -1;
}


int smscenter_pending_smsmessage(SMSCenter *smsc) {
	int ret;

	smscenter_lock(smsc);

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
	                if(errno==EINTR) goto got_data;
                        if(errno==EAGAIN) goto got_data;
			error(errno, "Error doing select for socket");
			goto error;
		} else if (ret == 0)
			goto got_data;

		if (smsc->buflen == smsc->bufsize) {
			p = gw_realloc(smsc->buffer, smsc->bufsize * 2);
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
static void smscenter_lock(SMSCenter *smsc) {
	if (smsc->type == SMSC_TYPE_DELETED)
		error(0, "smscenter_lock called on DELETED SMSC.");
	mutex_lock(smsc->mutex);
}


/*
 * Unlock an SMSCenter. Return -1 for error, 0 for OK.
 */
static void smscenter_unlock(SMSCenter *smsc) {
	mutex_unlock(smsc->mutex);
}


/*------------------------------------------------------------------------
 * Public SMSC functions
 */


SMSCenter *smsc_open(ConfigGroup *grp) {
	SMSCenter *smsc;
        char *type, *host, *port, *username, *password, *phone, *device;
        char *dial_prefix, *route_prefix;
        char *backup_port, *our_port;      /* EMI IP */
        char *alt_chars;
        char *smpp_system_id, *smpp_system_type, *smpp_address_range;

        int typeno, portno, backportno, ourportno;


        type = config_get(grp, "smsc");
        host = config_get(grp, "host");
        port = config_get(grp, "port");
        backup_port = config_get(grp, "backup-port");
        our_port = config_get(grp, "our-port");
        username = config_get(grp, "smsc-username");
        password = config_get(grp, "smsc-password");
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
	ourportno = (our_port != NULL ? atoi(our_port) : 0);

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
				   backportno, ourportno);
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
	    smsc->dial_prefix = dial_prefix;
	    smsc->route_prefix = route_prefix;
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
	    return smpp_reopen(smsc);
	default:		/* Unknown SMSC type */
	    return -2;		/* no use */
	}
}



char *smsc_name(SMSCenter *smsc)
{
    return smsc->name;
}


char *smsc_dial_prefix(SMSCenter *smsc)
{
    return smsc->dial_prefix;
}


int smsc_receiver(SMSCenter *smsc, char *number)
{
    char *p, *b;

    p = smsc->route_prefix;

    if(p==NULL) {
	error(0, "smsc_receiver: no route prefix");
	return 0;
    }

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
    if (strstr(smsc->route_prefix, "backup") != NULL)
	return 3;		/* backup */

    return 0;
}


int smsc_close(SMSCenter *smsc) {
	int errors = 0;

	if (smsc == NULL)
	    return 0;
	
	smscenter_lock(smsc);
	
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
	smscenter_unlock(smsc);

	if (errors)
		return -1;

	return 0;
}



int smsc_send_message(SMSCenter *smsc, RQueueItem *msg, RQueue *request_queue)
{
    int ret;
    int wait = 1, l;
    
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

retry:
	ret = smscenter_submit_msg(smsc, msg->msg);

	if (ret == -1) {
	    ret = smsc_reopen(smsc);
	    if (ret == -2) {
		error(0, "Submit failed and cannot reopen");
		return -1;
	    }
	    else if (ret == -1) {
		error(0, "Reopen failed, retrying after %d minutes...", wait);
		for(l = 0; l < wait*60; l++) {
		    if (smsc->killed)	/* only after failed re-open..*/
			return -1;
		    sleep(1);
		}
		wait = wait > 10 ? 10 : wait*2 + 1;
		goto retry;
	    }
	}
	wait = 1;
	/*
	 * put ACK to queue.. in the future!
	 *
	 msg->msg_type = R_MSG_TYPE_ACK;
	 rq_push_msg_ack(request_queue, msg);
	 return ret;
	*/
    }
    else {
	error(0, "SMSC:Unknown message type '%d' to be sent by SMSC, ignored",
	      msg->msg_type);
    }
    rqi_delete(msg);

    return 0;
}


int smsc_get_message(SMSCenter *smsc, RQueueItem **new)
{
	RQueueItem *msg = NULL;
	Msg *newmsg = NULL;
	int ret;
	int l, wait = 1;
   
	*new = NULL;
    
	if (smscenter_pending_smsmessage(smsc) == 1) {

		msg = rqi_new(R_MSG_CLASS_SMS, R_MSG_TYPE_MO);
		if (msg==NULL) goto error;

		if( smscenter_receive_msg(smsc, &newmsg) == 1 ) {
			/* OK */
			msg->msg = newmsg;
		} else {
			error(0, "Failed to receive the message, reconnecting...");
			/* reopen the connection etc. invisible to other end */
		retry:
			ret = smsc_reopen(smsc);
			if (ret == -2)
			    return -1;
			else if (ret == -1) {
			    error(0, "Reopen failed, retrying after %d minutes...", wait);
			    for(l = 0; l < wait*60; l++) {
				if (smsc->killed)	/* only after failed re-open..*/
				    return -1;
				sleep(1);
			    }
			    wait = wait > 10 ? 10 : wait*2 + 1;
			    goto retry;
			}
			wait = 1;
			return 0;		/* iterate */
		}

		*new = msg;
		return 1;
	}

	return 0;
error:
	error(0, "smsc_get_message: Failed to create message");
	rqi_delete(msg);
	return 0;
}


void smsc_set_killed(SMSCenter *smsc, int kill_status)
{
    if (smsc == NULL)
	return;
    smsc->killed = kill_status;
}

