#ifndef SMSC_SMPP_H
#define SMSC_SMPP_H

#include "gwlib/gwlib.h"
#include "smsc.h"
#include "smsc_p.h"

/******************************************************************************
* Command Codes from 
* SMPP 3.4 specification
* http://www.smpp.org/
*/

#define SMPP_GENERIC_NAK              0x80000000

#define SMPP_BIND_RECEIVER            0x00000001
#define SMPP_BIND_RECEIVER_RESP       0x80000001

#define SMPP_BIND_TRANSMITTER         0x00000002
#define SMPP_BIND_TRANSMITTER_RESP    0x80000002

#define SMPP_QUERY_SM                 0x00000003
#define SMPP_QUERY_SM_RESP            0x80000003

#define SMPP_SUBMIT_SM                0x00000004
#define SMPP_SUBMIT_SM_RESP           0x80000004

#define SMPP_SUBMIT_MULTI             0x00000021
#define SMPP_SUBMIT_MULTI_RESP        0x80000021

#define SMPP_DELIVER_SM               0x00000005
#define SMPP_DELIVER_SM_RESP          0x80000005

#define SMPP_UNBIND                   0x00000006
#define SMPP_UNBIND_RESP              0x80000006

#define SMPP_REPLACE_SM               0x00000007
#define SMPP_REPLACE_SM_RESP          0x80000007

#define SMPP_CANCEL_SM                0x00000008
#define SMPP_CANCEL_SM_RESP           0x80000008

#define SMPP_ENQUIRE_LINK             0x00000015
#define SMPP_ENQUIRE_LINK_RESP        0x80000015

/******************************************************************************
* Numering Plan Indicator and Type of Number codes from 
* GSM 03.40 Version 5.3.0 Section 9.1.2.5.
* http://www.etsi.org/
*/
#define GSM_ADDR_TON_UNKNOWN          0x00000000
#define GSM_ADDR_TON_INTERNATIONAL    0x00000001
#define GSM_ADDR_TON_NATIONAL         0x00000002
#define GSM_ADDR_TON_NETWORKSPECIFIC  0x00000003
#define GSM_ADDR_TON_SUBSCRIBER       0x00000004
#define GSM_ADDR_TON_ALPHANUMERIC     0x00000005 /* GSM TS 03.38 */
#define GSM_ADDR_TON_ABBREVIATED      0x00000006
#define GSM_ADDR_TON_EXTENSION        0x00000007 /* Reserved */

#define GSM_ADDR_NPI_UNKNOWN          0x00000000
#define GSM_ADDR_NPI_E164             0x00000001 /* "358503000009" */
#define GSM_ADDR_NPI_X121             0x00000003 /* "244053000009" */
#define GSM_ADDR_NPI_TELEX            0x00000004
#define GSM_ADDR_NPI_NATIONAL         0x00000008
#define GSM_ADDR_NPI_PRIVATE          0x00000009
#define GSM_ADDR_NPI_ERMES            0x0000000A /* ETSI DE/PS 3 01-3 */
#define GSM_ADDR_NPI_EXTENSION        0x0000000F /* Reserved */

