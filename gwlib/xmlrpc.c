/*
 * gwlib/xmlrpc.c: functions to handle XML-RPC structure - building and parsing
 *
 * XML-RPC is HTTP-based XML defination to handle remote procedure calls,
 * and is defined at http://www.xml-rpc.org
 *
 * Kalle Marjola 2001 for project Kannel
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include <libxml/xmlmemory.h>
#include <libxml/tree.h>
#include <libxml/debugXML.h>
#include <libxml/encoding.h>

#include "gwlib/gwlib.h"
#include "gwlib/xmlrpc.h"

struct xmlrpc_methodcall {
    Octstr *method_name;
    List *params;         /* List of XMLRPCValues */
    int parse_status;     /* enum here */
    Octstr *parse_error;  /* error string in case of parsing error */
};

struct xmlrpc_methodresponse {
    XMLRPCValue *param;		/* either this... */
    XMLRPCValue *fault; 	/* ..or this */
};

struct xmlrpc_value {
    int v_type;		/* enum here */
    XMLRPCScalar *v_scalar;
    List *v_array;     /* List of XMLRPCValues */
    List *v_struct;    /* List of XMLRPMembers */
};

struct xmlrpc_member {	/* member of struct */
    Octstr *name;
    XMLRPCValue *value;
};

struct xmlrpc_scalar {
    int	s_type;		/* enum here */
    Octstr *s_str;
    long s_int;
    int s_bool;
    double s_double;
    Octstr *s_date;
    Octstr *s_base64;
};

struct xmlrpc_table_t {
    char *name;
};

struct xmlrpc_2table_t {
    char *name;
    int s_type; /* enum here */
};

static xmlrpc_table_t methodcall_elements[] = {
    { "METHODNAME" },
    { "PARAMS" }
};

static xmlrpc_table_t params_elements[] = {
    { "PARAM" }
};

