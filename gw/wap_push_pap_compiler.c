/*
 * wap_push_pap_compiler.c - implementation of wap_push_pap_compiler.h inter-
 * face.
 *
 * Compiler can be used by PI or PPG (it will handle all possible PAP DTD 
 * elements). It checks that attribute values are legal and that an element 
 * has only legal attributes, but does not otherwise validate PAP documents 
 * against PAP DTD. (XML validation is quite another matter, of course.) 
 * Client address is parsed out from the relevant PAP message attribute 
 * containing lots of additional data, see PPG, 7.1. We do not yet support 
 * user defined addresses.
 *
 * By Aarno Syvänen for Wapit Ltd.
 */

#include <xmlmemory.h>
#include <parser.h>
#include <tree.h>
#include <debugXML.h>
#include <encoding.h>
#include <ctype.h>

#include "wap_push_pap_compiler.h"
#include "wap_push_ppg.h"

/****************************************************************************
 *
 * Global data structures
 *
 * Table for pap elements. These are defined in PAP, Chapter 9.
 */
static char *pap_elements[] = {
    "pap",
    "push-message",
    "address",
    "quality-of-service",
    "push-response",
    "progress-note",
    "response-result",
    "cancel-message",
    "cancel-result",
    "cancel-response",
    "resultnotification-message",
    "resultnotification-response",
    "statusquery-message",
    "statusquery-response",
    "statusquery-result",
    "ccq-message",
    "ccq-response",
    "badmessage-response"
};

#define NUM_ELEMENTS sizeof(pap_elements)/sizeof(pap_elements[0])

/*
 * Table for PAP attributes. These are defined in PAP, Chapter 9.
 */
struct pap_attributes_t {
    char *name;
    char *value;
};

typedef struct pap_attributes_t pap_attributes_t;

static pap_attributes_t pap_attributes[] = {
    { "product-name", NULL },
    { "push-id", NULL },
    { "deliver-before-timestamp", NULL },
    { "deliver-after-timestamp", NULL },
    { "source-reference", NULL },
    { "progress-notes-requested", "true" },
    { "progress-notes-requested", "false" },
    { "address-value", NULL },
    { "priority", "high" },
    { "priority", "medium" },
    { "priority", "low" },
    { "delivery-method", "confirmed" },
    { "delivery-method", "preferconfirmed" },
    { "delivery-method", "unconfirmed" },
    { "delivery-method", "notspecified" },
    { "network", NULL },
    { "network-required", "true" },
    { "network-required", "false" },
    { "bearer", NULL },
    { "bearer-required", "true" },
    { "bearer-required", "false" },
    { "sender-address", NULL },
    { "sender-name", NULL },
    { "reply-time", NULL },
    { "stage", NULL },
    { "note", NULL },
    { "time", NULL },
    { "code", NULL },
    { "desc", NULL },
    { "received-time", NULL },
    { "event-time", NULL },
    { "message-state", NULL },
    { "query-id", NULL },
    { "app-id", NULL },
    { "bad-message-fragment", NULL}
};

#define NUM_ATTRIBUTES sizeof(pap_attributes)/sizeof(pap_attributes[0])

/*
 * Tables of enumerations used as a pap attribute value field. These are defi-
 * ned (presently) in PAP 9.2.
 */
static int pap_requirements[] = {
    PAP_FALSE,
    PAP_TRUE
};

#define NUM_REQUIREMENTS sizeof(pap_requirements)/sizeof(pap_requirements[0])

static int pap_priorities[] = {
    PAP_HIGH,
    PAP_MEDIUM,
    PAP_LOW
};

#define NUM_PRIORITIES sizeof(pap_priorities)/sizeof(pap_priorities[0])

static int pap_delivery_methods[] = {
    PAP_CONFIRMED,
    PAP_PREFERCONFIRMED,
    PAP_UNCONFIRMED,
    PAP_NOT_SPECIFIED
};

#define NUM_DELIVERY_METHODS sizeof(pap_delivery_methods)/ \
                             sizeof(pap_delivery_methods[0])


/*
 * Message states defined by the protocol, see PAP, chapter 9.6.
 */
static int pap_states[] = {
    PAP_UNDELIVERABLE,
    PAP_PENDING,
    PAP_EXPIRED,
    PAP_DELIVERED,
    PAP_ABORTED,
    PAP_TIMEOUT,
    PAP_CANCELLED
};

#define NUM_STATES sizeof(pap_states)/sizeof(pap_states[0])

/*
 * PAP status codes are defined in PAP, chapters 9.13 - 9.14. 
 */
static int pap_codes[] = {
    PAP_ACCEPTED_FOR_PROCESSING,
    PAP_BAD_REQUEST, 
    PAP_FORBIDDEN,
    PAP_ADDRESS_ERROR,
    PAP_CAPABILITIES_MISMATCH,
    PAP_DUPLICATE_PUSH_ID,
    PAP_TRANSFORMATION_FAILURE,
    PAP_REQUIRED_BEARER_NOT_AVAILABLE,
    PAP_ABORT_USERPND
};

#define NUM_CODES sizeof(pap_codes)/sizeof(pap_codes[0])

