/*
 * wap_push_si_compiler.c: Implementation of wap_push_si_compiler.h interface.
 * SI DTD is defined in si, chapter 8.2.
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#include <ctype.h>
#include <xmlmemory.h>
#include <tree.h>
#include <debugXML.h>
#include <encoding.h>

#include "shared.h"
#include "xml_shared.h"
#include "wap_push_si_compiler.h"

/****************************************************************************
 *
 * Following global variables are unique to the SI compiler. See si, chapter 
 * 9.3.
 *
 * Two token table types, one and two token fields
 */

struct si_2table_t {
    char *name;
    unsigned char token;
};

typedef struct si_2table_t si_2table_t;

/*
 * Value part can mean part or whole of the value. It can be NULL, too, which
 * means that no part of the value will be tokenised. See si, chapter 9.3.2.
 */
struct si_3table_t {
    char *name;
    char *value_part;
    unsigned char token;
};

typedef struct si_3table_t si_3table_t;

/*
 * Si binary. We do not implement string table (we send SI using SMS, so si
 * document musr be very short).
 */

struct si_binary_t {
    unsigned char wbxml_version;
    unsigned char si_public_id;
    unsigned long charset;
    Octstr *si_binary;
};

typedef struct si_binary_t si_binary_t;

/*
 * Elements from tag code page zero. These are defined in si, chapter 9.3.1.
 */

static si_2table_t si_elements[] = {
    { "si", 0x05 },
    { "indication", 0x06 },
    { "info", 0x07 },
    { "item", 0x08 }
};

#define NUMBER_OF_ELEMENTS sizeof(si_elements)/sizeof(si_elements[0])

/*
 * Attributes (and start or whole value of ) from attribute code page zero. 
 * These are defined in si, chapter 9.3.2.
 */

static si_3table_t si_attributes[] = {
    { "action", "signal-none", 0x05 },
    { "action", "signal-low", 0x06 },
    { "action", "signal-medium", 0x07 },
    { "action", "signal-high", 0x08 },
    { "action", "delete", 0x09 },
    { "created", NULL, 0x0a },
    { "href", "https://www.", 0x0f },
    { "href", "http://www.", 0x0d },
    { "href", "https://", 0x0e },
    { "href", "http://", 0x0c },
    { "href", NULL, 0x0b },
    { "si-expires", NULL, 0x10 },
    { "si-id", NULL, 0x11 },
    { "class", NULL, 0x12 }
};

#define NUMBER_OF_ATTRIBUTES sizeof(si_attributes)/sizeof(si_attributes[0])

/*
 * Attribute value tokes (URL value codes), from si, chapter 9.3.3.
 */

static si_2table_t si_URL_values[] = {
  { ".com/", 0x85 },
  { ".edu/", 0x86 },
  { ".net/", 0x87 },
  { ".org/", 0x88 }
};

#define NUMBER_OF_URL_VALUES sizeof(si_URL_values)/sizeof(si_URL_values[0])

#include "xml_definitions.h"

/****************************************************************************
 *
 * Prototypes of internal functions. Note that 'Ptr' means here '*'.
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
			  si_binary_t **si_binary);
static int parse_node(xmlNodePtr node, si_binary_t **sibxml);    
static int parse_element(xmlNodePtr node, si_binary_t **sibxml);
static si_binary_t *si_binary_create(void);
static void si_binary_destroy(si_binary_t *sibxml);
static void si_binary_output(Octstr *os, si_binary_t *sibxml);
static int parse_text(xmlNodePtr node, si_binary_t **sibxml);   
static void parse_inline_string(Octstr *temp, si_binary_t **sibxml);
static int parse_cdata(xmlNodePtr node, si_binary_t **sibxml);  
static void parse_end(si_binary_t **sibxml);                     
static int parse_attribute(xmlAttrPtr attr, si_binary_t **sibxml);
static void output_char(int byte, si_binary_t **sibxml);         
static void output_octet_string(Octstr *os, si_binary_t **sibxml);
static int url(int hex);   
static int action(int hex);
static int date(int hex);
static void parse_octet_string(Octstr *ostr, si_binary_t **sibxml);
static Octstr *tokenize_date(Octstr *date);
static void octstr_drop_trailing_zeros(Octstr **date_token);
static void parse_url_value(Octstr *value, si_binary_t **sibxml);
static void flag_date_length(Octstr **token);
                          
/****************************************************************************
 *
 * Implementation of the external function
 */

