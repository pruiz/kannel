/*
 * smsc_at.c - implement interface to wireless modems using AT commands
 *
 * Yann Muller, Nick Clarey - 3G Lab, 2001.
 * 
 * Ericsson code by Chris Blown 30/01/2001 - Hinterlands Aust.
 *
 * Make sure your kannel configuration file contains the following lines
 * to be able to use the AT SMSC:
 *     group = smsc
 *     smsc = at
 *     modemtype = wavecom | premicell | siemens | siemens-tc35 | falcom |
 *                 nokiaphone | ericsson
 *     device = /dev/xxx 
 */


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <time.h>

#include "gwlib/gwlib.h"
#include "gwlib/charset.h"
#include "smsc.h"
#include "smsc_p.h"
#include "sms.h"

#ifndef CRTSCTS
#define CRTSCTS 0
#endif

/* The number of times to attempt to send a message should sending fail */
#define RETRY_SEND 3

/******************************************************************************
 * Prototypes for private functions
 */
static int at_data_read(int fd, Octstr *ostr);
static int send_modem_command(int fd, char *cmd, int multiline);
static int pdu_extract(SMSCenter *smsc, Octstr **ostr);
static Msg *pdu_decode(Octstr *data);
static Msg *pdu_decode_deliver_sm(Octstr *data);
static int pdu_encode(Msg *msg, unsigned char *pdu, SMSCenter *smsc);
static Octstr *convertpdu(Octstr *pdutext);
static int hexchar(int hexc);
static int encode7bituncompressed(Octstr *input, unsigned char *encoded, int offset);
static int encode8bituncompressed(Octstr *input, unsigned char *encoded);
static void decode7bituncompressed(Octstr *input, int len, Octstr *decoded, int offset);
static int numtext(int num);

/******************************************************************************
 * Types of GSM modems (as used in kannel.conf: at_type=xxxx)
 */
#define WAVECOM         "wavecom"
#define PREMICELL       "premicell"
#define SIEMENS         "siemens"
#define SIEMENS_TC35	"siemens-tc35"
#define FALCOM          "falcom"
#define NOKIAPHONE      "nokiaphone"
#define ERICSSON        "ericsson"

/******************************************************************************
 * Message types defines
 */
#define AT_DELIVER_SM   0
#define AT_SUBMIT_SM    1

/******************************************************************************
 * type of phone number defines
 */
#define PNT_UNKNOWN     0
#define PNT_INTER       1
#define PNT_NATIONAL    2

/******************************************************************************
 * Open the connection
 *
 * returns the file descriptor (fd) if ok, -1 on failure
 */
static int at_open_connection(SMSCenter *smsc) {
        int fd = -1;
        struct termios tios;
        int ret;
        
        fd = open(smsc->at_serialdevice, O_RDWR|O_NONBLOCK|O_NOCTTY);
        if(fd == -1) {
                error(errno, "at_open_data_link: error opening the character device <%s>",
                      smsc->at_serialdevice);
                return -1;
        }

        tcgetattr(fd, &tios);
        if((strcmp(smsc->at_modemtype, SIEMENS) == 0) 
	   || (strcmp(smsc->at_modemtype, SIEMENS_TC35) == 0)
           || (strcmp(smsc->at_modemtype, NOKIAPHONE) == 0)) {
                cfsetospeed(&tios, B19200);  /* check radio pad parameter*/
                cfsetispeed(&tios, B19200);
        } else {
                cfsetospeed(&tios, B9600);  /* check radio pad parameter*/
                cfsetispeed(&tios, B9600);
        }
        kannel_cfmakeraw(&tios);
        /* parameters:
         * IGNBRK, IGNPAR: ignore BREAK and PARITY errors
         * INPCK: enable parity check
         * CSIZE: for CS8
         * HUPCL: hang up on close
         * CREAD: enable receiver
         * CRTSCTS: enable flow control */
        tios.c_iflag |= IGNBRK | IGNPAR | INPCK;
        tios.c_cflag |= CSIZE | HUPCL | CREAD | CRTSCTS;
        if (strcmp(smsc->at_modemtype, NOKIAPHONE) == 0)
                tios.c_cflag ^= PARODD;
        tios.c_cflag |=CS8;
        ret = tcsetattr(fd, TCSANOW, &tios); /* apply changes now */
        if(ret == -1){
                error(errno,"at_data_link: fail to set termios attribute");
                goto error;
        }
        tcflush(fd, TCIOFLUSH);
        return fd;
  
 error:
        return -1;
}

/******************************************************************************
 * Open the (Virtual) SMSCenter
 */