/*
 * Possible bearer types. These are defined in WDP, appendix C.
 */
static char *pap_bearer_types[] = {
    "Any",
    "USSD",
    "SMS",
    "GUTS/R-Data",
    "CSD",
    "Packet Data",
    "GPRS",
    "CDPD",
    "FLEX",
    "SDS",
    "ReFLEX",
    "MPAK",
    "GHOST/R_DATA"
};

#define NUM_BEARER_TYPES sizeof(pap_bearer_types)/sizeof(pap_bearer_types[0])

/*
 * Possible network types. These are defined in WDP, appendix C.
 */

static char *pap_network_types[] = {
    "Any",
    "GSM",
    "ANSI-136",
    "IS-95 CDMA",
    "AMPS",
    "PDC",
    "IDEN",
    "Paging network",
    "PHS",
    "TETRA",
    "Mobitex",
};

#define NUM_NETWORK_TYPES sizeof(pap_network_types)/ \
                          sizeof(pap_network_types[0])

/****************************************************************************
 *
 * Prototypes of internal functions. Note that suffix 'Ptr' means '*'.
 */

static int parse_document(xmlDocPtr doc_p, WAPEvent **e);
static int parse_node(xmlNodePtr node, WAPEvent **e);  
static int parse_element(xmlNodePtr node, WAPEvent **e); 
static int parse_attribute(Octstr *element_name, xmlAttrPtr attribute, 
                           WAPEvent **e);
static int parse_attr_value(Octstr *element_name, Octstr *attr_name, 
                            Octstr *attr_value, WAPEvent **e);
static int set_attribute_value(Octstr *element_name, Octstr *attr_value, 
                               Octstr *attr_name, WAPEvent **e);
static Octstr *parse_date(Octstr *attr_value);
static int parse_code(Octstr *attr_value);
static Octstr *parse_bearer(Octstr *attr_value);
static Octstr *parse_network(Octstr *attr_value);
static int parse_requirement(Octstr *attr_value);
static int parse_priority(Octstr *attr_value);
static int parse_delivery_method(Octstr *attr_value);
static int parse_state(Octstr *attr_value);
static int parse_address(Octstr **attr_value);
static long parse_wappush_client_address(Octstr **address, long pos);
static long parse_ppg_specifier(Octstr **address, long pos);
static long parse_client_specifier(Octstr **address, long pos);
static long parse_constant(const char *field_name, Octstr **address, long pos);
static long parse_dom_fragment(Octstr **address, long pos);
static long drop_character(Octstr **address, long pos);
static long parse_type(Octstr **address, Octstr **type_value, long pos);
static long parse_ext_qualifiers(Octstr **address, long pos, 
                                 Octstr *type_value);
static long parse_global_phone_number(Octstr **address, long pos);
static long parse_ipv4(Octstr **address, long pos);
static long parse_ipv6(Octstr **address, long pos);
static long parse_escaped_value(Octstr **address, long pos); 
static Octstr *prepend_char(Octstr *address, unsigned char c);
static int qualifiers(Octstr *address, long pos, Octstr *type);
static long parse_qualifier_value(Octstr **address, long pos);
static long parse_qualifier_keyword(Octstr **address, long pos);
static long parse_ipv4_fragment(Octstr **address, long pos);
static long parse_ipv6_fragment(Octstr **address, long pos);
static int wina_bearer_identifier(Octstr *type_value);
static int create_peek_window(Octstr **address, long *pos);
static long rest_unescaped(Octstr **address, long pos);
static int issafe(Octstr **address, long pos);
static long accept_safe(Octstr **address, long pos);
static long accept_escaped(Octstr **address, long pos);
static long handle_two_terminators (Octstr **address, long pos, 
    unsigned char comma, unsigned char point, unsigned char c, 
    long fragment_parsed, long fragment_length);

/*
 * Macro for creating an octet string from a node content. This has two 
 * versions for different libxml node content implementation methods.
 */
#ifdef XML_USE_BUFFER_CONTENT
#define create_octstr_from_node(node) (octstr_create(node->content->content))
#else
#define create_octstr_from_node(node) (octstr_create(node->content))
#endif

/****************************************************************************
 *
 * Implementation of the external function. Note that entities in the DTD 
 * are parameter entities and they can appear only in DTD (See site http:
 * //www.w3.org/TR/REC-xml, Chapter 4.1). So we do not need to worry about
 * them in the document itself.
 *
 * Returns 0, when success
 *        -1, when a non-implemented pap feature is asked for
 *        -2, when error
 * In addition, returns a newly created wap event corresponding the pap 
 * control message, if success, wap event NULL otherwise.
 */

int pap_compile(Octstr *pap_content, WAPEvent **e)
{
    xmlDocPtr doc_p;
    size_t oslen;
    int ret;

    if (octstr_search_char(pap_content, '\0', 0) != -1) {
        warning(0, "PUSH_PAP: compiler: pap source contained a \\0 character");
        return -2;
    }

    octstr_strip_blanks(pap_content);
    oslen = octstr_len(pap_content);
    doc_p = xmlParseMemory(octstr_get_cstr(pap_content), oslen);
    if (doc_p == NULL) 
        goto error;

    if ((ret = parse_document(doc_p, e)) < 0) 
        goto parserror;
    xmlFreeDoc(doc_p);

    return 0;

parserror:
    xmlFreeDoc(doc_p);
    wap_event_destroy(*e);
    *e = NULL;
    return ret;

error:
    warning(0, "PUSH_PAP: pap compiler: parse error in pap source");
    xmlFreeDoc(doc_p);
    wap_event_destroy(*e);
    *e = NULL;
    return -2;
}

