/*
 * smsc_p.h - private interface to SMS center subsystem
 *
 * Lars Wirzenius for WapIT Ltd.
 *
 * New API by Kalle Marjola 1999
 */

#ifndef SMSC_P_H
#define SMSC_P_H


#include <stddef.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>

#include "smsc.h"
#include "sms_msg.h"

/*
 * List of SMS center types that we support.
 */
enum {
	SMSC_TYPE_DELETED,
	SMSC_TYPE_FAKE,
	SMSC_TYPE_CIMD,
	SMSC_TYPE_EMI,
	SMSC_TYPE_EMI_X31,
	SMSC_TYPE_EMI_IP,
	SMSC_TYPE_SMPP_IP,
};

/*
 * The implementation of the SMSCenter object. 
 */
#define DIAL_PREFIX_MAX_LEN 1024
struct SMSCenter {

	int type;
	int transport;
	
	char name[1024];
	int id;
        int latency;
    	
	/* Personal dialing prefix (normalization string). */
	char *dial_prefix;

	/* Routing prefix. */
	char *route_prefix;

	/* Alternative charset */
        int alt_charset;

	/* For locking/unlocking. */
	Mutex *mutex;

        /* for dying */
        sig_atomic_t killed;

	/* General IO device */
	int socket;

	/* TCP/IP */
	char *hostname;
	int port;
	
	/* PSTN/ISDN */
	char *phonenum;
	char *serialdevice;

	/* X.31 */
	char *x31_phonenum;
	char *x31_serialdevice;

	/* Unix pipes */
	char *pipe_command;

	/* CIMD */
	char *cimd_hostname;
	int cimd_port;
	char *cimd_username;
	char *cimd_password;
	time_t cimd_last_spoke;
	int cimd_config_bits;
	
	/* EMI */
	int emi_fd;
	FILE *emi_fp;
	char *emi_phonenum;
	char *emi_serialdevice;
	char *emi_hostname;
	int  emi_port;
	char *emi_username;
	char *emi_password;
	int emi_current_msg_number;
	time_t emi_last_spoke;

	int emi_backup_fd;
        int emi_backup_port;	/* different one! */

	/* SMPP */
	char *smpp_system_id, *smpp_password;
	char *smpp_system_type, *smpp_address_range;
	int smpp_t_state, smpp_r_state;
	struct fifostack *unsent_mt, *sent_mt, *delivered_mt, *received_mo;
	struct fifostack *fifo_t_in, *fifo_t_out;
	struct fifostack *fifo_r_in, *fifo_r_out;
	Octstr *data_t, *data_r;
	int fd_t, fd_r;
	int seq_t, seq_r;
    
	/* For buffering input. */
	char *buffer;
	size_t bufsize;
	size_t buflen;
};


/*
 * Operations on an SMSCenter object.
 */
SMSCenter *smscenter_construct(void);
void smscenter_destruct(SMSCenter *smsc);
int smscenter_read_into_buffer(SMSCenter *);
void smscenter_remove_from_buffer(SMSCenter *smsc, size_t n);

/* Send an SMS message via an SMS center. Return -1 for error,
   0 for OK. */
int smscenter_submit_msg(SMSCenter *smsc, Msg *msg);


/* Receive an SMS message from an SMS center. Return -1 for error,
   0 end of messages (other end closed their end of the connection),
   or 1 for a message was received. If a message was received, a 
   pointer to it is returned via `*msg'. Note that this operation
   blocks until there is a message. */
int smscenter_receive_msg(SMSCenter *smsc, Msg **msg);


/* Is there an SMS message pending from an SMS center? Return -1 for
   error, 0 for no, 1 for yes. This operation won't block, but may
   not be instantenous, if it has to read a few characters to see
   if there is a message. Use smscenter_receive_smsmessage to actually receive
   the message. */
int smscenter_pending_smsmessage(SMSCenter *smsc);

/*
 * Interface to fakesmsc.c. 
 */
SMSCenter *fake_open(char *hostname, int port);
int fake_reopen(SMSCenter *smsc);
int fake_close(SMSCenter *smsc);
int fake_pending_smsmessage(SMSCenter *smsc);
int fake_submit_msg(SMSCenter *smsc, Msg *msg);
int fake_receive_msg(SMSCenter *smsc, Msg **msg);

/*
 * Interface to Nokia SMS centers using CIMD.
 */
SMSCenter *cimd_open(char *hostname, int port, char *username, char *password);
int cimd_reopen(SMSCenter *smsc);
int cimd_close(SMSCenter *smsc);
int cimd_pending_smsmessage(SMSCenter *smsc);
int cimd_submit_msg(SMSCenter *smsc, Msg *msg);
int cimd_receive_msg(SMSCenter *smsc, Msg **msg);

/*
 * Interface to CMG SMS centers using EMI.
 */
SMSCenter *emi_open(char *phonenum, char *serialdevice, char *username, char *password);
int emi_reopen(SMSCenter *smsc);
int emi_close(SMSCenter *smsc);
SMSCenter *emi_open_ip(char *hostname, int port, char *username, char *password,
		       int backup_port);
int emi_reopen_ip(SMSCenter *smsc);
int emi_close_ip(SMSCenter *smsc);
int emi_pending_smsmessage(SMSCenter *smsc);
int emi_submit_msg(SMSCenter *smsc, Msg *msg);
int emi_receive_msg(SMSCenter *smsc, Msg **msg);

/*
 * Interface to Aldiscon SMS centers using SMPP 3.3.
 */
SMSCenter *smpp_open(char *hostname, int port, char*, char*, char*, char*);
int smpp_reopen(SMSCenter *smsc);
int smpp_close(SMSCenter *smsc);
int smpp_pending_smsmessage(SMSCenter *smsc);
int smpp_submit_msg(SMSCenter *smsc, Msg *msg);
int smpp_receive_msg(SMSCenter *smsc, Msg **msg);

#endif
