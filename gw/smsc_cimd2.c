/* Driver for CIMD 2 SMS centres.
 * Copyright 2000  WapIT Oy Ltd.
 * Author: Richard Braakman <dark@xs4all.nl>
 */

/* TODO: Check checksums on incoming packets */

/* This code is based on the CIMD 2 spec, version 1-0 en.
 * All USSD-specific parts have been left out, since we only want to
 * communicate with SMSC's.
 *
 * I found one contradiction in the spec:
 *
 * - The definition of Integer parameters specifies decimal digits only,
 *   but at least one Integer parameter (Validity Period Relative) can
 *   be negative.  I assume that this means a leading - is valid.
 */

#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#include <unistd.h>

#include "gwlib/gwlib.h"
#include "smsc_p.h"

#ifndef CIMD2_TRACE
#define CIMD2_TRACE 0
#endif

/* Microseconds before giving up on a request */
#define RESPONSE_TIMEOUT (10 * 1000000)

/* Textual names for the operation codes defined by the CIMD 2 spec. */
/* If you make changes here, also change the operation table. */
enum {
	/* Requests from client */
	LOGIN = 1,
	LOGOUT = 2,
	SUBMIT_MESSAGE = 3,
	ENQUIRE_MESSAGE_STATUS = 4,
	DELIVERY_REQUEST = 5,
	CANCEL_MESSAGE = 6,
	SET_REQ = 8,
	GET_REQ = 9,

	/* Requests from server */
	DELIVER_MESSAGE = 20,
	DELIVER_STATUS_REPORT = 23,

	/* Requests from either */
	ALIVE = 40,

	/* Not a request; add to any request to make it a response */
	RESPONSE = 50,
	
	/* Responses not related to requests */
        GENERAL_ERROR_RESPONSE = 98,
	NACK = 99
};

/* Textual names for the parameters defined by the CIMD 2 spec. */
/* If you make changes here, also change the parameter table. */
enum {
	P_USER_IDENTITY = 10,
	P_PASSWORD = 11,
	P_DESTINATION_ADDRESS = 21,
	P_ORIGINATING_ADDRESS = 23,
	P_DATA_CODING_SCHEME = 30,
	P_USER_DATA_HEADER = 32,
	P_USER_DATA = 33,
	P_USER_DATA_BINARY = 34,
	P_VALIDITY_PERIOD_RELATIVE = 50,
	P_VALIDITY_PERIOD_ABSOLUTE = 51,
	P_PROTOCOL_IDENTIFIER = 52,
	P_FIRST_DELIVERY_TIME_RELATIVE = 53,
	P_FIRST_DELIVERY_TIME_ABSOLUTE = 54,
	P_REPLY_PATH = 55,
	P_STATUS_REPORT_REQUEST = 56,
	P_CANCEL_ENABLED = 58,
	P_CANCEL_MODE = 59,
	P_MC_TIMESTAMP = 60,
	P_STATUS_CODE = 61,
	P_DISCHARGE_TIME = 63,
	P_TARIFF_CLASS = 64,
	P_SERVICE_DESCRIPTION = 65,
	P_MESSAGE_COUNT = 66,
	P_PRIORITY = 67,
	P_DELIVERY_REQUEST_MODE = 68,
	P_GET_PARAMETER = 500,
	P_MC_TIME = 501,
	P_ERROR_CODE = 900,
	P_ERROR_TEXT = 901
};

/***************************************************************************/
/* Table of properties of the parameters defined by CIMD 2, and some       */
/* functions to look up fields.                                            */
/***************************************************************************/

/* Parameter types, internal.  CIMD 2 spec considers P_TIME to be "Integer"
 * and P_SMS to be "User Data". */
enum { P_INT, P_STRING, P_ADDRESS, P_TIME, P_HEX, P_SMS };

/* Information about the parameters defined by the CIMD 2 spec.
 * Used for warning about invalid incoming messages, and for validating
 * outgoing messages. */
const static struct {
	unsigned char *name;
	int number;
	int maxlen;
	int type; /* P_ values */
	int minval, maxval; /* For P_INT */
} parameters[] = {
	{ "user identity", P_USER_IDENTITY, 32, P_STRING },
	{ "password", P_PASSWORD, 32, P_STRING },
	{ "destination address", P_DESTINATION_ADDRESS, 20, P_ADDRESS },
	{ "originating address", P_ORIGINATING_ADDRESS, 20, P_ADDRESS },
	{ "data coding scheme", P_DATA_CODING_SCHEME, 3, P_INT, 0, 255 },
	{ "user data header", P_USER_DATA_HEADER, 280, P_HEX },
	{ "user data", P_USER_DATA, 480, P_SMS },
	{ "user data binary", P_USER_DATA_BINARY, 280, P_HEX },
	{ "validity period relative", P_VALIDITY_PERIOD_RELATIVE, 3, P_INT, -1, 255 },
	{ "validity period absolute", P_VALIDITY_PERIOD_ABSOLUTE, 12, P_TIME },
	{ "protocol identifier", P_PROTOCOL_IDENTIFIER, 3, P_INT, 0, 255 },
	{ "first delivery time relative", P_FIRST_DELIVERY_TIME_RELATIVE, 3, P_INT, -1, 255 },
	{ "first delivery time absolute", P_FIRST_DELIVERY_TIME_ABSOLUTE, 12, P_TIME },
	{ "reply path", P_REPLY_PATH, 1, P_INT, 0, 1 },
	{ "status report request", P_STATUS_REPORT_REQUEST, 2, P_INT, 0, 32 },
	{ "cancel enabled", P_CANCEL_ENABLED, 1, P_INT, 0, 1 },
	{ "cancel mode", P_CANCEL_MODE, 1, P_INT, 0, 2 },
	{ "MC timestamp", P_MC_TIMESTAMP, 12, P_TIME },
	{ "status code", P_STATUS_CODE, 2, P_INT, 0, 9 },
	{ "discharge time", P_DISCHARGE_TIME, 12, P_TIME },
	{ "tariff class", P_TARIFF_CLASS, 2, P_INT, 0, 99 },
	{ "service description", P_SERVICE_DESCRIPTION, 1, P_INT, 0, 9 },
	{ "message count", P_MESSAGE_COUNT, 3, P_INT, 0, 999 },
	{ "priority", P_PRIORITY, 1, P_INT, 1, 9 },
	{ "delivery request mode", P_DELIVERY_REQUEST_MODE, 1, P_INT, 0, 2 },
	{ "get parameter", P_GET_PARAMETER, 3, P_INT, 501, 999 },
	{ "MC time", P_MC_TIME, 12, P_TIME },
	/* Spec is contradictory about error code.  It says they should be
	 * of max length 2, but it lists 3-digit error codes to use. */
	{ "error code", P_ERROR_CODE, 3, P_INT, 0, 999 },
	{ "error text", P_ERROR_TEXT, 64, P_STRING },
	{ NULL }
};

/* Return the index in the parameters array for this parameter id.
 * Return -1 if it is not found. */
static const int parm_index(int parmno) {
	int i;

	for (i = 0; parameters[i].name != NULL; i++) {
		if (parameters[i].number == parmno)
			return i;
	}

	return -1;
}

#ifndef NDEBUG
/* Return the type of this parameter id.  Return -1 if the id is unknown. */
static const int parm_type(int parmno) {
	int i = parm_index(parmno);

	if (i < 0)
		return -1;

	return parameters[i].type;
}
#endif

/* Return the max length for this parameter id.
 * Return -1 if the id is unknown. */
static const int parm_maxlen(int parmno) {
	int i = parm_index(parmno);

	if (i < 0)
		return -1;

	return parameters[i].maxlen;
}

static const char *parm_name(int parmno) {
	int i = parm_index(parmno);

	if (i < 0)
		return NULL;

	return parameters[i].name;
}

#ifndef NDEBUG
/* Return 1 if the value for this (Integer) parameter is in range.
 * Return 0 otherwise.  Return -1 if the parameter was not found.  */