/******************************************************************************
* Error Codes from 
* SMPP 3.4 specification
* http://www.smpp.org/
*/
#define ESME_ROK                      0x00000000 /* No error */
#define ESME_RINVMSGLEN               0x00000001 /* Message length is invalid */
#define ESME_RINVCMDLEN               0x00000002 /* Command length is invalid */
#define ESME_RINVCMDID                0x00000003 /* Invalid command ID */
#define ESME_RINVBNDSTS               0x00000004 /* Incorrect bind status */
#define ESME_RALYBND                  0x00000005 /* ESME already in bound state */
#define ESME_RINVPRTFLG               0x00000006 /* Invalid priority flag */
#define ESME_RINVREGDLVFLG            0x00000007 /* Invalid registered delivery flag */
#define ESME_RSYSERR                  0x00000008 /* System error */
#define ESME_RINVSRCADR               0x0000000A /* Invalid source address */
#define ESME_RINVDSTADR               0x0000000B /* Invalid destination address */
#define ESME_RINVMSGID                0x0000000C /* Invalid message ID */
#define ESME_RBINDFAIL                0x0000000D /* Bind failed */
#define ESME_RINVPASWD                0x0000000E /* Invalid password */
#define ESME_RINVSYSID                0x0000000F /* Invalid system ID */
#define ESME_RCANCELFAIL              0x00000011 /* Cancel SM failed */
#define ESME_RREPLACEFAIL             0x00000013 /* Replace SM failed */
#define ESME_RMSGQFUL                 0x00000014 /* Message queue full */
#define ESME_RINVSERTYP               0x00000015 /* Invalid service type */
#define ESME_RPARAMRETFAIL            0x00000031 /* Param Retrieve Failed */
#define ESME_RINVPARAM                0x00000032 /* Invalid Param */
#define ESME_RINVNUMDESTS             0x00000033 /* Invalid number of destinations */
#define ESME_RINVDESTFLAG             0x00000040 /* Destination flag is invalid (sm_multi) */
#define ESME_RINVUSBREP               0x00000042 /* Submit w/replace invalid */
#define ESME_RINVESMCLASS             0x00000043 /* ESM Class is invalid */
#define ESME_RCNTSUBDL                0x00000044 /* Cannot submit to DL */
#define ESME_RSUBMITFAIL              0x00000045 /* Submit SM/Multi failed */
#define ESME_RINVSRCTON               0x00000048 /* Invalid Source TON */
#define ESME_RINVSRCNPI               0x00000049 /* Invalid Source NPI */
#define ESME_RINVDSTTON               0x00000050 /* Invalid Destination TON */
#define ESME_RINVDSTNPI               0x00000051 /* Invalid Destination NPI */
#define ESME_RINVSYSTYP               0x00000053 /* Invalid System Type */
#define ESME_RINVREPFLAG              0x00000054 /* Invalid Replace if present flag */
#define ESME_RINVNUMMSGS              0x00000055 /* Invalid number of messages */
#define ESME_RTHROTTLED               0x00000058 /* I/F Throttled Error */
#define ESME_RINVSHCHED               0x00000061 /* Invalid Scheduled Delivery Time */
#define ESME_RINVEXPIRY               0x00000062 /* Expiry/Validity period is invalid */
#define ESME_RINVDFTMSGID             0x00000063 /* Predefined Message Not Found */
#define ESME_RX_T_APPN                0x00000064 /* ESME RX Reject Message Error Code */
#define ESME_RX_P_APPN                0x00000065 /* ESME EX Permanent App Error Code */
#define ESME_RX_R_APPN                0x00000066 /* ESME RX Temporary App Error Code */
#define ESME_RQUERYFAIL               0x00000067 /* Query SM Failed */
#define ESME_RUNKNOWNERR              0x000000FF /* Unknown Error */

#define SMPP_STATE_CONNECTED          0x00000001
#define SMPP_STATE_BOUND              0x00000002

typedef struct smpp_pdu {

	unsigned int length;
	unsigned int id;
	unsigned int status;
	unsigned int sequence_no;

	int fd;	

	void *message_body;

	struct smpp_pdu *left, *right;

} smpp_pdu;

/******************************************************************************
* PDU NAK
*/
typedef struct smpp_pdu_generic_nak {
	/* This space has been intentionally left blank. */
} smpp_pdu_generic_nak;

/******************************************************************************
* PDU BIND
*/
typedef struct smpp_pdu_bind_receiver {

	char system_id[16];
	char password[9];
	char system_type[13];
	int  interface_version;
	int  addr_ton;
	int  addr_npi;
	char address_range[41];

} smpp_pdu_bind_receiver;

typedef struct smpp_pdu_bind_receiver_resp {

	char system_id[16];

} smpp_pdu_bind_receiver_resp;


typedef struct smpp_pdu_bind_transmitter {

	char system_id[16];
	char password[9];
	char system_type[13];
	int  interface_version;
	int  addr_ton;
	int  addr_npi;
	char address_range[41];

} smpp_pdu_bind_transmitter;

typedef struct smpp_pdu_bind_transmitter_resp {

	char system_id[16];

} smpp_pdu_bind_transmitter_resp;