int si_compile(Octstr *si_doc, Octstr *charset, Octstr **si_binary)
{
    si_binary_t *sibxml;
    int ret;
    xmlDocPtr pDoc;
    size_t size;
    char *si_c_text;

    *si_binary = octstr_create(""); 
    sibxml = si_binary_create();

    octstr_strip_blanks(si_doc);
    set_charset(si_doc, charset);
    size = octstr_len(si_doc);
    si_c_text = octstr_get_cstr(si_doc);
    pDoc = xmlParseMemory(si_c_text, size);

    ret = 0;
    if (pDoc) {
        ret = parse_document(pDoc, charset, &sibxml);
        si_binary_output(*si_binary, sibxml);
        xmlFreeDoc(pDoc);
    } else {
        xmlFreeDoc(pDoc);
        octstr_destroy(*si_binary);
        si_binary_destroy(sibxml);
        error(0, "SI: No document to parse. Probably an error in SI source");
        return -1;
    }

    si_binary_destroy(sibxml);

    return ret;
}

/****************************************************************************
 *
 * Implementation of internal functions
 *
 * Parse document node. Store si version number, public identifier and char-
 * acter set into the start of the document. FIXME: Add parse_prologue!
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
                          si_binary_t **sibxml)
{
    xmlNodePtr node;

    (**sibxml).wbxml_version = 0x02; /* WBXML Version number 1.2  */
    (**sibxml).si_public_id = 0x05; /* SI 1.0 Public ID */
    
    charset = octstr_create("UTF-8");
    (**sibxml).charset = parse_charset(charset);
    octstr_destroy(charset);

    node = xmlDocGetRootElement(document);
    return parse_node(node, sibxml);
}

static si_binary_t *si_binary_create(void)
{
    si_binary_t *sibxml;

    sibxml = gw_malloc(sizeof(si_binary_t));
    
    sibxml->wbxml_version = 0x00;
    sibxml->si_public_id = 0x00;
    sibxml->charset = 0x00;
    sibxml->si_binary = octstr_create("");

    return sibxml;
}

static void si_binary_destroy(si_binary_t *sibxml)
{
    if (sibxml == NULL)
        return;

    octstr_destroy(sibxml->si_binary);
    gw_free(sibxml);
}

/*
 * Output the sibxml content field after field into octet string os. See si,
 * chapter 10, for an annotated example. We add string table length 0 (no 
 * string table) before the content.
 */
static void si_binary_output(Octstr *os, si_binary_t *sibxml)
{
    gw_assert(octstr_len(os) == 0);
    octstr_format_append(os, "%c", sibxml->wbxml_version);
    octstr_format_append(os, "%c", sibxml->si_public_id);
    octstr_append_uintvar(os, sibxml->charset);
    octstr_format_append(os, "%c", 0x00);
    octstr_format_append(os, "%S", sibxml->si_binary);
}

/*
 * Parse an element node. Check if there is a token for an element tag; if not
 * output the element as a string, else ouput the token. After that, call 
 * attribute parsing functions
 * Returns:      1, add an end tag (element node has no children)
 *               0, do not add an end tag (it has children)
 *              -1, an error occurred
 */