/****************************************************************************
 *
 * Implementation of internal functions
 *
 * Parse the document node of libxml syntax tree. FIXME: Add parsing of pap
 * version.
 * 
 * Returns 0, when success
 *        -1, when a non-implemented pap feature is requested
 *        -2, when error
 * In addition, return a newly created wap event corresponding the pap 
 * control message, if success, or partially parsed pap document, if not.
 */
static int parse_document(xmlDocPtr doc_p, WAPEvent **e)
{
    xmlNodePtr node;
    int ret;

    gw_assert(doc_p);
    node = xmlDocGetRootElement(doc_p);
    ret = parse_node(node, e);
    return ret;
}

/*
 * Parse node of the syntax tree. DTD, as defined in PAP, chapter 9, contains
 * only elements (entities are restricted to DTDs). 
 *
 * Returns 0, when success
 *        -1, when a non-implemented feature is requested
 *        -2, when error
 * In addition, return a newly created wap event containing attributes from
 * pap document node, if success; partially parsed node, if not. 
 */
static int parse_node(xmlNodePtr node, WAPEvent **e)
{
    int ret;

    switch (node->type) {
    case XML_COMMENT_NODE:        /* ignore comments and pi nodes */
    case XML_PI_NODE:
    break;

    case XML_ELEMENT_NODE:
        if ((ret = parse_element(node, e)) < 0)
            return ret;
    break;

    default:
        error(0, "PUSH_PAP: pap compiler: Unknown XML node in PAP source");
        return -2;
    }

    if (node->children != NULL)
	if ((ret = parse_node(node->children, e)) < 0)
	    return ret;

    if (node->next != NULL)
        if ((ret = parse_node(node->next, e)) < 0)
	    return ret;
    
    return 0;
}

/*
 * Parse elements of PAP source. 
 * Returns 0, when success
 *        -1, when a non-implemented feature is requested
 *        -2, when error
 * In addition, return a newly created wap event containing attributes from the
 * element, if success; containing some unparsed attributes, if not.
 */
static int parse_element(xmlNodePtr node, WAPEvent **e)
{
    Octstr *name;
    xmlAttrPtr attribute;
    long i;
    int ret;

    name = octstr_create(node->name);

    i = 0;
    while (i < NUM_ELEMENTS) {
        if (octstr_compare(name, octstr_imm(pap_elements[i])) == 0)
            break;
        ++i;
    }

    if (i == NUM_ELEMENTS)
        return -2;

    if (node->properties != NULL) {
        attribute = node->properties;
        while (attribute != NULL) {
	    if ((ret = parse_attribute(name, attribute, e)) < 0) {
	        octstr_destroy(name);
                return ret;
            }
            attribute = attribute->next;
        }
    }

    octstr_destroy(name);

    return 0;                     /* If we reach this point, our node does not
                                     have any attributes left (or it had no 
                                     attributes to start with). This is *not* 
                                     an error. */
}

/*
 * Parse attribute updates corresponding fields of the  wap event. Check that 
 * both attribute name and value are papwise legal. If value is enumerated, 
 * legal values are stored in the attributes table. Otherwise, call a separate
 * parsing function. 
 * 
 * Returns 0, when success
 *        -1, when a non-implemented feature is requested
 *        -2, when error
 * In addition, return a newly created wap event containing parsed attribute
 * from pap sorce, if successfull, unparsed otherwise.
 */
static int parse_attribute(Octstr *element_name, xmlAttrPtr attribute, 
                           WAPEvent **e)
{
    Octstr *attr_name, *value, *nameos;
    long i;
    int ret;

    nameos = octstr_imm("erroneous");
    attr_name = octstr_create(attribute->name);
    if (attribute->children != NULL)
        value = create_octstr_from_node(attribute->children);
    else
        value = NULL;

    i = 0;
    while (i < NUM_ATTRIBUTES) {
        if (octstr_compare(attr_name, nameos = 
                           octstr_imm(pap_attributes[i].name)) == 0)
	    break;
        ++i;
    }

    if (i == NUM_ATTRIBUTES)
        goto error;

/*
 * Parse an attribute (it is, check cdata is has for a value) that is *not* an
 * enumeration. Legal values are defined in PAP, chapter 9. 
 */
    if (pap_attributes[i].value == NULL) {
        ret = parse_attr_value(element_name, attr_name, value, e);
	goto parsed;
    }

    while (octstr_compare(attr_name, nameos) == 0) {
        if (octstr_compare(value, octstr_imm(pap_attributes[i].value)) == 0)
	    break;
        ++i;
    }

    if (octstr_compare(attr_name, nameos) != 0)
        goto ok;

/*
 * Check that the value of the attribute is one enumerated for this attribute
 * in PAP, chapter 9.
 */
    if (set_attribute_value(element_name, value, attr_name, e) == -1)
        goto error;

    octstr_destroy(attr_name);
    octstr_destroy(value);

    return 0;

error:
    octstr_destroy(attr_name);
    octstr_destroy(value);
    return -2;

parsed:
    octstr_destroy(attr_name);
    octstr_destroy(value);
    return ret;

ok:
    octstr_destroy(attr_name);
    octstr_destroy(value);
    return 0;
}

