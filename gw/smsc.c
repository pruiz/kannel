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

#include "gwlib/gwlib.h"
#include "smsc.h"
#include "smsc_p.h"
#include "msg.h"

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
	smsc->preferred_prefix = NULL;
	smsc->denied_prefix = NULL;
	smsc->alt_charset = 0;
	smsc->keepalive = 0;

	smsc->mutex = mutex_create();

	sprintf(smsc->name, "Unknown SMSC");
	smsc->id = next_id++;

	/* SMSC ID */
	smsc->smsc_id = NULL;
	smsc->preferred_id = NULL;
	smsc->denied_id = NULL;

	/* FAKE */
	smsc->hostname = NULL;
	smsc->port = -1;
	smsc->socket = -1;

	/* CIMD */
	smsc->cimd_hostname = NULL;
	smsc->cimd_port = -1;
	smsc->cimd_username = NULL;
	smsc->cimd_password = NULL;

	/* CIMD 2 */
	smsc->cimd2_hostname = NULL;
	smsc->cimd2_port = -1;
	smsc->cimd2_username = NULL;
	smsc->cimd2_password = NULL;
	smsc->cimd2_send_seq = 1;
	smsc->cimd2_receive_seq = 0;
	smsc->cimd2_inbuffer = NULL;
	smsc->cimd2_received = NULL;
	smsc->cimd2_error = 0;
	smsc->cimd2_next_ping = 0;

	/* EMI */
	smsc->emi_phonenum = NULL;
	smsc->emi_serialdevice = NULL;
	smsc->emi_username = NULL;
	smsc->emi_password = NULL;
	smsc->emi_backup_allow_ip = NULL;

	/* EMI IP */
	smsc->emi_hostname = NULL;
	smsc->emi_port = -1;

	/* SEMA SMS2000 */

	smsc->sema_smscnua = NULL;
	smsc->sema_homenua = NULL;
	smsc->sema_serialdevice = NULL;
	smsc->sema_fd = -1;

	/* SEMA SMS2000 OIS X.25 */
	smsc->ois_alive = 0;
	smsc->ois_alive2 = 0;
	smsc->ois_received_mo = NULL;
	smsc->ois_ack_debt = 0;
	smsc->ois_flags = 0;
	smsc->ois_listening_socket = -1;
	smsc->ois_socket = -1;
	smsc->ois_buflen = 0;
	smsc->ois_bufsize = 0;
	smsc->ois_buffer = 0;
	
	/* AT Wireless modems  (GSM 03.40 version 7.4.0) */

	smsc->at_serialdevice = NULL;
	smsc->at_fd = -1;
	smsc->at_modemtype = NULL;
	smsc->at_received = NULL;
	smsc->at_inbuffer = NULL;
	smsc->at_pin = NULL;
	
	 /* add new SMSCes here */

	/* Memory */
	smsc->buflen = 0;
	smsc->bufsize = 10*1024;
	smsc->buffer = gw_malloc(smsc->bufsize);
	memset(smsc->buffer, 0, smsc->bufsize);

	return smsc;
}


void smscenter_destruct(SMSCenter *smsc) {
	if (smsc == NULL)
		return;

	/* SMSC ID */
	octstr_destroy(smsc->smsc_id);
	octstr_destroy(smsc->preferred_id);
	octstr_destroy(smsc->denied_id);

	/* FAKE */
	gw_free(smsc->hostname);

	/* CIMD */
	gw_free(smsc->cimd_hostname);
	gw_free(smsc->cimd_username);
	gw_free(smsc->cimd_password);

	/* CIMD 2 */
	octstr_destroy(smsc->cimd2_hostname);
	octstr_destroy(smsc->cimd2_username);
	octstr_destroy(smsc->cimd2_password);
	octstr_destroy(smsc->cimd2_inbuffer);
	list_destroy(smsc->cimd2_received, NULL);

	/* EMI */
	gw_free(smsc->emi_phonenum);
	gw_free(smsc->emi_serialdevice);
	gw_free(smsc->emi_username);
	gw_free(smsc->emi_password);

	/* EMI IP */
	gw_free(smsc->emi_hostname);
	gw_free(smsc->emi_backup_allow_ip);

	/* SEMA */
	gw_free(smsc->sema_smscnua);
	gw_free(smsc->sema_homenua);
	gw_free(smsc->sema_serialdevice);

	/* OIS */
	ois_delete_queue(smsc);
	gw_free(smsc->ois_buffer);
	
	/* AT */
	gw_free(smsc->at_serialdevice);
	gw_free(smsc->at_modemtype);
	gw_free(smsc->at_pin);
	list_destroy(smsc->at_received, NULL);
	gw_free(smsc->at_inbuffer);
	
	 /* add new SMSCes here */

	/* Other fields */
	mutex_destroy(smsc->mutex);

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

	case SMSC_TYPE_CIMD2:
		if (cimd2_submit_msg(smsc, msg) == -1)
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

	case SMSC_TYPE_SEMA_X28:
                if(sema_submit_msg(smsc,msg) == -1)
		        goto error;
		break;

	case SMSC_TYPE_OIS:
	        if(ois_submit_msg(smsc, msg) == -1)
		        goto error;
		break;
	    
	case SMSC_TYPE_AT:
                if(at_submit_msg(smsc,msg) == -1)
		        goto error;
		break;
	    
	 /* add new SMSCes here */
		
	default:
		goto error;
	}

	smscenter_unlock(smsc);
	return 0;

