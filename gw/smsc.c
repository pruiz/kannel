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



SMSCenter *smscenter_construct(void)
{
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

    /* EMI IP */
    smsc->emi_hostname = NULL;
    smsc->emi_port = -1;
    smsc->emi_backup_fd = -1;
    smsc->emi_backup_port = -1;
    smsc->emi_backup_allow_ip = NULL;
    smsc->emi_our_port = -1;
    smsc->emi_secondary_fd = -1;

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
    smsc->at_validityperiod = NULL;

    /* add new SMSCes here */

    /* Memory */
    smsc->buflen = 0;
    smsc->bufsize = 10*1024;
    smsc->buffer = gw_malloc(smsc->bufsize);
    memset(smsc->buffer, 0, smsc->bufsize);

    return smsc;
}


void smscenter_destruct(SMSCenter *smsc)
{
    if (smsc == NULL)
        return;

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




int smscenter_submit_msg(SMSCenter *smsc, Msg *msg)
{
    smscenter_lock(smsc);

    switch (smsc->type) {

    case SMSC_TYPE_CIMD:
        if (cimd_submit_msg(smsc, msg) == -1)
            goto error;
        break;

    case SMSC_TYPE_CIMD2:
        if (cimd2_submit_msg(smsc, msg) == -1)
            goto error;
        break;

    case SMSC_TYPE_EMI:
    case SMSC_TYPE_EMI_IP:
        if (emi_submit_msg(smsc, msg) == -1)
            goto error;
        break;

    case SMSC_TYPE_SMPP_IP:
        if (smpp_submit_msg(smsc, msg) == -1)
            goto error;
        break;

    case SMSC_TYPE_SEMA_X28:
        if (sema_submit_msg(smsc, msg) == -1)
            goto error;
        break;

    case SMSC_TYPE_OIS:
        if (ois_submit_msg(smsc, msg) == -1)
            goto error;
        break;

    case SMSC_TYPE_AT:
        if (at_submit_msg(smsc, msg) == -1)
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
        if (ret == -1)
            goto error;
        break;


    case SMSC_TYPE_SEMA_X28:
        ret = sema_receive_msg(smsc, msg);
        if (ret == -1)
            goto error;
        break;

    case SMSC_TYPE_AT:
        ret = at_receive_msg(smsc, msg);
        if (ret == -1)
            goto error;
        break;

    default:
        goto error;

    }
    smscenter_unlock(smsc);

    /* If the SMSC didn't set the timestamp, set it here. */
    if (ret == 1 && msg_type(*msg) == sms && (*msg)->sms.time == 0)
        time(&(*msg)->sms.time);

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
        if (ret == -1)
            goto error;
        break;

    case SMSC_TYPE_OIS:
        ret = ois_pending_smsmessage(smsc);
        if (ret == -1)
            goto error;
        break;

    case SMSC_TYPE_AT:
        ret = at_pending_smsmessage(smsc);
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


int smscenter_read_into_buffer(SMSCenter *smsc)
{
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
            if (errno == EINTR) goto got_data;
            if (errno == EAGAIN) goto got_data;
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


void smscenter_remove_from_buffer(SMSCenter *smsc, size_t n)
{
    memmove(smsc->buffer, smsc->buffer + n, smsc->buflen - n);
    smsc->buflen -= n;
}


/*
 * Lock an SMSCenter. Return -1 for error, 0 for OK. 
 */
static void smscenter_lock(SMSCenter *smsc)
{
    if (smsc->type == SMSC_TYPE_DELETED)
        error(0, "smscenter_lock called on DELETED SMSC.");
    mutex_lock(smsc->mutex);
}


/*
 * Unlock an SMSCenter. Return -1 for error, 0 for OK.
 */
static void smscenter_unlock(SMSCenter *smsc)
{
    mutex_unlock(smsc->mutex);
}


/*------------------------------------------------------------------------
 * Public SMSC functions
 */


SMSCenter *smsc_open(CfgGroup *grp)
{
    SMSCenter *smsc;
    Octstr *type, *host, *username, *password, *phone, *device;
    Octstr *preferred_prefix, *denied_prefix;
    Octstr *alt_chars, *allow_ip;
    Octstr *smpp_system_id, *smpp_system_type, *smpp_address_range;
    Octstr *sema_smscnua, *sema_homenua, *sema_report;
    Octstr *at_modemtype, *at_pin, *at_validityperiod;

    long iwaitreport;
    long port, receive_port, our_port;
    long keepalive;
    long ois_debug;
    int typeno;


    type = cfg_get(grp, octstr_imm("smsc"));
    if (type == NULL) {
	error(0, "Required field 'smsc' missing for smsc group.");
	return NULL;
    }
    if (octstr_compare(type, octstr_imm("cimd")) == 0)
    	typeno = SMSC_TYPE_CIMD;
    else if (octstr_compare(type, octstr_imm("cimd2")) == 0)
    	typeno = SMSC_TYPE_CIMD2;
    else if (octstr_compare(type, octstr_imm("emi")) == 0)
    	typeno = SMSC_TYPE_EMI;
    else if (octstr_compare(type, octstr_imm("emi_ip")) == 0)
    	typeno = SMSC_TYPE_EMI_IP;
    else if (octstr_compare(type, octstr_imm("smpp")) == 0)
    	typeno = SMSC_TYPE_SMPP_IP;
    else if (octstr_compare(type, octstr_imm("sema")) == 0)
    	typeno = SMSC_TYPE_SEMA_X28;
    else if (octstr_compare(type, octstr_imm("ois")) == 0)
    	typeno = SMSC_TYPE_OIS;
    else if (octstr_compare(type, octstr_imm("at")) == 0)
    	typeno = SMSC_TYPE_AT;
    else {
	error(0, "Unknown SMSC type '%s'", octstr_get_cstr(type));
	octstr_destroy(type);
	return NULL;
    }

    host = cfg_get(grp, octstr_imm("host"));
    if (cfg_get_integer(&port, grp, octstr_imm("port")) == -1)
    	port = 0;
    if (cfg_get_integer(&receive_port, grp, octstr_imm("receive-port")) == -1)
    	receive_port = 0;
    if (cfg_get_integer(&our_port, grp, octstr_imm("our-port")) == -1)
    	our_port = 0;
    username = cfg_get(grp, octstr_imm("smsc-username"));
    password = cfg_get(grp, octstr_imm("smsc-password"));
    phone = cfg_get(grp, octstr_imm("phone"));
    device = cfg_get(grp, octstr_imm("device"));
    preferred_prefix = cfg_get(grp, octstr_imm("preferred-prefix"));
    denied_prefix = cfg_get(grp, octstr_imm("denied-prefix"));
    alt_chars = cfg_get(grp, octstr_imm("alt-charset"));

    allow_ip = cfg_get(grp, octstr_imm("connect-allow-ip"));

    smpp_system_id = cfg_get(grp, octstr_imm("system-id"));
    smpp_system_type = cfg_get(grp, octstr_imm("system-type"));
    smpp_address_range = cfg_get(grp, octstr_imm("address-range"));

    sema_smscnua = cfg_get(grp, octstr_imm("smsc_nua"));
    sema_homenua = cfg_get(grp, octstr_imm("home_nua"));
    sema_report = cfg_get(grp, octstr_imm("wait_report"));
    if (sema_report == NULL)
    	iwaitreport = 1;
    else
    	octstr_parse_long(&iwaitreport, sema_report, 0, 0);

    if (cfg_get_integer(&keepalive, grp, octstr_imm("keepalive")) == -1)
    	keepalive = 0;

    if (cfg_get_integer(&ois_debug, grp, octstr_imm("ois-debug-level")) == -1)
    	ois_debug = 0;

    at_modemtype = cfg_get(grp, octstr_imm("modemtype"));
    at_pin = cfg_get(grp, octstr_imm("pin"));
    at_validityperiod = cfg_get(grp, octstr_imm("validityperiod"));

    smsc = NULL;

    switch (typeno) {
    case SMSC_TYPE_CIMD:
        if (host == NULL || port == 0 || username == NULL || password == NULL)
            error(0, "Required field missing for CIMD center.");
        else
            smsc = cimd_open(octstr_get_cstr(host),
	    	    	     port, 
	    	    	     octstr_get_cstr(username), 
			     octstr_get_cstr(password));
        break;

    case SMSC_TYPE_CIMD2:
        if (host == NULL || port == 0 || username == NULL || password == NULL)
            error(0, "Required field missing for CIMD 2 center.");
        else
            smsc = cimd2_open(octstr_get_cstr(host),
	    	    	      port, 
			      octstr_get_cstr(username), 
			      octstr_get_cstr(password), 
			      keepalive);
        break;

    case SMSC_TYPE_EMI:
        if (phone == NULL || device == NULL || username == NULL ||
            password == NULL)
            error(0, "Required field missing for EMI center.");
        else
            smsc = emi_open(octstr_get_cstr(phone), 
	    	    	    octstr_get_cstr(device), 
			    octstr_get_cstr(username), 
			    octstr_get_cstr(password));
        break;

    case SMSC_TYPE_EMI_IP:
        if (host == NULL || port == 0)
            error(0, "Required field missing for EMI IP center.");
        else
	    smsc = emi_open_ip(octstr_get_cstr(host), 
	    	    	       port, 
			       octstr_get_cstr(username), 
			       octstr_get_cstr(password),
                               receive_port, 
			       octstr_get_cstr(allow_ip), 
			       our_port);
        break;

    case SMSC_TYPE_SMPP_IP:
        if (host == NULL || port == 0 || smpp_system_type == NULL ||
            smpp_address_range == NULL || smpp_system_id == NULL ||
            password == NULL)
            error(0, "Required field missing for SMPP center.");
        else
            smsc = smpp_open(octstr_get_cstr(host), 
	    	    	     port, 
	    	    	     octstr_get_cstr(smpp_system_id),
                             octstr_get_cstr(password), 
			     octstr_get_cstr(smpp_system_type),
                             octstr_get_cstr(smpp_address_range), 
			     receive_port);
        break;

    case SMSC_TYPE_SEMA_X28:
        if (device == NULL || sema_smscnua == NULL || sema_homenua == NULL)
            error(0, "Required field missing for SEMA center.");
        else
            smsc = sema_open(octstr_get_cstr(sema_smscnua), 
	    	    	     octstr_get_cstr(sema_homenua), 
			     octstr_get_cstr(device),
                             iwaitreport);
        break;

    case SMSC_TYPE_OIS:
        if (host == NULL || port == 0 || receive_port == 0)
            error(0, "Required field missing for OIS center.");
        else
            smsc = ois_open(receive_port, 
	    	    	    octstr_get_cstr(host), 
			    port, 
	    	    	    ois_debug);
        break;

    case SMSC_TYPE_AT:
        if (device == NULL)
            error(0, "Required field missing for AT virtual center.");
        else
            smsc = at_open(octstr_get_cstr(device), 
	    	    	   at_modemtype ? octstr_get_cstr(at_modemtype) : 0, 
			   at_pin ? octstr_get_cstr(at_pin) : 0,
	    	    	   at_validityperiod ? 
			       octstr_get_cstr(at_validityperiod) : 0);
        break;

        /* add new SMSCes here */

    default: 		/* Unknown SMSC type */
        break;
    }

    if (smsc != NULL) {
	if (cfg_get_integer(&smsc->alt_charset, grp, 
	    	    	    octstr_imm("alt-charset")) == -1)
	    smsc->alt_charset = 0;
    	if (preferred_prefix == NULL)
	    smsc->preferred_prefix = NULL;
	else
	    smsc->preferred_prefix = 
	    	gw_strdup(octstr_get_cstr(preferred_prefix));
    	if (denied_prefix == NULL)
	    smsc->denied_prefix = NULL;
	else
	    smsc->denied_prefix = gw_strdup(octstr_get_cstr(denied_prefix));
    }

    octstr_destroy(type);
    octstr_destroy(host);
    octstr_destroy(username);
    octstr_destroy(password);
    octstr_destroy(phone);
    octstr_destroy(device);
    octstr_destroy(preferred_prefix);
    octstr_destroy(denied_prefix);
    octstr_destroy(alt_chars);
    octstr_destroy(allow_ip);
    octstr_destroy(smpp_system_id);
    octstr_destroy(smpp_system_type);
    octstr_destroy(smpp_address_range);
    octstr_destroy(sema_smscnua);
    octstr_destroy(sema_homenua);
    octstr_destroy(sema_report);
    octstr_destroy(at_modemtype);
    octstr_destroy(at_pin);
    octstr_destroy(at_validityperiod);
    return smsc;
}



int smsc_reopen(SMSCenter *smsc)
{
    int ret;

    if (smsc->killed)
	return -2;

    smscenter_lock(smsc);

    switch (smsc->type) {
    case SMSC_TYPE_CIMD:
        ret = cimd_reopen(smsc);
	break;
    case SMSC_TYPE_CIMD2:
        ret = cimd2_reopen(smsc);
	break;
    case SMSC_TYPE_EMI_IP:
        ret = emi_reopen_ip(smsc);
	break;
    case SMSC_TYPE_EMI:
        ret = emi_reopen(smsc);
	break;
    case SMSC_TYPE_SMPP_IP:
        ret = smpp_reopen(smsc);
	break;
    case SMSC_TYPE_SEMA_X28:
        ret = sema_reopen(smsc);
	break;
    case SMSC_TYPE_OIS:
        ret = ois_reopen(smsc);
	break;
    case SMSC_TYPE_AT:
        ret = at_reopen(smsc);
	break;
        /* add new SMSCes here */
    default: 		/* Unknown SMSC type */
        ret = -2; 		/* no use */
    }

    smscenter_unlock(smsc);
    return ret;
}



char *smsc_name(SMSCenter *smsc)
{
    return smsc->name;
}

int smsc_close(SMSCenter *smsc)
{
    int errors = 0;

    if (smsc == NULL)
        return 0;

    smscenter_lock(smsc);

    switch (smsc->type) {
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
        if (sema_close(smsc) == -1)
            errors = 1;
        break;

    case SMSC_TYPE_OIS:
        if (ois_close(smsc) == -1)
            errors = 1;
        break;

    case SMSC_TYPE_AT:
        if (at_close(smsc) == -1)
            errors = 1;
        break;

        /* add new SMSCes here */

    default: 		/* Unknown SMSC type */
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