static xmlrpc_table_t param_elements[] = {
    { "VALUE" }
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

static xmlrpc_table_t struct_elements[] = {
    { "MEMBER" }
};

static xmlrpc_table_t member_elements[] = {
    { "NAME" },
    { "VALUE" }
};


#define NUMBER_OF_METHODCALL_ELEMENTS \
    sizeof(methodcall_elements)/sizeof(methodcall_elements[0])
#define NUMBER_OF_PARAMS_ELEMENTS \
    sizeof(params_elements)/sizeof(params_elements[0])
#define NUMBER_OF_PARAM_ELEMENTS \
    sizeof(param_elements)/sizeof(param_elements[0])
#define NUMBER_OF_VALUE_ELEMENTS \
    sizeof(value_elements)/sizeof(value_elements[0])
#define NUMBER_OF_STRUCT_ELEMENTS \
    sizeof(struct_elements)/sizeof(struct_elements[0])
#define NUMBER_OF_MEMBER_ELEMENTS \
    sizeof(member_elements)/sizeof(member_elements[0])


static int parse_document(xmlDocPtr document, XMLRPCMethodCall **msg);
static int parse_methodcall(xmlDocPtr doc, xmlNodePtr node, 
                            XMLRPCMethodCall **msg);
static int parse_methodcall_element(xmlDocPtr doc, xmlNodePtr node, 
                                    XMLRPCMethodCall **msg);
static int parse_params(xmlDocPtr doc, xmlNodePtr node, 
                        XMLRPCMethodCall **msg);
static int parse_params_element(xmlDocPtr doc, xmlNodePtr node, 
                                XMLRPCMethodCall **msg);
static int parse_param(xmlDocPtr doc, xmlNodePtr node, 
                       XMLRPCMethodCall **msg, int *n);
static int parse_param_element(xmlDocPtr doc, xmlNodePtr node, 
                               XMLRPCMethodCall **msg);
static int parse_value(xmlDocPtr doc, xmlNodePtr node, 
                       XMLRPCMethodCall **msg);
static int parse_value_element(xmlDocPtr doc, xmlNodePtr node, 
                               XMLRPCMethodCall **msg);


/*-------------------------------------
 * MethodCall
 */

XMLRPCMethodCall *xmlrpc_call_create(Octstr *name)
{
    XMLRPCMethodCall *nmsg = gw_malloc(sizeof(XMLRPCMethodCall));

    nmsg->method_name = octstr_duplicate(name);
    nmsg->params = list_create();
    nmsg->parse_status = XMLRPC_COMPILE_OK;
    nmsg->parse_error = NULL;
    
    return nmsg;
}

XMLRPCMethodCall *xmlrpc_call_parse(Octstr *post_body)
{
    XMLRPCMethodCall *msg = gw_malloc(sizeof(XMLRPCMethodCall));
    xmlDocPtr pDoc;
    size_t size;
    char *body;
    int ret;

    msg->method_name = octstr_create("");
    msg->params = list_create();
    msg->parse_status = XMLRPC_COMPILE_OK;
    msg->parse_error = NULL;

    octstr_strip_blanks(post_body);
    octstr_shrink_blanks(post_body);
    size = octstr_len(post_body);
    body = octstr_get_cstr(post_body);

    /* parse XML document to a XML tree */
    pDoc = xmlParseMemory(body, size);

    ret = parse_document(pDoc, &msg);
    xmlFreeDoc(pDoc);
    return msg;
}

void xmlrpc_call_destroy(XMLRPCMethodCall *call)
{
    if (call == NULL)
	return;

    octstr_destroy(call->method_name);
    list_destroy(call->params, xmlrpc_value_destroy_item);

    if (call->parse_error != NULL)
        octstr_destroy(call->parse_error);

    gw_free(call);
}

int xmlrpc_call_add_scalar(XMLRPCMethodCall *method, int type, void *arg)
{
    XMLRPCValue *nval;
    if (method == NULL)
        return -1;
    nval = xmlrpc_create_scalar_value(type, arg);
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

Octstr *xmlrpc_call_octstr(XMLRPCMethodCall *call)
{
    Octstr *body;
    XMLRPCValue *val;
    long i;
    
    body = octstr_format("<?xml version=\"1.0\"?>\n<methodCall>\n"
			             "  <methodName>%S</methodName>\n"
			             "  <params>", call->method_name);

    list_lock(call->params);
    for (i = 0; i < list_len(call->params); i++) {
        val = list_get(call->params, i);
        octstr_format_append(body, "\n    <param>\n"
                             "\n      ");
        xmlrpc_value_print(val, body);
        octstr_format_append(body, "\n    </param>\n");
        
    }
    list_unlock(call->params);
    
    octstr_format_append(body, "  </params>\n"
			             "</methodCall>\n");

    return body;
}

int xmlrpc_call_send(XMLRPCMethodCall *call, HTTPCaller *http_ref,
		     Octstr *url, List *headers, void *ref)
{
    Octstr *body;
    if (http_ref == NULL || call == NULL)
	return -1;
    
    if (headers == NULL)
	headers = list_create();
    
    http_header_add(headers, "Content-Type", "text/xml");

    /* 
     * XML-RPC specs say we at least need Host and User-Agent 
     * HTTP headers to be defined.
     * These are set anyway within gwlib/http.c:build_request()
     */
    body = xmlrpc_call_octstr(call);

    http_start_request(http_ref, HTTP_METHOD_GET, 
                       url, headers, body, 0, ref, NULL);
    
    octstr_destroy(body);
    http_destroy_headers(headers);
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
    return val;
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

void xmlrpc_value_print(XMLRPCValue *val, Octstr *os)
{
    if (val->v_type != xr_scalar)
	return;

    octstr_format_append(os, "<value>");
    xmlrpc_scalar_print(val->v_scalar, os);
    octstr_format_append(os, "</value>");
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
    scalar->s_int = 0;
    scalar->s_bool = 0;
    scalar->s_double = 0;
    scalar->s_str = NULL;
    scalar->s_date = NULL;
    scalar->s_base64 = NULL;
    
    switch (type) {
        case xr_int:
            scalar->s_int = (long)arg;
            break;
        case xr_bool:
            scalar->s_bool = (int)arg;
            break;
        case xr_string:
            scalar->s_str = octstr_duplicate((Octstr *)arg);
            break;
        case xr_date:
            scalar->s_date = octstr_duplicate((Octstr *)arg);
            break;
        case xr_base64:
            scalar->s_base64 = octstr_duplicate((Octstr *)arg);
            break;
        default:
            panic(0,"XML-RPC: scalar type not supported!");
            break;
    }
    return scalar;
}

XMLRPCScalar *xmlrpc_scalar_create_double(int type, double val)
{
    XMLRPCScalar *scalar = gw_malloc(sizeof(XMLRPCScalar));

    scalar->s_type = type;
    scalar->s_int = 0;
    scalar->s_bool = 0;
    scalar->s_double = 0;
    scalar->s_str = NULL;
    scalar->s_date = NULL;
    scalar->s_base64 = NULL;
    
    switch (type) {
        case xr_double:
            scalar->s_double = val;
            break;
        default:
            panic(0,"XML-RPC: scalar type not supported!");
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
    
    gw_free(scalar);
}

void xmlrpc_scalar_print(XMLRPCScalar *scalar, Octstr *os)
{
    switch (scalar->s_type) {
        case xr_int:
            octstr_format_append(os, "<int>%d</int>", scalar->s_int);
            break;
        case xr_bool:
            octstr_format_append(os, "<bool>%d</bool>", scalar->s_bool);
            break;
        case xr_string:
            octstr_format_append(os, "<string>%S</string>", scalar->s_str);
            break;
        case xr_double:
            octstr_format_append(os, "<double>%d</double>", scalar->s_double);
            break;
        case xr_date:
            octstr_format_append(os, "<date>%S</date>", scalar->s_date);
            break;
        case xr_base64:
            octstr_format_append(os, "<base64>%S</base64>", scalar->s_base64);
            break;
    }
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

XMLRPCValue *xmlrpc_create_scalar_value_double(int type, double val)
{
    XMLRPCValue *value = xmlrpc_value_create();
    value->v_type = xr_scalar;
    value->v_scalar = xmlrpc_scalar_create_double(type, val);

    return value;
}

XMLRPCValue *xmlrpc_create_struct_value(int type, void *arg)
{
    XMLRPCValue *value = xmlrpc_value_create();
    value->v_type = xr_struct;
    value->v_struct = list_create();

    return value;
}

int xmlrpc_parse_status(XMLRPCMethodCall *call)
{
    if (call == NULL)
	   return -1;

    return call->parse_status;
}

Octstr *xmlrpc_parse_error(XMLRPCMethodCall *call) 
{
    if (call == NULL)
        return NULL;
    
    return call->parse_error;
}

Octstr *xmlrpc_get_method_name(XMLRPCMethodCall *call)
{
    if (call == NULL)
        return NULL;

    return call->method_name;
}

int xmlrpc_call_len(XMLRPCMethodCall *call)
{
    if (call == NULL)
        return 0;

    return list_len(call->params);
}

int xmlrpc_get_type(XMLRPCMethodCall *call, int pos)
{
    XMLRPCValue *var;

    if (call == NULL)
        return -1;

    var = list_get(call->params, pos);
    return var->v_type;
}


/*-------------------------------------------------
 * Internal parser functions
 */


static int parse_member_element(xmlDocPtr doc, xmlNodePtr node, 
                                XMLRPCMethodCall **msg)
{
    Octstr *name;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_MEMBER_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(member_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_MEMBER_ELEMENTS) {
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                            "in XML source at level <member>", 
                                            octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   name [0]
     *   value [1]
     */
    if (i == 0) {
        /* this has been a <member> tag */
        /*if (parse_name(doc, node->xmlChildrenNode, msg) == -1)
            return -1; */
    } else {
        /* we should never be here */
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_member!");
        return -1;
    }
    return 0;
}


static int parse_member(xmlDocPtr doc, xmlNodePtr node, 
                        XMLRPCMethodCall **msg)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_member_element(doc, node, msg);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
	   /*
	    * XML has also many other node types, these are not needed with 
	    * XML-RPC. Therefore they are assumed to be an error.
	    */
        default:
            (*msg)->parse_status = XMLRPC_PARSING_FAILED;
            (*msg)->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
	   if (parse_member(doc, node->next, msg) == -1)
            return -1;

    return status;
}


static int parse_struct_element(xmlDocPtr doc, xmlNodePtr node, 
                                XMLRPCMethodCall **msg)
{
    Octstr *name;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_STRUCT_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(struct_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_STRUCT_ELEMENTS) {
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                            "in XML source at level <struct>", 
                                            octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   member [0]
     */
    if (i == 0) {
        /* this has been a <member> tag */
        if (parse_member(doc, node->xmlChildrenNode, msg) == -1)
            return -1;
    } else {
        /* we should never be here */
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_struct!");
        return -1;
    }
    return 0;
}


static int parse_struct(xmlDocPtr doc, xmlNodePtr node, 
                        XMLRPCMethodCall **msg)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_struct_element(doc, node, msg);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
	   /*
	    * XML has also many other node types, these are not needed with 
	    * XML-RPC. Therefore they are assumed to be an error.
	    */
        default:
            (*msg)->parse_status = XMLRPC_PARSING_FAILED;
            (*msg)->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
	   if (parse_struct(doc, node->next, msg) == -1)
            return -1;

    return status;
}


static int parse_value_element(xmlDocPtr doc, xmlNodePtr node, 
                               XMLRPCMethodCall **msg)
{
    Octstr *name;
    Octstr *value;
    long lval = 0;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_VALUE_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(value_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_VALUE_ELEMENTS) {
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                            "in XML source at level <value>", 
                                            octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    value = octstr_create(xmlNodeListGetString(doc, node->xmlChildrenNode, 1));
    if (value == NULL) {
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: no value for '%s'", 
                                            node->name);
        return -1;
    }

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   i4, int
     *   boolean
     *   string
     *   double
     *   dateTime.iso8601
     *   base64
     *   struct
     *   array
     */
    switch (value_elements[i].s_type) {
        
        /*
         * scalar types
         */
        case xr_int:
            if (octstr_parse_long(&lval, value, 0, 10) < 0) {
                (*msg)->parse_status = XMLRPC_PARSING_FAILED;
                (*msg)->parse_error = octstr_format("XML-RPC compiler: could not parse int value '%s'", 
                                                    octstr_get_cstr(value));
                return -1;
            }
            /* xmlrpc_call_add_int((*msg), lval); */
            xmlrpc_call_add_scalar((*msg), xr_int, (void *) lval);
            debug("", 0, "XML-RPC: added int %ld", lval);
            break;
        
        case xr_bool:
            if (octstr_parse_long(&lval, value, 0, 10) < 0) {
                (*msg)->parse_status = XMLRPC_PARSING_FAILED;
                (*msg)->parse_error = octstr_format("XML-RPC compiler: could not parse boolean value '%s'", 
                                                    octstr_get_cstr(value));
                return -1;
            }
            /* xmlrpc_call_add_bool((*msg), (int) lval); */
            xmlrpc_call_add_scalar((*msg), xr_bool, (void *) lval);
            debug("", 0, "XML-RPC: added boolean %d", (int) lval);
            break;

        case xr_string:
            /* xmlrpc_call_add_string((*msg), value); */
            xmlrpc_call_add_scalar((*msg), xr_string, (void *) value);
            debug("", 0, "XML-RPC: added string %s", octstr_get_cstr(value));
            break;

        case xr_date:
            /* xmlrpc_call_add_date((*msg), value); */
            xmlrpc_call_add_scalar((*msg), xr_date, (void *) value);
            debug("", 0, "XML-RPC: added date %s", octstr_get_cstr(value));
            break;

        case xr_base64:
            /* xmlrpc_call_add_base64((*msg), value); */
            xmlrpc_call_add_scalar((*msg), xr_base64, (void *) value);
            debug("", 0, "XML-RPC: added base64 %s", octstr_get_cstr(value));
            break;

        case xr_struct:
            if (parse_struct(doc, node->xmlChildrenNode, msg) == -1) {
                debug("", 0, "%s", octstr_get_cstr((*msg)->parse_error));
                /* (*msg)->parse_status = XMLRPC_PARSING_FAILED; */
                /* (*msg)->parse_error = octstr_format("XML-RPC compiler: could not parse struct"); */
                return -1;
            }
            /* debug("", 0, "XML-RPC: added struct %s", octstr_get_cstr(value)); */
            break;

        case xr_double:
        default:
            (*msg)->parse_status = XMLRPC_PARSING_FAILED;
            (*msg)->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_value!");
            return -1;
            break;
    }

    octstr_destroy(value);
    return 0;
}


static int parse_value(xmlDocPtr doc, xmlNodePtr node, 
                       XMLRPCMethodCall **msg)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_value_element(doc, node, msg);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
	   /*
	    * XML has also many other node types, these are not needed with 
	    * XML-RPC. Therefore they are assumed to be an error.
	    */
        default:
            (*msg)->parse_status = XMLRPC_PARSING_FAILED;
            (*msg)->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
	   if (parse_value(doc, node->next, msg) == -1)
            return -1;

    return status;
}


static int parse_param_element(xmlDocPtr doc, xmlNodePtr node, 
                               XMLRPCMethodCall **msg)
{
    Octstr *name;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_PARAM_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(param_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_PARAM_ELEMENTS) {
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                            "in XML source at level <param>", 
                                            octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   value [0]
     */
    if (i == 0) {
        /* this has been a <param> tag */
        if (parse_value(doc, node->xmlChildrenNode, msg) == -1)
            return -1;
    } else {
        /* we should never be here */
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_param!");
        return -1;
    }
    return 0;
}


static int parse_param(xmlDocPtr doc, xmlNodePtr node, 
                       XMLRPCMethodCall **msg, int *n)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:

            /* a <param> can only have one value element type */
            if ((*n) > 0) {
                (*msg)->parse_status = XMLRPC_PARSING_FAILED;
                (*msg)->parse_error = octstr_format("XML-RPC compiler: param may only have one value!");
                return -1;
            }

            status = parse_param_element(doc, node, msg);
            (*n)++;
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
	   /*
	    * XML has also many other node types, these are not needed with 
	    * XML-RPC. Therefore they are assumed to be an error.
	    */
        default:
            (*msg)->parse_status = XMLRPC_PARSING_FAILED;
            (*msg)->parse_error = octstr_format("XML-RPC compiler: Unknown XML node in the XML-RPC source.");
            return -1;
            break;
    }
   
    if (node->next != NULL)
	   if (parse_param(doc, node->next, msg, n) == -1)
            return -1;

    return status;
}


static int parse_params_element(xmlDocPtr doc, xmlNodePtr node, 
                                XMLRPCMethodCall **msg)
{
    Octstr *name;
    size_t i;
    int n = 0;

    /*
     * check if the element is allowed at this level
     * within <params> we only have one or more <param>
     */
    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_PARAMS_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(params_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_PARAMS_ELEMENTS) {
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' "
                                            "in XML source at level <params>", 
                                            octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   param [0]
     */
    if (i == 0) {
        /* this has been a <param> tag */
        if (parse_param(doc, node->xmlChildrenNode, msg, &n) == -1)
            return -1;
    } else {
        /* we should never be here */
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: bogus parsing exception in parse_params!");
        return -1;
    }
    return 0;
}


static int parse_params(xmlDocPtr doc, xmlNodePtr node, 
                        XMLRPCMethodCall **msg)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_params_element(doc, node, msg);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
	   /*
	    * XML has also many other node types, these are not needed with 
	    * XML-RPC. Therefore they are assumed to be an error.
	    */
        default:
            (*msg)->parse_status = XMLRPC_PARSING_FAILED;
            (*msg)->parse_error = octstr_format("XML-RPC compiler: unknown XML node in XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
	   if (parse_params(doc, node->next, msg) == -1)
            return -1;

    return status;
}


static int parse_methodcall_element(xmlDocPtr doc, xmlNodePtr node, 
                                    XMLRPCMethodCall **msg)
{
    Octstr *name;
    Octstr *value;
    size_t i;

    /*
     * check if the element is allowed at this level 
     */
    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_METHODCALL_ELEMENTS) {
        if (octstr_case_compare(name, octstr_imm(methodcall_elements[i].name)) == 0)
            break;
        ++i;
    }
    if (i == NUMBER_OF_METHODCALL_ELEMENTS) {
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: unknown tag '%s' in XML source "
                                            "at level <methodCall>", octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    } 
    octstr_destroy(name);

    /* 
     * now check which type it is and process 
     *
     * valid tags at this level are:
     *   methodCall [0]
     *   params     [1]
     */
    if (i == 0) {
        /* this has been the <methodName> tag */
        value = octstr_create(xmlNodeListGetString(doc, node->xmlChildrenNode, 1));
                
        /* destroy current msg->method_name and redefine */
        octstr_destroy((*msg)->method_name);
        (*msg)->method_name = octstr_duplicate(value);
        octstr_destroy(value);

    } else {
        /* 
         * ok, this has to be an <params> tag, otherwise we would 
         * have returned previosly
         */
        if (parse_params(doc, node->xmlChildrenNode, msg) == -1)
            return -1;
    }
    return 0;
}


static int parse_methodcall(xmlDocPtr doc, xmlNodePtr node, 
                            XMLRPCMethodCall **msg)
{
    int status = 0;
  
    /* call for the parser function of the node type. */
    switch (node->type) {

        case XML_ELEMENT_NODE:
            status = parse_methodcall_element(doc, node, msg);
            break;

        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
            /* Text nodes, comments and PIs are ignored. */
            break;
	   /*
	    * XML has also many other node types, these are not needed with 
	    * XML-RPC. Therefore they are assumed to be an error.
	    */
        default:
            (*msg)->parse_status = XMLRPC_PARSING_FAILED;
            (*msg)->parse_error = octstr_format("XML-RPC compiler: unknown XML node "
                                                "in the XML-RPC source.");
            return -1;
            break;
    }

    if (node->next != NULL)
	   if (parse_methodcall(doc, node->next, msg) == -1)
            return -1;

    return status;
}

static int parse_document(xmlDocPtr document, XMLRPCMethodCall **msg)
{
    xmlNodePtr node;
    Octstr *name;

    node = xmlDocGetRootElement(document);

    /*
     * check if this is at least a valid root element
     */
    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }
    if (octstr_case_compare(name, octstr_imm("METHODCALL")) != 0) {
        (*msg)->parse_status = XMLRPC_PARSING_FAILED;
        (*msg)->parse_error = octstr_format("XML-RPC compiler: wrong root element <%s>, "
                                            "<methodCall> expected!", 
                                            octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    }
    octstr_destroy(name);

    return parse_methodcall(document, node->xmlChildrenNode, msg);
}