SMSCenter *at_open(char *serialdevice, char *modemtype, char *pin,
                   char *validityperiod, int alt_dcs) 
{
        SMSCenter *smsc;
        char setpin[20];
        int ret;
        
        smsc = smscenter_construct();
        if(smsc == NULL)
                goto error;

        smsc->type = SMSC_TYPE_AT;
        smsc->at_serialdevice = gw_strdup(serialdevice);
        if (validityperiod != NULL)
                smsc->at_validityperiod = gw_strdup(validityperiod);
        smsc->at_modemtype = gw_strdup(modemtype);
        if(pin)
                smsc->at_pin = gw_strdup(pin);
        smsc->at_received = list_create();
        smsc->at_inbuffer = octstr_create("");
	smsc->at_alt_dcs = alt_dcs;
        
        smsc->at_fd = at_open_connection(smsc);
        if (smsc->at_fd < 0)
                goto error;

        /* Nokia 7110 and 6210 need some time between opening
         * the connection and sending the first AT commands */
        if (strcmp(smsc->at_modemtype, NOKIAPHONE) == 0) 
                sleep(1);

	/* lets initialize the modem to a safe state */
        send_modem_command(smsc->at_fd, "AT", 0);
        send_modem_command(smsc->at_fd, "AT&F", 0);
        send_modem_command(smsc->at_fd, "AT", 0);
	
        /* Turn Echo off on the modem: we don't need it */
        if(send_modem_command(smsc->at_fd, "ATE0", 0) == -1)
 	{
 		/* ok that was the first command we tried. */
                goto error;
	}
	/* Let's collect some information from modem */
	if(send_modem_command(smsc->at_fd, "ATI", 0) == -1)
	    goto error;
	sleep(1);
	if(send_modem_command(smsc->at_fd, "ATI1", 0) == -1)
	    goto error;
	sleep(1);
	if(send_modem_command(smsc->at_fd, "ATI2", 0) == -1)
	    goto error;
	sleep(1);
	if(send_modem_command(smsc->at_fd, "ATI3", 0) == -1)
	    goto error;
	sleep(1);
	if(send_modem_command(smsc->at_fd, "ATI4", 0) == -1)
	    goto error;
	sleep(1);

        /* Check does the modem require a PIN and, if so, send it
         * This is not supported by the Nokia Premicell */
        if(strcmp(smsc->at_modemtype, PREMICELL) != 0) {
                ret = send_modem_command(smsc->at_fd, "AT+CPIN?", 0); 
                if(ret == -1)
                        goto error;
                if(ret == -2) {
                        if(smsc->at_pin == NULL)
                                goto error;
                        sprintf(setpin, "AT+CPIN=%s", smsc->at_pin);
                        if(send_modem_command(smsc->at_fd, setpin, 0) == -1)
                                goto error;
                }
        }

        /* Set the modem to PDU mode and autodisplay of new messages */
        if(send_modem_command(smsc->at_fd, "AT+CMGF=0", 0) == -1)
                goto error;

	sleep(1);

        /* The Ericsson GM12 modem requires different new message 
         * indication options from the other modems */ 
        if(strcmp(smsc->at_modemtype, ERICSSON) == 0) {
                if(send_modem_command(smsc->at_fd, "AT+CNMI=3,2,0,0", 0) == -1)
                        goto error;
        }
        else if(strcmp(smsc->at_modemtype, SIEMENS_TC35) == 0) {
                if(send_modem_command(smsc->at_fd, "AT+CSMS=1", 0) == -1)
                        goto error;
                if(send_modem_command(smsc->at_fd, "AT+CNMI=1,2,0,0,1",0)== -1)
			goto error;
	}
        else {
                if(send_modem_command(smsc->at_fd, "AT+CNMI=1,2,0,0,0", 0) == -1)
                        goto error;
        }
                
        sprintf(smsc->name, "AT: %s", smsc->at_serialdevice); 
        
        info(0, "AT SMSC successfully opened.");

        return smsc;
  
 error:
        return NULL;
}

/******************************************************************************
 * Re-Open the AT (Virtual) SMSCenter
 */
