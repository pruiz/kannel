/*
 * smsc_at.c - implement interface to wireless modems using AT commands
 *
 * Yann Muller - 3G Lab, 2000.
 * 
 * Make sure your kannel configuration file contains the following lines
 * to be able to use the AT SMSC:
 *     group = smsc
 *     smsc = at
 *     modemtype = wavecom | premicell | siemens | falcom | nokiaphone
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
#include "smsc.h"
#include "smsc_p.h"

#ifndef CRTSCTS
#define CRTSCTS 0
#endif

/******************************************************************************
 * Prototypes for private functions
 */
static int at_data_read(int fd, Octstr *ostr);
static int send_modem_command(int fd, char *cmd, int multiline);
static int pdu_extract(SMSCenter *smsc, Octstr **ostr);
static Msg *pdu_decode(Octstr *data);
static Msg *pdu_decode_deliver_sm(Octstr *data);
static int pdu_encode(Msg *msg, unsigned char *pdu);
static Octstr *convertpdu(Octstr *pdutext);
static int hexchar(char hexc);
static int encode7bituncompressed(Octstr *input, unsigned char *encoded);
static int encode8bituncompressed(Octstr *input, unsigned char *encoded);
static void decode7bituncompressed(Octstr *input, int len, Octstr *decoded);
static int numtext(int num);
unsigned char gsm_alpha(unsigned char value);


/******************************************************************************
 * Types of GSM modems (as used in kannel.conf: at_type=xxxx)
 */
#define WAVECOM		"wavecom"
#define PREMICELL	"premicell"
#define SIEMENS		"siemens"
#define FALCOM		"falcom"
#define NOKIAPHONE	"nokiaphone"

/******************************************************************************
 * Message types defines
 */
#define AT_DELIVER_SM	0
#define AT_SUBMIT_SM	1

/******************************************************************************
 * type of phone number defines
 */
#define PNT_UNKNOWN	0
#define PNT_INTER	1
#define PNT_NATIONAL	2

/******************************************************************************
 * ETSI GSM 03.38, version 6.0.1, section 6.2.1; Default alphabet 
 * Characters in hex position 10, [12 to 1a] and 24 are not present on
 * latin1 charset, so we cannot reproduce on the screen 
 */

