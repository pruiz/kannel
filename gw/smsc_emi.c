/*****************************************************************************
* smsc_emi.c - implement interface to the CMG SMS Center (UCP/EMI).
* Mikael Gueck for WapIT Ltd.
*/

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/ioctl.h>

#include "smsc.h"
#include "smsc_p.h"
#include "gwlib.h"
#include "alt_charsets.h"

/******************************************************************************
* Static functions
*/
static int get_data(SMSCenter *smsc, char *buff, int length);

static int put_data(SMSCenter *smsc, char *buff, int length, int is_backup);

static int internal_emi_memorybuffer_append_data(
	SMSCenter *smsc, char *buff, int length);
	
static int internal_emi_memorybuffer_insert_data(
	SMSCenter *smsc, char *buff, int length);
	
static int internal_emi_memorybuffer_has_rawmessage(
	SMSCenter *smsc, int type, char auth);
	
static int internal_emi_memorybuffer_cut_rawmessage(
	SMSCenter *smsc, char *buff, int length);

static int internal_emi_parse_rawmessage_to_msg(
	SMSCenter *smsc, Msg **msg, char *rawmessage, int length);
	
static int internal_emi_parse_msg_to_rawmessage(
	SMSCenter *smsc, Msg *msg, char *rawmessage, int length);

static int internal_emi_acknowledge_from_rawmessage(
	SMSCenter *smsc, char *rawmessage, int length);

static int internal_emi_parse_emi_to_iso88591(
	char *from, char *to, int length, int alt_charset);
	
static int internal_emi_parse_iso88591_to_emi(
	char *from, char *to, int length, int alt_charset);
static int internal_emi_parse_binary_to_emi(
        char *from, char *to, int length);

static int at_dial(char *device, char *phonenum, 
	char *at_prefix, time_t how_long);
static char internal_gurantee_link(SMSCenter *smsc);


static char *internal_emi_generate_checksum(const char *fmt, ...);
static int internal_emi_wait_for_ack(SMSCenter *smsc);


static char internal_char_iso_to_sms(unsigned char from, int alt_charset);
static char internal_char_sms_to_iso(unsigned char from, int alt_charset);

#if 0
static char internal_is_connection_open(SMSCenter *smsc);
#endif

static int secondary_fd	= -1;	/* opened secondary fd */



/******************************************************************************
* Open the connection and log in - handshake baby
*/
int emi_open_connection(SMSCenter *smsc) {

	char      tmpbuff[1024];

	sprintf(tmpbuff, "/dev/%s", smsc->emi_serialdevice);
	smsc->emi_fd = at_dial(tmpbuff, smsc->emi_phonenum, "ATD", 30);

	if(smsc->emi_fd <= 0) 
	    return -1;

	return 0;
}

/* open EMI smscenter */

SMSCenter *emi_open(char *phonenum, char *serialdevice, char *username, char *password) {

	SMSCenter *smsc;

	smsc = smscenter_construct();
	if (smsc == NULL)
		goto error;

	smsc->type = SMSC_TYPE_EMI;
	smsc->latency = 1000*1000; /* 1 second */

	smsc->emi_phonenum = strdup(phonenum);
	smsc->emi_serialdevice = strdup(serialdevice);
	smsc->emi_username = strdup(username);
	smsc->emi_password = strdup(password);

	if (smsc->emi_phonenum == NULL || smsc->emi_serialdevice == NULL || 
	    smsc->emi_username == NULL || smsc->emi_password == NULL) {
		error(errno, "strdup failed");
		goto error;
	}
	if (emi_open_connection(smsc) < 0)
	    goto error;

	sprintf(smsc->name, "EMI:%s:%s", smsc->emi_phonenum, 
		smsc->emi_username);
	return smsc;

error:
	error(errno, "emi_open: could not open");
	smscenter_destruct(smsc);
	return NULL;
}

int emi_reopen(SMSCenter *smsc) {
    emi_close(smsc);

    if (emi_open_connection(smsc) < 0) {
	error(0, "Failed to re-open the connection!");
	return -1;
    }
    return 0;
}

int emi_close(SMSCenter *smsc) {
    return emi_close_ip(smsc);
}

/*******************************************************
 * the actual protocol open... quite simple here */

static int emi_open_connection_ip(SMSCenter *smsc)
{
    smsc->emi_fd =
	tcpip_connect_to_server_with_port(smsc->emi_hostname, 
					  smsc->emi_port, smsc->emi_our_port);
    if (smsc->emi_fd < 0)
	return -1;
    return 0;
}


/******************************************************************************
* Open the connection and log in
*/
SMSCenter *emi_open_ip(char *hostname, int port, char *username,
		       char *password, int backup_port, int our_port) {

	SMSCenter *smsc;

	smsc = smscenter_construct();

	if (smsc == NULL)
		goto error;

	smsc->type = SMSC_TYPE_EMI_IP;
	smsc->latency = 1000*1000; /* 1 second */ 

	smsc->emi_hostname = strdup(hostname);
	smsc->emi_port = port;
	smsc->emi_username = strdup(username);
	smsc->emi_password = strdup(password);
	smsc->emi_backup_port = backup_port;
	smsc->emi_our_port = our_port;

	if (smsc->emi_hostname == NULL || smsc->emi_username == NULL ||
	    smsc->emi_password == NULL) {
	        error(errno, "strdup failed");
		goto error;
	}

	if (emi_open_connection_ip(smsc) < 0)
	    goto error;

	sprintf(smsc->name, "EMIIP:%s:%s", smsc->emi_hostname, 
		smsc->emi_username);

	/* if backup-port is defined, set it ready */
	
	if (backup_port > 0) {
	    if ((smsc->emi_backup_fd = make_server_socket(backup_port)) <= 0)
		goto error;

	    debug(0, "EMI IP backup port at %d opened", backup_port);
	}	
	return smsc;

error:
	error(errno, "emi_open: could not open");
	smscenter_destruct(smsc);
	return NULL;

}

int emi_reopen_ip(SMSCenter *smsc)
{
    emi_close_ip(smsc);

    return emi_open_connection_ip(smsc);
}


int emi_close_ip(SMSCenter *smsc) {

    if (smsc->emi_fd == -1) {
	info(0, "Trying to close already closed EMI, ignoring");
	return 0;
    }
    close(smsc->emi_fd);
    smsc->emi_fd = -1;

    return 0;
}