static const int parm_in_range(int parmno, long value) {
	int i;

	i = parm_index(parmno);

	if (i < 0)
		return -1;

	return (value >= parameters[i].minval &&
		value <= parameters[i].maxval);
}
#endif

/* Helper function to check P_ADDRESS type */
static int isphonedigit(int c) {
	return isdigit(c) || c == '+' || c == '-';
}

static const int parm_valid_address(Octstr *value) {
	return octstr_check_range(value, 0, octstr_len(value), isphonedigit);
}

/***************************************************************************/
/* Some functions to look up information about operation codes             */
/***************************************************************************/

static int operation_find(int operation);
static Octstr *operation_name(int operation);
static const int operation_can_send(int operation);
static const int operation_can_receive(int operation);

const static struct {
	unsigned char *name;
	int code;
	int can_send;
	int can_receive;
} operations[] = {
	{ "Login", LOGIN, 1, 0 },
	{ "Logout", LOGOUT, 1, 0 },
	{ "Submit message", SUBMIT_MESSAGE, 1, 0 },
	{ "Enquire message status", ENQUIRE_MESSAGE_STATUS, 1, 0 },
	{ "Delivery request", DELIVERY_REQUEST, 1, 0 },
	{ "Cancel message", CANCEL_MESSAGE, 1, 0 },
	{ "Set parameter", SET_REQ, 1, 0 },
	{ "Get parameter", GET_REQ, 1, 0 },

	{ "Deliver message", DELIVER_MESSAGE, 0, 1 },
	{ "Deliver status report", DELIVER_STATUS_REPORT, 0, 1 },

	{ "Alive", ALIVE, 1, 1 },

	{ "NACK", NACK, 1, 1 },
	{ "General error response", GENERAL_ERROR_RESPONSE, 0, 1 },

	{ NULL, 0, 0, 0 }
};

static int operation_find(int operation) {
	int i;

	for (i = 0; operations[i].name != NULL; i++) {
		if (operations[i].code == operation)
			return i;
	}

	return -1;
}

/* Return a human-readable representation of this operation code */
static Octstr *operation_name(int operation) {
	int i;

	i = operation_find(operation);
	if (i >= 0)
		return octstr_create(operations[i].name);

	if (operation >= RESPONSE) {
		i = operation_find(operation - RESPONSE);
		if (i >= 0) {
			Octstr *name = octstr_create(operations[i].name);
			octstr_append_cstr(name, " response");
			return name;
		}
	}

	/* Put the operation number here when we have octstr_format */
	return octstr_create("(unknown)");
}

/* Return true if a CIMD2 client may send this operation */
static const int operation_can_send(int operation) {
	int i = operation_find(operation);

	if (i >= 0)
		return operations[i].can_send;

	/* If we can receive the request, then we can send the response. */
	if (operation >= RESPONSE)
		return operation_can_receive(operation - RESPONSE);

	return 0;
}


/* Return true if a CIMD2 server may send this operation */
static const int operation_can_receive(int operation) {
	int i = operation_find(operation);

	if (i >= 0)
		return operations[i].can_receive;

	/* If we can send the request, then we can receive the response. */
	if (operation >= RESPONSE)
		return operation_can_send(operation - RESPONSE);

	return 0;
}

/***************************************************************************/
/* Packet encoding/decoding functions.  They handle packets at the octet   */
/* level, and know nothing of the network.                                 */
/***************************************************************************/

struct packet {
	/* operation and seq are -1 if their value could not be parsed */
	int operation;
	int seq;  /* Sequence number */	
	Octstr *data;  /* Encoded packet */
	/* CIMD 2 packet structure is so simple that packet information is
	 * stored as a valid encoded packet, and decoded as necessary. 
	 * Exceptions: operation code and sequence number are also stored
	 * as ints for speed, and the checksum is not added until the packet
	 * is about to be sent.  Since checksums are optional, the packet
	 * is still valid without a checksum.
	 *
	 * The sequence number is kept at 0 until it's time to actually
	 * send the packet, so that the send functions have control over
	 * the sequence numbers.
	 */
};

/* These are the separators defined by the CIMD 2 spec */
#define STX 2   /* Start of packet */
#define ETX 3   /* End of packet */
#define TAB 9   /* End of parameter */

/* The same separators, in string form */
#define STX_str "\02"
#define ETX_str "\03"
#define TAB_str "\011"

/* A reminder that packets are created without a valid sequence number */
#define BOGUS_SEQUENCE 0

/* Look for the STX OO:SSS TAB header defined by CIMD 2, where OO is the
 * operation code in two decimals and SSS is the sequence number in three
 * decimals.  Leave the results in the proper fields of the packet.
 * Try to make sense of headers that don't fit this pattern; validating
 * the packet format is not our job. */
static void packet_parse_header(struct packet *packet) {
	int pos;
	long number;
	
	/* Set default values, in case we can't parse the fields */
	packet->operation = -1;
	packet->seq = -1;

	pos = octstr_parse_long(&number, packet->data, 1, 10);
	if (pos < 0) 
		return;
	packet->operation = number;

	if (octstr_get_char(packet->data, pos++) != ':')
		return;

	pos = octstr_parse_long(&number, packet->data, pos, 10);
	if (pos < 0)
		return;
	packet->seq = number;
}


/* Accept an Octstr containing one packet, build a struct packet around
 * it, and return that struct.  The Octstr is stored in the struct.
 * No error checking is done here yet. */
static struct packet *packet_parse(Octstr *packet_data) {
	struct packet *packet;

	packet = gw_malloc(sizeof(*packet));
	packet->data = packet_data;

	/* Fill in packet->operation and packet->seq */
	packet_parse_header(packet);

	return packet;
}

/* Deallocate this packet */
static void packet_destroy(struct packet *packet) {
	if (packet != NULL) {
		octstr_destroy(packet->data);
		gw_free(packet);
	}
}

/* Find the first packet in "in", delete it from "in", and return it as
 * a struct.  Return NULL if "in" contains no packet.  Always delete
 * leading non-packet data from "in".  (The CIMD 2 spec says we should
 * ignore any data between the packet markers). */
static struct packet *packet_extract(Octstr *in)
{
	int stx, etx;
	Octstr *packet;

	/* Find STX, and delete everything up to it */
	stx = octstr_search_char(in, STX);
	if (stx < 0) {
		octstr_delete(in, 0, octstr_len(in));
		return NULL;
	} else {
		octstr_delete(in, 0, stx);
	}

	/* STX is now in position 0.  Find ETX. */
	etx = octstr_search_char_from(in, ETX, 1);
	if (etx < 0) {
		return NULL;
	}

	/* What shall we do with STX data... STX data... ETX?
	 * Either skip to the second STX, or assume an ETX marker before
	 * the STX.  Doing the latter has a chance of succeeding, and
	 * will at least allow good logging of the error. */
	stx = octstr_search_char_from(in, STX, 1);
	if (stx >= 0 && stx < etx) {
		warning(0, "CIMD2: packet without end marker");
		packet = octstr_copy(in, 0, stx);
		octstr_delete(in, 0, stx);
		octstr_append_cstr(packet, ETX_str);
	} else {
		/* Normal case. Copy packet, and cut it from the source. */
		packet = octstr_copy(in, 0, etx + 1);
		octstr_delete(in, 0, etx + 1);
	}

	return packet_parse(packet);
}

/* The get_parm functions always return the first parameter with the
 * correct id.  There is only one case where the spec allows multiple
 * parameters with the same id, and that is when an SMS has multiple
 * destination addresses.  We only support one destination address anyway. */

/* Look for the first parameter with id 'parmno' and return its value.
 * Return NULL if the parameter was not found. */
