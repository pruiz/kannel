/*
 * Functions to handle XML-RPC structure - building and parsing
 *
 * XML-RPC is HTTP-based XML defination to handle remote procedure calls,
 * and is defined in http://www.xml-rpc.org
 *
 * Kalle Marjola 2001 for project Kannel
 */

#include "gwlib/gwlib.h"
#include "gwlib/xmlrpc.h"

struct xmlrpc_methodcall {
    Octstr *method_name;
    List   *params;		/* List of XMLRPCValues */
};

struct xmlrpc_methodresponse {
    XMLRPCValue *param;		/* either this... */
    XMLRPCValue *fault; 	/* ..or this */
};

struct xmlrpc_value {
    int v_type;		/* enum here */
    XMLRPCScalar *v_scalar;
    List *v_array;     /* List of XMLRPCValues */
    List *v_struct;    /* List of XMLRPCStructs */

};

struct xmlrpc_member {	/* member of struct */
    Octstr *name;
    XMLRPCValue *value;
};

struct xmlrpc_scalar {
    int 	s_type;		/* enum here */
    Octstr 	*s_str;
    long 	s_int;
    int 	s_bool;
    double	s_double;
    Octstr	*s_date;
    Octstr	*s_base64;
};



/*-------------------------------------
 * MethodCall
 */

XMLRPCMethodCall *xmlrpc_call_create(Octstr *name)
{
    XMLRPCMethodCall *nmsg = gw_malloc(sizeof(XMLRPCMethodCall));

    nmsg->method_name = octstr_duplicate(name);
    nmsg->params = list_create();
    
    return nmsg;
}

void xmlrpc_call_destroy(XMLRPCMethodCall *call)
{
    if (call == NULL)
	return;

    octstr_destroy(call->method_name);
    list_destroy(call->params, xmlrpc_value_destroy_item);

    gw_free(call);
}


int xmlrpc_call_add_int(XMLRPCMethodCall *method, long i4)
{
    XMLRPCValue *nval;
    if (method == NULL)
	return -1;
    nval = xmlrpc_create_scalar_value(xr_int, (void *)i4);
    list_produce(method->params, nval);
    return 0;
}

int xmlrpc_call_add_string(XMLRPCMethodCall *method, Octstr *string)
{
    XMLRPCValue *nval;
    if (method == NULL)
	return -1;
    nval = xmlrpc_create_scalar_value(xr_string, string);
    list_produce(method->params, nval);
    return 0;
}

int xmlrpc_call_add_value(XMLRPCMethodCall *method, XMLRPCValue *value)
{
    if (method == NULL || value == NULL)
	return -1;

    list_produce(method->params, value);
    return 0;
}



/*-------------------------------------
 * MethodResponse
 */


XMLRPCMethodResponse *xmlrpc_response_create(XMLRPCValue *param)
{
    XMLRPCMethodResponse *nmsg = gw_malloc(sizeof(XMLRPCMethodResponse));

    nmsg->param = param;
    nmsg->fault = NULL;
    
    return nmsg;
}

void xmlrpc_response_destroy(XMLRPCMethodResponse *response)
{
    if (response == NULL)
	return;

    xmlrpc_value_destroy(response->param);
    xmlrpc_value_destroy(response->fault);

    gw_free(response);
}






/* Create new value. Set type of it to undefined, so it must be
 * set laterwards to correct one
 */
XMLRPCValue *xmlrpc_value_create(void)
{
    XMLRPCValue *val = gw_malloc(sizeof(XMLRPCValue));

    val->v_type = xr_undefined;
    val->v_scalar = NULL;
    val->v_array = NULL;
    val->v_struct = NULL;
}

/* Destroy value with its information, recursively if need to */
void xmlrpc_value_destroy(XMLRPCValue *val)
{
    if (val == NULL)
	return;

    switch(val->v_type) {
    case xr_scalar:
	xmlrpc_scalar_destroy(val->v_scalar);
	break;
    case xr_array:
	list_destroy(val->v_array, xmlrpc_value_destroy_item);
	break;
    case xr_struct:
	list_destroy(val->v_struct, xmlrpc_member_destroy_item);
	break;
    }
    gw_free(val);
}

/* wrapper to destroy to be used with list */
void xmlrpc_value_destroy_item(void *val)
{
    xmlrpc_value_destroy(val);
}
    

/* Create new member with undefined value */
XMLRPCMember *xmlrpc_member_create(Octstr *name)
{
    XMLRPCMember *member = gw_malloc(sizeof(XMLRPCMember));

    gw_assert(name);
    member->name = octstr_duplicate(name);
    member->value = xmlrpc_value_create();

    return member;
}

/* Destroy member and its contents */
void xmlrpc_member_destroy(XMLRPCMember *member)
{
    if (member == NULL)
	return;

    xmlrpc_value_destroy(member->value);
    octstr_destroy(member->name);

    gw_free(member);
}

void xmlrpc_member_destroy_item(void *member)
{
    xmlrpc_member_destroy(member);
}


/* Create new scalar of given type with given argument */
XMLRPCScalar *xmlrpc_scalar_create(int type, void *arg)
{
    XMLRPCScalar *scalar = gw_malloc(sizeof(XMLRPCScalar));

    scalar->s_type = type;
    scalar->s_str = NULL;
    scalar->s_date = NULL;
    scalar->s_base64 = NULL;
    
    switch(type) {
    case xr_string:
	scalar->s_str = octstr_duplicate((Octstr *)arg);
	break;
    case xr_int:
	scalar->s_int = (long)arg;
	break;
    }
    return scalar;
}

/* Destroy scalar */
void xmlrpc_scalar_destroy(XMLRPCScalar *scalar)
{
    if (scalar == NULL)
	return;

    octstr_destroy(scalar->s_str);
    octstr_destroy(scalar->s_date);
    octstr_destroy(scalar->s_base64);
}


/*-------------------------------------------------
 * Utilities to make things easier
 */


/* create scalar value from given arguments
 * XXX: Should this take different kind of arguments?
 */
XMLRPCValue *xmlrpc_create_scalar_value(int type, void *arg)
{
    XMLRPCValue *value = xmlrpc_value_create();
    value->v_type = xr_scalar;
    value->v_scalar = xmlrpc_scalar_create(type, arg);

    return value;
}