/******************************************************************************
* Check if the buffers contain any messages
*/
int emi_pending_smsmessage(SMSCenter *smsc) {

	char *tmpbuff;
	int n = 0;
/*	time_t timenow; */

	/* Block until we have a connection */
	internal_gurantee_link(smsc);

	/* If we have MO-message, then act (return 1) */
	if( internal_emi_memorybuffer_has_rawmessage(smsc, 52, 'O') > 0 || 
	    internal_emi_memorybuffer_has_rawmessage(smsc, 1, 'O') > 0 )
	        return 1;

	tmpbuff = malloc(10*1024);
	bzero(tmpbuff, 10*1024);

	/* check for data */
	n = get_data( smsc, tmpbuff, 1024*10 );
	if (n > 0)
	    internal_emi_memorybuffer_insert_data( smsc, tmpbuff, n );

	/* delete all ACKs/NACKs/whatever */
	while( internal_emi_memorybuffer_has_rawmessage( smsc, 51, 'R' ) > 0 ||
	       internal_emi_memorybuffer_has_rawmessage( smsc, 1, 'R' ) > 0)
	    internal_emi_memorybuffer_cut_rawmessage( smsc, tmpbuff, 10*1024 );

	free(tmpbuff);

	/* If we have MO-message, then act (return 1) */
	
	if( internal_emi_memorybuffer_has_rawmessage(smsc, 52, 'O') > 0 ||
	    internal_emi_memorybuffer_has_rawmessage(smsc, 1, 'O') > 0)
		return 1;

/*
	time(&timenow);
	if( (smsc->emi_last_spoke + 60*20) < timenow) {
		time(&smsc->emi_last_spoke);
	}
*/

	return 0;

}




/******************************************************************************
 * Submit (send) a Mobile Terminated message to the EMI server
 */
int emi_submit_msg(SMSCenter *smsc, Msg *omsg) {

	char *tmpbuff = NULL;

	if (smsc == NULL) goto error;
	if (omsg == NULL) goto error;

	tmpbuff = malloc(10*1024);
	bzero(tmpbuff, 10*1024);

	if(internal_emi_parse_msg_to_rawmessage( smsc, omsg, tmpbuff, 10*1024 ) < 1)
		goto error;

	if(put_data( smsc, tmpbuff, strlen(tmpbuff),0) < 0) {
	    info(0, "put_data failed!");
	    goto error;
	}

	if(smsc->type == SMSC_TYPE_EMI_IP) {
		if (!internal_emi_wait_for_ack(smsc)) {
		    info(0, "emi_submit_smsmessage: wait for ack failed!");
		    goto error;
		}
	}

	if(smsc->type == SMSC_TYPE_EMI) {
		internal_emi_wait_for_ack(smsc);
	}

/*	smsc->emi_current_msg_number += 1; */
	debug(0, "Submit Ok...");
	
	free(tmpbuff);
	return 1;

error:
	debug(0, "Submit Error...");

	free(tmpbuff);
	return 0;
}

/******************************************************************************
* Receive a Mobile Terminated message to the EMI server
*/
int emi_receive_msg(SMSCenter *smsc, Msg **tmsg) {

	char *tmpbuff;
	Msg *msg = NULL;

	*tmsg = NULL;
	
	tmpbuff = malloc(10*1024);
	if(tmpbuff==NULL) goto error;
	bzero(tmpbuff, 10*1024);

	/* get and delete message from buffer */
	internal_emi_memorybuffer_cut_rawmessage(smsc, tmpbuff, 10*1024 );
	internal_emi_parse_rawmessage_to_msg( smsc, &msg, tmpbuff, strlen(tmpbuff) );

	/* yeah yeah, I got the message... */
	internal_emi_acknowledge_from_rawmessage(smsc, tmpbuff, strlen(tmpbuff));

	/* return with the joyful news */
	free(tmpbuff);

	if (msg == NULL) goto error;

	*tmsg = msg;
	
	return 1;

error:
	free(tmpbuff);
	msg_destroy(msg);
	return -1;
}


/******************************************************************************
* In(f)ternal functions
*/


/******************************************************************************
* Gurantee that we have a link
*/
static char internal_gurantee_link(SMSCenter *smsc) {

	char tmpbuff[1024];
	int need_to_connect = 0;

	if(smsc->type == SMSC_TYPE_EMI_IP) { 
		/* We don't currently gurantee TCP connections. */
		return 0;
	}

	/* If something is obviously wrong. */
	if( strstr(smsc->buffer, "OK") ) need_to_connect = 1;
	if( strstr(smsc->buffer, "NO CARRIER") ) need_to_connect = 1;
	if( strstr(smsc->buffer, "NO DIALTONE") ) need_to_connect = 1;

	/* Clear the buffer */
	for(;;) {
	
		if(need_to_connect == 0) break;
		
		/* Connect */
		sprintf(tmpbuff, "/dev/%s", smsc->emi_serialdevice);
		smsc->emi_fd = at_dial(tmpbuff, smsc->emi_phonenum, "ATD", 30);
		if(smsc->emi_fd != -1) need_to_connect = 0;

		/* Clear the buffer so that the next call to gurantee
		   doesn't find the "NO CARRIER" string again. */
		smsc->buflen = 0;
		bzero(smsc->buffer, smsc->bufsize);
		
	}

	return 0;

}

