/*
 * xmlrpc.h - XML-RPC functions
 *
 * Functions to handle XML-RPC structure - building and parsing
 *
 * XML-RPC is HTTP-based XML defination to handle remote procedure calls,
 * and is defined in http://www.xml-rpc.org
 *
 * The current implementation is not yet ready (it does not, for example,
 *  do any parsing nor building the tree), and is not used for any real use,
 * yet, but probably future interfaces might be able to use this, too
 *
 *
 * Kalle Marjola 2001 for project Kannel
 */

#ifndef __XMLRPC_H
#define __XMLRPC_H

#include "gwlib/gwlib.h"

/*
 * types and structures defined by www.xml-rpc.com
 */
typedef struct xmlrpc_methodcall XMLRPCMethodCall;
typedef struct xmlrpc_methodresponse XMLRPCMethodResponse;
typedef struct xmlrpc_value XMLRPCValue;
typedef struct xmlrpc_scalar XMLRPCScalar;
typedef struct xmlrpc_member XMLRPCMember;
    
#define create_octstr_from_node(node) (octstr_create(node->content))


struct xmlrpc_table_t {
    char *name;
};

struct xmlrpc_2table_t {
    char *name;
    int s_type; /* enum here */
};

typedef struct xmlrpc_table_t xmlrpc_table_t;
typedef struct xmlrpc_2table_t xmlrpc_2table_t;

static xmlrpc_table_t methodcall_elements[] = {
    "METHODNAME",
    "PARAMS"
};

static xmlrpc_table_t params_elements[] = {
    "PARAM"
};

static xmlrpc_table_t param_elements[] = {
    "VALUE"
};

enum { 
    xr_undefined, xr_scalar, xr_array, xr_struct, 
    xr_string, xr_int, xr_bool, xr_double, xr_date, xr_base64 
};

static xmlrpc_2table_t value_elements[] = {
    { "I4", xr_int },
    { "INT", xr_int },
    { "BOOLEAN", xr_bool },
    { "STRING", xr_string },
    { "DOUBLE", xr_double },
    { "DATETIME.ISO8601", xr_date },
    { "BASE64", xr_base64 },
    { "STRUCT", xr_struct },
    { "ARRAY", xr_array }
};

#define NUMBER_OF_METHODCALL_ELEMENTS \
    sizeof(methodcall_elements)/sizeof(methodcall_elements[0])
#define NUMBER_OF_PARAMS_ELEMENTS \
    sizeof(params_elements)/sizeof(params_elements[0])
#define NUMBER_OF_PARAM_ELEMENTS \
    sizeof(param_elements)/sizeof(param_elements[0])
#define NUMBER_OF_VALUE_ELEMENTS \
    sizeof(value_elements)/sizeof(value_elements[0])


/*** METHOD CALLS ***/

/* Create new MethodCall object with given name and no params */
XMLRPCMethodCall *xmlrpc_call_create(Octstr *method);

/* Create new MethodCall object from given body of text/xml */
/* PARTIALLY IMPLEMENTED */
XMLRPCMethodCall *xmlrpc_call_parse(Octstr *post_body);

/* Destroy MethodCall object */
void xmlrpc_call_destroy(XMLRPCMethodCall *call);

/* Add i4/int scalar param to MethodCall object. Always return 0 */
int xmlrpc_call_add_int(XMLRPCMethodCall *method, long i4);

/* Add boolean scalar param to MethodCall object. Always return 0 */
int xmlrpc_call_add_bool(XMLRPCMethodCall *method, int bool);

/* Add string scalar param to MethodCall object. Always return 0 */
int xmlrpc_call_add_string(XMLRPCMethodCall *method, Octstr *string);

/* Add double scalar param to MethodCall object. Always return 0 */
int xmlrpc_call_add_double(XMLRPCMethodCall *method, double val);

/* Add date scalar param to MethodCall object. Always return 0 */
int xmlrpc_call_add_date(XMLRPCMethodCall *method, Octstr *date);

/* Add base64 scalar param to MethodCall object. Always return 0 */
int xmlrpc_call_add_base64(XMLRPCMethodCall *method, Octstr *b64);

/* Add given <value> param to MethodCall object. Always return 0.
 * Note that value is NOT duplicated */
/* NOTE: As currently only string and int4 is supported, this function
 * has no real use, just use above functions */
int xmlrpc_call_add_value(XMLRPCMethodCall *method, XMLRPCValue *value);

/* Create Octstr (text/xml string) out of given MethodCall. Caller
 * must free returned Octstr */
/* PARTIALLY IMPLEMENTED */
Octstr *xmlrpc_call_octstr(XMLRPCMethodCall *call);

/* Send MethodCall to given URL with given Headers. Note: adds XML-RPC
 *  spesified headers into given list. 'headers' are always destroyed, and
 * if NULL when this function called, automatically generated
 *
 * Return 0 if all went fine, -1 if failure. As user reference, uses *void
 */
/* PARTIALLY IMPLEMENTED, as is based on above function, for example */
int xmlrpc_call_send(XMLRPCMethodCall *call, HTTPCaller *http_ref,
		     Octstr *url, List *headers, void *ref);


/*** METHOD RESPONSES ***/

/* Create a new MethodResponse object with given <value> */
XMLRPCMethodResponse *xmlrpc_response_create(XMLRPCValue *value);

/* As above, but with <fault> part with filled-up structs */
/* NOT YET IMPLEMENTED */
XMLRPCMethodResponse *xmlrpc_response_fault_create(long faultcode,
						   Octstr *faultstring);

/* Create a new MethodResponse object from given text/xml string */
/* NOT YET IMPLEMENTED */
XMLRPCMethodResponse *xmlrpc_response_parse(Octstr *post_body);

/* Destroy MethodResponse object */
void xmlrpc_response_destroy(XMLRPCMethodResponse *response);


/*** STRUCT HANDLING ***/

/* Create a new <value> object of undefined type */ 
XMLRPCValue *xmlrpc_value_create(void);

/* Destroy given <value> object */
void xmlrpc_value_destroy(XMLRPCValue *val);

/* Wrapper for destroy */
void xmlrpc_value_destroy_item(void *val);

/* append output of value to given octstr */
/* THIS SHOULD GO AWAY LATER */
void xmlrpc_value_print(XMLRPCValue *val, Octstr *os);

/* Create a new <member> for xs_struct with undefined <value> */
XMLRPCMember *xmlrpc_member_create(Octstr *name);

/* Destroy struct member along with value */
void xmlrpc_member_destroy(XMLRPCMember *member);

/* Wrapper for destroy */
void xmlrpc_member_destroy_item(void *member);

/* Create a new scalar of given type and value
 * (which must be in right format) */
XMLRPCScalar *xmlrpc_scalar_create(int type, void *arg);
XMLRPCScalar *xmlrpc_scalar_create_double(int type, double val);

/* Destroy scalar */
void xmlrpc_scalar_destroy(XMLRPCScalar *scalar);

/* append output of scalar to given octstr */
/* THIS SHOULD GO AWAY LATER */
void xmlrpc_scalar_print(XMLRPCScalar *scalar, Octstr *os);

/* Create <value> of <scalar> type with given type and value */
XMLRPCValue *xmlrpc_create_scalar_value(int type, void *arg);
XMLRPCValue *xmlrpc_create_scalar_value_double(int type, double val);

#endif