int at_reopen(SMSCenter *smsc) {
        /* Do we really have an open connection to start with? */
        if (smsc->at_fd == -1) {
                info(0, "trying to close already closed AT, ignoring");
        }

        /* If we do, then try to close the file descriptor */
        else if (close(smsc->at_fd) == -1) {
                /* This situation could occur as a result of errors not being reported until
                   the serial connection is closed. Supposing we do get here, we
                   should reset the at_fd value to -1 to stop infinitely retrying the close. We
                   also need to printing out the error message reported, just in case it's
                   significant.
                */
                smsc->at_fd = -1;
                error(errno, "Attempt to close connection to modem `%s' failed. Forcing reset.",
                      smsc->at_serialdevice);
        }

        /* Reopen the connection. Note that the at_open_connection call returns
           a file descriptor, which we should set in the smsc structure */
        smsc->at_fd = at_open_connection(smsc);

        /* Supposing that failed */
        if (smsc->at_fd == -1) {
                error(0, "Attempt to open connection to modem '%s' failed.",
                      smsc->at_serialdevice);
                /* Give up */
                return -1;
        }

        /* Report success */
        return 0;
}

/******************************************************************************
 * Close the SMSCenter
 */
int at_close(SMSCenter *smsc) {
        /* Do we really have an open connection to start with? */
        if (smsc->at_fd == -1) {
                info(0, "trying to close already closed AT, ignoring");
        }

        /* If we do, then try to close the file descriptor */
        else if (close(smsc->at_fd) == -1) {
                error(errno, "Attempt to close connection to modem `%s' failed. Forcing reset.",
                      smsc->at_serialdevice);
        }
        
        /* Our file descriptor can now be safely declared closed */
        smsc->at_fd = -1;
        
        /* Deallocate any miscellany */
        smscenter_destruct(smsc);

        /* Report success */
        return 0;
}

/******************************************************************************
 * Check for pending messages
 */
int at_pending_smsmessage(SMSCenter *smsc) {
        Octstr *pdu = NULL;
        int ret=0;
        Msg *msg = NULL;

        /* Receive raw data */
        ret = at_data_read(smsc->at_fd, smsc->at_inbuffer);
        if(ret == -1) {
                ret = at_reopen(smsc);
                if(ret == -1) goto error;
                return 0;
        } 

        ret = 0;
        while( pdu_extract(smsc, &pdu) == 1) {
                msg = pdu_decode(pdu);
                if(msg != NULL) {
                        list_append(smsc->at_received, (void *)msg);
                        ret = 1;
                }
                octstr_destroy(pdu);
        }
        if(list_len(smsc->at_received) > 0)
                ret = 1;
        return ret; 

 error:
        error(errno,"at_pending message: device error");
        return -1;
}

/******************************************************************************
 * Send a message
 */
int at_submit_msg(SMSCenter *smsc, Msg *msg) {
        unsigned char command[500], pdu[500];
        int ret = -1; 
        char sc[3];
        int retries = RETRY_SEND;

        /* Some modem types need a '00' prepended to the PDU
         * to indicate to use the default SC.
         * NB: This extra padding is not counted in the CMGS byte count */

        sc[0] = '\0';
        if((strcmp(smsc->at_modemtype, WAVECOM ) == 0) || 
           (strcmp(smsc->at_modemtype, SIEMENS ) == 0) ||
           (strcmp(smsc->at_modemtype, SIEMENS_TC35 ) == 0) ||
           (strcmp(smsc->at_modemtype, NOKIAPHONE ) == 0) ||
           (strcmp(smsc->at_modemtype, ERICSSON ) == 0))
                strcpy(sc, "00");
        
        if(msg_type(msg)==sms) {
                pdu_encode(msg, &pdu[0], smsc);            
                sprintf(command, "AT+CMGS=%d", strlen(pdu)/2);
                if(send_modem_command(smsc->at_fd, command, 1) == 0)
                {
                        sprintf(command, "%s%s%c", sc, pdu, 26);
                        ret = send_modem_command(smsc->at_fd, command, 0);
                        debug("AT", 0, "send command status: %d", ret);
                        while (ret != 0 && retries > 0) {
				sprintf(command, "AT+CMGS=%d", strlen(pdu)/2);
				if(! send_modem_command(smsc->at_fd, command, 1) == 0)
				    break;
                               sprintf(command, "%s%s%c", sc, pdu, 26);
                               ret = send_modem_command(smsc->at_fd, command, 0);
                               debug("AT", 0, "send command status: %d", ret);
                               retries--;
                        }
			if (retries<0){
			  debug("AT", 0, "AT: Retries exceeded \nSMS send failure :");
			  debug("AT", 0, "Phone number: %s", octstr_get_cstr(msg->sms.receiver));
			  debug("AT", 0, "SMS data: %s", octstr_get_cstr(msg->sms.msgdata));
			}

                }
        }
        return ret;
}

/******************************************************************************
 * There are messages to read!
 */
int at_receive_msg(SMSCenter *smsc, Msg **msg) {

        *msg = list_consume(smsc->at_received);
        if(msg == NULL)
                goto error;
                
        return 1;
        
 error:
        return -1;
}