static int at_dial(char *device, char *phonenum, char *at_prefix, time_t how_long) {

	char tmpbuff[1024];
	int howmanyread = 0, thistime = 0;
	int redial = 1;
	int fd = -1;
	int ret;
	time_t timestart;
	struct termios tios;

	/* The time at the start of the function is used when
	   determining whether we have used up our allotted
	   dial time and have to abort. */
	time(&timestart);

	/* Open the device properly. Remember to set the
	   access codes correctly. */
	fd = open(device, O_RDWR|O_NONBLOCK|O_NOCTTY);
	if(fd==-1) {
		error(errno, "at_dial: error open(2)ing the character device <%s>", device);
		if(errno == EACCES)
			error(0, "at_dial: remember to give the user running the smsgateway"
			" process the right to access the serial device");
		goto error;
	}
	tcflush(fd, TCIOFLUSH);

	/* The speed initialisation is pretty important. */
	tcgetattr(fd, &tios);
	cfsetospeed(&tios, B115200);
	cfsetispeed(&tios, B115200);
	cfmakeraw(&tios);
	tios.c_cflag |= (HUPCL | CREAD | CRTSCTS);
	tcsetattr(fd, TCSANOW, &tios);

	/* Dial using an AT command string. */
	for(;;) {

		/* Do want to dial or redial? */
		if(redial==0) break;

		info(0, "at_dial: dialing <%s> on <%s> for <%i> seconds",
			phonenum, device, 
			(int)how_long - ((int)time(NULL)-(int)timestart));

		/* Send AT dial request. */
		howmanyread = 0;
		sprintf(tmpbuff, "%s%s\r\n", at_prefix, phonenum);
		ret = write(fd, tmpbuff, strlen(tmpbuff)); /* errors... -mg */
		bzero(&tmpbuff, sizeof(tmpbuff));

		/* Read the answer to the AT command and react accordingly. */
		for(;;) {

			/* We don't want to dial forever */
			if( time(NULL) > (timestart+how_long) )
				if(how_long != 0) goto timeout;

			/* We don't need more space for dialout */
			if(howmanyread >= sizeof(tmpbuff))
				goto error;

			/* We read 1 char a time so that we don't
			   accidentally read past the modem chat and
			   into the SMSC datastream -mg */
			thistime = read(fd, &tmpbuff[howmanyread], 1);
			if(thistime==-1) {
				if(errno==EAGAIN) continue;
				if(errno==EINTR) continue;
				goto error;
			} else {
				howmanyread += thistime;
			}

			/* Search for the newline on the AT status line. */
			if( (tmpbuff[howmanyread-1] == '\r') 
			 || (tmpbuff[howmanyread-1] == '\n') ) {

				/* XXX ADD ALL POSSIBLE CHAT STRINGS XXX */

				if(strstr(tmpbuff, "CONNECT")!=NULL) {
				
					debug(0, "at_dial: CONNECT");
					redial = 0;
					break;
					
				} else if(strstr(tmpbuff, "NO CARRIER")!=NULL) {
				
					debug(0, "at_dial: NO CARRIER");
					redial = 1;
					break;

				} else if(strstr(tmpbuff, "BUSY")!=NULL) {
				
					debug(0, "at_dial: BUSY");
					redial = 1;
					break;
					
				} else if(strstr(tmpbuff, "NO DIALTONE")!=NULL) {
				
					debug(0, "at_dial: NO DIALTONE");
					redial = 1;
					break;
					
				}

			} /* End of if lastchr=='\r'||'\n'. */

			/* Thou shall not consume all system resources
			   by repeatedly looping a strstr search when
			   the string update latency is very high as it
			   is in serial communication. -mg */
			usleep(1000);

		} /* End of read loop. */

		/* Thou shall not flood the modem with dial requests. -mg */
		sleep(1);

	} /* End of dial loop. */

	debug(0, "at_dial: done with dialing");
	return fd;

timeout:
	error(0, "at_dial: timed out");
	close(fd);
	return -1;

error:
	error(0, "at_dial: done with dialing");
	close(fd);
	return -1;		
}

/******************************************************************************
 * Wait for an ACK or NACK from the remote
 *
 * REQUIRED by the protocol that it must be waited...
 */
static int internal_emi_wait_for_ack(SMSCenter *smsc) {
    char *tmpbuff;
    int found = 0;
    int n;
    time_t start;

    tmpbuff = malloc(10*1024);
    bzero(tmpbuff, 10*1024);
    start = time(NULL);
    do {
	/* check for data */
	n = get_data( smsc, tmpbuff, 1024*10 );
	
	if(smsc->type == SMSC_TYPE_EMI) {
		/* At least the X.31 interface wants to append the data.
		   Kalle, what about the TCP/IP interface? Am I correct
		   that you are assuming that the message arrives in a 
		   single read(2)? -mg */
		if(n>0) internal_emi_memorybuffer_append_data(smsc, tmpbuff, n);
		
	} else if(smsc->type == SMSC_TYPE_EMI_IP) {

		if(n>0) internal_emi_memorybuffer_insert_data(smsc, tmpbuff, n);

	}
    
	/* act on data */
	if( internal_emi_memorybuffer_has_rawmessage( smsc, 51, 'R' ) > 0 ||
	    internal_emi_memorybuffer_has_rawmessage( smsc, 1, 'R' ) > 0 ) {
	       internal_emi_memorybuffer_cut_rawmessage( smsc, tmpbuff, 10*1024 );
	       debug(0,"Found ACK/NACK: <%s>",tmpbuff);
	       found = 1;

	}
    } while ((!found) && ((time(NULL) - start) < 5));

    free(tmpbuff);  
    return found;
}


/******************************************************************************
 * Get the modem buffer data to buff, return the amount read
 *
 * Reads from main fd, but also from backup-fd - does accept if needed
 */
static int get_data(SMSCenter *smsc, char *buff, int length) {
    
	int n = 0;

	struct sockaddr client_addr;
	socklen_t client_addr_len;
	
	fd_set rf;
	struct timeval to;
	int ret;

	bzero(buff, length);

	if(smsc->type == SMSC_TYPE_EMI) {
		tcdrain(smsc->emi_fd);
		n = read(smsc->emi_fd, buff, length);
/*
		if(n > 0)
			debug(0, "modembuffer_get_data(X.31): got <%s>", buff);
*/
		return n;
	}

	FD_ZERO(&rf);
	if (smsc->emi_fd >= 0) FD_SET(smsc->emi_fd, &rf);
	if (secondary_fd >= 0) FD_SET(secondary_fd, &rf);
	FD_SET(smsc->emi_backup_fd, &rf);
	    
	FD_SET(0, &rf);
	to.tv_sec = 0;
	to.tv_usec = 100;

	ret = select(FD_SETSIZE, &rf, NULL, NULL, &to);


	if (ret > 0) {
	    if (secondary_fd >= 0 && FD_ISSET(secondary_fd, &rf)) {
		n = read(secondary_fd, buff, length-1);

		if (n == -1) {
		    error(errno, "Error - Secondary socket closed");
		    close(secondary_fd);
		    secondary_fd = -1;
		}
		else if (n == 0) {
		    info(0, "Secondary socket closed by SMSC");
		    close(secondary_fd);
		    secondary_fd = -1;
		}
		else {			/* UGLY! We  put 'X' after message */
		    buff[n] = 'X';	/* if it is from secondary fd!!!  */
		    n++;
		}		    
	    }
	    else if (smsc->emi_fd >= 0 && FD_ISSET(smsc->emi_fd, &rf)) {
		n = read(smsc->emi_fd, buff, length);
		if (n == 0) {
		    close(smsc->emi_fd);
		    info(0, "Main EMI socket closed by SMSC");
		    smsc->emi_fd = -1;	/* ready to be re-opened */
		}
	    }
	    if (FD_ISSET(smsc->emi_backup_fd, &rf)) {

		if (secondary_fd == -1) {
		    /* well we actually should check if the connector is really
		     * that SMS Center... back to that in a few minutes... */
		
		    secondary_fd = accept(smsc->emi_backup_fd, &client_addr, &client_addr_len);
		    info(0, "Secondary socket opened by SMSC");
		}
		else
		    info(0, "New connection request while old secondary is open!");
	    }
	}
	if (n > 0) { 
	    debug(0,"get_data:Read %d bytes: <%.*s>", n, n, buff); 
	    debug(0,"get_data:smsc->buffer == <%s>", smsc->buffer); 
	}
	return n;

}

