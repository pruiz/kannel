/*
 * wsp.h - WSP implementation header
 */

#ifndef WSP_H
#define WSP_H

/*
 * int WSP_accepted_extended_methods[] = { -1 };
 * int WSP_accepted_header_code_pages[] = { -1 };
 */

/* well, aliases are not negotiated... */

/* use bitflags for set values */
   
#define WSP_CSDU_SET 	1
#define WSP_SSDU_SET 	2
#define WSP_PO_SET 	4
#define WSP_MMOR_SET	8
#define WSP_PMOR_SET	16
#define WSP_EM_SET	32
#define WSP_HCP_SET	64
#define WSP_A_SET	128

/* See Table 35 */
enum wsp_abort_values {
	WSP_ABORT_PROTOERR = 0xe0,
	WSP_ABORT_DISCONNECT = 0xe1,
	WSP_ABORT_SUSPEND = 0xe2,
	WSP_ABORT_RESUME = 0xe3,
	WSP_ABORT_CONGESTION = 0xe4,
	WSP_ABORT_CONNECTERR = 0xe5,
	WSP_ABORT_MRUEXCEEDED = 0xe6,
	WSP_ABORT_MOREXCEEDED = 0xe7,
	WSP_ABORT_PEERREQ = 0xe8,
	WSP_ABORT_NETERR = 0xe9,
	WSP_ABORT_USERREQ = 0xea
};


typedef struct WSPMachine WSPMachine;
typedef struct WSPMethodMachine WSPMethodMachine;

#include "gwlib/gwlib.h"
#include "wtp.h"
#include "wap-events.h"
#include "wapbox.h"
#include "cookies.h"

struct WSPMachine {
	#define INTEGER(name) long name;
	#define OCTSTR(name) Octstr *name;
	#define HTTPHEADERS(name) List *name;
	#define ADDRTUPLE(name) WAPAddrTuple *name;
	#define COOKIES(name) List *name;
	#define METHODMACHINES(name) List *name;
	#define CAPABILITIES(name) List *name;
	#define MACHINE(fields) fields
	#include "wsp-session-machine.h"
};


struct WSPMethodMachine {
	#define INTEGER(name) long name;
	#define ADDRTUPLE(name) WAPAddrTuple *name;
	#define EVENT(name) WAPEvent *name;
	#define MACHINE(fields) fields
	#include "wsp-method-machine.h"
};


/* configure an URL mapping; parses string on whitespace, uses left
 * part for the source URL, and right part for the destination URL
 */
void wsp_http_map_url_config(char *);

/* configure an URL mapping from source DEVICE:home to given string */
void wsp_http_map_url_config_device_home(char *);

/* show all configured URL mappings */
void wsp_http_map_url_config_info(void);


/*
 * The application layer.
 */

void wap_appl_init(void);
void wap_appl_shutdown(void);
void wap_appl_dispatch(WAPEvent *event);

/*
 * WSP session oriented mode.
 */
void wsp_session_init(void);
void wsp_session_shutdown(void);
void wsp_session_dispatch_event(WAPEvent *event);


/*
 * Connectionless mode.
 */
void wsp_unit_init(void);
void wsp_unit_shutdown(void);
void wsp_unit_dispatch_event(WAPEvent *event);
WAPEvent *wsp_unit_unpack_wdp_datagram(Msg *msg);


/*
 * Shared stuff.
 */
Octstr *wsp_encode_http_headers(Octstr *content_type);
long wsp_convert_http_status_to_wsp_status(long http_status);

#endif