/*
 * Validates non-enumeration attributes and stores their value to a newly
 * created wap event e. (Even when attribute value parsing was not success-
 * full)
 * Some values are just validated - their value is not used by the event. 
 * Character data does not always require validation. Value types of attribu-
 * tes are defined in PAP, chapter 9. Note that multiple addresses are not yet
 * supported.
 *
 * Returns 0, when success,
 *        -1, when a non-implemented feature requested.
 *        -2, when an error
 * In addition, returns the newly created wap event containing attribute value
 * from pap source, if successfull; filler value if not (mandatory fields of 
 * a wap event must be non-NULL).
 */
static int parse_attr_value(Octstr *element_name, Octstr *attr_name, 
                            Octstr *attr_value, WAPEvent **e)
{
    Octstr *ros;
    int ret;

    ret = -2;

/*
 * Do not create multiple events
 */
    if (octstr_compare(element_name, octstr_imm("push-message")) == 0 &&
            *e == NULL) {         
        *e = wap_event_create(Push_Message); 
    } else if (octstr_compare(element_name, octstr_imm("push-response")) == 0 
            && *e == NULL) {
        *e = wap_event_create(Push_Response);
    } else if (octstr_compare(element_name, octstr_imm("progress-note")) == 0 
            && *e == NULL) {
        *e = wap_event_create(Progress_Note);
    } 

    if (octstr_compare(element_name, octstr_imm("push-message")) == 0) {
        if (octstr_compare(attr_name, octstr_imm("push-id")) == 0) {
	    if ((ros = octstr_duplicate(attr_value)) != NULL) {
	        (**e).u.Push_Message.pi_push_id = ros;
                return 0;
            } else {
	        (**e).u.Push_Message.pi_push_id = octstr_imm("erroneous");
	        return -2;
            }
        } else if (octstr_compare(attr_name, 
                                 octstr_imm("deliver-before-timestamp"))== 0) {
	    if ((ros = parse_date(attr_value)) != NULL) {
	        (**e).u.Push_Message.deliver_before_timestamp = 
                     octstr_duplicate(ros);
                return 0;
            } else
	        return -2;
        } else if (octstr_compare(attr_name, 
                                 octstr_imm("deliver-after-timestamp")) == 0) {
	    if ((ros = parse_date(attr_value)) != NULL) {
	        (**e).u.Push_Message.deliver_after_timestamp = 
                     octstr_duplicate(ros);
                return 0;
            } else
	        return -2;
        } else if (octstr_compare(attr_name, 
                                 octstr_imm("source-reference")) == 0) {
	    if ((ros = octstr_duplicate(attr_value)) != NULL) {
	        (**e).u.Push_Message.source_reference = ros;
		return 0;
            } else
	        return -2;
        } else if (octstr_compare(attr_name, 
                                 octstr_imm("ppg-notify-requested-to")) == 0) {
	    if ((ros = octstr_duplicate(attr_value)) != NULL) {
	        (**e).u.Push_Message.ppg_notify_requested_to = ros;
                return 0;
            } else
	        return -2;
        }
    
    } else if (octstr_compare(element_name, octstr_imm("address")) == 0) {
        if (octstr_compare(attr_name, octstr_imm("address-value")) == 0) {
	    (**e).u.Push_Message.address_value = 
                 (ret = parse_address(&attr_value)) == 0 ? 
                 octstr_duplicate(attr_value) : octstr_imm("not successfull");
            return ret;
        } 

    } else if (octstr_compare(element_name, 
                             octstr_imm("quality-of-service")) == 0) {
        if (octstr_compare(attr_name, octstr_imm("network")) == 0) {
	    if ((ros = parse_network(attr_value)) != NULL) {
	        (**e).u.Push_Message.network = parse_network(attr_value);
                return 0;
            } else
	        return -2;
        }
        if (octstr_compare(attr_name, octstr_imm("bearer")) == 0) {
	    if ((ros = parse_bearer(attr_value)) != NULL) {
	        (**e).u.Push_Message.bearer = parse_bearer(attr_value);
                return 0;
            } else
	        return -2;
        }

    } else if (octstr_compare(element_name, 
                              octstr_imm("push-response")) == 0) {
        if (octstr_compare(attr_name, octstr_imm("push-id")) == 0) {
	    if ((ros = octstr_duplicate(attr_value)) != NULL) {
	        (**e).u.Push_Response.pi_push_id = ros;
                return 0;
            } else {
	      (**e).u.Push_Response.pi_push_id = octstr_imm("erroneous");
	        return -2;
            }
        } else if (octstr_compare(attr_name, octstr_imm("sender-name")) == 0) {
	    if ((ros = octstr_duplicate(attr_value)) != NULL) {
	        (**e).u.Push_Response.sender_name = ros;
                return 0;
            } else
	        return -2; 
        } else if (octstr_compare(attr_name, octstr_imm("reply-time")) == 0) {
	    if ((ros = parse_date(attr_value)) != NULL) {
	        (**e).u.Push_Response.reply_time = octstr_duplicate(ros);
                return 0;
            } else
	        return -2;
        } else if (octstr_compare(attr_name, octstr_imm("code")) == 0) {
	    (**e).u.Push_Response.code = 
                 (ret = parse_code(attr_value)) ? ret : 0;
            return ret;
        } else if (octstr_compare(attr_name, octstr_imm("desc")) == 0) {
	    if ((ros = octstr_duplicate(attr_value)) != NULL) {
	        (**e).u.Push_Response.desc = ros;
                return 0;
            } else
	        return -2;
        }

    } else if (octstr_compare(element_name, 
                             octstr_imm("progress-note")) == 0) {
        if (octstr_compare(attr_name, octstr_imm("stage")) == 0) {
	    (**e).u.Progress_Note.stage = 
                  (ret = parse_state(attr_value)) ? ret : 0;
            return ret;
        } else if (octstr_compare(attr_name, octstr_imm("note")) == 0) {
	    if ((ros = octstr_duplicate(attr_value)) != NULL) {
	        (**e).u.Progress_Note.note = ros;
                return 0;
            } else
	        return -2;
        } else if (octstr_compare(attr_name, octstr_imm("time")) == 0) {
	    if ((ros = parse_date(attr_value)) != NULL) {
	        (**e).u.Progress_Note.time = octstr_duplicate(ros);
                return 0;
            } else
	        return -2;
        }
    } 

    return -2; 
}