/******************************************************************************
* Put the buff data to the modem buffer, return the amount of data put
*/
static int put_data(SMSCenter *smsc, char *buff, int length, int is_backup) {

	size_t len = length;
	int ret;
	int fd;

	if(smsc->type == SMSC_TYPE_EMI_IP) {
		if (is_backup) {
		    fd = secondary_fd;
		    info(0, "Writing into secondary (backup) fd!");
		} else {
		    if (smsc->emi_fd == -1) {
			info(0, "Reopening connection to SMSC");
			smsc->emi_fd = tcpip_connect_to_server(smsc->emi_hostname,
							       smsc->emi_port);
			if (smsc->emi_fd == -1) {
			    error(errno, "put_data: Reopening failed!");
			    return -1;
			}
		    }
		    fd = smsc->emi_fd;
		}
	}

	if(smsc->type == SMSC_TYPE_EMI) {
		fd = smsc->emi_fd;
		tcdrain(smsc->emi_fd);
	}

	/* Write until all data has been successfully written to the fd. */
	while (len > 0) {
	        ret = write(fd, buff, len);
		if (ret == -1) {
			if(errno==EINTR) continue;
			if(errno==EAGAIN) continue;
			error(errno, "Writing to fd failed");
			if (fd == smsc->emi_fd && smsc->type == SMSC_TYPE_EMI_IP) {
			    close(fd);
			    smsc->emi_fd = -1;
			    info(0, "Closed main EMI socket.");
			}
			return -1;
		}
		/* ret may be less than len, if the writing
		   was interrupted by a signal. */
		len -= ret;
		buff += ret;
	}

	if(smsc->type == SMSC_TYPE_EMI) {
		/* Make sure the data gets written immediately.
		   Wait a while just to add some latency so
		   that the modem (or the UART) doesn't choke
		   on the data. */
		tcdrain(smsc->emi_fd);
		usleep(1000);
	}

	return 0;
}

/******************************************************************************
* Append the buff data to smsc->buffer
*/
static int internal_emi_memorybuffer_append_data(SMSCenter *smsc, char *buff, int length) {

        char *p;

	while( smsc->bufsize < (smsc->buflen + length) ) { /* buffer too small */
	        p = realloc(smsc->buffer, smsc->bufsize * 2);
		if (p == NULL) {
		    error(errno, 
			  "Couldn't allocate read buffer, using original (too small)");
		    return -1;
		}
		smsc->buffer = p;
		smsc->bufsize *= 2;
	}

	memcpy(smsc->buffer + smsc->buflen, buff, length);

	smsc->buflen += length;

	return 0;

}

/******************************************************************************
* Insert (put to head) the buff data to smsc->buffer
*/
static int internal_emi_memorybuffer_insert_data(SMSCenter *smsc, char *buff, int length) {
	
    char *p;
    
    while( smsc->bufsize < (smsc->buflen + length) ) { /* buffer too small */
	p = realloc(smsc->buffer, smsc->bufsize * 2);
	if (p == NULL) {
	    error(errno, 
		  "Couldn't allocate read buffer, using original (too small)");
	    return -1;
	}
	smsc->buffer = p;
	smsc->bufsize *= 2;
    }
    memmove(smsc->buffer + length, smsc->buffer, smsc->buflen);
    memcpy(smsc->buffer, buff, length);

    smsc->buflen += length;
/*    debug(0, "insert: buff = <%s> (length=%d)", smsc->buffer, smsc->buflen); */
    
    return 0;

}

/******************************************************************************
* Check the smsc->buffer for a raw STX...ETX message
*/
static int internal_emi_memorybuffer_has_rawmessage(SMSCenter *smsc,
						    int type, char auth) {

	char tmpbuff[1024], tmpbuff2[1024];
	char *stx, *etx;

	stx = memchr(smsc->buffer, '\2', smsc->buflen);
	etx = memchr(smsc->buffer, '\3', smsc->buflen);

	if ( ( stx != NULL ) && ( etx != NULL ) && (stx < etx) ) {

	    strncpy(tmpbuff, stx, etx-stx+1);
	    if (auth)
		sprintf(tmpbuff2, "/%c/%02i/", auth, type);
	    else
		sprintf(tmpbuff2, "/%02i/", type);
		
	    if( strstr(tmpbuff, tmpbuff2) != NULL ) {
		
		debug(0, "found message <%c/%02i>...", auth, type);
		debug(0, "has_rawmessage: <%s>", tmpbuff);
		return 1;
	    }

	}
	return 0;

}

/******************************************************************************
* Cut the first raw message from the smsc->buffer
* and put it in buff, return success 0, failure -1
*/
static int internal_emi_memorybuffer_cut_rawmessage(
	SMSCenter *smsc, char *buff, int length) {

	char *stx, *etx;
	int  size_of_cut_piece;
	int  size_of_the_rest;
	
	/* We don't check for NULLs since we're sure that nobody has fooled 
	   around with smsc->buffer since has_rawmessage was last called... */

	stx = memchr(smsc->buffer, '\2', smsc->buflen);
	etx = memchr(smsc->buffer, '\3', smsc->buflen);

	if (*(etx+1)=='X')	/* secondary! UGLY KLUDGE */
	    etx++; 
	
	size_of_cut_piece = (etx - stx) + 1;
	size_of_the_rest  = (smsc->buflen - size_of_cut_piece);

/*
	debug(0, "size_of_cut_piece == <%i> ; size_of_the_rest == <%i>",
	      size_of_cut_piece, size_of_the_rest);
*/
	
	if(length < size_of_cut_piece) {
		error(0, "the buffer you provided for cutting was too small");
		return -1;
	}

	/* move the part before our magic rawmessage to the safe house */
	memcpy(buff, stx, size_of_cut_piece);
	buff[size_of_cut_piece] = '\0';	/* NULL-terminate */

	/* move the stuff in membuffer one step down */
	memmove(stx, etx+1, (smsc->buffer + smsc->bufsize) - stx );

	smsc->buflen -= size_of_cut_piece;
/*	debug(0,"cut: buff == <%s>\nsmsc->buffer == <%s>",buff,smsc->buffer); */

	return 0;

}