static int parse_element(xmlNodePtr node, si_binary_t **sibxml)
{
    Octstr *name;
    size_t i;
    unsigned char status_bits,
             si_hex;
    int add_end_tag;
    xmlAttrPtr attribute;

    name = octstr_create(node->name);
    if (octstr_len(name) == 0) {
        octstr_destroy(name);
        return -1;
    }

    i = 0;
    while (i < NUMBER_OF_ELEMENTS) {
        if (octstr_compare(name, octstr_imm(si_elements[i].name)) == 0)
            break;
        ++i;
    }

    status_bits = 0x00;
    si_hex = 0x00;
    add_end_tag = 0;

    if (i != NUMBER_OF_ELEMENTS) {
        si_hex = si_elements[i].token;
        if ((status_bits = element_check_content(node)) > 0) {
	    si_hex = si_hex | status_bits;
	    
	    if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
	        add_end_tag = 1;
        }
        output_char(si_hex, sibxml);
    } else {
        warning(0, "unknown tag %s in SI source", octstr_get_cstr(name));
        si_hex = WBXML_LITERAL;
        if ((status_bits = element_check_content(node)) > 0) {
	    si_hex = si_hex | status_bits;
	    /* If this node has children, the end tag must be added after 
	       them. */
	    if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
		add_end_tag = 1;
	}
	output_char(si_hex, sibxml);
        output_octet_string(octstr_duplicate(name), sibxml);
    }

    if (node->properties != NULL) {
	attribute = node->properties;
	while (attribute != NULL) {
	    parse_attribute(attribute, sibxml);
	    attribute = attribute->next;
	}
	parse_end(sibxml);
    }

    octstr_destroy(name);
    return add_end_tag;
}

/*
 * Parse a text node of a si document. Ignore empty text nodes (space addi-
 * tions to certain points will produce these). Si codes text nodes as an
 * inline string.
 */

static int parse_text(xmlNodePtr node, si_binary_t **sibxml)
{
    Octstr *temp;

    temp = create_octstr_from_node(node);

    octstr_shrink_blanks(temp);
    octstr_strip_blanks(temp);

    if (octstr_len(temp) == 0) {
        octstr_destroy(temp);
        return 0;
    }

    parse_inline_string(temp, sibxml);    
    octstr_destroy(temp);

    return 0;
}

/*
 * Add global tokens to the start and to the end of an inline string.
 */ 
static void parse_inline_string(Octstr *temp, si_binary_t **sibxml)
{
    Octstr *startos;   

    octstr_insert(temp, startos = octstr_format("%c", WBXML_STR_I), 0);
    octstr_destroy(startos);
    octstr_format_append(temp, "%c", WBXML_STR_END);
    parse_octet_string(temp, sibxml);
}

/*
 * Tokenises an attribute, and in most cases, the start of its value (some-
 * times whole of it). Tokenisation is based on tables in si, chapters 9.3.2
 * and 9.3.3. 
 * Returns 0 when success, -1 when error.
 */
static int parse_attribute(xmlAttrPtr attr, si_binary_t **sibxml)
{
    Octstr *name,
           *value,
           *valueos,
           *tokenized_date;
    unsigned char si_hex;
    size_t i,
           value_len;

    name = octstr_create(attr->name);

    if (attr->children != NULL)
	value = create_octstr_from_node(attr->children);
    else 
	value = NULL;

    if (value == NULL)
        goto error;

    i = 0;
    valueos = NULL;
    while (i < NUMBER_OF_ATTRIBUTES) {
        if (octstr_compare(name, octstr_imm(si_attributes[i].name)) == 0) {
	    if (si_attributes[i].value_part == NULL) {
	        break; 
            } else {
                value_len = octstr_len(valueos = 
                    octstr_imm(si_attributes[i].value_part));
	        if (octstr_ncompare(value, valueos, value_len) == 0) {
		    break;
                }
            }
        }
       ++i;
    }

    if (i == NUMBER_OF_ATTRIBUTES)
        goto error;

    tokenized_date = NULL;
    si_hex = si_attributes[i].token;
    if (action(si_hex)) {
        output_char(si_hex, sibxml);
    } else if (url(si_hex)) {
        output_char(si_hex, sibxml);
        octstr_delete(value, 0, octstr_len(valueos));
        parse_url_value(value, sibxml);
    } else if (date(si_hex)) {
        if ((tokenized_date = tokenize_date(value)) == NULL)
            goto error;
        output_char(si_hex, sibxml);
        output_octet_string(tokenized_date, sibxml);
    } else {
        output_char(si_hex, sibxml);
        parse_inline_string(value, sibxml);
    }  

    octstr_destroy(tokenized_date);
    octstr_destroy(name);
    octstr_destroy(value);
    return 0;

error:
    octstr_destroy(name);
    octstr_destroy(value);
    return -1;
}