static Octstr *packet_get_parm(struct packet *packet, int parmno) {
	long pos, next;
	long valuepos;
	long number;

	gw_assert(packet != NULL);
	pos = octstr_search_char(packet->data, TAB);
	if (pos < 0)
		return NULL; /* Bad packet, nothing we can do */

	/* Parameters have a tab on each end.  If we don't find the
	 * closing tab, we're at the checksum, so we stop. */
	for ( ;
	     (next = octstr_search_char_from(packet->data, TAB, pos+1)) >= 0;
	     pos = next) {
		if (octstr_parse_long(&number, packet->data, pos+1, 10) < 0)
			continue;
		if (number != parmno)
			continue;
		valuepos = octstr_search_char_from(packet->data, ':', pos+1);
		if (valuepos < 0)
			continue; /* badly formatted parm */
		valuepos++; /* skip the ':' */

		/* Found the right parameter */
		return octstr_copy(packet->data, valuepos, next - valuepos);
	}

	return NULL;
}
	

/* Look for an Integer parameter with id 'parmno' in the packet and
 * return its value.  Return INT_MIN if the parameter was not found.
 * (Unfortunately, -1 is a valid parameter value for at least one
 * parameter.) */
static long packet_get_int_parm(struct packet *packet, int parmno) {
	Octstr *valuestr = NULL;
	long value;

	/* Our code should never even try a bad parameter access. */
	gw_assert(parm_type(parmno) == P_INT);
	
	valuestr = packet_get_parm(packet, parmno);
	if (!valuestr)
		goto error;

	if (octstr_parse_long(&value, valuestr, 0, 10) < 0)
		goto error;

	octstr_destroy(valuestr);
	return value;

error:
	octstr_destroy(valuestr);
	return INT_MIN;
}

/* Look for a String parameter with id 'parmno' in the packet and
 * return its value.  Return NULL if the parameter was not found.
 * No translations are done on the value. */
static Octstr *packet_get_string_parm(struct packet *packet, int parmno) {
	/* Our code should never even try a bad parameter access. */
	gw_assert(parm_type(parmno) == P_STRING);

	return packet_get_parm(packet, parmno);
}

/* Look for an Address parameter with id 'parmno' in the packet and
 * return its value.  Return NULL if the parameter was not found.
 * No translations are done on the value. */
static Octstr *packet_get_address_parm(struct packet *packet, int parmno) {
	/* Our code should never even try a bad parameter access. */
	gw_assert(parm_type(parmno) == P_ADDRESS);

	return packet_get_parm(packet, parmno);
}

/* Look for an SMS parameter with id 'parmno' in the packet and return its
 * value.  Return NULL if the parameter was not found.  No translations
 * are done on the value, so it will be in the ISO-Latin-1 character set
 * with CIMD2-specific escapes. */
static Octstr *packet_get_sms_parm(struct packet *packet, int parmno) {
	/* Our code should never even try a bad parameter access. */
	gw_assert(parm_type(parmno) == P_SMS);

	return packet_get_parm(packet, parmno);
}

/* There is no packet_get_time_parm because the CIMD 2 timestamp
 * format is useless.  It's in the local time of the MC, with
 * a 2-digit year and no DST information.  We can do without.
 */

/* Look for a Hex parameter with id 'parmno' in the packet and return
 * its value.  Return NULL if the parameter was not found.  The value
 * is de-hexed. */
static Octstr *packet_get_hex_parm(struct packet *packet, int parmno) {
	Octstr *value = NULL;

	/* Our code should never even try a bad parameter access. */
	gw_assert(parm_type(parmno) == P_HEX);

	value = packet_get_parm(packet, parmno);
	if (!value)
		goto error;

	if (octstr_hex_to_binary(value) < 0)
		goto error;

	return value;
		
error:
	octstr_destroy(value);
	return NULL;
}


/* Check if the header is according to CIMD 2 spec, generating log
 * entries as necessary.  Return -1 if anything was wrong, otherwise 0. */
static int packet_check_header(struct packet *packet) {
	Octstr *data;

	gw_assert(packet != NULL);
	data = packet->data;

	/* The header must have a two-digit operation code, a colon,
	 * and a three-digit sequence number, followed by a tab.
	 * (CIMD2, 3.1) */
	if (octstr_len(data) < 8 ||
	    !octstr_check_range(data, 1, 2, isdigit) ||
	    octstr_get_char(data, 3) != ':' ||
	    !octstr_check_range(data, 4, 3, isdigit) ||
	    octstr_get_char(data, 7) != TAB) {
		warning(0, "CIMD2 packet header in wrong format");
		return -1;
	}

	return 0;
}

static int packet_check_parameter(struct packet *packet, long pos, long len) {
	Octstr *data;
	long parm;
	long dpos, dlen;
	int negative;
	long value;
	int i;
	int errors = 0;

	gw_assert(packet != NULL);
	data = packet->data;

	/* The parameter header should be TAB, followed by a three-digit
	 * parameter number, a colon, and the data.  We already know about
	 * the tab. */

	if (len < 5 ||
	    !octstr_check_range(data, pos + 1, 3, isdigit) ||
	    octstr_get_char(data, pos + 4) != ':') {
		warning(0, "CIMD2 parameter at offset %ld in wrong format",
			pos);
		errors++;
	}

	/* If we can't parse a parameter number, there's nothing more
	 * that we can check. */
	dpos = octstr_parse_long(&parm, data, pos + 1, 10);
	if (dpos < 0) 
		return -1;
	if (octstr_get_char(data, dpos) == ':')
		dpos++;
	dlen = len - (dpos - pos);
	/* dlen can not go negative because octstr_parse_long must have
	 * been stopped by the TAB at the end of the parameter data. */
	gw_assert(dlen >= 0);

	i = parm_index(parm);

	if (i < 0) {
		warning(0, "CIMD2 packet contains unknown parameter %ld", parm);
		return -1;
	}

	if (dlen > parameters[i].maxlen) {
		warning(0, "CIMD2 packet has '%s' parameter with length %ld, spec says max %d",
			parameters[i].name, len, parameters[i].maxlen);
		errors++;
	}

	switch(parameters[i].type) {
	case P_INT:
		/* Allow a leading - */
		negative = (octstr_get_char(data, dpos) == '-');
		if (!octstr_check_range(data, dpos + negative,
				        dlen - negative, isdigit)) {
			warning(0, "CIMD2 packet has '%s' parameter with non-integer contents", parameters[i].name);
			errors++;
		}
		if (octstr_parse_long(&value, data, dpos, 10) >= 0 &&
		    (value < parameters[i].minval ||
		     value > parameters[i].maxval)) {
			warning(0, "CIMD2 packet has '%s' parameter out of range (value %ld, min %d, max %d)",
				parameters[i].name, value,
				parameters[i].minval, parameters[i].maxval);
			errors++;
		}
		break;
	case P_TIME:
		if (!octstr_check_range(data, dpos, dlen, isdigit)) {
			warning(0, "CIMD2 packet has '%s' parameter with non-digit contents", parameters[i].name);
			errors++;
		}
		break;
	case P_ADDRESS:
		if (!octstr_check_range(data, dpos, dlen, isphonedigit)) {
			warning(0, "CIMD2 packet has '%s' parameter with non phone number contents", parameters[i].name);
			errors++;
		}
		break;
	case P_HEX:
		if (!octstr_check_range(data, dpos, dlen, isxdigit)) {
			warning(0, "CIMD2 packet has '%s' parameter with non-hex contents", parameters[i].name);
			errors++;
		}
		if (dlen % 2 != 0) {
			warning(0, "CIMD2 packet has odd-length '%s' parameter", parameters[i].name);
			errors++;
		}
		break;
	case P_SMS:
	case P_STRING: /* nothing to check */
		break;
	}

	if (errors > 0)
		return -1;
	return 0;
}


/* Check the packet against the CIMD 2 spec, generating log entries as
 * necessary. Return -1 if anything was wrong, otherwise 0. */
