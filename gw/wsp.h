/*
 * wsp.h - WSP implementation header
 */

#ifndef WSP_H
#define WSP_H

/*
 * these capability maximum values are real maximum
 * values to our server. It cannot negotiate larger values
 * than these. No way.
 */

#define WSP_MAX_CLIENT_SDU 0
#define WSP_MAX_SERVER_SDU 0
#define WSP_MAX_PROTOCOL_OPTIONS 0x00
#define WSP_MAX_METHOD_MOR 1
#define WSP_MAX_PUSH_MOR 1

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


typedef struct WSPMachine WSPMachine;
typedef struct WSPMethodMachine WSPMethodMachine;

#include "gwlib/gwlib.h"
#include "wtp.h"
#include "wap-events.h"
#include "wapbox.h"

struct WSPMachine {
	#define INTEGER(name) long name;
	#define OCTSTR(name) Octstr *name;
	#define EVENT_POINTER(name) WSPEvent *name;
	#define METHOD_POINTER(name) WSPMethodMachine *name;
	#define SESSION_POINTER(name) WSPMachine *name;
	#define HTTPHEADER(name) List *name;
	#define LIST(name) List *name;
	#define SESSION_MACHINE(fields) fields
	#define METHOD_MACHINE(fields)
	#define ADDRTUPLE(name) WAPAddrTuple *name;
	#include "wsp_machine-decl.h"
};


struct WSPMethodMachine {
	#define INTEGER(name) long name;
	#define OCTSTR(name) Octstr *name;
	#define EVENT_POINTER(name) WSPEvent *name;
	#define METHOD_POINTER(name) WSPMethodMachine *name;
	#define SESSION_POINTER(name) WSPMethodMachine *name;
	#define HTTPHEADER(name) List *name;
	#define LIST(name) List *name;
	#define SESSION_MACHINE(fields)
	#define METHOD_MACHINE(fields) fields
	#define ADDRTUPLE(name) WAPAddrTuple *name;
	#include "wsp_machine-decl.h"
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
Octstr *wsp_encode_http_headers(long type);
long wsp_convert_http_status_to_wsp_status(long http_status);


#endif