/*
 * Si documents does not contain variables
 */
static void parse_octet_string(Octstr *os, si_binary_t **sibxml)
{
    output_octet_string(os, sibxml);
}

/*
 * checks whether a si attribute value is an URL or some other kind of value. 
 * Returns 1 for an URL and 0 otherwise.
 */

static int url(int hex)
{
    switch ((unsigned char) hex) {
    case 0x0b:            /* href */
    case 0x0c: case 0x0e: /* href http://, href https:// */
    case 0x0d: case 0x0f: /* href http://www., href https://www. */
	return 1;
    }
    return 0;
}

/*
 * checks whether a si attribute value is an action attribute or some other 
 * kind of value. 
 * Returns 1 for an action attribute and 0 otherwise.
 */

static int action(int hex)
{
    switch ((unsigned char) hex) {
    case 0x05: case 0x06: /* action signal-none, action signal-low */
    case 0x07: case 0x08: /* action signal-medium, action signal-high */
    case 0x09:            /* action delete */
	return 1;
    }
    return 0;
}

/*
 * checks whether a si attribute value is an OSI date or some other kind of 
 * value. 
 * Returns 1 for an action attribute and 0 otherwise.
 */

static int date(int hex)
{
    switch ((unsigned char) hex) {
    case 0x0a: case 0x10: /* created, si-expires */
	return 1;
    }
    return 0;
}

/*
 * Tokenises an OSI date. Procedure is defined in si, chapter 9.2.2. Validate
 * OSI date as specified in 9.2.1.1. Returns NULL when error, a tokenised date 
 * string otherwise.
 */
static Octstr *tokenize_date(Octstr *date)
{
    Octstr *date_token;
    long j;
    size_t i,
           date_len;
    unsigned char c;

    if (!parse_date(date)) {
        return NULL;
    }

    date_token = octstr_create("");
    octstr_append_char(date_token, WBXML_OPAQUE);

    i = 0;
    j = 0;
    date_len = octstr_len(date);
    while (i < date_len) {
        c = octstr_get_char(date, i);
        if (c != 'T' && c != 'Z' && c != '-' && c != ':') {
            if (isdigit(c)) {
                octstr_set_bits(date_token, 4*j + 8, 4, c & 0x0f);
                ++j;
            } else {
                octstr_destroy(date_token);
                return NULL;
            }
        }  
        ++i; 
    }

    octstr_drop_trailing_zeros(&date_token);
    flag_date_length(&date_token);

    return date_token;
}

static void octstr_drop_trailing_zeros(Octstr **date_token)
{
    while (1) {
        if (octstr_get_char(*date_token, octstr_len(*date_token) - 1) == '\0')
            octstr_delete(*date_token, octstr_len(*date_token) - 1, 1);
        else
            return;
    }
}

static void flag_date_length(Octstr **token)
{
    Octstr *lenos;

    lenos = octstr_format("%c", octstr_len(*token) - 1);
    octstr_insert(*token, lenos, 1);

    octstr_destroy(lenos);
}