/* TODO: Check if parameters found actually belong in the packet type */
static int packet_check(struct packet *packet) {
	int errors = 0;
	long pos, len, next;
	Octstr *data;

	gw_assert(packet != NULL);
	data = packet->data;

	if (octstr_search_char(data, 0) >= 0) {
		/* CIMD2 spec does not allow NUL bytes in a packet */
		warning(0, "CIMD2 packet contains NULs");
		errors++;
	}

	/* Assume the packet starts with STX and ends with ETX, 
	 * because we parsed it that way in the first place. */

	errors += (packet_check_header(packet) < 0);

	/* Parameters are separated by tabs.  After the last parameter
	 * there is a tab, an optional two-digit checksum, and the ETX.
	 * Check each parameter in turn, by skipping from tab to tab.
	 */
	len = octstr_len(data);
	/* Start at the first tab, wherever it is, so that we can still
	 * check parameters if the header was weird. */
	pos = octstr_search_char(data, TAB);
	for ( ; pos >= 0; pos = next) {
		next = octstr_search_char_from(data, TAB, pos + 1);
		if (next >= 0) {
			errors += (packet_check_parameter(packet, pos, next - pos) < 0);
		} else {
			/* Check if the checksum has the right format.  Don't
			 * check the sum itself here, that will be done in a
			 * separate call later. */
			/* There are two valid formats: TAB ETX (no checksum)
			 * and TAB digit digit ETX.  We already know the TAB
			 * and the ETX are there. */
			if (!(octstr_len(data) - pos == 2 ||
			      (octstr_len(data) - pos == 4 &&
			       octstr_check_range(data, pos+1, 2, isxdigit)))) {
				warning(0, "CIMD2 packet checksum in wrong format");
				errors++;
			}
		}
	}

	
	if (errors > 0) {
		octstr_dump(packet->data, 0);
		return -1;
	}

	return 0;
}

static void packet_check_can_receive(struct packet *packet) {
	gw_assert(packet != NULL);

	if (!operation_can_receive(packet->operation)) {
		Octstr *name = operation_name(packet->operation);
		warning(0, "CIMD2 SMSC sent us %s request",
			octstr_get_cstr(name));
		octstr_destroy(name);
	}
}

/* Table of known error codes */
static struct {
	int code;
	unsigned char *text;
} cimd2_errors[] = {
	{ 0, "No error" },
	{ 1, "Unexpected operation" },
	{ 2, "Syntax error" },
	{ 3, "Unsupported parameter error" },
	{ 4, "Connection to message center lost" },
	{ 5, "No response from message center" },
	{ 6, "General system error" },
	{ 7, "Cannot find information" },
	{ 8, "Parameter formatting error" },
	{ 9, "Requested operation failed" },
	/* LOGIN error codes */
	{ 100, "Invalid login" },
	{ 101, "Incorrect access type" },
	{ 102, "Too many users with this login id" },
	{ 103, "Login refused by message center" },
	/* SUBMIT MESSAGE error codes */
	{ 300, "Incorrect destination address" },
	{ 301, "Incorrect number of destination addresses" },
	{ 302, "Syntax error in user data parameter" },
	{ 303, "Incorrect bin/head/normal user data parameter combination" },
	{ 304, "Incorrect data coding scheme parameter usage" },
	{ 305, "Incorrect validity period parameters usage" },
	{ 306, "Incorrect originator address usage" },
	{ 307, "Incorrect pid paramterer usage" },
	{ 308, "Incorrect first delivery parameter usage" },
	{ 309, "Incorrect reply path usage" },
	{ 310, "Incorrect status report request parameter usage" },
	{ 311, "Incorrect cancel enabled parameter usage" },
	{ 312, "Incorrect priority parameter usage" },
	{ 313, "Incorrect tariff class parameter usage" },
	{ 314, "Incorrect service description parameter usage" },
	{ 315, "Incorrect transport type parameter usage" },
	{ 316, "Incorrect message type parameter usage" },
	{ 318, "Incorrect mms parameter usage" },
	{ 319, "Incorrect operation timer parameter usage" },
	/* ENQUIRE MESSAGE STATUS error codes */
	{ 400, "Incorrect address parameter usage" },
	{ 401, "Incorrect scts parameter usage" },
	/* DELIVERY REQUEST error codes */
	{ 500, "Incorrect scts parameter usage" },
	{ 501, "Incorrect mode parameter usage" },
	{ 502, "Incorrect parameter combination" },
	/* CANCEL MESSAGE error codes */
	{ 600, "Incorrect scts parameter usage" },
	{ 601, "Incorrect address parameter usage" },
	{ 602, "Incorrect mode parameter usage" },
	{ 603, "Incorrect parameter combination" },
	/* SET error codes */
	{ 800, "Changing password failed" },
	{ 801, "Changing password not allowed" },
	/* GET error codes */
	{ 900, "Unsupported item requested" },
	{ -1, NULL }
};

static int packet_display_error(struct packet *packet) {
	int code;
	Octstr *text = NULL;
	Octstr *opname = NULL;
	
	code = packet_get_int_parm(packet, P_ERROR_CODE);
	text = packet_get_string_parm(packet, P_ERROR_TEXT);

	if (code <= 0) {
		octstr_destroy(text);
		return 0;
	}

	if (text == NULL) {
		/* No error text.  Try to find it in the table. */
		int i;
		for (i = 0; cimd2_errors[i].text != NULL; i++) {
			if (cimd2_errors[i].code == code) {
				text = octstr_create(cimd2_errors[i].text);
				break;
			}
		}
	}

	if (text == NULL) {
		/* Still no error text.  Make one up. */
		text = octstr_create("Unknown error");
	}

	opname = operation_name(packet->operation);
	error(0, "CIMD2 %s contained error message:",
		octstr_get_cstr(opname));
	error(0, "code %03d: %s", code, octstr_get_cstr(text));
	octstr_destroy(opname);
	octstr_destroy(text);
	return code;
}

/* Table of special combinations, for convert_gsm_to_latin1. */
/* Each cimd1, cimd2 pair is mapped to a character in the GSM default
 * character set. */
const static struct {
	unsigned char cimd1, cimd2;
	unsigned char gsm;
} cimd_combinations[] = {
	{ 'O', 'a', 0 },    /* @ */
	{ 'L', '-', 1 },    /* Pounds sterling */
	{ 'Y', '-', 3 },    /* Yen */
	{ 'e', '`', 4 },    /* egrave */
	{ 'e', '\'', 5 },   /* eacute */
	{ 'u', '`', 6 },    /* ugrave */
	{ 'i', '`', 7 },    /* igrave */
	{ 'o', '`', 8 },    /* ograve */
	{ 'C', ',', 9 },    /* C cedilla */
	{ 'O', '/', 11 },   /* Oslash */
	{ 'o', '/', 12 },   /* oslash */
	{ 'A', '*', 14 },   /* Aring */
	{ 'a', '*', 15 },   /* aring */
	{ 'g', 'd', 16 },   /* greek delta */
	{ '-', '-', 17 },   /* underscore */
	{ 'g', 'f', 18 },   /* greek phi */
	{ 'g', 'g', 19 },   /* greek gamma */
	{ 'g', 'l', 20 },   /* greek lambda */
	{ 'g', 'o', 21 },   /* greek omega */
	{ 'g', 'p', 22 },   /* greek pi */
	{ 'g', 'i', 23 },   /* greek psi */
	{ 'g', 's', 24 },   /* greek sigma */
	{ 'g', 't', 25 },   /* greek theta */
	{ 'g', 'x', 26 },   /* greek xi */
	{ 'X', 'X', 27 },   /* escape */
	{ 'A', 'E', 28 },   /* AE ligature */
	{ 'a', 'e', 29 },   /* ae ligature */
	{ 's', 's', 30 },   /* german double s */
	{ 'E', '\'', 31 },  /* Eacute */
	{ 'q', 'q', '"' },
	{ 'o', 'x', 36 },   /* international currency symbol */
	{ '!', '!', 64 },   /* inverted ! */
	{ 'A', '"', 91 },   /* Adieresis */
	{ 'O', '"', 92 },   /* Odieresis */
	{ 'N', '~', 93 },   /* N tilde */
	{ 'U', '"', 94 },   /* Udieresis */
	{ 's', 'o', 95 },   /* section mark */
	{ '?', '?', 96 },   /* inverted ? */
	{ 'a', '"', 123 },  /* adieresis */
	{ 'o', '"', 124 },  /* odieresis */
	{ 'n', '~', 125 },  /* n tilde */
	{ 'u', '"', 126 },  /* udieresis */
	{ 'a', '`', 127 },  /* agrave */
	{ 0, 0, 0 }
};