/******************************************************************************
* Parse the raw message to the Msg structure
*/
static int internal_emi_parse_rawmessage_to_msg(
	SMSCenter *smsc, Msg **msg, char *rawmessage, int length) {

	char emivars[128][1024];
	char *leftslash, *rightslash;
	char isotext[2048];
	int msgnbr;
	int tmpint;

	bzero(isotext, sizeof(isotext));

	strncpy(isotext, rawmessage, length);
	leftslash = isotext;
	
	for(tmpint=0;leftslash!=NULL;tmpint++) {
		rightslash = strchr(leftslash+1, '/');
		
		if(rightslash == NULL)
			rightslash = strchr(leftslash+1, '\3');

		if(rightslash == NULL)
			break;
						
		*rightslash = '\0';
		strcpy(emivars[tmpint], leftslash+1);
		leftslash = rightslash;
	}

	if (strcmp(emivars[3], "01")==0) {
	    if (strcmp(emivars[7], "2")==0) {
		strcpy(isotext, emivars[8]);
	    } else if (strcmp(emivars[7], "3")==0) {
		internal_emi_parse_emi_to_iso88591(emivars[8], isotext,
						   sizeof(isotext), smsc->alt_charset);
	    } else {
		error(0, "Unknown 01-type EMI SMS (%s)", emivars[7]);
		strcpy(isotext, "");
	    }
	} else if (strcmp(emivars[3], "51")==0) {
	    internal_emi_parse_emi_to_iso88591(emivars[24], isotext,
					       sizeof(isotext), smsc->alt_charset);
	} else if (strcmp(emivars[3], "52")==0) {
	    internal_emi_parse_emi_to_iso88591(emivars[24], isotext,
					       sizeof(isotext), smsc->alt_charset);
	} else {
	    error(0, "HEY WE SHOULD NOT BE HERE!! Type = %s", emivars[3]);
	    strcpy(isotext, "");
	}

	*msg = msg_create(smart_sms);
	if(*msg==NULL) goto error;

	(*msg)->smart_sms.sender = octstr_create(emivars[5]);
	(*msg)->smart_sms.receiver = octstr_create(emivars[4]);
	(*msg)->smart_sms.msgdata = octstr_create(isotext);
	(*msg)->smart_sms.udhdata = NULL;

	return msgnbr;

error:
	return -1;
}

/*
 * notify the SMSC that we got the message
 */
static int internal_emi_acknowledge_from_rawmessage(
	SMSCenter *smsc, char *rawmessage, int length) {

	char emivars[128][1024];
	char timestamp[2048], sender[2048], receiver[2048];
	char emitext[2048], isotext[2048];
	char *leftslash, *rightslash;
	int msgnbr;
	int tmpint;
	int is_backup = 0;
	
	bzero(&sender, sizeof(sender));
	bzero(&receiver, sizeof(receiver));
	bzero(&emitext, sizeof(emitext));
	bzero(&isotext, sizeof(isotext));
	bzero(&timestamp, sizeof(timestamp));

	strncpy(isotext, rawmessage, length);
	leftslash = isotext;

	if(isotext[length-1] == 'X')
	    is_backup = 1;
	
	for(tmpint=0;leftslash!=NULL;tmpint++) {
		rightslash = strchr(leftslash+1, '/');
		
		if(rightslash == NULL)
			rightslash = strchr(leftslash+1, '\3');

		if(rightslash == NULL)
			break;
						
		*rightslash = '\0';
		strcpy(emivars[tmpint], leftslash+1);
		leftslash = rightslash;
	}


	/* BODY */
	if(smsc->type == SMSC_TYPE_EMI) {
		sprintf(isotext, "A//%s:%s", emivars[4], emivars[18]);
		sprintf(isotext, "A//%s:", emivars[5]);
		is_backup = 0;
	}

	if(smsc->type == SMSC_TYPE_EMI_IP) {
		if (strcmp(emivars[3],"01")==0)
		    sprintf(isotext, "A/%s:", emivars[4]);
		else 
		    sprintf(isotext, "A//%s:%s", emivars[4], emivars[18]);
	}
	
	/* HEADER */

	debug(0, "acknowledge: type = '%s'", emivars[3]);
	
	sprintf(emitext, "%s/%05i/%s/%s", emivars[0], strlen(isotext)+17,
		"R", emivars[3]);

	smsc->emi_current_msg_number = atoi(emivars[0]) + 1;

	/* FOOTER */
	sprintf(timestamp, "%s/%s/", emitext, isotext);
	strcpy(receiver, internal_emi_generate_checksum(timestamp) );

 	sprintf(sender, "%c%s/%s/%s%c", 0x02, emitext, isotext, receiver, 0x03);
	put_data(smsc, sender, strlen(sender), is_backup);

	return msgnbr;

}