error:
	smscenter_unlock(smsc);
	return -1;
}



int smscenter_receive_msg(SMSCenter *smsc, Msg **msg)
{
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

    case SMSC_TYPE_CIMD2:
	ret = cimd2_receive_msg(smsc, msg);
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

    case SMSC_TYPE_OIS:
        ret = ois_receive_msg(smsc, msg);
	if(ret == -1)
	    goto error;
	break;

	
    case SMSC_TYPE_SEMA_X28:
	ret = sema_receive_msg(smsc, msg);
	if(ret == -1)
	    goto error;
	break;
    
    case SMSC_TYPE_AT:
	ret = at_receive_msg(smsc, msg);
	if(ret == -1)
	    goto error;
	break;
    
    default:
	goto error;

    }
    smscenter_unlock(smsc);
	
    /* If the SMSC didn't set the timestamp, set it here. */
    if (ret == 1 && msg_type(*msg) == smart_sms && (*msg)->smart_sms.time == 0)
	time(&(*msg)->smart_sms.time);

    return ret;

error:
    smscenter_unlock(smsc);
    return -1;
}


int smscenter_pending_smsmessage(SMSCenter *smsc)
{
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

    case SMSC_TYPE_CIMD2:
	ret = cimd2_pending_smsmessage(smsc);
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


    case SMSC_TYPE_SEMA_X28:
	ret = sema_pending_smsmessage(smsc);
	if(ret == -1)
	    goto error;
	break;

    case SMSC_TYPE_OIS:
	ret = ois_pending_smsmessage(smsc);
	if(ret == -1)
	    goto error;
	break;
	
    case SMSC_TYPE_AT:
	ret = at_pending_smsmessage(smsc);
	if(ret == -1)
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


SMSCenter *smsc_open(ConfigGroup *grp)
{
	SMSCenter *smsc;
        char *type, *host, *port, *username, *password, *phone, *device;
	char *smsc_id;
        char *preferred_id, *denied_id;
        char *preferred_prefix, *denied_prefix;
        char *backup_port, *receive_port, *our_port; 
        char *alt_chars, *allow_ip;
        char *smpp_system_id, *smpp_system_type, *smpp_address_range;
	char *sema_smscnua, *sema_homenua, *sema_report;
	char *at_modemtype, *at_pin;
	char *keepalive;
	char *ois_debug_level;

        int typeno, portno, backportno, ourportno, receiveportno, iwaitreport;
	int keepalivetime, ois_debug;


        type = config_get(grp, "smsc");
        host = config_get(grp, "host");
        port = config_get(grp, "port");
        backup_port = config_get(grp, "backup-port");
        receive_port = config_get(grp, "receive-port");
        our_port = config_get(grp, "our-port");
        username = config_get(grp, "smsc-username");
        password = config_get(grp, "smsc-password");
        phone = config_get(grp, "phone");
        device = config_get(grp, "device");
        preferred_prefix = config_get(grp, "preferred-prefix");
        denied_prefix = config_get(grp, "denied-prefix");
        alt_chars = config_get(grp, "alt-charset");

        allow_ip = config_get(grp, "connect-allow-ip");
	
	smsc_id = config_get(grp, "smsc-id");
	preferred_id = config_get(grp, "preferred-smsc-id");
	denied_id = config_get(grp, "denied-smsc-id");
	    
        smpp_system_id = config_get(grp, "system-id");
        smpp_system_type = config_get(grp, "system-type");
        smpp_address_range = config_get(grp, "address-range");

	sema_smscnua = config_get(grp, "smsc_nua");
	sema_homenua = config_get(grp, "home_nua");
	sema_report = config_get(grp, "wait_report");
	iwaitreport = (sema_report != NULL ? atoi(sema_report) : 1);
	keepalive = config_get(grp, "keepalive");

	ois_debug_level = config_get(grp, "ois-debug-level");
	
	at_modemtype = config_get(grp, "modemtype");
	at_pin = config_get(grp, "pin");
		
	if (backup_port)
	    warning(0, "Depricated SMSC config variable 'backup-port' used, "
		    "'receive-port' recommended (but backup-port functions"); 
	
	portno = (port != NULL ? atoi(port) : 0);
	backportno = (backup_port != NULL ? atoi(backup_port) : 0);
	receiveportno = (receive_port != NULL ? atoi(receive_port) : 0);
	keepalivetime = (keepalive != NULL? atoi(keepalive) : 0);
	ois_debug = (ois_debug_level != NULL ? atoi(ois_debug_level) : 0);

	/* Use either, but prefer receive-port */
	
	if (!receiveportno && backportno)
	    receiveportno = backportno;

	
	ourportno = (our_port != NULL ? atoi(our_port) : 0);

	smsc = NULL;

	if (type == NULL) {
		error(0, "Required field 'smsc' missing for smsc group.");
		return NULL;
	}

	if (strcmp(type, "fake") == 0) typeno = SMSC_TYPE_FAKE;
	else if (strcmp(type, "cimd") == 0) typeno = SMSC_TYPE_CIMD;
	else if (strcmp(type, "cimd2") == 0) typeno = SMSC_TYPE_CIMD2;
	else if (strcmp(type, "emi") == 0) typeno = SMSC_TYPE_EMI;
	else if (strcmp(type, "emi_ip") == 0) typeno = SMSC_TYPE_EMI_IP;
	else if (strcmp(type, "smpp") == 0) typeno = SMSC_TYPE_SMPP_IP;
	else if (strcmp(type, "sema") == 0) typeno = SMSC_TYPE_SEMA_X28;
	else if (strcmp(type, "ois") == 0) typeno = SMSC_TYPE_OIS;
	else if (strcmp(type, "at") == 0) typeno = SMSC_TYPE_AT;
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

	case SMSC_TYPE_CIMD2:
	    if (host == NULL || portno == 0 || username == NULL ||
		password == NULL)
		error(0, "Required field missing for CIMD 2 center.");
	    else
		smsc = cimd2_open(host, portno, username, password,
				keepalivetime);
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
				   receiveportno, allow_ip, ourportno);
	    break;

	case SMSC_TYPE_SMPP_IP:
	    if (host == NULL || port == NULL || smpp_system_type == NULL ||
		smpp_address_range == NULL || smpp_system_id == NULL ||
		password == NULL)
		error(0, "Required field missing for SMPP center.");
	    else
		smsc = smpp_open(host, portno, smpp_system_id,
				 password, smpp_system_type,
				 smpp_address_range, receiveportno);
	    break;

	case SMSC_TYPE_SEMA_X28:
	    if (device == NULL || sema_smscnua == NULL ||
		sema_homenua == NULL)
		error(0, "Required field missing for SEMA center.");
	    else
		smsc = sema_open(sema_smscnua, sema_homenua, device,
				 iwaitreport);
	    break;    

	case SMSC_TYPE_OIS:
	    if (host == NULL || portno == 0 || receiveportno == 0)
		error(0, "Required field missing for OIS center.");
            else
		smsc = ois_open(receiveportno, host, portno, ois_debug);
	    break;
	    
	case SMSC_TYPE_AT:
	    if (device == NULL)
		error(0, "Required field missing for AT virtual center.");
	    else
		smsc = at_open(device, at_modemtype, at_pin);
	    break;    
	    
	 /* add new SMSCes here */

	default:		/* Unknown SMSC type */
		break;
	}
	if (smsc != NULL) {
	    smsc->alt_charset = (alt_chars != NULL ? atoi(alt_chars) : 0);
	    smsc->preferred_prefix = preferred_prefix;
	    smsc->denied_prefix = denied_prefix;

	    if (smsc_id)
		smsc->smsc_id = octstr_create(smsc_id);
	    if (preferred_id)
		smsc->preferred_id = octstr_create(preferred_id);
	    if (denied_id)
		smsc->denied_id = octstr_create(denied_id);
	}
	
	return smsc;
}