/*
 * Stores values of enumeration fields of a pap control message to wap event e.
 */
static int set_attribute_value(Octstr *element_name, Octstr *attr_value, 
                               Octstr *attr_name, WAPEvent **e)
{
    int ret;

    ret = -1;
    
    if (octstr_compare(element_name, octstr_imm("push-message")) == 0) {
        if (octstr_compare(attr_name, 
                          octstr_imm("progress-notes-requested")) == 0)
            (**e).u.Push_Message.progress_notes_requested = 
                 (ret = parse_requirement(attr_value)) >= 0 ? ret : 0;

    } else if (octstr_compare(element_name, 
			     octstr_imm("quality-of-service")) == 0) {
        if (octstr_compare(attr_name, octstr_imm("priority")) == 0)
            (**e).u.Push_Message.priority = 
                 (ret = parse_priority(attr_value)) >= 0 ? ret : 0;
        else if (octstr_compare(attr_name, octstr_imm("delivery-method")) == 0)
            (**e).u.Push_Message.delivery_method = 
                 (ret = parse_delivery_method(attr_value)) >= 0 ? ret : 0;
        else if (octstr_compare(attr_name, 
                               octstr_imm("network-required")) == 0)
            (**e).u.Push_Message.network_required = 
                 (ret = parse_requirement(attr_value)) >= 0 ? ret : 0;
        else if (octstr_compare(attr_name, octstr_imm("bearer-required")) == 0)
            (**e).u.Push_Message.bearer_required = 
                 (ret = parse_requirement(attr_value)) >= 0 ? ret : 0;
    }

    return ret;
}

static Octstr *parse_date(Octstr *attr_value)
{
    long date_value;

    if (octstr_parse_long(&date_value, attr_value, 0, 10) < 0)
        return NULL;
    if (octstr_parse_long(&date_value, attr_value, 5, 10) < 0)
        return NULL;
    if (date_value < 1 || date_value > 12)
        return NULL;
    if (octstr_parse_long(&date_value, attr_value, 8, 10) < 0)
        return NULL;
    if (date_value < 1 || date_value > 31)
        return NULL;
    if (octstr_parse_long(&date_value, attr_value, 11, 10) < 0)
        return NULL;
    if (date_value < 0 || date_value > 23)
        return NULL;
    if (octstr_parse_long(&date_value, attr_value, 14, 10) < 0)
        return NULL;
    if (date_value < 0 || date_value > 59)
        return NULL;
    if (date_value < 0 || date_value > 59)
        return NULL;
    if (octstr_parse_long(&date_value, attr_value, 17, 10) < 0)
        return NULL;

    return attr_value;
}

static int parse_code(Octstr *attr_value)
{
    long i,
         attr_as_number;

    attr_as_number = strtol(octstr_get_cstr(attr_value), NULL, 10);

    for (i = 0; i < NUM_CODES; i++) {
         if (pap_codes[i] == attr_as_number)
	     return i;
    }

    return -1;
}

static Octstr *parse_bearer(Octstr *attr_value)
{
    long i;
    Octstr *ros;

    for (i = 0; i < NUM_BEARER_TYPES; i++) {
         if (octstr_compare(attr_value, 
                 ros = octstr_imm(pap_bearer_types[i])) == 0)
	     return ros;
    }

    return NULL;
}