/******************************************************************************
 * Reads from the modem
 */
static int at_data_read(int fd, Octstr *ostr) {
        int ret;
        fd_set read_fd;
        struct timeval tv;
        size_t readall;
        char cbuffer[257];

     
        tv.tv_sec = 0;
        tv.tv_usec = 1000;
     
        readall = 0;
        for (;;) {
                memset(&cbuffer, 0, sizeof(cbuffer));
        
                FD_ZERO(&read_fd);
                FD_SET(fd, &read_fd);
                ret = select(fd + 1, &read_fd, NULL, NULL, &tv);
                if (ret == -1) {
                        if(errno==EINTR) goto got_data;
                        if(errno==EAGAIN) goto got_data;
                        error(errno, "Error doing select for fd");
                        goto error;
                } else if (ret == 0)
                        goto got_data;
                ret = read(fd, cbuffer, 256);
                if (ret == -1) {
                        goto error;
                }
                if (ret == 0)
                        goto eof;

                octstr_append_data(ostr, cbuffer, strlen(cbuffer));
                if(ret > 0)
                        break;
        }
     
 eof:
        ret = 1;
        goto unblock;
     
 got_data:
        ret = 0;
        goto unblock;

 error:
        error(errno," read device file");
        ret = -1;
        goto unblock;
     
 unblock:
        return ret;  

}

/******************************************************************************
 * Send an AT command to the modem
 * returns 0 if OK, -1 on failure, -2 on SIM PIN needed.
 * Set multiline to 1 if the command will expect more data to be sent.
 */
static int send_modem_command(int fd, char *cmd, int multiline) {
        Octstr *ostr;
        int ret, i;

        ostr = octstr_create("");

        /* debug */
        debug("bb.smsc.at", 0, "AT: Command: %s", cmd);
        
        /* DEBUG !!! - pretend to send but just return success (0)*/
        /* return 0; */
        
        /* send the command */  
        write(fd, cmd, strlen(cmd));
        write(fd, "\r", 1);

        /* We don't want to wait forever -
         * This is not perfect but OK for now */
        for( i=0; i<1000; i++) {
                ret = at_data_read(fd, ostr);
                /* debug */
                /*
                  if(octstr_len(ostr)) {
                  printf("Read from modem: ");
                  for(i=0; i<octstr_len(ostr); i++) {
                  if(octstr_get_char(ostr, i) <32)
                  printf("[%02x] ", octstr_get_char(ostr, i));
                  else
                  printf("%c ", octstr_get_char(ostr, i));
                  }
                  printf("\n");
                  }*/
                
                if(ret == -1)
                        goto error;

                ret = octstr_search(ostr, 
                                    octstr_imm("SIM PIN"), 0);
                if(ret != -1) {
			ret = -2;
                        goto error;
                }
                if(multiline)
                        ret = octstr_search(ostr, 
                                            octstr_imm(">"), 
                                            0);
                else {
                        ret = octstr_search(ostr, 
                                            octstr_imm("OK"), 
                                            0);
                        if(ret == -1)
                                ret = octstr_search(ostr, 
                                                    octstr_imm("READY"), 0);
                        if(ret == -1)
                                ret = octstr_search(ostr,
                                                    octstr_imm("CMGS"), 0);
                }
                if(ret != -1) {
			ret = 0;
                        goto error;
                }
                ret = octstr_search(ostr, 
                                    octstr_imm("ERROR"), 0);
                if(ret != -1) {
			ret = -1;
			goto error;
                }
        }
	ret = -1;
	goto error;
        
 error:
	for (i=0; i< octstr_len(ostr); i++) {
	    if (octstr_get_char(ostr, i) < 32)
		octstr_set_char(ostr, i, ' ');
	}
	octstr_strip_blanks(ostr);

	debug("AT", 0, "Read from modem: '%s'",octstr_get_cstr(ostr) );

        octstr_destroy(ostr);
        return ret;
}

/******************************************************************************
 * Extract the first PDU in the string
 */
