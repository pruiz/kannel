/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2004 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * ota_compiler.c: Tokenizes an ota settings or bookmarks document. DTD is 
 * defined in Over The Air Settings Specification  (hereafter called ota), 
 * chapter 6. (See http://www.americas.nokia.com/messaging/default.asp)
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#include <ctype.h>
#include <libxml/xmlmemory.h>
#include <libxml/tree.h>
#include <libxml/debugXML.h>
#include <libxml/encoding.h>

#include "shared.h"
#include "xml_shared.h"
#include "ota_compiler.h"

/****************************************************************************
 *
 * Global variables
 *
 * Two token table types, one and two token fields
 */

struct ota_2table_t {
    char *name;
    unsigned char token;
};

typedef struct ota_2table_t ota_2table_t;

/*
 * Ota tokenizes whole of attribute value, or uses an inline string. See ota, 
 * chapter 8.2.
 */
struct ota_3table_t {
    char *name;
    char *value;
    unsigned char token;
};

typedef struct ota_3table_t ota_3table_t;

/*
 * Elements from tag code page zero. These are defined in ota, chapter 8.1.
 */

static ota_2table_t ota_elements[] = {
    { "CHARACTERISTIC-LIST", 0x05 },
    { "CHARACTERISTIC", 0x06 },
    { "PARM", 0x07 }
};

#define NUMBER_OF_ELEMENTS sizeof(ota_elements)/sizeof(ota_elements[0])

/*
 * Attribute names and values from code page zero. These are defined in ota,
 * chapter 8.2. Some values are presented as inline strings; in this case 
 * value "INLINE" is used. (Note a quirk: there is an attribute with name 
 * "VALUE".)
 *
 * For a documentation of the single attributes see gw/ota_prov_attr.h.
 */

static ota_3table_t ota_attributes[] = {
    { "TYPE", "ADDRESS", 0x06 },
    { "TYPE", "URL", 0x07 },
    { "TYPE", "MMSURL", 0x7c },
    { "TYPE", "NAME", 0x08 },
    { "TYPE", "ID", 0x7d },
    { "TYPE", "BOOKMARK", 0x7f },
    { "NAME", "BEARER", 0x12 },
    { "NAME", "PROXY", 0x13 },
    { "NAME", "PORT", 0x14 },
    { "NAME", "NAME", 0x15 },
    { "NAME", "PROXY_TYPE", 0x16 },
    { "NAME", "URL", 0x17 },
    { "NAME", "PROXY_AUTHNAME", 0x18 },
    { "NAME", "PROXY_AUTHSECRET", 0x19 },
    { "NAME", "SMS_SMSC_ADDRESS", 0x1a },
    { "NAME", "USSD_SERVICE_CODE", 0x1b },
    { "NAME", "GPRS_ACCESSPOINTNAME", 0x1c },
    { "NAME", "PPP_LOGINTYPE", 0x1d },
    { "NAME", "PROXY_LOGINTYPE", 0x1e },
    { "NAME", "CSD_DIALSTRING", 0x21 },
    { "NAME", "CSD_CALLTYPE", 0x28 },
    { "NAME", "CSD_CALLSPEED", 0x29 },
    { "NAME", "PPP_AUTHTYPE", 0x22 },
    { "NAME", "PPP_AUTHNAME", 0x23 },
    { "NAME", "PPP_AUTHSECRET", 0x24 },
    { "NAME", "ISP_NAME", 0x7e },
    { "NAME", "INLINE", 0x10 },
    { "VALUE", "GSM/CSD", 0x45 },
    { "VALUE", "GSM/SMS", 0x46 },
    { "VALUE", "GSM/USSD", 0x47 },
    { "VALUE", "IS-136/CSD", 0x48 },
    { "VALUE", "GPRS", 0x49 },
    { "VALUE", "9200", 0x60 },
    { "VALUE", "9201", 0x61 },
    { "VALUE", "9202", 0x62 },
    { "VALUE", "9203", 0x63 },
    { "VALUE", "AUTOMATIC", 0x64 },
    { "VALUE", "MANUAL", 0x65 },
    { "VALUE", "AUTO", 0x6a },
    { "VALUE", "9600", 0x6b },
    { "VALUE", "14400", 0x6c },
    { "VALUE", "19200", 0x6d },
    { "VALUE", "28800", 0x6e },
    { "VALUE", "38400", 0x6f },
    { "VALUE", "PAP", 0x70 },
    { "VALUE", "CHAP", 0x71 },
    { "VALUE", "ANALOGUE", 0x72 },
    { "VALUE", "ISDN", 0x73 },
    { "VALUE", "43200", 0x74 },
    { "VALUE", "57600", 0x75 },
    { "VALUE", "MSISDN_NO", 0x76 },
    { "VALUE", "IPV4", 0x77 },
    { "VALUE", "MS_CHAP", 0x78 },
    { "VALUE", "INLINE", 0x11 }
};

#define NUMBER_OF_ATTRIBUTES sizeof(ota_attributes)/sizeof(ota_attributes[0])

#include "xml_definitions.h"

/****************************************************************************
 *
 * Prototypes of internal functions. Note that 'Ptr' means here '*'.
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
			  simple_binary_t **ota_binary);
static int parse_node(xmlNodePtr node, simple_binary_t **otabxml);    
static int parse_element(xmlNodePtr node, simple_binary_t **otabxml);
static int parse_attribute(xmlAttrPtr attr, simple_binary_t **otabxml); 
static int use_inline_string(Octstr *valueos);      

/***************************************************************************
 *
 * Implementation of the external function
 */

int ota_compile(Octstr *ota_doc, Octstr *charset, Octstr **ota_binary)
{
    simple_binary_t *otabxml;
    int ret;
    xmlDocPtr pDoc;
    size_t size;
    char *ota_c_text;

    *ota_binary = octstr_create(""); 
    otabxml = simple_binary_create();

    octstr_strip_blanks(ota_doc);
    octstr_shrink_blanks(ota_doc);
    set_charset(ota_doc, charset);
    size = octstr_len(ota_doc);
    ota_c_text = octstr_get_cstr(ota_doc);
    pDoc = xmlParseMemory(ota_c_text, size);

    ret = 0;
    if (pDoc) {
        ret = parse_document(pDoc, charset, &otabxml);
        simple_binary_output(*ota_binary, otabxml);
        xmlFreeDoc(pDoc);
    } else {
        xmlFreeDoc(pDoc);
        octstr_destroy(*ota_binary);
        simple_binary_destroy(otabxml);
        error(0, "OTA: No document to parse. Probably an error in OTA source");
        return -1;
    }

    simple_binary_destroy(otabxml);

    return ret;
}

/*****************************************************************************
 *
 * Implementation of internal functions
 *
 * Parse document node. Store wbmxl version number and character set into the 
 * start of the document. There are no wapforum public identifier for ota. 
 * FIXME: Add parse_prologue!
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
                          simple_binary_t **otabxml)
{
    xmlNodePtr node;

    (*otabxml)->wbxml_version = 0x01; /* WBXML Version number 1.1  */
    (*otabxml)->public_id = 0x01; /* Public id for an unknown document type */
    
    charset = octstr_create("UTF-8");
    (*otabxml)->charset = parse_charset(charset);
    octstr_destroy(charset);

    node = xmlDocGetRootElement(document);
    return parse_node(node, otabxml);
}

