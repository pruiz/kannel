/*
 * wtp.h - WTP implementation general header, common things for the iniator 
 * and the responder.
 */

#ifndef WTP_H
#define WTP_H

#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>

#include "gwlib/gwlib.h"
#include "wapbox.h"
#include "msg.h"
#include "wap_addr.h"
#include "wap-events.h"
#include "wtp_pdu.h"

/* 
 * Use this structure for storing segments to be reassembled
 */
typedef struct WTPSegment WTPSegment;
#include "timers.h"
#include "wtp_send.h"
#include "wtp_tid.h"
#include "wapbox.h"

/*
 * For removing the magic
 */
enum {
    NUMBER_OF_ABORT_TYPES = 2,
    NUMBER_OF_ABORT_REASONS  = 9,
    NUMBER_OF_TRANSACTION_CLASSES = 3
};

/*
 * For now, timers are defined. They will depend on bearer information fetched
 * from address (or from a header field of the protocol speaking with the
 * bearerbox). For suggested timers, see WTP, Appendix A.
 */
enum {
    L_A_WITH_USER_ACK = 4,
    L_R_WITH_USER_ACK = 7,
    S_R_WITHOUT_USER_ACK = 3,
    S_R_WITH_USER_ACK = 4,
    G_R_WITHOUT_USER_ACK = 3,
    G_R_WITH_USER_ACK = 3,
    W_WITH_USER_ACK = 30
};

/*
 * Maximum values for counters (for retransmissions and acknowledgement waiting
 * periods)
 */
enum {
    AEC_MAX = 6,
    MAX_RCR = 8
};

/*
 * Types of acknowledgement PDU (normal acknowledgement or tid verification)
 */
enum {
    ACKNOWLEDGEMENT = 0,
    TID_VERIFICATION = 1
};

/*
 * Who is aborting (WTP or WTP user)
 */
enum {
    PROVIDER = 0x00,
    USER = 0x01
};

/*
 * WTP abort types (i.e., provider abort codes defined by WAP)
 */
enum {
    UNKNOWN = 0x00,
    PROTOERR = 0x01,
    INVALIDTID = 0x02,
    NOTIMPLEMENTEDCL2 = 0x03,
    NOTIMPLEMENTEDSAR = 0x04,
    NOTIMPLEMENTEDUACK = 0x05,
    WTPVERSIONZERO = 0x06,
    CAPTEMPEXCEEDED = 0x07,
    NORESPONSE = 0x08,
    MESSAGETOOLARGE = 0x09
};    

/*
 * Responder set first tid, initiaotr not. So all tids send by initiator are 
 * greater than 2**15.
 */
#define INITIATOR_TID_LIMIT (1 << 15)

/*
 *  Transaction is identified by the address four-tuple and tid.
 */
struct machine_pattern {
    WAPAddrTuple *tuple;
    long tid;
    long mid;
};

typedef struct machine_pattern machine_pattern;

/*
 * Handles possible concatenated messages. Returns a list of wap events, 
 * consisting of thes events. 
 * 
 * Real unpacking is done by an internal function.
 */
List *wtp_unpack_wdp_datagram(Msg *msg);

/*
 * Responder set the first bit of the tid field. If we get a packet from the 
 * responder, we are the initiator. 
 *
 * Returns 1 for responder, 0 for iniator and -1 for error.
 */
int wtp_event_is_for_responder(WAPEvent *event);

#endif
