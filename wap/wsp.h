/*
 * wsp.h - WSP implementation header
 */

#ifndef WSP_H
#define WSP_H

/*
 * int WSP_accepted_extended_methods[] = { -1 };
 * int WSP_accepted_header_code_pages[] = { -1 };
 */

/* See Table 35 of the WSP standard */
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
typedef struct WSPPushMachine WSPPushMachine;

#include "gwlib/gwlib.h"
#include "wap_addr.h"
#include "wap_events.h"

struct WSPMachine {
	#define INTEGER(name) long name;
	#define OCTSTR(name) Octstr *name;
	#define HTTPHEADERS(name) List *name;
	#define ADDRTUPLE(name) WAPAddrTuple *name;
	#define COOKIES(name) List *name;
	#define MACHINESLIST(name) List *name;
	#define CAPABILITIES(name) List *name;
	#define MACHINE(fields) fields
	#include "wsp_server_session_machine.def"
};


struct WSPMethodMachine {
	#define INTEGER(name) long name;
	#define ADDRTUPLE(name) WAPAddrTuple *name;
	#define EVENT(name) WAPEvent *name;
	#define MACHINE(fields) fields
	#include "wsp_server_method_machine.def"
};

struct WSPPushMachine {
       #define INTEGER(name) long name;
       #define ADDRTUPLE(name) WAPAddrTuple *name;
       #define HTTPHEADER(name) List *name;
       #define MACHINE(fields) fields
       #include "wsp_server_push_machine.def"
};

/*
 * Shared stuff.
 */
long wsp_convert_http_status_to_wsp_status(long http_status);

#endif