static int pdu_extract(SMSCenter *smsc, Octstr **pdu) {
        Octstr *buffer;
        long len = 0;
        int pos = 0;
        int tmp;

        buffer = smsc->at_inbuffer;
        
        /* find the beginning of a message from the modem*/     
        pos = octstr_search(buffer, octstr_imm("+CMT:"), 0);
        if(pos == -1) 
                goto nomsg;
        pos += 5;
        pos = octstr_search(buffer, octstr_imm(","), pos);
        if(pos == -1)
                goto nomsg;
        pos++;

        /* The message length is after the comma */
        pos = octstr_parse_long(&len, buffer, pos, 10);
        if(pos == -1)
                goto nomsg;

        /* skip the spaces and line return */
        while( isspace(octstr_get_char(buffer, pos)))
                pos++;
        
        /* skip the SMSC address on some modem types */
        if((strcmp(smsc->at_modemtype, WAVECOM) == 0) ||
           (strcmp(smsc->at_modemtype, SIEMENS) == 0) ||
           (strcmp(smsc->at_modemtype, ERICSSON) == 0) ||
           (strcmp(smsc->at_modemtype, NOKIAPHONE) == 0) ) {
                tmp = hexchar(octstr_get_char(buffer, pos))*16
                        + hexchar(octstr_get_char(buffer, pos+1));
                if (tmp < 0)
                        goto nomsg;
                tmp = 2 + tmp * 2;
                pos += tmp;
        }
        
        /* check if the buffer is long enough to contain the full message */
        if( octstr_len(buffer) < len * 2 + pos)
                goto nomsg;
        
        /* copy the PDU then remove it from the input buffer*/
        *pdu = octstr_copy(buffer, pos, len*2);
        octstr_delete(buffer, 0, pos+len*2);

        return 1;

 nomsg:
        return 0;
}

/******************************************************************************
 * Decode a raw PDU into a Msg
 */
static Msg *pdu_decode(Octstr *data) {
        int type;
        Msg *msg = NULL;

        /* Get the PDU type */
        type = octstr_get_char(data, 1) & 3;
        
        switch(type) {

        case AT_DELIVER_SM:
                msg = pdu_decode_deliver_sm(data);
                break;

                /* Add other message types here: */
        
        }

        return msg;
}

/******************************************************************************
 * Decode a DELIVER PDU
 */
static Msg *pdu_decode_deliver_sm(Octstr *data) {
        int len, pos, i;
        char origaddr[21];
        int udhi, dcs, udhlen;
        Octstr *origin = NULL;
        Octstr *udh = NULL;
        Octstr *text = NULL, *tmpstr;
        Octstr *pdu = NULL;
        Msg *message = NULL;
        struct universaltime mtime;     /* time structure */
        long stime;             /* time in seconds */

        /* Note: some parts of the PDU are not decoded because they are
         * not needed for the Msg type. */

        /* convert the pdu to binary format for ease of processing */
        pdu = convertpdu(data);
        
        /* UDH Indicator */
        udhi = (octstr_get_char(pdu, 0) & 64) >> 6;

        /* originating address */
        len = octstr_get_char(pdu, 1);
        pos = 3;
        for(i=0; i<len; i+=2, pos++) {
                origaddr[i] = (octstr_get_char(pdu, pos) & 15) + 48 ;
                origaddr[i+1] = (octstr_get_char(pdu, pos) >> 4) + 48;
        }
        origaddr[i] = '\0';
        origin = octstr_create_from_data(origaddr, len);

        /* skip the PID for now */
        pos++;
	
        /* DCS */
	dcs = octstr_get_char(pdu, pos); 
        pos++;
        
        /* get the timestamp */
        mtime.year   = octstr_get_char(pdu, pos) + 1900; pos++;
        mtime.month  = octstr_get_char(pdu, pos); pos++;
        mtime.day    = octstr_get_char(pdu, pos); pos++;
        mtime.hour   = octstr_get_char(pdu, pos); pos++;
        mtime.minute = octstr_get_char(pdu, pos); pos++;
        mtime.second = octstr_get_char(pdu, pos); pos++;
        /* time zone: */
        /* XXX handle negative time zones */
        mtime.hour  += octstr_get_char(pdu, pos); pos++;
        stime = date_convert_universal(&mtime);
        
        /* get data length */
        len = octstr_get_char(pdu, pos);
        pos++;

        /* if there is a UDH */
    	udhlen = 0;
        if(udhi) {
                udhlen = octstr_get_char(pdu, pos);
                pos++;
                udh = octstr_copy(pdu, pos, udhlen);
                pos += udhlen;
                len -= udhlen +1;
        }

        /* build the message */         
        message = msg_create(sms);
	if (!dcs_to_fields(&message, dcs)) {
	    /* XXX Should reject this message ? */
	    debug("AT", 0, "Invalid DCS");
	    dcs_to_fields(&message, 0);
	}

        /* deal with the user data -- 7 or 8 bit encoded */     
        tmpstr = octstr_copy(pdu,pos,len);
        if(message->sms.coding == DC_8BIT || message->sms.coding == DC_UCS2) {
                text = octstr_duplicate(tmpstr);
        } else {
                int offset=0;
                text = octstr_create("");
                if (udhi && message->sms.coding == DC_7BIT) {
                        int nbits;
                        nbits = (udhlen + 1)*8;
                        offset = (((nbits/7)+1)*7-nbits)%7;     /* Fill bits for UDH to septet boundary */
                }
                decode7bituncompressed(tmpstr, len, text, offset);
        }

        message->sms.sender = origin;
        /* Put a dummy address in the receiver for now (SMSC requires one) */
        message->sms.receiver = octstr_create_from_data("1234", 4);
        /*message->sms.receiver = destination;*/
        if (udhi) {
                message->sms.udhdata = udh;
        }
        message->sms.msgdata = text;
        message->sms.time = stime;

        /* cleanup */
        octstr_destroy(pdu);
        octstr_destroy(tmpstr);
        
        return message;
}