/******************************************************************************
* Parse the Msg structure to the raw message format
*/
static int internal_emi_parse_msg_to_rawmessage(SMSCenter *smsc, Msg *msg, char *rawmessage, int rawmessage_length) {

	char message_whole[10*1024];
	char message_body[10*1024];
	char message_header[1024];
	char message_footer[1024];

	static char my_buffer[10*1024];
	char my_buffer2[10*1024];
	char msgtext[1024];
	int  length;
	char mt;
	char mcl[20];
	char snumbits[20];
	char xser[1024];
	int udh_len;

	bzero(&message_whole, sizeof(message_whole));
	bzero(&message_body, sizeof(message_body));
	bzero(&message_header, sizeof(message_header));
	bzero(&message_footer, sizeof(message_footer));
	bzero(&my_buffer, sizeof(my_buffer));
	bzero(&my_buffer2, sizeof(my_buffer2));
	mt = '3';
	bzero(&snumbits,sizeof(snumbits));

	/* XXX internal_emi_parse_iso88591_to_emi shouldn't use NUL terminated
	 * strings, but Octstr directly, or a char* and a length.
	 */
	if (msg->smart_sms.flag_udh == 1) {
	  char xserbuf[258];
	  /* we need a properbly formated UDH here, there first byte contains his length 
	   * this will be formatted in the xser field of the EMI Protocol
	   */
	  udh_len = octstr_get_char(msg->smart_sms.msgdata,0)+1;
	  xserbuf[0] = 1;
	  xserbuf[1] = udh_len;
	  octstr_get_many_chars(&xserbuf[2], msg->smart_sms.msgdata, 0, udh_len);
	  internal_emi_parse_binary_to_emi(xserbuf, xser,udh_len+2);	   
	} else {
	  udh_len = 0;
	}

	if (msg->smart_sms.flag_8bit != 1) {
	  /* skip the probably existing UDH */
	  octstr_get_many_chars(msgtext, msg->smart_sms.msgdata, udh_len, octstr_len(msg->smart_sms.msgdata) - udh_len);
	  msgtext[octstr_len(msg->smart_sms.msgdata)] = '\0';
	  internal_emi_parse_iso88591_to_emi(msgtext, my_buffer2,
					     octstr_len(msg->smart_sms.msgdata) - udh_len,
					     smsc->alt_charset);	  

	  strcpy(snumbits,"");
	  mt = '3';
	  strcpy(mcl,"");
	} else {
	  octstr_get_many_chars(msgtext, msg->smart_sms.msgdata, udh_len, octstr_len(msg->smart_sms.msgdata) - udh_len);
	  msgtext[octstr_len(msg->smart_sms.msgdata)] = '\0';
	  internal_emi_parse_binary_to_emi(msgtext, my_buffer2, octstr_len(msg->smart_sms.msgdata) - udh_len);	  
	  
	  sprintf(snumbits,"%04d",(octstr_len(msg->smart_sms.msgdata)-udh_len)*8);
	  mt = '4';
	  strcpy(mcl,"1");
	}

	if(smsc->type == SMSC_TYPE_EMI) {
		sprintf(message_body, 
		"%s/%s/%s/%s/%s//%s////////////%c/%s/%s////%s//////%s//",
		octstr_get_cstr(msg->smart_sms.receiver),
		octstr_get_cstr(msg->smart_sms.sender),
		"",
		"",
		"",
		"0100",
		mt, 
		snumbits,
		my_buffer2,
		mcl,
		xser);
	} else {
		sprintf(message_body, 
		"%s/%s/%s/%s/%s//%s////////////%c/%s/%s////%s//////%s//",
		octstr_get_cstr(msg->smart_sms.receiver),
		octstr_get_cstr(msg->smart_sms.sender),
		"",
		"",
		"",
		"0100",
		mt, 
		snumbits,
		my_buffer2,
		mcl,
		xser);
	}

	/* HEADER */

	length  = strlen(message_body);
	length += 13; /* header (fixed) */
	length +=  2; /* footer (fixed) */
	length +=  2; /* slashes between header, body, footer */

	sprintf(message_header, "%02i/%05i/%s/%s", (smsc->emi_current_msg_number++ % 100), length, "O", "51");

	/* FOOTER */

	sprintf(my_buffer, "%s/%s/", message_header, message_body);
	strcpy(message_footer, internal_emi_generate_checksum(my_buffer) );

	sprintf(message_whole, "%c%s/%s/%s%c", 0x02, message_header, message_body, message_footer, 0x03);

	strncpy(rawmessage, message_whole, rawmessage_length);

	if(smsc->type == SMSC_TYPE_EMI) {
		/* IC3S braindead EMI stack chopes on this... must fix it at the next time... */
		strcat(rawmessage, "\r");
	}	
	
	return strlen(rawmessage);

}

/******************************************************************************
* Parse the data from the two byte EMI code to normal ISO-8869-1
*/
static int internal_emi_parse_emi_to_iso88591(char *from, char *to,
					      int length, int alt_charset) {

	int hmtg = 0;
	unsigned int mychar;
	char tmpbuff[128];
	
	for(hmtg=0;hmtg<=strlen(from);hmtg+=2) {
		strncpy(tmpbuff, from+hmtg, 2);
		sscanf(tmpbuff, "%x", &mychar);
		to[hmtg/2] = internal_char_sms_to_iso(mychar, alt_charset);
	}

	to[(hmtg/2)-1] = '\0';

	return 0;

}

/******************************************************************************
* Parse the data from normal ISO-8869-1 to the two byte EMI code
*/
static int internal_emi_parse_iso88591_to_emi(char *from, char *to,
					      int length, int alt_charset) {

	char buf[10];
	unsigned char tmpchar;
	char *ptr;

	if( (from == NULL) || (to == NULL) || (length <= 0) )
		return -1;

	*to = '\0';

	for (ptr = from; length > 0; ptr++,length--) {
		tmpchar = internal_char_iso_to_sms(*ptr, alt_charset);
		sprintf(buf, "%02X", tmpchar);
		strncat(to, buf, 2);
	}

	return 0;
}

/******************************************************************************
* Parse the data from binary to the two byte EMI code
*/
static int internal_emi_parse_binary_to_emi(char *from, char *to, int length) {

	char buf[10];
	char *ptr;

	if( (from == NULL) || (to == NULL) || (length <= 0) )
		return -1;

	*to = '\0';

	for (ptr = from; length > 0; ptr++,length--) {
		sprintf(buf, "%02X", (unsigned char)*ptr);
		strncat(to, buf, 2);
	}

	return 0;
}


/******************************************************************************
* Generate the EMI message checksum
*/
static char *internal_emi_generate_checksum(const char *fmt, ...) {

	va_list args;
	static 	char	buf[1024];
	char	*ptr;
	int	j;

	va_start(args, fmt);
	vsnprintf(buf, 1024, fmt, args);
	va_end(args);


	/* ------------------------ */

	j = 0;
	for (ptr = buf; *ptr != '\0'; ptr++) {
		j += *ptr;

		if (j >= 256) {
			j -= 256;
		}
	}

	sprintf(buf, "%02X", j);
	return buf;
}