int smsc_reopen(SMSCenter *smsc) {

	switch (smsc->type) {
	case SMSC_TYPE_FAKE:
	    return fake_reopen(smsc);
	case SMSC_TYPE_CIMD:
	    return cimd_reopen(smsc);
	case SMSC_TYPE_CIMD2:
	    return cimd2_reopen(smsc);
	case SMSC_TYPE_EMI_IP:
	    return emi_reopen_ip(smsc);
	case SMSC_TYPE_EMI:
	    return emi_reopen(smsc);
	case SMSC_TYPE_SMPP_IP:
	    return smpp_reopen(smsc);
	case SMSC_TYPE_SEMA_X28:
	    return sema_reopen(smsc);
	case SMSC_TYPE_OIS:
	    return ois_reopen(smsc);
	case SMSC_TYPE_AT:
	    return at_reopen(smsc);
	 /* add new SMSCes here */
	default:		/* Unknown SMSC type */
	    return -2;		/* no use */
	}
}



char *smsc_name(SMSCenter *smsc)
{
    return smsc->name;
}

char *smsc_id(SMSCenter *smsc)
{
    if (smsc->smsc_id != NULL)
	return octstr_get_cstr(smsc->smsc_id);
    else
	return smsc->name;
}


static int does_prefix_match(char *p, char *number)
{
    char *b;

    if(p==NULL)
	return 0;

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
    return 0;
}