/* Convert text in the CIMD2 User Data format to the GSM default
 * character set.
 * CIMD2 allows 8-bit characters in this format; they map directly
 * to the corresponding ISO-8859-1 characters.  Since we are heading
 * toward that character set in the end, we don't bother converting
 * those to GSM. */
static void convert_cimd2_to_gsm(Octstr *text) {
	long pos, len;
	int cimd1, cimd2;
	int c;
	int i;

	/* CIMD2 uses four single-character mappings that do not map
	 * to themselves:
	 * '@' from 64 to 0, '$' from 36 to 2, ']' from 93 to 14 (A-ring),
	 * and '}' from 125 to 15 (a-ring).
	 * Other than those, we only have to worry about the escape
	 * sequences introduced by _ (underscore).
	 */

	len = octstr_len(text);
	for (pos = 0; pos < len; pos++) {
		c = octstr_get_char(text, pos);
		if (c == '@')
			octstr_set_char(text, pos, 0);
		else if (c == '$')
			octstr_set_char(text, pos, 2);
		else if (c == ']')
			octstr_set_char(text, pos, 14);
		else if (c == '}')
			octstr_set_char(text, pos, 15);
		else if (c == '_' && pos + 2 < len) {
			cimd1 = octstr_get_char(text, pos + 1);
			cimd2 = octstr_get_char(text, pos + 2);
			for (i = 0; cimd_combinations[i].cimd1 != 0; i++) {
				if (cimd_combinations[i].cimd1 == cimd1 &&
				    cimd_combinations[i].cimd2 == cimd2)
					break;
			}
			if (cimd_combinations[i].cimd1 == 0)
				warning(0, "CIMD2: Encountered unknown "
					   "escape code _%c%c, ignoring.",
					   cimd1, cimd2);
			else {
				octstr_delete(text, pos, 2);
				octstr_set_char(text, pos,
					cimd_combinations[i].gsm);
				len = octstr_len(text);
			}
		}
	}
}


/* Convert text in the GSM default character set to the CIMD2 User Data
 * format, which is a representation of the GSM default character set
 * in the lower 7 bits of ISO-8859-1.  (8-bit characters are also
 * allowed, but it's just as easy not to use them.) */
static void convert_gsm_to_cimd2(Octstr *text) {
	long pos, len;

	len = octstr_len(text);
	for (pos = 0; pos < len; pos++) {
		int c, i;

		c = octstr_get_char(text, pos);
		/* If c is not in the GSM alphabet at this point,
		 * the caller did something badly wrong. */
		gw_assert(c >= 0);
		gw_assert(c < 128);

		for (i = 0; cimd_combinations[i].cimd1 != 0; i++) {
			if (cimd_combinations[i].gsm == c)
				break;
		}

		if (cimd_combinations[i].gsm == c) {
			/* Escape sequence */
			octstr_insert_data(text, pos, "_ ", 2);
			pos += 2;
			len += 2;
			octstr_set_char(text, pos - 1,
				cimd_combinations[i].cimd1);
			octstr_set_char(text, pos,
				cimd_combinations[i].cimd2);
		} else if (c == 2) {
			/* The dollar sign is the only GSM character that
 			 * does not have a CIMD escape sequence and does not
			 * map to itself. */
			octstr_set_char(text, pos, '$');
		}
	}
}


/***************************************************************************/
/* Packet encoding functions.  They do not allow the creation of invalid   */
/* CIMD 2 packets.                                                         */
/***************************************************************************/

/* Build a new packet struct with this operation code and sequence number. */
static struct packet *packet_create(int operation, int seq) {
	struct packet *packet;
	unsigned char minpacket[sizeof("sOO:SSSte")];

	packet = gw_malloc(sizeof(*packet));
	packet->operation = operation;
	packet->seq = seq;
	sprintf(minpacket, STX_str "%02d:%03d" TAB_str ETX_str, operation, seq);
	packet->data = octstr_create(minpacket);

	return packet;
}

/* Add a parameter to the end of packet */
static void packet_add_parm(struct packet *packet, int parmtype,
			    int parmno, Octstr *value) {
	unsigned char parmh[sizeof("tPPP:")];
	long position;
	long len;
	int copied = 0;

	len = octstr_len(value);

	gw_assert(packet != NULL);
	gw_assert(parm_type(parmno) == parmtype);
	
	if (len > parm_maxlen(parmno)) {
		warning(0, "CIMD2: %s parameter too long, truncating from "
			"%ld to %ld characters", parm_name(parmno),
			len, (long) parm_maxlen(parmno));
		value = octstr_copy(value, 0, parm_maxlen(parmno));
		copied = 1;
	}

	/* There's a TAB and ETX at the end; insert it before those.
	 * The new parameter will come with a new starting TAB. */
	position = octstr_len(packet->data) - 2;

	sprintf(parmh, TAB_str "%03d:", parmno);
	octstr_insert_data(packet->data, position, parmh, strlen(parmh));
	octstr_insert(packet->data, value, position + strlen(parmh));
	if (copied)
		octstr_destroy(value);
}

/* Add a String parameter to the packet */
static void packet_add_string_parm(struct packet *packet, int parmno, Octstr *value) {
	packet_add_parm(packet, P_STRING, parmno, value);
}

/* Add an Address parameter to the packet */
static void packet_add_address_parm(struct packet *packet, int parmno, Octstr *value) {
	gw_assert(octstr_check_range(value, 0,
		   		     octstr_len(value), isphonedigit));
	packet_add_parm(packet, P_ADDRESS, parmno, value);
}

/* Add an SMS parameter to the packet.  The caller is expected to have done
 * the translation to the GSM character set already.  */
static void packet_add_sms_parm(struct packet *packet, int parmno, Octstr *value) {
	packet_add_parm(packet, P_SMS, parmno, value);
}

/* There is no function for adding a Time parameter to the packet, because
 * the format makes Time parameters useless for us.  If you find that you
 * need to use them, then also add code for querying the SMS center timestamp
 * and using that for synchronization.  And beware of DST changes. */

/* Add a Hexadecimal parameter to the packet */
static void packet_add_hex_parm(struct packet *packet, int parmno, Octstr *value) {
	value = octstr_duplicate(value);
	octstr_binary_to_hex(value, 1);  /* 1 for uppercase hex, i.e. A .. F */
	packet_add_parm(packet, P_HEX, parmno, value);
	octstr_destroy(value);
}

/* Add an Integer parameter to the packet */
static void packet_add_int_parm(struct packet *packet, int parmno, long value) {
	unsigned char buf[128];
	Octstr *valuestr;

	gw_assert(parm_in_range(parmno, value));

	sprintf(buf, "%ld", value);
	valuestr = octstr_create(buf);
	packet_add_parm(packet, P_INT, parmno, valuestr);
	octstr_destroy(valuestr);
}

static void packet_set_checksum(struct packet *packet) {
	Octstr *data;
	int checksum;
	long pos, len;
	unsigned char buf[16];

	gw_assert(packet != NULL);

	data = packet->data;
	if (octstr_get_char(data, octstr_len(data) - 2) != TAB) {
		/* Packet already has checksum; kill it. */
		octstr_delete(data, octstr_len(data) - 3, 2);
	}

	gw_assert(octstr_get_char(data, octstr_len(data) - 2) == TAB);

	/* Sum all the way up to the last TAB */
	checksum = 0;
	for (pos = 0, len = octstr_len(data); pos < len - 1; pos++) {
		checksum += octstr_get_char(data, pos);
		checksum &= 0xff;
	}

	sprintf(buf, "%02X", checksum);
	octstr_insert_data(data, len - 1, buf, 2);
}

