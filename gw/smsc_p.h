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

/*
 * List of SMS center types that we support.
 */
enum {
	SMSC_TYPE_DELETED,
	SMSC_TYPE_FAKE,
	SMSC_TYPE_CIMD,
	SMSC_TYPE_CIMD2,
	SMSC_TYPE_EMI,
	SMSC_TYPE_EMI_X31,
	SMSC_TYPE_EMI_IP,
	SMSC_TYPE_SMPP_IP,
	SMSC_TYPE_SEMA_X28,
	SMSC_TYPE_OIS,
	SMSC_TYPE_AT
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
    	
	/* Routing prefixes. */
	char *preferred_prefix;
	char *denied_prefix;

	/* Alternative charset */
        int alt_charset;

	/* For locking/unlocking. */
	Mutex *mutex;

        /* for dying */
        volatile sig_atomic_t killed;

	/* General IO device */
	int socket;

	/* Maximum minutes idle time before ping is sent. 0 for no pings. */
	int keepalive;

	/* TCP/IP */
	char *hostname;
	int port;
        int receive_port; /* if used, with EMI 2.0/SMPP 3.3/OIS 4.5 */
	
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

	/* CIMD 2 */
	Octstr *cimd2_hostname;
	int cimd2_port;
	Octstr *cimd2_username;
	Octstr *cimd2_password;
	int cimd2_send_seq;
	int cimd2_receive_seq;
	Octstr *cimd2_inbuffer;
	List *cimd2_received;
	int cimd2_error;
	time_t cimd2_next_ping;
	
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
        int emi_backup_port;	/* different one! rename! */
        int emi_our_port;	/* port to bind us when connecting smsc */

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

        /* SEMA SMS2000 OIS 4.5 X28 */

        char * sema_smscnua;
        char * sema_homenua;
        char * sema_serialdevice;
        struct sema_msglist *sema_mt, *sema_mo;
        int sema_fd;

        /* SEMA SMS2000 OIS 5.0 (TCP/IP to X.25 router) */

        time_t ois_alive;
        time_t ois_alive2;
        void *ois_received_mo;
        int ois_ack_debt;
        int ois_flags;
        int ois_listening_socket;
        int ois_socket;
        char *ois_buffer;
        size_t ois_bufsize;
        size_t ois_buflen;
    
	/* AT Commands (wireless modems...) */
	char *at_serialdevice;
	int at_fd;
	char *at_modemtype;
	char *at_pin;
	List *at_received;
	Octstr *at_inbuffer;
	
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
 * Interface to Nokia SMS centers using CIMD 2.
 */
SMSCenter *cimd2_open(char *hostname, int port, char *username, char *password, int keepalive);
int cimd2_reopen(SMSCenter *smsc);
int cimd2_close(SMSCenter *smsc);
int cimd2_pending_smsmessage(SMSCenter *smsc);
int cimd2_submit_msg(SMSCenter *smsc, Msg *msg);
int cimd2_receive_msg(SMSCenter *smsc, Msg **msg);

/*
 * Interface to CMG SMS centers using EMI.
 */
SMSCenter *emi_open(char *phonenum, char *serialdevice, char *username, char *password);
int emi_reopen(SMSCenter *smsc);
int emi_close(SMSCenter *smsc);
SMSCenter *emi_open_ip(char *hostname, int port, char *username,
		       char *password, int receive_port, int our_port);
int emi_reopen_ip(SMSCenter *smsc);
int emi_close_ip(SMSCenter *smsc);
int emi_pending_smsmessage(SMSCenter *smsc);
int emi_submit_msg(SMSCenter *smsc, Msg *msg);
int emi_receive_msg(SMSCenter *smsc, Msg **msg);

/*
 * Interface to Aldiscon SMS centers using SMPP 3.3.
 */
SMSCenter *smpp_open(char *hostname, int port, char*, char*, char*, char*,
		     int receiveport);
int smpp_reopen(SMSCenter *smsc);
int smpp_close(SMSCenter *smsc);
int smpp_pending_smsmessage(SMSCenter *smsc);
int smpp_submit_msg(SMSCenter *smsc, Msg *msg);
int smpp_receive_msg(SMSCenter *smsc, Msg **msg);

/*
 * Interface to Sema SMS centers using SM2000
 */
SMSCenter *sema_open(char *smscnua,  char *homenua, char* serialdevice,
		     int waitreport);
int sema_reopen(SMSCenter *smsc);
int sema_close(SMSCenter *smsc);
int sema_pending_smsmessage(SMSCenter *smsc);
int sema_submit_msg(SMSCenter *smsc, Msg *msg);
int sema_receive_msg(SMSCenter *smsc, Msg **msg);

/*
 * Interface to Sema SMS centers using OIS 5.0.
 * Interface to Sema SMS centers using SM2000
 */
SMSCenter *ois_open(int receiveport, const char *hostname, int port,
		    int debug_level);
int ois_reopen(SMSCenter *smsc);
int ois_close(SMSCenter *smsc);
int ois_pending_smsmessage(SMSCenter *smsc);
int ois_submit_msg(SMSCenter *smsc, const Msg *msg);
int ois_receive_msg(SMSCenter *smsc, Msg **msg);
void ois_delete_queue(SMSCenter *smsc);


/*
 * Interface to wireless modems using AT commands.
 */
SMSCenter *at_open(char *serialdevice, char *modemtype, char *pin);
int at_reopen(SMSCenter *smsc);
int at_close(SMSCenter *smsc);
int at_pending_smsmessage(SMSCenter *smsc);
int at_submit_msg(SMSCenter *smsc, Msg *msg);
int at_receive_msg(SMSCenter *smsc, Msg **msg);


#endif
