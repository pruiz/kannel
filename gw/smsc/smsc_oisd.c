/*
 * smsc.oisd.c - Driver for Sema Group SMS Center G8.1 (OIS 5.8)
 * using direct TCP/IP access interface
 *
 * Dariusz Markowicz <dm@tenbit.pl>
 *
 * This code is based on the CIMD2 module design.
 */

#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <unistd.h>

#include "gwlib/gwlib.h"
#include "smsc_p.h"
#include "sms.h"
#include "dlr.h"

/* Microseconds before giving up on a request */
#define RESPONSE_TIMEOUT (10 * 1000000)

enum {
    INVOKE = 0,
    RESULT = 1
};

/* Textual names for the operation codes defined by the OISD spec. */
/* If you make changes here, also change the operation table. */
enum {
    SUBMIT_SM = 0,
    STATUS_REPORT = 4,
    DELIVER_SM = 9,
    RETRIEVE_REQUEST = 11,

    /* Not a request; add to any request to make it a response */
    RESPONSE = 50
};

/* Helper function to check P_ADDRESS type */
static int isphonedigit(int c)
{
    return isdigit(c) || c == '+' || c == '-';
}

static const int parm_valid_address(Octstr *value)
{
    return octstr_check_range(value, 0, octstr_len(value), isphonedigit);
}

/***************************************************************************/
/* Some functions to look up information about operation codes             */
/***************************************************************************/

static int operation_find(int operation);
static Octstr *operation_name(int operation);
static const int operation_can_send(int operation);
static const int operation_can_receive(int operation);

static const struct
{
    unsigned char *name;
    int code;
    int can_send;
    int can_receive;
}
operations[] = {
    { "Submit SM", SUBMIT_SM, 1, 0 },
    { "Status Report", STATUS_REPORT, 0, 1 },
    { "Deliver SM", DELIVER_SM, 0, 1 },
    { "Retrieve Request", RETRIEVE_REQUEST, 1, 0 },

    { NULL, 0, 0, 0 }
};

static int operation_find(int operation)
{
    int i;

    for (i = 0; operations[i].name != NULL; i++) {
        if (operations[i].code == operation)
            return i;
    }

    return -1;
}