static void packet_set_sequence(struct packet *packet, int seq) {
	unsigned char buf[16];

	gw_assert(packet != NULL);
	gw_assert(seq >= 0);
	gw_assert(seq < 256);

	sprintf(buf, "%03d", seq);

	/* Start at 4 to skip the <STX> ZZ: part of the header. */
	octstr_set_char(packet->data, 4, buf[0]);
	octstr_set_char(packet->data, 5, buf[1]);
	octstr_set_char(packet->data, 6, buf[2]);
	packet->seq = seq;
}

static struct packet *packet_encode_message(Msg *msg) {
	struct packet *packet;
	Octstr *text;
	int spaceleft;
	long truncated;

	gw_assert(msg != NULL);
	gw_assert(msg->type == smart_sms);

	if (!parm_valid_address(msg->smart_sms.receiver)) {
		warning(0, "cimd2_submit_msg: non-digits in "
			"destination phone number '%s', discarded",
			octstr_get_cstr(msg->smart_sms.receiver));
		return NULL;
	}

	if (!parm_valid_address(msg->smart_sms.sender)) {
		warning(0, "cimd2_submit_msg: non-digits in "
			"originating phone number '%s', discarded",
			octstr_get_cstr(msg->smart_sms.receiver));
		return NULL;
	}

	packet = packet_create(SUBMIT_MESSAGE, BOGUS_SEQUENCE);

	packet_add_address_parm(packet,
		P_DESTINATION_ADDRESS, msg->smart_sms.receiver);

	/* We used to also set the originating address here, but CIMD2
 	 * interprets such numbers as a sub-address to our connection
	 * number (so if the connection is "400" and we fill in "600"
	 * as the sender number, the user sees "400600".  Since most
	 * of the SMSC protocols ignore the sender field, we just ignore
	 * it here, too. */

	/* Explicitly ask not to get status reports.
	 * If we do not do this, the server's default might be to
	 * send status reports in some cases, and we don't do anything
	 * with those reports anyway. */
	packet_add_int_parm(packet, P_STATUS_REPORT_REQUEST, 0);

	/* CIMD2 specs are not entirely clear on this, but it looks like
	 * once UDH is used, even a plaintext body can be at most 140 octets.
	 * That's why we set it to 140 if either UDH or 8bit is true. 
         * Currently it does not matter, since they're always both true
	 * or both false. */
	if (msg->smart_sms.flag_udh || msg->smart_sms.flag_8bit) {
		spaceleft = 140;
	} else {
		spaceleft = 160;
	}
	truncated = 0;

	if (msg->smart_sms.flag_udh) {
		/* udhdata will be truncated and warned about if
		 * it does not fit. */
		packet_add_hex_parm(packet,
			P_USER_DATA_HEADER, msg->smart_sms.udhdata);
		spaceleft -= octstr_len(msg->smart_sms.udhdata);
		if (spaceleft < 0)
			spaceleft = 0;
	}

	text = octstr_duplicate(msg->smart_sms.msgdata);
	if (octstr_len(text) > 0 && spaceleft == 0) {
		warning(0, "CIMD2: message filled up with "
			"UDH, no room for message text");
	} else if (msg->smart_sms.flag_8bit) {
		if (octstr_len(text) > spaceleft) {
			truncated = octstr_len(text) - spaceleft;
			octstr_truncate(text, spaceleft);
		}
		packet_add_hex_parm(packet, P_USER_DATA_BINARY, text);
		/* 245 is 8-bit-data, message class "User 1 defined",
		 * whatever that means. */
		packet_add_int_parm(packet, P_DATA_CODING_SCHEME, 245);
	} else {
#if CIMD2_TRACE
		debug("bb.sms.cimd2", 0, "CIMD2 sending message.  Text:");
		octstr_dump(text, 0);
#endif
		/* Going from latin1 to GSM to CIMD2 may seem like a
		 * detour, but it's the only way to get all the escape
		 * codes right. */
		charset_latin1_to_gsm(text);
		truncated = charset_gsm_truncate(text, spaceleft);
		convert_gsm_to_cimd2(text);
#if CIMD2_TRACE
		debug("bb.sms.cimd2", 0, "After CIMD2 encoding:");
		octstr_dump(text, 0);
#endif
		packet_add_sms_parm(packet, P_USER_DATA, text);
	}

	if (truncated > 0) {
		warning(0, "CIMD2: truncating message text to fit "
			"in %d characters.", spaceleft);
	}

	octstr_destroy(text);
	return packet;
}

/***************************************************************************/
/* Protocol functions.  These implement various transactions.              */
/***************************************************************************/

/* Give this packet a proper sequence number for sending. */
static void packet_set_send_sequence(struct packet *packet, SMSCenter *smsc) {
	gw_assert(smsc != NULL);
	/* Send sequence numbers are always odd, receiving are always even */
	gw_assert(smsc->cimd2_send_seq % 2 == 1);

	packet_set_sequence(packet, smsc->cimd2_send_seq);
	smsc->cimd2_send_seq += 2;
	if (smsc->cimd2_send_seq > 256)
		smsc->cimd2_send_seq = 1;
}

static struct packet *cimd2_get_packet(SMSCenter *smsc) {
	struct packet *packet = NULL;

	gw_assert(smsc != NULL);

	/* If packet is already available, don't try to read anything */
	packet = packet_extract(smsc->cimd2_inbuffer);

	while (packet == NULL) {
		if (read_available(smsc->socket, RESPONSE_TIMEOUT) != 1) {
			warning(0, "CIMD2 SMSCenter is not responding");
			return NULL;
		}

		if (octstr_append_from_socket(smsc->cimd2_inbuffer,
				smsc->socket) <= 0) {
			error(0, "cimd2_get_packet: read failed");
			return NULL;
		}

		packet = packet_extract(smsc->cimd2_inbuffer);
	}

	packet_check(packet);
	packet_check_can_receive(packet);

	if (smsc->keepalive > 0)
		smsc->cimd2_next_ping = time(NULL) + 60 * smsc->keepalive;

	return packet;
}

/* Acknowledge a request.  The CIMD 2 spec only defines positive responses
 * to the server, because the server is perfect. */
static void cimd2_send_response(struct packet *request, SMSCenter *smsc) {
	struct packet *response;

	gw_assert(request != NULL);
	gw_assert(request->operation < RESPONSE);

	response = packet_create(request->operation + RESPONSE,
				request->seq);
	packet_set_checksum(response);

	/* Don't check errors here because if there is something
	 * wrong with the socket, the main loop will detect it. */
	octstr_write_to_socket(smsc->socket, response->data);

	packet_destroy(response);
}