/*
 * The recursive parsing function for the parsing tree. Function checks the 
 * type of the node, calls for the right parse function for the type, then 
 * calls itself for the first child of the current node if there's one and 
 * after that calls itself for the next child on the list.
 */

static int parse_node(xmlNodePtr node, simple_binary_t **otabxml)
{
    int status = 0;
    
    /* Call for the parser function of the node type. */
    switch (node->type) {
    case XML_ELEMENT_NODE:
	status = parse_element(node, otabxml);
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
	error(0, "OTA compiler: Unknown XML node in the OTA source.");
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
	    if (parse_node(node->children, otabxml) == -1)
		return -1;
	break;
    case 1:
	if (node->children != NULL)
	    if (parse_node(node->children, otabxml) == -1)
		return -1;
	parse_end(otabxml);
	break;

    case -1: /* Something went wrong in the parsing. */
	return -1;
    default:
	warning(0,"OTA compiler: undefined return value in a parse function.");
	return -1;
	break;
    }

    if (node->next != NULL)
	if (parse_node(node->next, otabxml) == -1)
	    return -1;

    return 0;
}

/*
 * Parse an element node. Check if there is a token for an element tag; if not
 * output the element as a string, else output the token. After that, call 
 * attribute parsing functions
 * Returns:      1, add an end tag (element node has no children)
 *               0, do not add an end tag (it has children)
 *              -1, an error occurred
 */
static int parse_element(xmlNodePtr node, simple_binary_t **otabxml)
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

    i = 0;
    while (i < NUMBER_OF_ELEMENTS) {
        if (octstr_compare(name, octstr_imm(ota_elements[i].name)) == 0)
            break;
        ++i;
    }

    status_bits = 0x00;
    ota_hex = 0x00;
    add_end_tag = 0;

    if (i != NUMBER_OF_ELEMENTS) {
        ota_hex = ota_elements[i].token;
        if ((status_bits = element_check_content(node)) > 0) {
	    ota_hex = ota_hex | status_bits;
	    
	    if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
	        add_end_tag = 1;
        }
        output_char(ota_hex, otabxml);
    } else {
        warning(0, "unknown tag %s in OTA source", octstr_get_cstr(name));
        ota_hex = WBXML_LITERAL;
        if ((status_bits = element_check_content(node)) > 0) {
	    ota_hex = ota_hex | status_bits;
	    /* If this node has children, the end tag must be added after 
	       them. */
	    if ((status_bits & WBXML_CONTENT_BIT) == WBXML_CONTENT_BIT)
		add_end_tag = 1;
	}
	output_char(ota_hex, otabxml);
        output_octet_string(octstr_duplicate(name), otabxml);
    }

    if (node->properties != NULL) {
	attribute = node->properties;
	while (attribute != NULL) {
	    parse_attribute(attribute, otabxml);
	    attribute = attribute->next;
	}
	parse_end(otabxml);
    }

    octstr_destroy(name);
    return add_end_tag;
}

/*
 * Tokenises an attribute, and in most cases, its value. (Some values are re-
 * presented as an inline string). Tokenisation is based on tables in ota, 
 * chapters 8.1 and 8.2. 
 * Returns 0 when success, -1 when error.
 */
static int parse_attribute(xmlAttrPtr attr, simple_binary_t **otabxml)
{
    Octstr *name,
           *value,
           *valueos,
           *nameos;
    unsigned char ota_hex;
    size_t i;

    name = octstr_create(attr->name);

    if (attr->children != NULL)
	value = create_octstr_from_node(attr->children);
    else 
	value = NULL;

    if (value == NULL)
        goto error;

    i = 0;
    valueos = NULL;
    nameos = NULL;
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

    octstr_destroy(name);
    octstr_destroy(value);
    return 0;

error:
    octstr_destroy(name);
    octstr_destroy(value);
    return -1;
}

static int use_inline_string(Octstr *valueos)
{
    return octstr_compare(valueos, octstr_imm("INLINE")) == 0;
}