int smsc_preferred(SMSCenter *smsc, char *number, Octstr *smsc_id)
{
    if (does_prefix_match(smsc->preferred_prefix, number)==1)
	return 1;
    if (smsc->preferred_id
	&& str_find_substr(octstr_get_cstr(smsc->preferred_id),
			   octstr_get_cstr(smsc_id), ";")==1)
	return 1;
    return 0;
}

int smsc_denied(SMSCenter *smsc, char *number, Octstr *smsc_id)
{
    if (does_prefix_match(smsc->denied_prefix, number)==1)
	return 1;
    if (smsc->denied_id
	&& str_find_substr(octstr_get_cstr(smsc->denied_id),
			   octstr_get_cstr(smsc_id), ";")==1)
	return 1;
    return 0;
}



int smsc_close(SMSCenter *smsc)
{
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

    case SMSC_TYPE_CIMD2:
	if (cimd2_close(smsc) == -1)
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

    case SMSC_TYPE_SEMA_X28:
	if(sema_close(smsc) == -1)
	    errors = 1;
	break;
		
    case SMSC_TYPE_OIS:
	if (ois_close(smsc) == -1)
	    errors = 1;
	break;

    case SMSC_TYPE_AT:
	if(at_close(smsc) == -1)
	    errors = 1;
	break;
		
	/* add new SMSCes here */

    default:		/* Unknown SMSC type */
	break;
    }
    /*
     smsc->type = SMSC_TYPE_DELETED;
     smscenter_unlock(smsc);
    */
    if (errors)
	return -1;

    return 0;
}



int smsc_send_message(SMSCenter *smsc, Msg *msg)
{
    int ret;
    int wait = 1, l;
    
retry:
    ret = smscenter_submit_msg(smsc, msg);

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
     * XXX put ACK to queue.. in the future!
     */

    return 0;
}


int smsc_get_message(SMSCenter *smsc, Msg **new)
{
	Msg *newmsg = NULL;
	int ret;
	int l, wait = 1;
   
	*new = NULL;
    
	if (smscenter_pending_smsmessage(smsc) == 1) {

		ret = smscenter_receive_msg(smsc, &newmsg);
		if( ret == 1 ) {
		    /* OK */

		    /* if any smsc_id available, use it */
		    newmsg->smart_sms.smsc_id = octstr_duplicate(smsc->smsc_id);
		    
		} else if (ret == 0) { /* "NEVER" happens */
		    warning(0, "SMSC: Pending message returned '1', "
			    "but nothing to receive!");
		    msg_destroy(newmsg);
		    return 0;
		} else {
			error(0, "Failed to receive the message, reconnecting...");
		        msg_destroy(newmsg);
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

		*new = newmsg;
		return 1;
	}

	return 0;
}


void smsc_set_killed(SMSCenter *smsc, int kill_status)
{
    if (smsc == NULL)
	return;
    smsc->killed = kill_status;
}