static Octstr *parse_network(Octstr *attr_value)
{
    long i;
    Octstr *ros;

    for (i = 0; i < NUM_NETWORK_TYPES; i++) {
         if (octstr_compare(attr_value, 
                 ros = octstr_imm(pap_network_types[i])) == 0)
	     return ros;
    }

    return NULL;
}

static int parse_requirement(Octstr *attr_value)
{
    long i,
         attr_as_number;

    attr_as_number = strtol(octstr_get_cstr(attr_value), NULL, 10);

    for (i = 0; i < NUM_REQUIREMENTS; i++) {
         if (pap_requirements[i] == attr_as_number)
	     return i;
    }

    return -1;
}

static int parse_priority(Octstr *attr_value)
{
    long i,
         attr_as_number;

    attr_as_number = strtol(octstr_get_cstr(attr_value), NULL, 10);

    for (i = 0; i < NUM_PRIORITIES; i++) {
         if (pap_priorities[i] == attr_as_number)
	     return i;
    }

    return -1;
}

static int parse_delivery_method(Octstr *attr_value)
{
    long i,
         attr_as_number;

    attr_as_number = strtol(octstr_get_cstr(attr_value), NULL, 10);

    for (i = 0; i < NUM_DELIVERY_METHODS; i++) {
         if (pap_delivery_methods[i] == attr_as_number) 
	     return i;
    }

    return -1;
}

static int parse_state(Octstr *attr_value)
{
    long i,
         attr_as_number;

    attr_as_number = strtol(octstr_get_cstr(attr_value), NULL, 10);

    for (i = 0; i < NUM_STATES; i++) {
         if (pap_states[i] == attr_as_number) 
	     return i;
    }

    return -1;
}

/*
 * Check legality of pap client address attribute and transform it to the 
 * client address usable in Kannel wap address tuple data type. The grammar 
 * for client address is specified in PPG, chapter 7.1.
 *
 * Returns:   0, when success
 *           -1, a non-implemented pap feature requested by pi
 *           -2, address parsing error  
 */

static int parse_address(Octstr **address)
{
    long pos;

    pos = octstr_len(*address) - 1;
/*
 * Delete first separator, if there is one. This will help our parsing later.
 */
    if (octstr_get_char(*address, 0) == '/')
        octstr_delete(*address, 0, 1);

    if ((pos = parse_ppg_specifier(address, pos)) < 0) {
        return -2;
    }

    pos = parse_wappush_client_address(address, pos);
    
    return pos;
}

static long parse_wappush_client_address(Octstr **address, long pos)
{
    if ((pos = parse_client_specifier(address, pos)) < 0) {
        return pos;
    }

    pos = parse_constant("WAPPUSH", address, pos);
    
    return pos;
}

/*
 * We are not interested of ppg specifier, but we must check its format.
 */
static long parse_ppg_specifier(Octstr **address, long pos)
{
    if (pos >= 0) {
        pos = parse_dom_fragment(address, pos);
    }

    while (octstr_get_char(*address, pos) != '@' && pos >= 0) {
        if (octstr_get_char(*address, pos) == '.') {
	    octstr_delete(*address, pos, 1);
            --pos;
        } else
	    return -2;

        pos = parse_dom_fragment(address, pos);
    } 

    pos = drop_character(address, pos);

    if (octstr_get_char(*address, pos) == '/' && pos >= 0) {
        octstr_delete(*address, pos, 1);
        if (pos > 0)
            --pos;
    }

    if (pos < 0)
       return -2;

    return pos;
}

static long parse_client_specifier(Octstr **address, long pos)
{
    Octstr *type_value;

    type_value = octstr_create("");

    if ((pos = parse_type(address, &type_value, pos)) < 0) {
        goto parse_error;
    }

    pos = drop_character(address, pos);

    if ((pos = parse_constant("/TYPE", address, pos)) < 0) {
        goto parse_error;
    }

    if (octstr_compare(type_value, octstr_imm("USER")) == 0)
        goto not_implemented;

    if ((pos = parse_ext_qualifiers(address, pos, type_value)) < 0)
        goto parse_error;

    if (octstr_compare(type_value, octstr_imm("PLMN")) == 0) {
        pos = parse_global_phone_number(address, pos);
    }

    else if (octstr_compare(type_value, octstr_imm("IPv4")) == 0) {
        pos = parse_ipv4(address, pos);
    }

    else if (octstr_compare(type_value, octstr_imm("IPv6")) == 0) {
        pos = parse_ipv6(address, pos);
    }

    else if (wina_bearer_identifier(type_value)) {
        pos = parse_escaped_value(address, pos); 
    }    

    else
        goto parse_error; 

    octstr_destroy(type_value);
    return pos;

not_implemented:
    octstr_destroy(type_value);
    return -1;

parse_error:
    octstr_destroy(type_value);
    return -2;
}

static long parse_constant(const char *field_name, Octstr **address, long pos)
{
    long i;    
    size_t size;

    size = strlen(field_name);
    i = 0;
    
    while (octstr_get_char(*address, pos - i)  == field_name[size-1 - i] && 
            i <  size) {
        ++i;
    }

    while (octstr_get_char(*address, pos) != field_name[0] && pos >= 0) {
        pos = drop_character(address, pos);
    }

    pos = drop_character(address, pos);    

    if (pos < 0 || i != size) {
        return -2;
    }

    return pos;
}