unsigned char GSM_Default_Alphabet[] = {
  '@',  0xa3, '$',  0xa5, 0xe8, 0xe9, 0xf9, 0xec,
  0xf2, 0xc7, '\n', 0xd8, 0xf8, '\r', 0xc5, 0xe5,
  '?',  '_',  '?',  '?',  '?',  '?',  '?',  '?',
  '?',  '?',  '?',  '?',  0xc6, 0xe6, 0xdf, 0xc9,
  ' ',  '!',  '\"', '#',  0xa4,  '%',  '&',  '\'',
  '(',  ')',  '*',  '+',  ',',  '-',  '.',  '/',
  '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',
  '8',  '9',  ':',  ';',  '<',  '=',  '>',  '?',
  0xa1, 'A',  'B',  'C',  'D',  'E',  'F',  'G',
  'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',
  'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',
  'X',  'Y',  'Z',  0xc4, 0xd6, 0xd1, 0xdc, 0xa7,
  0xbf, 'a',  'b',  'c',  'd',  'e',  'f',  'g',
  'h',  'i',  'j',  'k',  'l',  'm',  'n',  'o',
  'p',  'q',  'r',  's',  't',  'u',  'v',  'w',
  'x',  'y',  'z',  0xe4, 0xf6, 0xf1, 0xfc, 0xe0
};


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
		error(errno, "at_open_data_link: error open(2)ing the character device <%s>",
		      smsc->at_serialdevice);
		return -1;
	}

	tcgetattr(fd, &tios);
	if((strcmp(smsc->at_modemtype, SIEMENS) == 0) 
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
SMSCenter *at_open(char *serialdevice, char *modemtype, char *pin) {
	SMSCenter *smsc;
	char setpin[20];
	int ret;
	
	smsc = smscenter_construct();
	if(smsc == NULL)
		goto error;

	smsc->type = SMSC_TYPE_AT;
	smsc->at_serialdevice = gw_strdup(serialdevice);
	smsc->at_modemtype = gw_strdup(modemtype);
	if(pin)
		smsc->at_pin = gw_strdup(pin);
	smsc->at_received = list_create();
	smsc->at_inbuffer = octstr_create("");

	smsc->at_fd = at_open_connection(smsc);
	if (smsc->at_fd < 0)
		goto error;

	/* Nokia 7110 and 6210 need some time between opening
	 * the connection and sending the first	AT commands */
	if (strcmp(smsc->at_modemtype, NOKIAPHONE) == 0) 
		sleep(1);

	/* Turn Echo off on the modem: we don't need it */
	if(send_modem_command(smsc->at_fd, "ATE0", 0) == -1)
		goto error;
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
	if(send_modem_command(smsc->at_fd, "AT+CNMI=1,2,0,0,0", 0) == -1)
		goto error;
		
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
	if (smsc->at_fd == -1) {
		info(0, "trying to close already closed AT, ignoring");
	}
	else if (close(smsc->at_fd) == -1) {
		error(errno, "Closing connection to modem `%s' failed.",
		smsc->at_serialdevice);
		return -1;
	}
	return at_open_connection(smsc);
}

/******************************************************************************
 * Close the SMSCenter
 */
int at_close(SMSCenter *smsc) {
	if (smsc->at_fd == -1) {
		info(0, "trying to close previously closed connection to modem, ignoring");
		return 0;
	}
	if (close(smsc->at_fd) == -1) {
		error(errno, "Closing connection to modem `%s' failed.",
		smsc->at_serialdevice);
		return -1;
	}
	smscenter_destruct(smsc);
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

	/* Some modem types need a '00' prepended to the PDU
	 * to indicate to use the default SC. */
	sc[0] = '\0';
	if((strcmp(smsc->at_modemtype, WAVECOM) == 0) || 
       (strcmp(smsc->at_modemtype, SIEMENS) == 0) ||
	   (strcmp(smsc->at_modemtype, NOKIAPHONE) == 0))
		strcpy(sc, "00");
	
	if(msg_type(msg)==sms) {
		pdu_encode(msg, &pdu[0]);
		
		sprintf(command, "AT+CMGS=%d", strlen(pdu)/2);
		if(send_modem_command(smsc->at_fd, command, 1) == 0)
		{
			sprintf(command, "%s%s%c", sc, pdu, 26);
			ret = send_modem_command(smsc->at_fd, command, 0);
			debug("AT", 0, "send command status: %d", ret);
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
	/* printf("Command: %s\n", cmd); */
	
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
		    	    	    octstr_create_immutable("SIM PIN"), 0);
		if(ret != -1) {
			octstr_destroy(ostr);
			return -2;
		}
		if(multiline)
			ret = octstr_search(ostr, 
					    octstr_create_immutable(">"), 
					    0);
		else {
			ret = octstr_search(ostr, 
			    	    	    octstr_create_immutable("OK"), 
					    0);
			if(ret == -1)
				ret = octstr_search(ostr, 
				    	 octstr_create_immutable("READY"), 0);
			if(ret == -1)
				ret = octstr_search(ostr,
				    	 octstr_create_immutable("CMGS"), 0);
		}
		if(ret != -1) {
			octstr_destroy(ostr);
			return 0;
		}
		ret = octstr_search(ostr, 
		    	    	    octstr_create_immutable("ERROR"), 0);
		if(ret != -1) {
			octstr_destroy(ostr);
			return -1;
		}
	}
	octstr_destroy(ostr);
	return -1;
	
error:
	octstr_destroy(ostr);
	return -1;
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
	pos = octstr_search(buffer, octstr_create_immutable("+CMT:"), 0);
	if(pos == -1) 
		goto nomsg;
	pos += 5;
	pos = octstr_search(buffer, octstr_create_immutable(","), pos);
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
       (strcmp(smsc->at_modemtype, NOKIAPHONE) == 0)) {
		tmp = hexchar(octstr_get_char(buffer, pos))*16
		    + hexchar(octstr_get_char(buffer, pos+1));
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
	int udhi, eightbit, udhlen;
	Octstr *origin = NULL;
	Octstr *udh = NULL;
	Octstr *text = NULL, *tmpstr;
	Octstr *pdu = NULL;
	Msg *message = NULL;
	struct tm mtime;	/* time structure */
	time_t stime;		/* time in seconds */

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
	/* DCS : 8bit? */
	eightbit = (octstr_get_char(pdu, pos) & 4) >> 2;
	pos++;
	
	/* get the timestamp */
	mtime.tm_year = octstr_get_char(pdu, pos) + 100; pos++;
	mtime.tm_mon  = octstr_get_char(pdu, pos); pos++;
	mtime.tm_mday = octstr_get_char(pdu, pos); pos++;
	mtime.tm_hour = octstr_get_char(pdu, pos); pos++;
	mtime.tm_min  = octstr_get_char(pdu, pos); pos++;
	mtime.tm_sec = octstr_get_char(pdu, pos); pos++;
	/* time zone: */
	mtime.tm_hour += octstr_get_char(pdu, pos); pos++;
	stime = mktime(&mtime);
	
	/* get data length */
	len = octstr_get_char(pdu, pos);
	pos++;

	/* if there is a UDH */
	if(udhi) {
		udhlen = octstr_get_char(pdu, pos);
		pos++;
		udh = octstr_copy(pdu, pos, udhlen);
		pos += udhlen;
		len -= udhlen +1;
	}

	/* deal with the user data -- 7 or 8 bit encoded */	
	tmpstr = octstr_copy(pdu,pos,len);
	if(eightbit == 1) {
		text = octstr_duplicate(tmpstr);
	} else {
		text = octstr_create("");
		decode7bituncompressed(tmpstr, len, text);
	}

	/* build the message */		
	message = msg_create(sms);
	message->sms.sender = origin;
	/* Put a dummy address in the receiver for now (SMSC requires one) */
	message->sms.receiver = octstr_create_from_data("1234", 4);
	/*message->sms.receiver = destination;*/
	if (udhi) {
		message->sms.flag_udh = 1;
		message->sms.udhdata = udh;
	}
	message->sms.flag_8bit = eightbit;
	message->sms.msgdata = text;
	message->sms.time = (int)stime;

	/* cleanup */
	octstr_destroy(pdu);
	octstr_destroy(tmpstr);
	
	return message;
}

/******************************************************************************
 * Encode a Msg into a PDU
 */
static int pdu_encode(Msg *msg, unsigned char *pdu) {
	int pos = 0, i,len;
	int ntype = PNT_UNKNOWN; /* number type default */
	int nstartpos = 0;	 /* offset for the phone number */
	
	/* The message is encoded directly in the text representation of 
	 * the hex values that will be sent to the modem.
	 * Each octet is coded with two characters. */
	
	/* message type SUBMIT */
	pdu[pos] = (msg->sms.flag_udh != 0) ? numtext(4) : numtext(0);
	pos++;
	pdu[pos] = numtext(AT_SUBMIT_SM);
	pos++;
	
	/* message reference (0 for now) */
	pdu[pos] = numtext(0);
	pos++;
	pdu[pos] = numtext(0);
	pos++;
	
	/* destination address */
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
	
	debug("AT", 0, "nstartpos: %d", nstartpos);
	
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
	/* 0x1F GSM 03.40 default value */
 	pdu[pos] = numtext(1);
	pos++;
	pdu[pos] = numtext(15);
	pos++;
	
	/* data coding scheme */
	/* coding group bits: 1111 */
	pdu[pos] = numtext(15);
	pos++;
	/* data coding/message class: class 1 */
	pdu[pos] = numtext(msg->sms.flag_8bit << 2) + 1;
	pos++;

	/* user data length - include length of UDH if it exists*/
	len = octstr_len(msg->sms.msgdata);
	if(msg->sms.flag_udh != 0) {
		if(msg->sms.flag_8bit != 0)
			len += octstr_len(msg->sms.udhdata) + 1;
		else
			len += octstr_len(msg->sms.udhdata) / 2 * 8 / 7 + 1;
	}
	pdu[pos] = numtext((len & 240) >> 4);
	pos++;
	pdu[pos] = numtext(len & 15);
	pos++;
	
	/* udh */
	if(msg->sms.flag_udh != 0) {
            if(msg->sms.flag_8bit == 0)
                len = roundup_div(octstr_len(msg->sms.udhdata)*8, 7);
            else
                len = octstr_len(msg->sms.udhdata);

		/* udh length */
		pdu[pos] = numtext((len & 240) >> 4);
		pos++;
		pdu[pos] = numtext(len & 15);
		pos++;

        if(msg->sms.flag_8bit == 0)
            pos += encode7bituncompressed(msg->sms.udhdata, &pdu[pos]);
        else
            pos += encode8bituncompressed(msg->sms.udhdata, &pdu[pos]);
	}

	/* user data */
	/* if the data is too long, it is cut */
	if(msg->sms.flag_8bit == 1) {
		pos += encode8bituncompressed(msg->sms.msgdata, &pdu[pos]);
	} else {
		pos += encode7bituncompressed(msg->sms.msgdata, &pdu[pos]);
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

static int encode7bituncompressed(Octstr *input, unsigned char *encoded) {
	unsigned char prevoctet, tmpenc;
	int i;
	int c = 1;
	int r = 7;
	int pos = 0;
	int len;

	len = octstr_len(input);

	/* prevoctet is set to the first character and we'll start the loop
	 * at the following char. */
	prevoctet = gsm_alpha(octstr_get_char(input ,0) & 0x7f);
	for(i=1; i<octstr_len(input); i++) {
		/* a byte is encoded with what is left of the previous character
		 * and filled with as much as possible of the current one. */
		tmpenc = prevoctet + ((gsm_alpha(octstr_get_char(input,i) & 0x7f) & ermask[c]) << r);
		encoded[pos] = numtext((tmpenc & 240) >> 4); pos++;
		encoded[pos] = numtext(tmpenc & 15); pos++;
		c = (c>6)? 1 : c+1;
		r = (r<2)? 7 : r-1;

		/* prevoctet becomes the part of the current octet that hasn't
		 * been copied to 'encoded' or the next char if the current has
		 * been completely copied already. */
		prevoctet = (gsm_alpha(octstr_get_char(input,i) & 0x7f) & elmask[r]) >> (c-1);
		if(r == 7) {
			i++;
			prevoctet = gsm_alpha(octstr_get_char(input, i) & 0x7f);
		}
	}
	/* if the length of the message is a multiple of 8 then we
	 * are finished. Otherwise prevoctet still contains part of a 
	 * character so we add it. */
	if((len/8)*8 != len) {
		encoded[pos] = numtext((prevoctet & 240) >> 4); pos++;
		encoded[pos] = numtext(prevoctet & 15); pos++;
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

static void decode7bituncompressed(Octstr *input, int len, Octstr *decoded) {
	unsigned char septet, octet, prevoctet;
	int i;
	int r = 1;
	int c = 7;
	int pos = 0;
	
	octet = octstr_get_char(input, pos);
	prevoctet = 0;
	for(i=0; i<len; i++) {
		septet = ((octet & rmask[c]) << (r-1)) + prevoctet;
                octstr_append_char(decoded, GSM_Default_Alphabet[septet]);

		prevoctet = (octet & lmask[r]) >> c;
	
		/* When r=7 we have a full character in prevoctet*/
		if((r==7) && (i<len-1)){
			i++;
                        octstr_append_char(decoded, GSM_Default_Alphabet[prevoctet]);
			prevoctet = 0;
		}

		r = (r>6)? 1 : r+1;
		c = (c<2)? 7 : c-1;

		pos++;
		octet = octstr_get_char(input, pos);
	}
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
static int hexchar(char hexc) {
	hexc = toupper(hexc) - 48;
	return (hexc>9) ? hexc-7 : hexc;
}

/**********************************************************************
 * GSM default character encoding
 */

unsigned char gsm_alpha(unsigned char value)
{

  unsigned char i;

  if (value == '?') return  0x3f;

  for (i = 0 ; i < 128 ; i++)
    if (GSM_Default_Alphabet[i] == value)
      return i;

  return 0x3f; /* '?' */
}