/******************************************************************************
* PDU DELIVER
*/
typedef struct smpp_pdu_deliver_sm {

	char service_type[6];
	
	int source_addr_ton;
	int source_addr_npi;
	char source_addr[21];

	int dest_addr_ton;
	int dest_addr_npi;
	char dest_addr[21];

	int esm_class;
	int protocol_id;
	int priority_flag;

	char schedule_delivery_time[17];
	char validity_period[17];

	int registered_delivery_flag;
	int replace_if_present_flag;

	int data_coding;

	int sm_default_msg_id;

	int sm_length;
	char short_message[161];

} smpp_pdu_deliver_sm;

typedef struct smpp_pdu_deliver_sm_resp {

	char system_id[16];

} smpp_pdu_deliver_sm_resp;

/******************************************************************************
* PDU SUBMIT
*/
typedef struct smpp_pdu_submit_sm {

	char service_type[6];
	
	int source_addr_ton;
	int source_addr_npi;
	char source_addr[21];

	int dest_addr_ton;
	int dest_addr_npi;
	char dest_addr[21];

	int esm_class;
	int protocol_id;
	int priority_flag;

	char schedule_delivery_time[17];
	char validity_period[17];

	int registered_delivery_flag;
	int replace_if_present_flag;

	int data_coding;

	int sm_default_msg_id;

	int sm_length;
	char short_message[161];

} smpp_pdu_submit_sm;

typedef struct smpp_pdu_submit_sm_resp {

	char system_id[16];

} smpp_pdu_submit_sm_resp;

/******************************************************************************
* PDU QUERY
*/
typedef struct smpp_pdu_query_sm {

	char original_message_id[9];
	int  originating_ton;
	int  originating_npi;
	char originating_addr[21];
	
} smpp_pdu_query_sm;

typedef struct smpp_pdu_query_sm_resp {

	char original_message_id[9];
	char final_date[17];
	int  message_status;
	int  error_code;
	
} smpp_pdu_query_sm_resp;

static int smpp_append_oct(char**, int*, int);
static int smpp_read_oct(char**, int*, Octet*);
static int smpp_append_cstr(char**, int*, char*);
static int smpp_read_cstr(char**, int*, char**);

static Octstr* data_new(void);
static int data_free(Octstr*);
static int data_pop(Octstr*, Octstr**);
static int data_receive(int, Octstr*);
static int data_send(int, Octstr*);

static smpp_pdu* pdu_new(void);
static int pdu_free(smpp_pdu*);

static int pdu_act(SMSCenter*, smpp_pdu*);

static int pdu_act_bind_transmitter_resp(SMSCenter*, smpp_pdu*);
static int pdu_act_bind_receiver_resp(SMSCenter*, smpp_pdu*);
static int pdu_act_unbind_resp(SMSCenter*, smpp_pdu*);
static int pdu_act_submit_sm_resp(SMSCenter*, smpp_pdu*);
static int pdu_act_submit_multi_resp(SMSCenter*, smpp_pdu*);
static int pdu_act_deliver_sm(SMSCenter*, smpp_pdu*);
static int pdu_act_query_sm_resp(SMSCenter*, smpp_pdu*);
static int pdu_act_cancel_sm_resp(SMSCenter*, smpp_pdu*);
static int pdu_act_replace_sm_resp(SMSCenter*, smpp_pdu*);

/* These might come down from either of the IO streams. */
static int pdu_act_enquire_link(SMSCenter*, smpp_pdu*);
static int pdu_act_enquire_link_resp(SMSCenter*, smpp_pdu*);
static int pdu_act_generic_nak(SMSCenter*, smpp_pdu*);


static int pdu_decode(smpp_pdu**, Octstr*);
static int pdu_encode(smpp_pdu*, Octstr**);

static int pdu_header_decode(smpp_pdu*, Octstr*);
static int pdu_header_encode(smpp_pdu*, Octstr**);

static int pdu_decode_bind(smpp_pdu*, Octstr*);
static int pdu_encode_bind(smpp_pdu*, Octstr**);

static int pdu_decode_deliver_sm(smpp_pdu*, Octstr*);
static int pdu_encode_deliver_sm_resp(smpp_pdu*, Octstr**);

static int pdu_decode_submit_sm_resp(smpp_pdu*, Octstr*);
static int pdu_encode_submit_sm(smpp_pdu*, Octstr**);

#endif