static Msg *cimd2_accept_message(struct packet *request) {
	Msg *message = NULL;
	Octstr *destination = NULL;
	Octstr *origin = NULL;
	Octstr *UDH = NULL;
	Octstr *text = NULL;
	long DCS;
	int flag_8bit = 0;

	/* See GSM 03.38.  The bit patterns we can handle are:
	 *   000xyyxx  Uncompressed text, yy indicates alphabet.
         *                   yy = 00, default alphabet
	 *                   yy = 01, 8-bit data
	 *                   yy = 10, UCS2 (can't handle yet)
	 *                   yy = 11, reserved
	 *   1111xyxx  Data, y indicates alphabet.
	 *                   y = 0, default alphabet
	 *                   y = 1, 8-bit data
	 */
	DCS = packet_get_int_parm(request, P_DATA_CODING_SCHEME);
	if ((DCS & 0xe0) == 0 && (DCS & 0x0c) != 0x0c) {
		/* Pass UCS2 as 8-bit data for now. */
		if ((DCS & 0x0c) == 0x00)
			flag_8bit = 0;
		else 
			flag_8bit = 1;
	} else if ((DCS & 0xf0) == 0xf0) {
		if ((DCS & 0x04) == 0x00)
			flag_8bit = 0;
		else
			flag_8bit = 1;
	} else {
		info(0, "CIMD2: Got SMS with data coding %ld, "
			"can't handle, ignoring.", DCS);
		return NULL;
	}

	destination = packet_get_address_parm(request, P_DESTINATION_ADDRESS);
	origin = packet_get_address_parm(request, P_ORIGINATING_ADDRESS);
	UDH = packet_get_hex_parm(request, P_USER_DATA_HEADER);
	/* Text is either in User Data or User Data Binary field. */
	text = packet_get_sms_parm(request, P_USER_DATA);
	if (text != NULL) { 
#if CIMD2_TRACE
		debug("bb.sms.cimd2", 0, "CIMD2 received message.  Text:");
		octstr_dump(text, 0);
#endif
		convert_cimd2_to_gsm(text);
		charset_gsm_to_latin1(text);
#if CIMD2_TRACE
		debug("bb.sms.cimd", 0, "Text in latin1:");
		octstr_dump(text, 0);
#endif
	} else {
		text = packet_get_hex_parm(request, P_USER_DATA_BINARY);
#if CIMD2_TRACE
		debug("bb.sms.cimd2", 0, "CIMD2 received message.  Text:");
		octstr_dump(text, 0);
#endif
	}

	/* Code elsewhere in the gateway always expects the sender and
	 * receiver fields to be filled, so we discard messages that
 	 * lack them.  If they should not be discarded, then the code
	 * handling smart_sms messages should be reviewed.  -- RB */
	if (!destination || octstr_len(destination) == 0) {
		info(0, "CIMD2: Got SMS without receiver, discarding.");
		goto error;
	}
	if (!origin || octstr_len(origin) == 0) {
		info(0, "CIMD2: Got SMS without sender, discarding.");
		goto error;
	}

	if ((!text || octstr_len(text) == 0) &&
	    (!UDH || octstr_len(UDH) == 0)) {
		info(0, "CIMD2: Got empty SMS, ignoring.");
		goto error;
	}

	message = msg_create(smart_sms);
	message->smart_sms.sender = origin;
	message->smart_sms.receiver = destination;
	if (UDH) {
		message->smart_sms.flag_udh = 1;
		message->smart_sms.udhdata = UDH;
	}
	message->smart_sms.flag_8bit = flag_8bit;
	message->smart_sms.msgdata = text;
	return message;

error:
	msg_destroy(message);
	octstr_destroy(destination);
	octstr_destroy(origin);
	octstr_destroy(UDH);
	octstr_destroy(text);
	return NULL;
}

/* Deal with a request from the CIMD2 server, and acknowledge it. */
static void cimd2_handle_request(struct packet *request, SMSCenter *smsc) {
	Msg *message = NULL;

	/* TODO: Check if the sequence number of this request is what we
	 * expected. */

	if (request->operation == DELIVER_STATUS_REPORT) {
		info(0, "CIMD2: received status report we didn't ask for.\n");
	} else if (request->operation == DELIVER_MESSAGE) {
		message = cimd2_accept_message(request);
		if (message)
			list_append(smsc->cimd2_received, message);
	}

	cimd2_send_response(request, smsc);
}

/* Send a request and wait for the ack.  If the other side responds with
 * an error code, attempt to correct and retry. 
 * If other packets arrive while we wait for the ack, handle them.
 *
 * Return -1 if the SMSC refused the request.  Return -2 for other
 * errors, such as being unable to send the request at all.  If the
 * function returns -2, the caller would do well to try to reopen the
 * connection.
 *
 * The SMSCenter must be already open.
 *
 * TODO: This function has grown large and complex.  Break it up
 * into smaller pieces.
 */
static int cimd2_request(struct packet *request, SMSCenter *smsc) {
	int ret;
	struct packet *reply = NULL;
	int errorcode;
	int tries = 0;

	gw_assert(smsc != NULL);
	gw_assert(request != NULL);
	gw_assert(smsc->socket >= 0);
	gw_assert(operation_can_send(request->operation));

retransmit:
	packet_set_send_sequence(request, smsc);
	packet_set_checksum(request);

	ret = octstr_write_to_socket(smsc->socket, request->data);
	if (ret < 0)
		goto io_error;

next_reply:
	reply = cimd2_get_packet(smsc);
	if (!reply)
		goto io_error;

	errorcode = packet_display_error(reply);

	if (reply->operation == NACK) {
		warning(0, "CIMD2 received NACK");
		octstr_dump(reply->data, 0);
		/* Correct sequence number if server says it was wrong,
	 	 * but only if server's number is sane. */
		if (reply->seq != request->seq && (reply->seq % 1) == 1) {
			warning(0, "correcting sequence number "
				"from %ld to %ld.",
				(long) smsc->cimd2_send_seq, (long) reply->seq);
			smsc->cimd2_send_seq = reply->seq;
		}
		goto retry;
	}

	if (reply->operation == GENERAL_ERROR_RESPONSE) {
		error(0, "CIMD2 received general error response");
		goto io_error;
	}

	/* The server sent us a request.  Handle it, then wait for
	 * a new reply. */
	if (reply->operation < RESPONSE) {
		cimd2_handle_request(reply, smsc);
		packet_destroy(reply);
		goto next_reply;
	}

	if (reply->seq != request->seq) {
		/* We got a response to a different request number than
		 * what we send.  Strange. */
		warning(0, "CIMD2: response had unexpected sequence number; "
			   "ignoring.\n");
		goto next_reply;
	}

	if (reply->operation != request->operation + RESPONSE) {
		/* We got a response that didn't match our request */
		Octstr *request_name = operation_name(request->operation);
		Octstr *reply_name = operation_name(reply->operation);
		warning(0, "CIMD2: %s request got a %s",
			octstr_get_cstr(request_name),
			octstr_get_cstr(reply_name));
		octstr_destroy(request_name);
		octstr_destroy(reply_name);
		octstr_dump(reply->data, 0);
		goto retry;
	}

	if (errorcode > 0)
		goto error;

	/* The reply passed all the checks... looks like the SMSC accepted
	 * our request! */
	packet_destroy(reply);
	return 0;
	
io_error:
	packet_destroy(reply);
	return -2;

error:
	packet_destroy(reply);
	return -1;

retry:
	if (++tries < 3) {
		warning(0, "Retransmitting (take %d)", tries);
		goto retransmit;
	}
	warning(0, "Giving up.");
	goto io_error;
}

/* Close the SMSC socket without fanfare. */
static void cimd2_close_socket(SMSCenter *smsc) {
	gw_assert(smsc != NULL);

	if (smsc->socket < 0)
		return;

	if (close(smsc->socket) < 0)
		warning(errno, "error closing CIMD2 socket");
	smsc->socket = -1;
}

/* Open a socket to the SMSC, send a login packet, and wait for ack.
 * This may block.  Return 0 for success, or -1 for failure. */
/* Make sure the socket is closed before calling this function, otherwise
 * we will leak fd's. */