static long parse_dom_fragment(Octstr **address, long pos)
{
    unsigned char c;

    if (pos >= 0) { 
        if (isalnum(octstr_get_char(*address, pos))) {
	    pos = drop_character(address, pos);
        } else
	    return -2;
    }

    while ((c = octstr_get_char(*address, pos)) != '@' && 
               octstr_get_char(*address, pos) != '.' && pos >= 0)  {
        if (isalnum(c) || c == '-') {
	    pos = drop_character(address, pos);
        } else
	    return -2;
    } 

    return pos;
}

static long drop_character(Octstr **address, long pos)
{
    if (pos >= 0) {
        octstr_delete(*address, pos, 1);
        if (pos > 0)
            --pos;
    }

    return pos;
}

static long parse_type(Octstr **address, Octstr **type_value, long pos)
{
    unsigned char c;

    while ((c = octstr_get_char(*address, pos)) != '=' && pos >= 0) {   
        *type_value = prepend_char(*type_value, c);
        pos = drop_character(address, pos);
    } 

    if (pos < 0)
        return -2;

    return pos;
}

static long parse_ext_qualifiers(Octstr **address, long pos, 
                                 Octstr *type)
{
    while (qualifiers(*address, pos, type)) {
        if ((pos = parse_qualifier_value(address, pos)) < 0)
            return pos;

        if ((pos = parse_qualifier_keyword(address, pos)) < 0)
            return pos;
    }

    return pos;
}

static long parse_global_phone_number(Octstr **address, long pos)
{
    unsigned char c;

    while ((c = octstr_get_char(*address, pos)) != '+' && pos >= 0) {
        if (!isdigit(c) && c != '-' && c != '.')
             return -2;
        else
	     --pos;
    }

    if (pos > 0)
        --pos;

    pos = drop_character(address, pos);

    return pos;
}

static long parse_ipv4(Octstr **address, long pos)
{
    long i;

    if ((pos = parse_ipv4_fragment(address, pos)) < 0) 
        return -2;

    i = 1;

    while (i <= 3 && octstr_get_char(*address, pos) != '=' && pos >= 0) {
        pos = parse_ipv4_fragment(address, pos);
        ++i;
    }

    return pos;
}

static long parse_ipv6(Octstr **address, long pos)
{
    long i;

    if ((pos = parse_ipv6_fragment(address, pos)) < 0)
        return -2;

    i = 1;

    while (i <= 7 && pos >= 0) {
        pos = parse_ipv6_fragment(address, pos);
        ++i;
    }

    return pos;
}

/*
 * WINA web page does not include address type identifiers. Following ones are
 * from WDP, Appendix C.
 */

static char *bearer_address[] = {
    "GSM_MSISDN",
    "ANSI_136_MSISDN",
    "IS_637_MSISDN",
    "iDEN_MSISDN",
    "FLEX_MSISDN",
    "PHS_MSISDN",
    "GSM_Service_Code",
    "TETRA_ITSI",
    "TETRA_MSISDN",
    "ReFLEX_MSIDDN",
    "MAN",
};

static size_t bearer_address_size = sizeof(bearer_address) / 
                                    sizeof(bearer_address[0]);

static int wina_bearer_identifier(Octstr *type_value)
{
    long i;

    i = 0;
    while (i < bearer_address_size) {
        if (octstr_compare(type_value, octstr_imm(bearer_address[i])) == 0)
	    return 1;
        ++i;
    }

    return 0;
}

/*
 * Note that we parse backwards. First we create a window of three characters
 * (representing a possible escaped character). If the first character of the 
 * window is not escape, we handle the last character and move the window one
 * character backwards; if it is, we handle escaped sequence and create a new
 * window. If we cannot create a window, rest of characters are unescaped.
 */
static long parse_escaped_value(Octstr **address, long pos)
{
    int ret;

    if (create_peek_window(address, &pos) == 0)
         if ((pos = rest_unescaped(address, pos)) == -2)
             return -2;

    while (octstr_get_char(*address, pos) != '=' && pos >= 0) {
        if ((ret = issafe(address, pos)) == 1) {
	    pos = accept_safe(address, pos);

        } else if (ret == 0) {
	    if ((pos = accept_escaped(address, pos)) < 0)
                return -2;  
            if (create_peek_window(address, &pos) == 0)
                if ((pos = rest_unescaped(address, pos)) == -2)
                    return -2;
        }
    }

    pos = drop_character(address, pos);

    return pos;
}

static Octstr *prepend_char(Octstr *os, unsigned char c)
{
    Octstr *tmp;

    tmp = octstr_format("%c", c);
    octstr_insert(os, tmp, 0);
    octstr_destroy(tmp);
    return os;
}

/*
 * Ext qualifiers contain /, ipv4 address contains . , ipv6 address contains :.
 * phone number contains + and escaped-value contain no specific tokens. Lastly
 * mentioned are for future extansions, but we must parse them.
 */