/******************************************************************************
 * Encode a Msg into a PDU
 */
static int pdu_encode(Msg *msg, unsigned char *pdu, SMSCenter *smsc) {
        int pos = 0, i,len, setvalidity=0;
        int ntype = PNT_UNKNOWN; /* number type default */
        int nstartpos = 0;       /* offset for the phone number */
        int dcs;  /* data coding scheme (GSM 03.38) */
        
        /* The message is encoded directly in the text representation of 
         * the hex values that will be sent to the modem.
         * Each octet is coded with two characters. */
        
        /* message type SUBMIT
         *    01010001 = 0x51 indicating add. UDH, TP-VP(Rel) & MSG_SUBMIT
         * or 00010001 = 0x11 for just TP-VP(Rel) & MSG_SUBMIT */

        pdu[pos] = octstr_len(msg->sms.udhdata) ? numtext(5) : numtext(1);
        pos++;
        pdu[pos] = numtext(AT_SUBMIT_SM);
        pos++;
        
        /* message reference (0 for now) */
        pdu[pos] = numtext(0);
        pos++;
        pdu[pos] = numtext(0);
        pos++;
        
        /* destination address */
        octstr_strip_blanks(msg->sms.receiver); /* strip blanks before length calculation */
        len = octstr_len(msg->sms.receiver);

        /* Check for international numbers
         * number starting with '+' or '00' are international,
         * others are national. */
        if (strncmp(octstr_get_cstr(msg->sms.receiver), "+", 1) == 0) {
                debug("AT", 0, "international starting with + (%s)",octstr_get_cstr(msg->sms.receiver) );
                nstartpos++;
                ntype = PNT_INTER; /* international */
        } else if (strncmp(octstr_get_cstr(msg->sms.receiver), "00", 2) == 0) {
                debug("AT", 0, "international starting with 00 (%s)",octstr_get_cstr(msg->sms.receiver) );
                nstartpos += 2;
                ntype = PNT_INTER; /* international */
        }
        
        /* address length */
        pdu[pos] = numtext(((len - nstartpos) & 240) >> 4);
        pos++;
        pdu[pos] = numtext((len - nstartpos) & 15);
        pos++;
                        
        /* Type of number */
        pdu[pos] = numtext(8 + ntype);
        pos++;
        /* numbering plan: ISDN/Telephone numbering plan */
        pdu[pos] = numtext(1);
        pos++;
        
        /* make sure there is no blank in the phone number and encode
         * an even number of digits */
        octstr_strip_blanks(msg->sms.receiver);
        for(i=nstartpos; i<len; i+=2) {
                if (i+1 < len) {
                        pdu[pos] = octstr_get_char(msg->sms.receiver, i+1);
                } else {
                        pdu[pos] = numtext (15);
                }
                pos++;
                pdu[pos] = octstr_get_char(msg->sms.receiver, i);
                pos++;
        }
        
        /* protocol identifier */
        /* 0x00 implicit */
        pdu[pos] = numtext(0);
        pos++;
        pdu[pos] = numtext(0);
        pos++;
        
        /* data coding scheme */
	dcs = fields_to_dcs(msg, smsc->at_alt_dcs);
        pdu[pos] = numtext(dcs >> 4);
        pos++;
        pdu[pos] = numtext(dcs % 16);
        pos++;

        /* Validity-Period (TP-VP)
         * see GSM 03.40 section 9.2.3.12
         * defaults to 24 hours = 167 if not set */
	if ( msg->sms.validity) {
	    if (msg->sms.validity > 635040)
		setvalidity = 255;
	    if (msg->sms.validity >= 50400 && msg->sms.validity <= 635040)
		setvalidity = (msg->sms.validity - 1) / 7 / 24 / 60 + 192 + 1;
	    if (msg->sms.validity > 43200 && msg->sms.validity < 50400)
		setvalidity = 197;
	    if (msg->sms.validity >= 2880 && msg->sms.validity <= 43200)
		setvalidity = (msg->sms.validity - 1) / 24 / 60 + 166 + 1;
	    if (msg->sms.validity > 1440 && msg->sms.validity < 2880)
		setvalidity = 168;
	    if (msg->sms.validity >= 750 && msg->sms.validity <= 1440)
		setvalidity = (msg->sms.validity - 720 - 1) / 30 + 143 + 1;
	    if (msg->sms.validity > 720 && msg->sms.validity < 750)
		setvalidity = 144;
	    if (msg->sms.validity >= 5 && msg->sms.validity <= 720)
		setvalidity = (msg->sms.validity - 1) / 5 - 1 + 1;
	    if (msg->sms.validity < 5)
		setvalidity = 0;
	} else 
	    setvalidity = (smsc->at_validityperiod != NULL ? atoi(smsc->at_validityperiod) : 167);
        
        if (setvalidity >= 0 && setvalidity <= 143)
                debug("AT", 0, "TP-Validity-Period: %d minutes", 
                      (setvalidity+1)*5);
        else if (setvalidity >= 144 && setvalidity <= 167)
                debug("AT", 0, "TP-Validity-Period: %3.1f hours", 
                      ((float)(setvalidity-143)/2)+12);
        else if (setvalidity >= 168 && setvalidity <= 196)
                debug("AT", 0, "TP-Validity-Period: %d days", 
                      (setvalidity-166));
        else 
                debug("AT", 0, "TP-Validity-Period: %d weeks", 
                      (setvalidity-192));
        pdu[pos] = numtext((setvalidity & 240) >> 4);
        pos++;
        pdu[pos] = numtext(setvalidity & 15);
        pos++;

        /* user data length - include length of UDH if it exists*/
        len = octstr_len(msg->sms.msgdata);

        if(octstr_len(msg->sms.udhdata)) {
                if (msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2) {
		    len += octstr_len(msg->sms.udhdata);
                } else {
                        /* The reason we branch here is because UDH data length is determined
                           in septets if we are in GSM coding, otherwise it's in octets. Adding 6
                           will ensure that for an octet length of 0, we get septet length 0,
                           and for octet length 1 we get septet length 2.*/
                        len += (((8*octstr_len(msg->sms.udhdata)) + 6)/7);                
        }
        }

        pdu[pos] = numtext((len & 240) >> 4);
        pos++;
        pdu[pos] = numtext(len & 15);
        pos++;
        
        /* udh */
        if(octstr_len(msg->sms.udhdata)) {
                        pos += encode8bituncompressed(msg->sms.udhdata, &pdu[pos]);
        }

        /* user data */
        /* if the data is too long, it is cut */
        if(msg->sms.coding == DC_8BIT || msg->sms.coding == DC_UCS2) {
                pos += encode8bituncompressed(msg->sms.msgdata, &pdu[pos]);
        } else {
                int offset=0;

                if (octstr_len(msg->sms.udhdata)) {                                   /* Have UDH */
                        int nbits = octstr_len(msg->sms.udhdata)*8;     /* Includes UDH length byte */
                        offset = (((nbits/7)+1)*7-nbits)%7;                     /* Fill bits */
                }
                pos += encode7bituncompressed(msg->sms.msgdata, &pdu[pos],offset);
        }
        pdu[pos] = 0;

        return 0;
}