static int cimd2_login(SMSCenter *smsc) {
	int ret;
	struct packet *packet = NULL;

	gw_assert(smsc != NULL);

	if (smsc->socket >= 0) {
		warning(0, "cimd2_login: socket was already open; closing");
		cimd2_close_socket(smsc);
	}

	smsc->socket = tcpip_connect_to_server(octstr_get_cstr(smsc->cimd2_hostname), smsc->cimd2_port);
	if (smsc->socket == -1)
		goto error;

	packet = packet_create(LOGIN, BOGUS_SEQUENCE);
	packet_add_string_parm(packet, P_USER_IDENTITY, smsc->cimd2_username);
	packet_add_string_parm(packet, P_PASSWORD, smsc->cimd2_password);

	ret = cimd2_request(packet, smsc);
	if (ret < 0)
		goto error;

	packet_destroy(packet);

	/* Just in case the connection is configured to only deliver
	 * new messages, and we have to query for old ones.  This does
	 * no harm in other configurations. */
	packet = packet_create(DELIVERY_REQUEST, BOGUS_SEQUENCE);
	/* Mode 2 for "deliver all messages" */
	packet_add_int_parm(packet, P_DELIVERY_REQUEST_MODE, 2);
	/* We don't actually care if the request fails.  cimd2_request
	 * will log a warning if it fails, that is enough. */
	cimd2_request(packet, smsc);
	packet_destroy(packet);

	info(0, "%s logged in.", smsc_name(smsc));

	return 0;

error:
	error(0, "cimd2_login failed");
	cimd2_close_socket(smsc);
	packet_destroy(packet);
	return -1;
}

static void cimd2_logout(SMSCenter *smsc) {
	struct packet *packet = NULL;

	gw_assert(smsc != NULL);

	packet = packet_create(LOGOUT, BOGUS_SEQUENCE);
	/* TODO: Don't wait very long for a response in this case. */
	cimd2_request(packet, smsc);
	packet_destroy(packet);
}

static void cimd2_send_alive(SMSCenter *smsc) {
	struct packet *packet = NULL;

	gw_assert(smsc != NULL);

	packet = packet_create(ALIVE, BOGUS_SEQUENCE);
	cimd2_request(packet, smsc);
	packet_destroy(packet);
}

/***************************************************************************/
/* SMSC Interface, as defined in smsc_interface.def                        */
/***************************************************************************/

SMSCenter *cimd2_open(char *hostname, int port, char *username, char *password, int keepalive) {
	SMSCenter *smsc = NULL;
	int maxlen;

	smsc = smscenter_construct();
	gw_assert(smsc != NULL);

	smsc->type = SMSC_TYPE_CIMD2;
	smsc->keepalive = keepalive;
	smsc->cimd2_hostname = octstr_create(hostname);
	smsc->cimd2_port = port;
	smsc->cimd2_username = octstr_create(username);
	smsc->cimd2_password = octstr_create(password);
	sprintf(smsc->name, "CIMD2:%s:%d:%s", hostname, port, username);
	smsc->cimd2_received = list_create();
	smsc->cimd2_inbuffer = octstr_create_empty();
	smsc->cimd2_error = 0;
	if (keepalive > 0)
		smsc->cimd2_next_ping = time(NULL) + keepalive * 60;

	maxlen = parm_maxlen(P_USER_IDENTITY);
	if (octstr_len(smsc->cimd2_username) > maxlen) {
		octstr_truncate(smsc->cimd2_username, maxlen);
		warning(0, "Truncating CIMD2 username to %d chars", maxlen);
	}

	maxlen = parm_maxlen(P_PASSWORD);
	if (octstr_len(smsc->cimd2_password) > maxlen) {
		octstr_truncate(smsc->cimd2_password, maxlen);
		warning(0, "Truncating CIMD2 password to %d chars", maxlen);
	}

	if (cimd2_login(smsc) < 0)
		goto error;

	return smsc;

error:
	error(0, "cimd2_open failed");
	smscenter_destruct(smsc);
	return NULL;
}

int cimd2_reopen(SMSCenter *smsc) {
	gw_assert(smsc != NULL);

	warning(0, "Attempting to re-open CIMD2 connection");

	cimd2_close_socket(smsc);

	/* Restore message counters to their default values */
	smsc->cimd2_send_seq = 1;
	smsc->cimd2_receive_seq = 0;

	/* Clear leftover input */
	octstr_destroy(smsc->cimd2_inbuffer);
	smsc->cimd2_inbuffer = octstr_create_empty();

	return cimd2_login(smsc);
}

int cimd2_close(SMSCenter *smsc) { 
	int ret;
	int discarded;

	gw_assert(smsc != NULL);

	debug("bb.sms.cimd2", 0, "Closing CIMD2 SMSC");

	if (smsc->socket < 0) {
		warning(0, "cimd2_close: already closed.\n");
		return 0;
	}

	cimd2_logout(smsc);

	ret = close(smsc->socket);
	smsc->socket = -1;

	smsc->cimd2_send_seq = 0;
	smsc->cimd2_receive_seq = 1;
	octstr_destroy(smsc->cimd2_hostname);
	octstr_destroy(smsc->cimd2_username);
	octstr_destroy(smsc->cimd2_password);
	octstr_destroy(smsc->cimd2_inbuffer);

	discarded = 0;
	while (list_len(smsc->cimd2_received) > 0) {
		msg_destroy(list_extract_first(smsc->cimd2_received));
		discarded++;
	}
	list_destroy(smsc->cimd2_received);

	if (discarded > 0)
		warning(0, "CIMD2: discarded %d received messages",
			discarded);

	return ret;
}

int cimd2_submit_msg(SMSCenter *smsc, Msg *msg) {
	struct packet *packet;
	int ret = 0;
	int tries;

	gw_assert(smsc != NULL);

	packet = packet_encode_message(msg);
	if (!packet)
		return 0;  /* We can't signal protocol errors yet */

	for (tries = 0; tries < 3; tries++) {
		ret = cimd2_request(packet, smsc);
		if (ret == 0 || ret == -1)
			break;
		if (cimd2_reopen(smsc) < 0) {
			ret = -1;
			break;
		}
	}

	packet_destroy(packet);
	return ret;
}

/* The bearerbox really doesn't like it if pending_smsmessage returns
 * an error code.  We work around it until the bearerbox is rewritten.
 * Record the error here, and return it in cimd2_receive_msg.  Return
 * "message available" if there is an error so that cimd2_receive_msg
 * is called. */
int cimd2_pending_smsmessage(SMSCenter *smsc) {
	long ret;
	struct packet *packet;

	gw_assert(smsc != NULL);
	gw_assert(smsc->type == SMSC_TYPE_CIMD2);

	if (list_len(smsc->cimd2_received) > 0)
		return 1;

	ret = read_available(smsc->socket, 0);
	if (ret == 0) {
		if (smsc->keepalive > 0 && smsc->cimd2_next_ping < time(NULL))
			cimd2_send_alive(smsc);
		return 0;
	}

	if (ret < 0) {
		warning(errno, "cimd2_pending_smsmessage: "
				"read_available failed");
		smsc->cimd2_error = 1;
		return 1;
	}

	/* We have some data waiting... see if it is an sms delivery. */
	ret = octstr_append_from_socket(smsc->cimd2_inbuffer, smsc->socket);

	if (ret == 0) {
		warning(0, "cimd2_pending_smsmessage: "
			   "service center closed connection.");
		smsc->cimd2_error = 1;
		return 1;
	}
	if (ret < 0) {
		warning(errno, "cimd2_pending_smsmessage: read failed");
		smsc->cimd2_error = 1;
		return 1;
	}

	for (;;) {
		packet = packet_extract(smsc->cimd2_inbuffer);
		if (!packet)
			break;

		packet_check(packet);
		packet_check_can_receive(packet);

		if (packet->operation < RESPONSE)
			cimd2_handle_request(packet, smsc);
		else {
			error(0, "cimd2_pending_smsmessage: unexpected response packet");
			octstr_dump(packet->data, 0);
		}
			
		packet_destroy(packet);
	}
	
	if (list_len(smsc->cimd2_received) > 0)
		return 1;

	return 0;
}

int cimd2_receive_msg(SMSCenter *smsc, Msg **msg) {
	gw_assert(smsc != NULL);
	gw_assert(msg != NULL);

	if (smsc->cimd2_error) {
		smsc->cimd2_error = 0;
		return -1;
	}

	*msg = list_consume(smsc->cimd2_received);

	return 1;
}