/* Return a human-readable representation of this operation code */
static Octstr *operation_name(int operation)
{
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

/* Return true if a OISD client may send this operation */
static const int operation_can_send(int operation)
{
    int i = operation_find(operation);

    if (i >= 0)
        return operations[i].can_send;

    /* If we can receive the request, then we can send the response. */
    if (operation >= RESPONSE)
        return operation_can_receive(operation - RESPONSE);

    return 0;
}


/* Return true if a OISD server may send this operation */
static const int operation_can_receive(int operation)
{
    int i = operation_find(operation);

    if (i >= 0)
        return operations[i].can_receive;

    /* If we can send the request, then we can receive the response. */
    if (operation >= RESPONSE)
        return operation_can_send(operation - RESPONSE);

    return 0;
}

/***************************************************************************
 * Packet encoding/decoding functions.  They handle packets at the octet   *
 * level, and know nothing of the network.                                 *
 ***************************************************************************/

struct packet
{
    unsigned long opref; /* operation reference */
    int operation;
    Octstr *data;        /* Encoded packet */
};

/* A reminder that packets are created without a valid sequence number */
#define BOGUS_SEQUENCE 0

static Msg *oisd_accept_delivery_report_message(struct packet *request,
                                                SMSCenter *smsc);
static Msg *oisd_submit_failed(SMSCenter *smsc, Msg* msg, Octstr* ts);

static void packet_parse_header(struct packet *packet)
{
    packet->opref = (octstr_get_char(packet->data, 3) << 24)
                  | (octstr_get_char(packet->data, 2) << 16)
                  | (octstr_get_char(packet->data, 1) << 8)
                  | (octstr_get_char(packet->data, 0));

    packet->operation = octstr_get_char(packet->data, 5);
    if (octstr_get_char(packet->data, 4) == 1)
        packet->operation += RESPONSE;
}


/*
 * Accept an Octstr containing one packet, build a struct packet around
 * it, and return that struct.  The Octstr is stored in the struct.
 * No error checking is done here yet.
 */
static struct packet *packet_parse(Octstr *packet_data)
{
    struct packet *packet;

    packet = gw_malloc(sizeof(*packet));
    packet->data = packet_data;

    /* Fill in packet->operation and packet->opref */
    packet_parse_header(packet);

    return packet;
}

/* Deallocate this packet */
static void packet_destroy(struct packet *packet)
{
    if (packet != NULL) {
        octstr_destroy(packet->data);
        gw_free(packet);
    }
}

/*
 * Find the first packet in "in", delete it from "in", and return it as
 * a struct.  Return NULL if "in" contains no packet.  Always delete
 * leading non-packet data from "in".
 */
static struct packet *packet_extract(SMSCenter *smsc)
{
    Octstr *packet, *in;
    int size, i;
    static char s[4][4] = {
        { 0x01, 0x0b, 0x00, 0x00 },
        { 0x01, 0x00, 0x00, 0x00 },
        { 0x00, 0x04, 0x00, 0x00 },
        { 0x00, 0x09, 0x00, 0x00 }
    }; /* msgtype, oper, 0, 0 */
    char known_bytes[4];

    in = smsc->oisd_inbuffer;

    if (octstr_len(in) < 10)
        return NULL;
    octstr_get_many_chars(known_bytes, in, 4, 4);
    /* Find s, and delete everything up to it. */
    /* If packet starts with one of s, it should be good packet */
    for (i = 0; i < 4; i++) {
        if (memcmp(s[i], known_bytes, 4) == 0)
            break;
    }

    if (i >= 4) {
        debug("bb.sms.oisd", 0, "oisd:packet_extract:wrong packet");
        octstr_dump(in, 0);

        oisd_reopen(smsc);
        return NULL;
    }

    /* Find end of packet */
    size = (octstr_get_char(in, 9) << 8) | octstr_get_char(in, 8);

    if (size + 10 > octstr_len(in))
        return NULL;

    packet = octstr_copy(in, 0, size + 10);
    octstr_delete(in, 0, size + 10);

    return packet_parse(packet);
}

static void packet_check_can_receive(struct packet *packet)
{
    gw_assert(packet != NULL);

    if (!operation_can_receive(packet->operation)) {
        Octstr *name = operation_name(packet->operation);
        warning(0, "OISD SMSC sent us %s request",
                octstr_get_cstr(name));
        octstr_destroy(name);
    }
}

static int oisd_expand_gsm7_to_bits(char *bits, Octstr *raw7)
{
    int i, j, k;
    int len;
    char ch;

    len = octstr_len(raw7) * 7; /* number of bits in the gsm 7-bit msg */

    for (j = i = 0; j < len; ++i) {
        ch = octstr_get_char(raw7, i);
        for (k = 0; k < 8; ++k) {
            bits[j++] = (char) (ch & 0x01);
            ch >>= 1;
        }
    }

    return j;
}

static char oisd_expand_gsm7_from_bits(const char *bits, int pos)
{
    int i;
    char ch;

    pos *= 7; /* septet position in bits */
    ch = '\0';
    for (i = 6; i >= 0; --i) {
        ch <<= 1;
        ch |= bits[pos + i];
    }

    return ch;
}

static Octstr *oisd_expand_gsm7(Octstr *raw7)
{
    Octstr *raw8;
    int i, len;
    char *bits;

    raw8 = octstr_create("");
    bits = gw_malloc(8 * octstr_len(raw7) + 1);

    oisd_expand_gsm7_to_bits(bits, raw7);
    len = octstr_len(raw7);

    for (i = 0; i < len; ++i) {
        octstr_append_char(raw8, oisd_expand_gsm7_from_bits(bits, i));
    }

    debug("bb.sms.oisd", 0, "oisd_expand_gsm7 raw8=%s ", octstr_get_cstr(raw8));

    gw_free(bits);

    return raw8;
}

static void oisd_shrink_gsm7(Octstr *str)
{
    Octstr *result;
    int len, i;
    int numbits, value;

    result = octstr_create("");
    len = octstr_len(str);
    value = 0;
    numbits = 0;
    for (i = 0; i < len; i++) {
        value += octstr_get_char(str, i) << numbits;
        numbits += 7;
        if (numbits >= 8) {
            octstr_append_char(result, value & 0xff);
            value >>= 8;
            numbits -= 8;
        }
    }
    if (numbits > 0)
        octstr_append_char(result, value);
    octstr_delete(str, 0, LONG_MAX);
    octstr_append(str, result);
    octstr_destroy(result);
}

/****************************************************************************
 * Packet encoding functions.  They do not allow the creation of invalid
 * OISD packets.
 ***************************************************************************/

/* Build a new packet struct with this operation code and sequence number. */
static struct packet *packet_create(int operation, unsigned long opref)
{
    struct packet *packet;
    unsigned char header[10];

    packet = gw_malloc(sizeof(*packet));
    packet->operation = operation;
    packet->opref = opref;

    /* Opref */
    header[0] = opref & 0xff;
    header[1] = (opref >> 8) & 0xff;
    header[2] = (opref >> 16) & 0xff;
    header[3] = (opref >> 24) & 0xff;

    /* Message Type & Operation */
    if (operation > RESPONSE) {
        header[4] = RESULT;
        header[5] = operation - RESPONSE;
    } else {
        header[4] = INVOKE;
        header[5] = operation;
    }

    /* Unused */
    header[6] = 0;
    header[7] = 0;

    /* Data Size */
    header[8] = 0;
    header[9] = 0;

    packet->data = octstr_create_from_data(header, 10);

    return packet;
}

static void packet_set_sequence(struct packet *packet, unsigned long opref)
{
    gw_assert(packet != NULL);

    octstr_set_char(packet->data, 0, opref & 0xff);
    octstr_set_char(packet->data, 1, (opref >> 8) & 0xff);
    octstr_set_char(packet->data, 2, (opref >> 16) & 0xff);
    octstr_set_char(packet->data, 3, (opref >> 24) & 0xff);
    packet->opref = opref;
}

static struct packet *packet_encode_message(Msg *msg, Octstr *sender_prefix)
{
    struct packet *packet;
    int dcs;
    int so = 0;
    int udhlen7, udhlen8;
    int msglen7, msglen8;
    Octstr *udhdata = NULL;
    Octstr *msgdata = NULL;

    gw_assert(msg != NULL);
    gw_assert(msg->type == sms);
    gw_assert(msg->sms.receiver != NULL);

    dcs = fields_to_dcs(msg, 0);
    if (msg->sms.sender == NULL)
        msg->sms.sender = octstr_create("");

    if (!parm_valid_address(msg->sms.receiver)) {
        warning(0, "oisd_submit_msg: non-digits in "
                "destination phone number '%s', discarded",
                octstr_get_cstr(msg->sms.receiver));
        return NULL;
    }

    if (!parm_valid_address(msg->sms.sender)) {
        warning(0, "oisd_submit_msg: non-digits in "
                "originating phone number '%s', discarded",
                octstr_get_cstr(msg->sms.sender));
        return NULL;
    }

    packet = packet_create(SUBMIT_SM, BOGUS_SEQUENCE);

    gw_assert(octstr_check_range(msg->sms.receiver, 0,
                                 octstr_len(msg->sms.receiver), isphonedigit));
    /* MSISDN length */
    octstr_append_char(packet->data,
                       (unsigned char) octstr_len(msg->sms.receiver));

    /* MSISDN */
    octstr_append(packet->data, msg->sms.receiver);

    /* Duplicate msg. behaviour */
    /* 1=reject duplicates, 2=allow duplicates */
    octstr_append_char(packet->data, (char) 2);

    /* SME ref. no. unused in this protocol implementation, but set */
    octstr_append_char(packet->data, (char) 0);
    octstr_append_char(packet->data, (char) 0);
    octstr_append_char(packet->data, (char) 0);
    octstr_append_char(packet->data, (char) 0);   

    /* Priority 0=high, 1=normal */
    octstr_append_char(packet->data, (char) 1);
    gw_assert(octstr_check_range(msg->sms.sender, 0,
                                 octstr_len(msg->sms.sender), isphonedigit));

    /* Originating address length */
    octstr_append_char(packet->data,
                       (unsigned char) (octstr_len(msg->sms.sender) + 2));

    /* XXX: GSM operator dependent ? */
    /* TON */
    octstr_append_char(packet->data, (char) 0x42);

    /* NPI */
    octstr_append_char(packet->data, (char) 0x44);

    /* Originating address */
    octstr_append(packet->data, msg->sms.sender);

    /* Validity period type 0=none, 1=absolute, 2=relative */
    octstr_append_char(packet->data, (char) 0);

    /* Data coding scheme */
    octstr_append_char(packet->data, (char) dcs);

    /* Status report request */
    if (msg->sms.dlr_mask & 0x07)
        octstr_append_char(packet->data, (char) 7);
    else
        octstr_append_char(packet->data, (char) 0);

    /* Protocol id 0=default */
    octstr_append_char(packet->data, (char) 0);

    if (octstr_len(msg->sms.udhdata))
        so |= 0x02;
    if (msg->sms.coding == DC_8BIT)
        so |= 0x10;

    /* Submission options */
    octstr_append_char(packet->data, (char) so);

    udhlen8 = octstr_len(msg->sms.udhdata);
    msglen8 = octstr_len(msg->sms.msgdata);

    udhdata = octstr_duplicate(msg->sms.udhdata);
    msgdata = octstr_duplicate(msg->sms.msgdata);

    if (msg->sms.coding == DC_7BIT) {
        charset_latin1_to_gsm(msgdata);
        oisd_shrink_gsm7(msgdata);
    }

    /* calculate lengths */

    udhlen7 = octstr_len(udhdata);
    msglen7 = octstr_len(msgdata);

    /* copy text */

    /*
     * debug("bb.sms.oisd", 0, "packet_encode_message udhlen8=%d, msglen8=%d",
     *       udhlen8, msglen8);
     * debug("bb.sms.oisd", 0, "packet_encode_message udhlen7=%d, msglen7=%d",
     *       udhlen7, msglen7);
     */

    octstr_append_char(packet->data, (unsigned char) (udhlen8 + msglen8));
    octstr_append_char(packet->data, (unsigned char) (udhlen7 + msglen7));

    octstr_append(packet->data, udhdata);
    octstr_append(packet->data, msgdata);

    /* Sub-logical SME number */
    octstr_append_char(packet->data, (char) 0);
    octstr_append_char(packet->data, (char) 0);

    octstr_destroy(udhdata);
    octstr_destroy(msgdata);

    return packet;
}

/***************************************************************************
 * Protocol functions.  These implement various transactions.              *
 ***************************************************************************/

/* Give this packet a proper sequence number for sending. */
static void packet_set_send_sequence(struct packet *packet, SMSCenter *smsc)
{
    gw_assert(smsc != NULL);

    packet_set_sequence(packet, smsc->oisd_send_seq);
    smsc->oisd_send_seq++;
}

static struct packet *oisd_get_packet(SMSCenter *smsc, Octstr **ts)
{
    struct packet *packet = NULL;

    gw_assert(smsc != NULL);

    /* If packet is already available, don't try to read anything */
    packet = packet_extract(smsc);

    while (packet == NULL) {
        if (read_available(smsc->socket, RESPONSE_TIMEOUT) != 1) {
            warning(0, "OISD SMSCenter is not responding");
            return NULL;
        }

        if (octstr_append_from_socket(smsc->oisd_inbuffer,
                                      smsc->socket) <= 0) {
            error(0, "oisd_get_packet: read failed");
            return NULL;
        }

        packet = packet_extract(smsc);
    }

    packet_check_can_receive(packet);
    if (ts)
        *ts = octstr_copy(packet->data, 15, 14);

    if (smsc->keepalive > 0)
        smsc->oisd_next_ping = time(NULL) + smsc->keepalive;

    return packet;
}

/*
 * Acknowledge a request. The OISD spec only defines positive responses
 * to the server, because the server is perfect.
 */
static void oisd_send_response(struct packet *request, SMSCenter *smsc)
{
    struct packet *response;
    int len;

    gw_assert(request != NULL);
    gw_assert(request->operation < RESPONSE);

    response = packet_create(request->operation + RESPONSE, request->opref);

    octstr_append_char(response->data, (char) 0);

    len = octstr_len(response->data) - 10;
    if (len < 1)
        goto error;

    octstr_set_char(response->data, 8, len & 0xff); /* Data Size */
    octstr_set_char(response->data, 9, (len >> 8) & 0xff);

    debug("bb.sms.oisd", 0, "oisd_send_response.");

    /* Don't check errors here because if there is something
     * wrong with the socket, the main loop will detect it. */
    octstr_write_to_socket(smsc->socket, response->data);

error:
    packet_destroy(response);
}

static Msg *oisd_accept_message(struct packet *request, SMSCenter *smsc)
{
    Msg *message = NULL;
    Octstr *destination = NULL;
    Octstr *origin = NULL;
    Octstr *UDH = NULL;
    Octstr *text = NULL;
    int DCS;
    int dest_len;
    int origin_len;
    int add_info;
    int msglen7, msglen8;

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

    /*
     * Destination addr. and Originating addr. w/o TOA
     */

    /* Destination addr. length */
    dest_len = octstr_get_char(request->data, 10);
    
    /* Destination addr. */
    destination = octstr_copy(request->data, 11+2, dest_len-2);
    
    /* Originating addr. length */
    origin_len = octstr_get_char(request->data, 11+dest_len+4);

    /* Originating addr. */
    origin = octstr_copy(request->data, 11+dest_len+5+2, origin_len-2);

    DCS = octstr_get_char(request->data, 11+dest_len+5+origin_len);

    add_info = octstr_get_char(request->data, 11+dest_len+5+origin_len+2);

    msglen7 = octstr_get_char(request->data, 11+dest_len+5+origin_len+3);
    msglen8 = octstr_get_char(request->data, 11+dest_len+5+origin_len+4);

    switch (DCS) {
    case 0x00:  /* gsm7 */
        if (add_info & 0x02) {
            text = oisd_expand_gsm7(octstr_copy(request->data,
                                    11+dest_len+5+origin_len+5, msglen7));
            UDH = octstr_create("");
            warning(0, "oisd_accept_message: 7-bit UDH ?");
        } else {
            text = oisd_expand_gsm7(octstr_copy(request->data,
                                    11+dest_len+5+origin_len+5, msglen7));
            charset_gsm_to_latin1(text);
            UDH = octstr_create("");
        }
        break;
    default:  /* 0xf4, 0xf5, 0xf6, 0xf7; 8bit to disp, mem, sim or term */
        if (add_info & 0x02) {
            text = octstr_create("");
            UDH = octstr_copy(request->data,
                              11+dest_len+5+origin_len+5, msglen8);
        } else {
            text = octstr_copy(request->data,
                               11+dest_len+5+origin_len+5, msglen8);
            UDH = octstr_create("");
        }
        break;
    }

    /* Code elsewhere in the gateway always expects the sender and
     * receiver fields to be filled, so we discard messages that
     * lack them.  If they should not be discarded, then the code
     * handling sms messages should be reviewed.  -- RB */
    if (!destination || octstr_len(destination) == 0) {
        info(0, "OISD: Got SMS without receiver, discarding.");
        goto error;
    }

    if (!origin || octstr_len(origin) == 0) {
        info(0, "OISD: Got SMS without sender, discarding.");
        goto error;
    }

    if ((!text || octstr_len(text) == 0) && (!UDH || octstr_len(UDH) == 0)) {
        info(0, "OISD: Got empty SMS, ignoring.");
        goto error;
    }

    message = msg_create(sms);
    if (!dcs_to_fields(&message, DCS)) {
        /* XXX Should reject this message ? */
        debug("OISD", 0, "Invalid DCS");
        dcs_to_fields(&message, 0);
    }
    message->sms.sender = origin;
    message->sms.receiver = destination;
    if (UDH) {
        message->sms.udhdata = UDH;
    }
    message->sms.msgdata = text;

    debug("OISD", 0, "oisd_accept_message");
    return message;

error:
    msg_destroy(message);
    octstr_destroy(destination);
    octstr_destroy(origin);
    octstr_destroy(UDH);
    octstr_destroy(text);
    return NULL;
}

/* Deal with a request from the OISD server, and acknowledge it. */
static void oisd_handle_request(struct packet *request, SMSCenter *smsc)
{
    Msg *message = NULL;

    /* TODO: Check if the sequence number of this request is what we
     * expected. */

    debug("OISD", 0, "oisd_handle_request");

    if (request->operation == STATUS_REPORT) {
        message = oisd_accept_delivery_report_message(request, smsc);
        if (message) {
            list_append(smsc->oisd_received, message);
        }
    } else if (request->operation == DELIVER_SM) {
        message = oisd_accept_message(request, smsc);
        if (message) {
            list_append(smsc->oisd_received, message);
        }
    }

    oisd_send_response(request, smsc);
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
 */
static int oisd_request(struct packet *request, SMSCenter *smsc, Octstr **ts)
{
    int ret;
    struct packet *reply = NULL;
    int errorcode;
    int tries = 0;
    int len;

    gw_assert(smsc != NULL);
    gw_assert(request != NULL);
    gw_assert(operation_can_send(request->operation));

    if (smsc->socket < 0) {
        warning(0, "oisd_request: socket not open.");
        goto io_error;
    }

    len = octstr_len(request->data) - 10;
    if (len < 1)
        goto error;

    octstr_set_char(request->data, 8, len & 0xff); /* Data Size */
    octstr_set_char(request->data, 9, (len >> 8) & 0xff);

retransmit:
    packet_set_send_sequence(request, smsc);

    ret = octstr_write_to_socket(smsc->socket, request->data);
    if (ret < 0)
        goto io_error;

next_reply:
    reply = oisd_get_packet(smsc, ts);
    if (!reply)
        goto io_error;

    /* The server sent us a request.  Handle it, then wait for
     * a new reply. */
    if (reply->operation < RESPONSE) {
        oisd_handle_request(reply, smsc);
        packet_destroy(reply);
        goto next_reply;
    }

    if (reply->opref != request->opref) {
        /* We got a response to a different request number than
         * what we send.  Strange. */
        warning(0, "OISD: response had unexpected sequence number; ignoring.\n");
        goto next_reply;
    }

    if (reply->operation != request->operation + RESPONSE) {
        /* We got a response that didn't match our request */
        Octstr *request_name = operation_name(request->operation);
        Octstr *reply_name = operation_name(reply->operation);
        warning(0, "OISD: %s request got a %s",
                octstr_get_cstr(request_name),
                octstr_get_cstr(reply_name));
        octstr_destroy(request_name);
        octstr_destroy(reply_name);
        octstr_dump(reply->data, 0);
        goto retry;
    }

    errorcode = octstr_get_char(reply->data, 10); /* Result */

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
static void oisd_close_socket(SMSCenter *smsc)
{
    gw_assert(smsc != NULL);

    if (smsc->socket < 0)
        return ;

    if (close(smsc->socket) < 0)
        warning(errno, "error closing OISD socket");
    smsc->socket = -1;
}

/*
 * Open a socket to the SMSC, send a login packet, and wait for ack.
 * This may block.  Return 0 for success, or -1 for failure.
 * Make sure the socket is closed before calling this function, otherwise
 * we will leak fd's.
 */
static int oisd_login(SMSCenter *smsc)
{
    gw_assert(smsc != NULL);

    if (smsc->socket >= 0) {
        warning(0, "oisd_login: socket was already open; closing");
        oisd_close_socket(smsc);
    }

    smsc->socket = tcpip_connect_to_server(octstr_get_cstr(smsc->oisd_hostname),
                                           smsc->oisd_port, NULL);
    /* XXX add interface_name if required */
    if (smsc->socket == -1)
        goto error;

    info(0, "%s logged in.", smsc_name(smsc));

    return 0;

error:
    error(0, "oisd_login failed");
    oisd_close_socket(smsc);
    return -1;
}

static int oisd_send_delivery_request(SMSCenter *smsc)
{
    struct packet *packet = NULL;
    int ret;

    gw_assert(smsc != NULL);

    /* debug("bb.sms.oisd", 0, "Sending OISD retrieve request."); */
    packet = packet_create(RETRIEVE_REQUEST, BOGUS_SEQUENCE);

    gw_assert(octstr_check_range(smsc->sender_prefix, 0,
                                 octstr_len(smsc->sender_prefix),
                                 isphonedigit));
    /* Originating address length */
    octstr_append_char(packet->data,
                       (char) (octstr_len(smsc->sender_prefix) + 2));
    /* TON */
    octstr_append_char(packet->data, (char) 0x42);
    /* NPI */
    octstr_append_char(packet->data, (char) 0x44);
    /* Originating address */
    octstr_append(packet->data, smsc->sender_prefix);
    /* Receive ready flag */
    octstr_append_char(packet->data, (char) 1);
    /* Retrieve order */
    octstr_append_char(packet->data, (char) 0);

    ret = oisd_request(packet, smsc, NULL);
    packet_destroy(packet);

    if (ret < 0)
        warning(0, "OISD: Sending delivery request failed.\n");

    return ret;
}

/***************************************************************************/
/* SMSC Interface, as defined in smsc_interface.def                        */
/***************************************************************************/

SMSCenter *oisd_open(Octstr *hostname, int port, int keepalive,
                     Octstr *sender_prefix)
{
    SMSCenter *smsc = NULL;

    smsc = smscenter_construct();
    gw_assert(smsc != NULL);

    smsc->type = SMSC_TYPE_OISD;
    smsc->keepalive = keepalive;
    smsc->oisd_hostname = octstr_duplicate(hostname);
    smsc->oisd_port = port;
    smsc->sender_prefix = octstr_duplicate(sender_prefix);
    sprintf(smsc->name, "OISD:%s:%d", octstr_get_cstr(hostname), port);
    smsc->oisd_received = list_create();
    smsc->oisd_inbuffer = octstr_create("");
    smsc->oisd_error = 0;
    if (keepalive > 0)
        smsc->oisd_next_ping = time(NULL) + keepalive;

    if (oisd_login(smsc) < 0)
        goto error;

    return smsc;

error:
    error(0, "oisd_open failed");
    smscenter_destruct(smsc);
    return NULL;
}

int oisd_reopen(SMSCenter *smsc)
{
    gw_assert(smsc != NULL);

    warning(0, "Attempting to re-open OISD connection");

    oisd_close_socket(smsc);

    /* Restore message counters to their default values */
    smsc->oisd_send_seq = 0;

    /* Clear leftover input */
    octstr_destroy(smsc->oisd_inbuffer);
    smsc->oisd_inbuffer = octstr_create("");

    return oisd_login(smsc);
}

int oisd_close(SMSCenter *smsc)
{
    int ret;
    int discarded;

    gw_assert(smsc != NULL);

    debug("bb.sms.oisd", 0, "Closing OISD SMSC");

    if (smsc->socket < 0) {
        warning(0, "oisd_close: already closed.\n");
        return 0;
    }

    ret = close(smsc->socket);
    smsc->socket = -1;

    smsc->oisd_send_seq = 0;
    octstr_destroy(smsc->oisd_hostname);
    octstr_destroy(smsc->oisd_inbuffer);
    octstr_destroy(smsc->sender_prefix);

    discarded = list_len(smsc->oisd_received);
    list_destroy(smsc->oisd_received, msg_destroy_item);

    if (discarded > 0)
        warning(0, "OISD: discarded %d received messages", discarded);

    return ret;
}

int oisd_submit_msg(SMSCenter *smsc, Msg *msg)
{
    struct packet *packet;
    int ret = 0;
    int tries;
    Octstr *ts;
    ts = NULL;

    gw_assert(smsc != NULL);

    packet = packet_encode_message(msg, smsc->sender_prefix);
    if (!packet)
        return 0;   /* We can't signal protocol errors yet */

    for (tries = 0; tries < 3; tries++) {
        ret = oisd_request(packet, smsc, &ts);
        if ((ret == 0) && (ts) && (msg->sms.dlr_mask & 0x03)) {
            debug("bb.sms.oisd", 0, "oisd_submit_msg dlr_add url=%s ",
                  octstr_get_cstr(msg->sms.dlr_url));
            dlr_add(smsc->name,
                    octstr_get_cstr(ts),
                    octstr_get_cstr(msg->sms.sender),
                    octstr_get_cstr(msg->sms.receiver),
                    octstr_get_cstr(msg->sms.service),
                    octstr_get_cstr(msg->sms.dlr_url),
                    msg->sms.dlr_mask,
                    octstr_get_cstr(msg->sms.boxc_id));
            octstr_destroy(ts);
            ts = NULL;
        }
        if ((ret < 0) && (msg->sms.dlr_mask & 0x03)) {
            Msg* fake_dlr_msg;
            debug("bb.sms.oisd", 0, "oisd_submit_msg request ret=%d ", ret);
            fake_dlr_msg = oisd_submit_failed(smsc, msg, ts);
            if (fake_dlr_msg) {
                list_append(smsc->oisd_received, fake_dlr_msg);
            }
        }
        if (ret == 0 || ret == -1)
            break;
        if (oisd_reopen(smsc) < 0) {
            ret = -1;
            break;
        }
    }

    packet_destroy(packet);
    return ret;
}

/*
 * The bearerbox really doesn't like it if pending_smsmessage returns
 * an error code.  We work around it until the bearerbox is rewritten.
 * Record the error here, and return it in oisd_receive_msg.  Return
 * "message available" if there is an error so that oisd_receive_msg
 * is called.
 */
int oisd_pending_smsmessage(SMSCenter *smsc)
{
    long ret;
    struct packet *packet;

    gw_assert(smsc != NULL);
    gw_assert(smsc->type == SMSC_TYPE_OISD);

    if (list_len(smsc->oisd_received) > 0)
        return 1;

    if (smsc->socket < 0) {
        /* XXX We have to assume that smsc_send_message is
         * currently trying to reopen, so we have to make
         * this thread wait.  It should be done in a nicer
         * way. */ 
        return 0;
    }

    ret = read_available(smsc->socket, 0);
    if (ret == 0) {
        if (smsc->keepalive > 0 && smsc->oisd_next_ping < time(NULL)) {
            if (oisd_send_delivery_request(smsc) < 0) {
                smsc->oisd_error = 1;
                return 1;
            }
        }
        return 0;
    }

    if (ret < 0) {
        warning(errno, "oisd_pending_smsmessage: read_available failed");
        smsc->oisd_error = 1;
        return 1;
    }

    /* We have some data waiting... see if it is an sms delivery. */
    ret = octstr_append_from_socket(smsc->oisd_inbuffer, smsc->socket);

    if (ret == 0) {
        warning(0, "oisd_pending_smsmessage: service center closed connection.");
        smsc->oisd_error = 1;
        return 1;
    }
    if (ret < 0) {
        warning(0, "oisd_pending_smsmessage: read failed");
        smsc->oisd_error = 1;
        return 1;
    }

    for (;;) {
        packet = packet_extract(smsc);
        if (!packet)
            break;

        packet_check_can_receive(packet);

        if (packet->operation < RESPONSE)
            oisd_handle_request(packet, smsc);
        else {
            error(0, "oisd_pending_smsmessage: unexpected response packet");
            octstr_dump(packet->data, 0);
        }

        packet_destroy(packet);
    }

    if (list_len(smsc->oisd_received) > 0)
        return 1;

    return 0;
}

int oisd_receive_msg(SMSCenter *smsc, Msg **msg)
{
    gw_assert(smsc != NULL);
    gw_assert(msg != NULL);

    if (smsc->oisd_error) {
        smsc->oisd_error = 0;
        return -1;
    }

    *msg = list_consume(smsc->oisd_received);

    return 1;
}

static Msg *oisd_accept_delivery_report_message(struct packet *request,
                                                SMSCenter *smsc)
{
    Msg *msg = NULL;
    Octstr *destination = NULL;
    Octstr *timestamp = NULL;
    int st_code;
    int code;
    int dest_len;

    /* MSISDN length */
    dest_len = octstr_get_char(request->data, 10);
    /* MSISDN */
    destination = octstr_copy(request->data, 10+1, dest_len);
    /* Accept time */
    timestamp = octstr_copy(request->data, 10+1+dest_len+1+4+4, 14);
    /* SM status */
    st_code = octstr_get_char(request->data, 10+1+dest_len+1+4+4+14);

    switch (st_code) {
    case 1:
    case 2:
        code = DLR_FAIL;
        break;
    case 3:   /* success */
        code = DLR_SUCCESS;
        break;
    case 4:
    case 5:
    case 6:
    default:
        code = 0;
    }
    if (code) {
        msg = dlr_find(smsc->name,
                       octstr_get_cstr(timestamp),
                       octstr_get_cstr(destination),
                       code);
        debug("bb.sms.oisd", 0, "oisd_accept_dlr_message val=%d ",
              st_code);
        if (msg != NULL && msg->sms.msgdata != NULL) {
            debug("bb.sms.oisd", 0, "oisd_accept_dlr_message url=%s ",
                  octstr_get_cstr(msg->sms.msgdata));
        }
    } else
        msg = NULL;
    octstr_destroy(destination);
    octstr_destroy(timestamp);

    return msg;
}

static Msg *oisd_submit_failed(SMSCenter *smsc, Msg* msg, Octstr* ts)
{
    Msg *dlr_msg;

    if (! msg->sms.dlr_url || ! octstr_len(msg->sms.dlr_url))
        return NULL;
    dlr_msg = msg_create(sms);
    dlr_msg->sms.service = octstr_duplicate(msg->sms.service);
    dlr_msg->sms.dlr_mask = DLR_FAIL;
    dlr_msg->sms.sms_type = report;
    dlr_msg->sms.smsc_id = octstr_create(smsc->name);
    dlr_msg->sms.sender = octstr_duplicate(msg->sms.sender);
    dlr_msg->sms.receiver = octstr_duplicate(msg->sms.receiver);
    dlr_msg->sms.dlr_url = octstr_len(msg->sms.dlr_url) ?
                           octstr_duplicate(msg->sms.dlr_url) : NULL;
    dlr_msg->sms.msgdata = NULL;
    time(&dlr_msg->sms.time);

    debug("bb.sms.oisd", 0, "oisd_submit_failed url=%s ",
          octstr_get_cstr(dlr_msg->sms.dlr_url));
    return dlr_msg;
}