/******************************************************************************
 * Converts the text representation of hexa to binary
 */
static Octstr *convertpdu(Octstr *pdutext) {
        Octstr *pdu;
        int i;
        int len = octstr_len(pdutext);

        pdu = octstr_create("");
        for (i=0; i<len; i+=2) {
                octstr_append_char(pdu, hexchar(octstr_get_char(pdutext,i))*16 
                                   + hexchar(octstr_get_char(pdutext,i+1)));
        }
        return pdu;
}

/**********************************************************************
 * Encode 7bit uncompressed user data
 */
int ermask[8] = { 0, 1, 3, 7, 15, 31, 63, 127 };
int elmask[8] = { 0, 64, 96, 112, 120, 124, 126, 127 };
static int encode7bituncompressed(Octstr *input, unsigned char *encoded,int offset) {
        unsigned char prevoctet, tmpenc;
        int i;
        int c = 1;
        int r = 7;
        int pos = 0;
        int len;
        unsigned char enc7bit[256];
        int j,encpos = 0;

        charset_latin1_to_gsm(input);
        len = octstr_len(input);

        /* prevoctet is set to the first character and we'll start the loop
         * at the following char. */
        prevoctet = octstr_get_char(input ,0);
        for(i=1; i<octstr_len(input); i++) {
                /* a byte is encoded with what is left of the previous character
                 * and filled with as much as possible of the current one. */
                tmpenc = prevoctet + ((octstr_get_char(input,i) & ermask[c]) << r);
                enc7bit[encpos] = tmpenc; encpos++;
                c = (c>6)? 1 : c+1;
                r = (r<2)? 7 : r-1;

                /* prevoctet becomes the part of the current octet that hasn't
                 * been copied to 'encoded' or the next char if the current has
                 * been completely copied already. */
                prevoctet = (octstr_get_char(input,i) & elmask[r]) >> (c-1);
                if(r == 7) {
                        i++;
                        prevoctet = octstr_get_char(input, i);
                }
        }

        /* if the length of the message is a multiple of 8 then we
         * are finished. Otherwise prevoctet still contains part of a 
         * character so we add it. */
        if((len/8)*8 != len) {
                enc7bit[encpos] = prevoctet;encpos++;
        }

        /* Now shift the buffer by the offset */
        if (offset > 0) {
                unsigned char nextdrop, lastdrop;

    	    	nextdrop = lastdrop = 0;
                for (i = 0; i < encpos; i++) {
                        nextdrop = enc7bit[i] >> (8 - offset);          /* This drops off by shifting */
                        if (i == 0)
                                enc7bit[i] = enc7bit[i] << offset;              /* This drops off by shifting */
                        else
                                enc7bit[i] = (enc7bit[i] << offset) | lastdrop;
                        lastdrop = nextdrop;
                }

                if (offset > ((len*7) % 8)) {
                        enc7bit [i] = nextdrop;
                        i++;
                }
        }
        else
                i = encpos;

        for (j = 0; j < i; j++) {
                encoded[pos] = numtext((enc7bit [j] & 240) >> 4); pos++;
                encoded[pos] = numtext(enc7bit [j] & 15); pos++;
        }
        return pos;
                
}