/******************************************************************************
* Translate character from iso to emi_mt
* PGrönholm
*/
static char internal_char_iso_to_sms(unsigned char from, int alt_charset) {

	switch((char)from) {	

		case 'A': return 0x41;
		case 'B': return 0x42;
		case 'C': return 0x43;
		case 'D': return 0x44;
		case 'E': return 0x45;
		case 'F': return 0x46;
		case 'G': return 0x47;
		case 'H': return 0x48;
		case 'I': return 0x49;
		case 'J': return 0x4A;
		case 'K': return 0x4B;
		case 'L': return 0x4C;
		case 'M': return 0x4D;
		case 'N': return 0x4E;
		case 'O': return 0x4F;
		case 'P': return 0x50;
		case 'Q': return 0x51;
		case 'R': return 0x52;
		case 'S': return 0x53;
		case 'T': return 0x54;
		case 'U': return 0x55;
		case 'V': return 0x56;
		case 'W': return 0x57;
		case 'X': return 0x58;
		case 'Y': return 0x59;
		case 'Z': return 0x5A;

		case 'a': return 0x61;
		case 'b': return 0x62;
		case 'c': return 0x63;
		case 'd': return 0x64;
		case 'e': return 0x65;
		case 'f': return 0x66;
		case 'g': return 0x67;
		case 'h': return 0x68;
		case 'i': return 0x69;
		case 'j': return 0x6A;
		case 'k': return 0x6B;
		case 'l': return 0x6C;
		case 'm': return 0x6D;
		case 'n': return 0x6E;
		case 'o': return 0x6F;
		case 'p': return 0x70;
		case 'q': return 0x71;
		case 'r': return 0x72;
		case 's': return 0x73;
		case 't': return 0x74;
		case 'u': return 0x75;
		case 'v': return 0x76;
		case 'w': return 0x77;
		case 'x': return 0x78;
		case 'y': return 0x79;
		case 'z': return 0x7A;

		case '0': return 0x30;
		case '1': return 0x31;
		case '2': return 0x32;
		case '3': return 0x33;
		case '4': return 0x34;
		case '5': return 0x35;
		case '6': return 0x36;
		case '7': return 0x37;
		case '8': return 0x38;
		case '9': return 0x39;
		case ':': return 0x3A;
		case ';': return 0x3B;
		case '<': return 0x3C;
		case '=': return 0x3D;
		case '>': return 0x3E;
		case '?': return 0x3F;

		case 'Ä': return '[';
		case 'Ö': return '\\';
		case 'Å': return 0x0E;
		case 'Ü': return ']';
		case 'ä': return '{';
		case 'ö': return '|';
		case 'å': return 0x0F;
		case 'ü': return '}';
		case 'ß': return '~';
		case '§': return '^';
		case 'Ñ': return 0x5F;
		case 'ø': return 0x0C;
		
/*		case 'Delta': return 0x10;	*/
/*		case 'Fii': return 0x12;	*/
/*		case 'Lambda': return 0x13;	*/
/*		case 'Alpha': return 0x14;	*/
/*		case 'Omega': return 0x15;	*/
/*		case 'Pii': return 0x16;	*/
/*		case 'Pii': return 0x17;	*/
/*		case 'Delta': return 0x18;	*/
/*		case 'Delta': return 0x19;	*/
/*		case 'Delta': return 0x1A;	*/

		case ' ': return 0x20;
	        case '@':
		    if (alt_charset == EMI_SWAPPED_CHARS)
			return 0x00;
		    else
			return 0x40;
		case '£': return 0x01;
		case '$': return 0x24;
		case '¥': return 0x03;
		case 'è': return 0x04;
		case 'é': return 0x05;
		case 'ù': return 0x06;
		case 'ì': return 0x07;
		case 'ò': return 0x08;
		case 'Ç': return 0x09;
		case '\r': return 0x0A;
		case 'Ø': return 0x0B;
		case '\n': return 0x0D;
		case 'Æ': return 0x1C;
		case 'æ': return 0x1D;
		case 'É': return 0x1F;

		case '!': return 0x21;
		case '"': return 0x22;
		case '#': return 0x23;
		case '¤': return 0x02;
		case '%': return 0x25;

		case '&': return 0x26;
		case '\'': return 0x27;
		case '(': return 0x28;
		case ')': return 0x29;
		case '*': return 0x2A;

		case '+': return 0x2B;
		case ',': return 0x2C;
		case '-': return 0x2D;
		case '.': return 0x2E;
		case '/': return 0x2F;

		case '¿': return 0x60;
		case 'ñ': return 0x1E;
		case 'à': return 0x7F;
		case '¡':
		    if (alt_charset == EMI_SWAPPED_CHARS)
			return 0x40;
		    else
			return 0x00;
		case '_': return 0x11;

		default:  return 0x20; /* space */
			
	} /* switch */
}