/*
 * In the case of SI document, only attribute values to be tokenised are parts
 * of urls. See si, chapter 9.3.3. The caller removes the start of the url.
 * Check whether we can find one of tokenisable values in value. If not, parse
 * value as a inline string, else parse parts before and after the tokenisable
 * url value as a inline string.
 */
static void parse_url_value(Octstr *value, si_binary_t **sibxml)
{
    size_t i;
    long pos;
    Octstr *urlos,
           *first_part,
	   *last_part;
    size_t first_part_len;

    i = 0;
    first_part_len = 0;
    first_part = NULL;
    last_part = NULL;
    while (i < NUMBER_OF_URL_VALUES) {
        pos = octstr_search(value, 
            urlos = octstr_imm(si_URL_values[i].name), 0);
        if (pos >= 0) {
	    first_part = octstr_duplicate(value);
            octstr_delete(first_part, pos, octstr_len(first_part) - pos);
            first_part_len = octstr_len(first_part);
            parse_inline_string(first_part, sibxml);
            output_char(si_URL_values[i].token, sibxml);
            last_part = octstr_duplicate(value);
            octstr_delete(last_part, 0, first_part_len + octstr_len(urlos));
            parse_inline_string(last_part, sibxml);
	    octstr_destroy(first_part);
            octstr_destroy(last_part);
            break;
        }
        octstr_destroy(urlos);
        ++i;
    }

    if (pos < 0) 
	parse_inline_string(value, sibxml);
        
}

/*
 * The recursive parsing function for the parsing tree. Function checks the 
 * type of the node, calls for the right parse function for the type, then 
 * calls itself for the first child of the current node if there's one and 
 * after that calls itself for the next child on the list.
 */

static int parse_node(xmlNodePtr node, si_binary_t **sibxml)
{
    int status = 0;
    
    /* Call for the parser function of the node type. */
    switch (node->type) {
    case XML_ELEMENT_NODE:
	status = parse_element(node, sibxml);
	break;
    case XML_TEXT_NODE:
	status = parse_text(node, sibxml);
	break;
    case XML_CDATA_SECTION_NODE:
	status = parse_cdata(node, sibxml);
	break;
    case XML_COMMENT_NODE:
    case XML_PI_NODE:
	/* Comments and PIs are ignored. */
	break;
	/*
	 * XML has also many other node types, these are not needed with 
	 * WML. Therefore they are assumed to be an error.
	 */
    default:
	error(0, "WML compiler: Unknown XML node in the WML source.");
	return -1;
	break;
    }

    /* 
     * If node is an element with content, it will need an end tag after it's
     * children. The status for it is returned by parse_element.
     */
    switch (status) {
    case 0:

	if (node->children != NULL)
	    if (parse_node(node->children, sibxml) == -1)
		return -1;
	break;
    case 1:
	if (node->children != NULL)
	    if (parse_node(node->children, sibxml) == -1)
		return -1;
	parse_end(sibxml);
	break;

    case -1: /* Something went wrong in the parsing. */
	return -1;
    default:
	error(0,
	      "WML compiler: undefined return value in a parse function.");
	return -1;
	break;
    }

    if (node->next != NULL)
	if (parse_node(node->next, sibxml) == -1)
	    return -1;

    return 0;
}

static void parse_end(si_binary_t **sibxml)
{
    output_char(WBXML_END, sibxml);
}
	
static void output_octet_string(Octstr *os, si_binary_t **sibxml)
{
    octstr_insert((**sibxml).si_binary, os, octstr_len((**sibxml).si_binary));
}

static void output_char(int byte, si_binary_t **sibxml)
{
    octstr_append_char((**sibxml).si_binary, byte);
}

/*
 * Cdata section parsing function. Output this "as it is"
 */

static int parse_cdata(xmlNodePtr node, si_binary_t **sibxml)
{
    int ret = 0;
    Octstr *temp;

    temp = create_octstr_from_node(node);

    parse_octet_string(temp, sibxml);
    
    octstr_destroy(temp);

    return ret;
}