/**********************************************************************
 * Encode 8bit uncompressed user data
 */
static int encode8bituncompressed(Octstr *input, unsigned char *encoded) {
        int len, i;

        len = octstr_len(input);
        
        for(i=0; i<len; i++) {
                /* each character is encoded in its hex representation (2 chars) */
                encoded[i*2] = numtext((octstr_get_char(input, i) & 240) >> 4);
                encoded[i*2+1] = numtext(octstr_get_char(input, i) & 15);
        }
        return len*2;
}

/**********************************************************************
 * Decode 7bit uncompressed user data
 */
int rmask[8] = { 0, 1, 3, 7, 15, 31, 63, 127 };
int lmask[8] = { 0, 128, 192, 224, 240, 248, 252, 254 };

static void decode7bituncompressed(Octstr *input, int len, Octstr *decoded, int offset) {
        unsigned char septet, octet, prevoctet;
        int i;
        int r = 1;
        int c = 7;
        int pos = 0;
        
        /* Shift the buffer offset bits to the left */
        if (offset > 0) {
                unsigned char *ip;
                for (i = 0, ip = octstr_get_cstr(input); i < octstr_len(input); i++) {
                        if (i == octstr_len(input) - 1)
                                *ip = *ip >> offset;
                        else
                                *ip = (*ip >> offset) | (*(ip + 1) << (8 - offset));
                        ip++;
                }
        }
        octet = octstr_get_char(input, pos);
        prevoctet = 0;
        for(i=0; i<len; i++) {
                septet = ((octet & rmask[c]) << (r-1)) + prevoctet;
                octstr_append_char(decoded, septet);

                prevoctet = (octet & lmask[r]) >> c;
        
                /* When r=7 we have a full character in prevoctet*/
                if((r==7) && (i<len-1)){
                        i++;
                        octstr_append_char(decoded, prevoctet);
                        prevoctet = 0;
                }

                r = (r>6)? 1 : r+1;
                c = (c<2)? 7 : c-1;

                pos++;
                octet = octstr_get_char(input, pos);
        }
        charset_gsm_to_latin1(decoded);
}

/**********************************************************************
 * Code a half-byte to its text hexa representation
 */
static int numtext(int num) {
        return (num > 9) ? (num+55) : (num+48);
}


/**********************************************************************
 * Get the numeric value of the text hex
 */
static int hexchar(int hexc) {
        hexc = toupper(hexc) - 48;
        return (hexc>9) ? hexc-7 : hexc;
}