static int qualifiers(Octstr *address, long pos, Octstr *type)
{
    unsigned char term,
         c;
    long i;

    i = pos;
    c = '+';

    if (octstr_compare(type, octstr_imm("PLMN")) == 0)
        term = '+';
    else if (octstr_compare(type, octstr_imm("IPv4")) == 0)
        term = '.';
    else if (octstr_compare(type, octstr_imm("IPv6")) == 0)
        term = ':';
    else
        term = 'N';

    if (term != 'N')
        while ((c = octstr_get_char(address, i)) != term) {
            if (c == '/')
                return 1;
            --i;
    }

    if (term == 'N') {
        while (i != 0) {
            if (c == '/')
                return 1;
            --i;
        }
    } 

    return 0;
}

static long parse_qualifier_value(Octstr **address, long pos)
{
    unsigned char c;

    while ((c = octstr_get_char(*address, pos)) != '=' && pos >= 0) {
        if (c < 0x20 || (c > 0x2e && c < 0x30) || (c > 0x3c && c < 0x3e) ||
            c > 0x7e)
            return -2;

        pos = drop_character(address, pos);
    }

    pos = drop_character(address, pos);
  
    return pos;
}

static long parse_qualifier_keyword(Octstr **address, long pos)
{
    unsigned char c;  

    while ((c = octstr_get_char(*address, pos)) != '/') {
        if (isalnum(c) || c == '-') {
	    pos = drop_character(address, pos);
        } else
	    return -2;
    }

    pos = drop_character(address, pos);       

    return pos;
}

static long parse_ipv4_fragment(Octstr **address, long pos)
{
    long i;
    unsigned char c;

    i = 0;
    c = '=';

    if (isdigit(octstr_get_char(*address, pos)) && pos >= 0) {
        --pos;
        ++i;
    } else {
        return -2;
    }
    
    while (i <= 3 && ((c = octstr_get_char(*address, pos)) != '.' &&  c != '=')
            && pos >= 0) {
        if (isdigit(c)) {
	    --pos;
            ++i;
        } else {
	    return -2;
        }
    }

    pos = handle_two_terminators(address, pos, '.', '=', c, i, 3);

    return pos;
}

static long parse_ipv6_fragment(Octstr **address, long pos)
{
    long i;
    unsigned char c;

    i = 0;

    if (isxdigit(octstr_get_char(*address, pos)) && pos >= 0) {
        --pos;
        ++i;
    } else {
        return -2;
    }

    c = '=';

    while (i <= 4 && ((c = octstr_get_char(*address, pos)) != ':' && c != '=')
            && pos >= 0) {
        if (isxdigit(c)) {
	    --pos;
            ++i;
        } else {
	    return -2;
        }
    }

    pos = handle_two_terminators(address, pos, ':', '=', c, i, 4);

    return pos;
}

/*
 * Return -1, it was impossible to create the window because of there is no
 * more enough characters left and 0 if OK.
 */
static int create_peek_window(Octstr **address, long *pos)
{
   long i;
    unsigned char c;

    i = 0;
    c = '=';
    while (i < 2 && (c = octstr_get_char(*address, *pos)) != '=') {
        if (*pos > 0)
            --*pos;
        ++i;
    }

    if (c == '=')
        return 0;

    return 1; 
}

static long rest_unescaped(Octstr **address, long pos)
{
    long i,
         ret;

    for (i = 2; i > 0; i--) {
         if ((ret = accept_safe(address, pos)) == -2)
	     return -2;
         else if (ret == -1)
	     return pos;
    }

    return pos;
}

static int issafe(Octstr **address, long pos)
{
    if (octstr_get_char(*address, pos) == '%')
        return 0;
    else
        return 1;
}

static long accept_safe(Octstr **address, long pos)
{
    unsigned char c;

    c = octstr_get_char(*address, pos);
    if ((isalnum(c) || c == '+' || c == '-' || c == '.' || c == '_') && 
            pos >= 0)
	--pos;
    else if (c == '=')
        return -1;
    else
        return -2;

    return pos;
}

static long accept_escaped(Octstr **address, long pos)
{
    Octstr *temp;
    long i;
    unsigned char c;

    pos = drop_character(address, pos);
    temp = octstr_create("");

    for (i = 2; i > 0; i--) {
        c = octstr_get_char(*address, pos + i);
        temp = prepend_char(temp, c);
        pos = drop_character(address, pos + i);
        if (pos > 0)
	  --pos;
    }

    if (octstr_hex_to_binary(temp) < 0) {
        octstr_destroy(temp);
        return -2;
    }

    octstr_insert(*address, temp, pos + 2);   /* To the end of the window */

    octstr_destroy(temp);
    return pos + 1;                           /* The position preceding the 
                                                 inserted character */
              
}

/*
 * Point ends the string to be parsed, comma separates its fragments.
 */
static long handle_two_terminators (Octstr **address, long pos, 
    unsigned char comma, unsigned char point, unsigned char c, 
    long fragment_parsed, long fragment_length)
{
    if (fragment_parsed == fragment_length && c != comma && c != point)
        return -2;

    if (c == point) 
        octstr_delete(*address, pos, 1);

    --pos;

    return pos;
}









