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

static int parse_document(xmlDocPtr document, XMLRPCMethodCall **msg);
static int parse_node(xmlNodePtr node, XMLRPCMethodCall **msg);
static int parse_element(xmlNodePtr node, XMLRPCMethodCall **msg);
static int parse_attribute(xmlAttrPtr attr, XMLRPCMethodCall **msg);


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


static int parse_document(xmlDocPtr document, XMLRPCMethodCall **msg)
{
    xmlNodePtr node;
    xmlNodePtr next;
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
        error(0, "XML-RPC: wrong root element <%s>", octstr_get_cstr(name));
        octstr_destroy(name);
        return -1;
    }
    
    octstr_destroy(name);
    return parse_node(node, msg);
}

static int parse_node(xmlNodePtr node, XMLRPCMethodCall **msg)
{
    int status = 0;
    
    /* Call for the parser function of the node type. */
    switch (node->type) {
        case XML_ELEMENT_NODE:
	       status = parse_element(node, msg);
	       break;
        case XML_TEXT_NODE:
        case XML_COMMENT_NODE:
        case XML_PI_NODE:
	       /* Text nodes, comments and PIs are ignored. */
	       break;
	   /*
	    * XML has also many other node types, these are not needed with 
	    * OTA. Therefore they are assumed to be an error.
	    */
        default:
	       error(0, "XML-RPC compiler: Unknown XML node in the XML-RPC source.");
	       return -1;
	       break;
    }

    /* 
     * If node is an element with content, it will need an end tag after it's
     * children. The status for it is returned by parse_element.
     */
    switch (status) {
        case 0:
        case 1:
	       if (node->children != NULL)
	           if (parse_node(node->children, msg) == -1)
		          return -1;
	       break;
        case -1: 
            /* Something went wrong in the parsing. */
	       return -1;
        default:
	       warning(0,"XML-RPC compiler: undefined return value in a parse function.");
	       return -1;
	       break;
    }

    if (node->next != NULL)
	   if (parse_node(node->next, msg) == -1)
            return -1;

    return 0;
}

static int parse_element(xmlNodePtr node, XMLRPCMethodCall **msg)
{
    Octstr *name;
    size_t i;
    unsigned char status_bits,
             ota_hex;
    int add_end_tag;
    xmlAttrPtr attribute;

    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    debug("",0,"parse_element: name=%s", octstr_get_cstr(name));

    /*
    i = 0;
    while (i < NUMBER_OF_ELEMENTS) {
        if (octstr_compare(name, octstr_imm(ota_elements[i].name)) == 0)
            break;
        ++i;
    }
    */
    
    add_end_tag = 0;

    if (node->properties != NULL) {
        attribute = node->properties;
        while (attribute != NULL) {
            parse_attribute(attribute, msg);
            attribute = attribute->next;
        }
    }

    octstr_destroy(name);
    return add_end_tag;
}

static int parse_attribute(xmlAttrPtr attr, XMLRPCMethodCall **msg)
{
    Octstr *name,
           *value,
           *valueos,
           *nameos;
    unsigned char ota_hex;
    size_t i;

    name = octstr_create(attr->name);

    debug("",0,"parse_attribute: name=%s", octstr_get_cstr(name));

    if (attr->children != NULL)
        value = create_octstr_from_node(attr->children);
    else 
        value = NULL;

    if (value == NULL)
        goto error;

    i = 0;
    valueos = NULL;
    nameos = NULL;

    /*
    while (i < NUMBER_OF_ATTRIBUTES) {
	nameos = octstr_imm(ota_attributes[i].name);
        if (octstr_compare(name, nameos) == 0) {
	    if (ota_attributes[i].value != NULL) {
                valueos = octstr_imm(ota_attributes[i].value);
            }
            if (octstr_compare(value, valueos) == 0) {
	        break;
            }
            if (octstr_compare(valueos, octstr_imm("INLINE")) == 0) {
	        break;
            }
        }
       ++i;
    }

    if (i == NUMBER_OF_ATTRIBUTES) {
        warning(0, "unknown attribute %s in OTA source", 
                octstr_get_cstr(name));
        warning(0, "its value being %s", octstr_get_cstr(value));
        goto error;
    }

    ota_hex = ota_attributes[i].token;
    if (!use_inline_string(valueos)) {
        output_char(ota_hex, otabxml);
    } else {
        output_char(ota_hex, otabxml);
        parse_inline_string(value, otabxml);
    }  
    */

    octstr_destroy(name);
    octstr_destroy(value);
    return 0;

error:
    octstr_destroy(name);
    octstr_destroy(value);
    return -1;
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

    octstr_strip_blanks(post_body);
    octstr_shrink_blanks(post_body);
    size = octstr_len(post_body);
    body = octstr_get_cstr(post_body);
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

Octstr *xmlrpc_call_octstr(XMLRPCMethodCall *call)
{
    Octstr *body;
    XMLRPCValue *val;
    long i;
    
    body = octstr_format("<?xml version=\"1.0\"?>\n<methodCall>\n"
			 "    <methodName>%S</methodName>\n"
			 "    <params>", call->method_name);

    list_lock(call->params);
    for(i = 0; i < list_len(call->params); i++) {
	val = list_get(call->params, i);
	octstr_format_append(body, "\n    ");
	xmlrpc_value_print(val, body);
    }
    list_unlock(call->params);
    
    octstr_format_append(body, "\n    </params>\n"
			 "   </methodCall>\n");

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

    /* XXX these must be set according to XML-RPC specification,
    *      but Kannel is not user-agent... */
    http_header_add(headers, "User-Agent", "Kannel");
    http_header_add(headers, "Host", "localhost");

    body = xmlrpc_call_octstr(call);

    http_start_request(http_ref, url, headers, body, 0, ref, NULL);
    
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

void xmlrpc_scalar_print(XMLRPCScalar *scalar, Octstr *os)
{
    switch(scalar->s_type) {
    case xr_string:
	octstr_format_append(os, "%S", scalar->s_str);
	break;
    case xr_int:
	octstr_format_append(os, "<int>%d</int>", scalar->s_int);
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