/******************************************************************************
* Translate character from emi_mo to iso
* PGrönholm
*/
static char internal_char_sms_to_iso(unsigned char from, int alt_charset) {

	switch((int)from) {	

		case 0x41: return 'A';
		case 0x42: return 'B';
		case 0x43: return 'C';
		case 0x44: return 'D';
		case 0x45: return 'E';
		case 0x46: return 'F';
		case 0x47: return 'G';
		case 0x48: return 'H';
		case 0x49: return 'I';
		case 0x4A: return 'J';
		case 0x4B: return 'K';
		case 0x4C: return 'L';
		case 0x4D: return 'M';
		case 0x4E: return 'N';
		case 0x4F: return 'O';
		case 0x50: return 'P';
		case 0x51: return 'Q';
		case 0x52: return 'R';
		case 0x53: return 'S';
		case 0x54: return 'T';
		case 0x55: return 'U';
		case 0x56: return 'V';
		case 0x57: return 'W';
		case 0x58: return 'X';
		case 0x59: return 'Y';
		case 0x5A: return 'Z';

		case 0x61: return 'a';
		case 0x62: return 'b';
		case 0x63: return 'c';
		case 0x64: return 'd';
		case 0x65: return 'e';
		case 0x66: return 'f';
		case 0x67: return 'g';
		case 0x68: return 'h';
		case 0x69: return 'i';
		case 0x6A: return 'j';
		case 0x6B: return 'k';
		case 0x6C: return 'l';
		case 0x6D: return 'm';
		case 0x6E: return 'n';
		case 0x6F: return 'o';
		case 0x70: return 'p';
		case 0x71: return 'q';
		case 0x72: return 'r';
		case 0x73: return 's';
		case 0x74: return 't';
		case 0x75: return 'u';
		case 0x76: return 'v';
		case 0x77: return 'w';
		case 0x78: return 'x';
		case 0x79: return 'y';
		case 0x7A: return 'z';

		case 0x30: return '0';
		case 0x31: return '1';
		case 0x32: return '2';
		case 0x33: return '3';
		case 0x34: return '4';
		case 0x35: return '5';
		case 0x36: return '6';
		case 0x37: return '7';
		case 0x38: return '8';
		case 0x39: return '9';
		case 0x3A: return ':';
		case 0x3B: return ';';
		case 0x3C: return '<';
		case 0x3D: return '=';
		case 0x3E: return '>';
		case 0x3F: return '?';

		case '[':	return 'Ä';
		case '\\':	return 'Ö';
		case '\xC5':	return 'Å';
		case ']':	return 'Ü';
		case '{':	return 'ä';
		case '|':	return 'ö';
		case 0xE5:	return 'å';
		case '}':	return 'ü';
		case '~':	return 'ß';
		case 0xA7:	return '§';
		case 0xD1:	return 'Ñ';
		case 0xF8:	return 'ø';
		
/*		case 'Delta':	return 0x10;	*/
/*		case 'Fii':		return 0x12;	*/
/*		case 'Lambda':	return 0x13;	*/
/*		case 'Alpha':	return 0x14;	*/
/*		case 'Omega':	return 0x15;	*/
/*		case 'Pii':		return 0x16;	*/
/*		case 'Pii':		return 0x17;	*/
/*		case 'Delta':	return 0x18;	*/
/*		case 'Delta':	return 0x19;	*/
/*		case 'Delta':	return 0x1A;	*/

		case 0x20: return ' ';
		case 0x40: return '@';
		case 0xA3: return '£';
		case 0x24: return '$';
		case 0xA5: return '¥';
		case 0xE8: return 'è';
		case 0xE9: return 'é';
		case 0xF9: return 'ù';
		case 0xEC: return 'ì';
		case 0xF2: return 'ò';
		case 0xC7: return 'Ç';
		case 0x0A: return '\r';
		case 0xD8: return 'Ø';
		case 0x0D: return '\n';
		case 0xC6: return 'Æ';
		case 0xE6: return 'æ';
		case 0x1F: return 'É';

		case 0x21: return '!';
		case 0x22: return '"';
		case 0x23: return '#';
		case 0xA4: return '¤';
		case 0x25: return '%';

		case 0x26: return '&';
		case 0x27: return '\'';
		case 0x28: return '(';
		case 0x29: return ')';
		case 0x2A: return '*';

		case 0x2B: return '+';
		case 0x2C: return ',';
		case 0x2D: return '-';
		case 0x2E: return '.';
		case 0x2F: return '/';

		case 0xBF: return '¿';
		case 0xF1: return 'ñ';
		case 0xE0: return 'à';
		case 0xA1: return '¡';
		case 0x5F: return '_';

		default: return ' ';
			
	} /* switch */
}




#if 0
/*-----------------------------------------------------------
 * convert ISO-8859-1 to SMS charset and return result
 * return underside question mark if not mappable
 *
 * NOTE: there is differences between documentation and real world; this
 *  is according to Sonera real world
 */
static unsigned char internal_char_iso_to_sms(unsigned char from) {

    switch(from) {	


    case 0x40: return 0x00;	/* '@' */
    case 0xA3: return 0x01;	/* '£' */
    case 0x24: return 0x02;	/* '$' */
    case 0xA5: return 0x03;
    case 0xE8: return 0x04;
    case 0xE9: return 0x05;
    case 0xF9: return 0x06;
    case 0xEC: return 0x07;
    case 0xF2: return 0x08;
    case 0xC7: return 0x09;
    case '\r': return 0x0A;
    case 0xD8: return 0x0B;
    case '\n': return 0x0D;
    case 0xC5: return 0x0E;
    case 0xE5: return 0x0F;
    case 0x80: return 0x10;
    case 0x5F: return 0x11;	/* '_' */
    case 0xC6: return 0x1C;
    case 0xE6: return 0x1D;
    case 0xDF: return 0x1E;
    case 0xC9: return 0x1F;

    case 0xA4: return 0x24;	/* '¤' */
    case 0xA1: return 0x40;

    case 0xC4: return 0x5B;	/* 'Ä' */
    case 0xD6: return 0x5C;
    case 0xD1: return 0x5D;
    case 0xDC: return 0x5E;    
    case 0xA7: return 0x5F;	/* '§' */
    case 0xBF: return 0x60;

    case 0xE4: return 0x7B;	/* 'ä' */
    case 0xF6: return 0x7C;	/* 'ö' */
    case 0xF1: return 0x7D;
    case 0xFC: return 0x7E;
    case 0xE0: return 0x7F;

    default:
	return 0x60;	/* '¿' if not recognized */

    } /* switch */

}


/*-----------------------------------------------------------
 */
static unsigned char internal_char_sms_to_iso(unsigned char from) {

    switch(from) {	

    case 0x00: return 0x40;	/* '@' */
    case 0x01: return 0xA3;	/* '£' */
    case 0x02: return 0x24;	/* '$' */
    case 0x03: return 0xA5;
    case 0x04: return 0x38;
    case 0x05: return 0xE9;
    case 0x06: return 0xF9;
    case 0x07: return 0xEC;
    case 0x08: return 0xF2;
    case 0x09: return 0xC7;
    case 0x0A: return '\r';
    case 0x0B: return 0xD8;
    case 0x0D: return '\n';
    case 0x0E: return 0xC5;
    case 0x0F: return 0xE5;
    case 0x10: return 0x80;
    case 0x11: return 0xF5;	/* '_' */
    case 0x1C: return 0xC6;
    case 0x1D: return 0xE6;
    case 0x1E: return 0xDF;
    case 0x1F: return 0xC9;

    case 0x24: return 0xA4;	/* '¤' */
    case 0x40: return 0xA1;

    case 0x5B: return 0xC4;	/* 'Ä' */
    case 0x5C: return 0xD6;
    case 0x5D: return 0xD1;
    case 0x5E: return 0xDC;    
    case 0x5F: return 0xA7;	/* '§' */
    case 0x60: return 0xBF;

    case 0x7B: return 0xE4;	/* 'ä' */
    case 0x7C: return 0xF6;	/* 'ö' */
    case 0x7D: return 0xF1;
    case 0x7E: return 0xFC;
    case 0x7F: return 0xE0;

    default:
	return 0xBF;	/* '¿' if not recognized */

    } /* switch */

}
#endif

