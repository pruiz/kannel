/*
 * xml_shared.c: Common functions of xml compilers (mainly charset handling 
 * and operations with wbxml binary not using a string table)
 *
 * By Tuomas Luttinen & Aarno Syvänen (for Wiral Ltd) 
 */

#include <ctype.h>

#include "xml_shared.h"
#include "xml_definitions.h"

#include <string.h>

struct charset_t {
    char *charset; 
    char *nro;
    unsigned char MIBenum;
};

charset_t character_sets[] = {
    { "ISO", "8859-1", 4 },
    { "ISO", "8859-2", 5 },
    { "ISO", "8859-3", 6 },
    { "ISO", "8859-4", 7 },
    { "ISO", "8859-5", 8 },
    { "ISO", "8859-6", 9 },
    { "ISO", "8859-7", 10 },
    { "ISO", "8859-8", 11 },
    { "ISO", "8859-9", 12 },
    { "UTF", "8", 106 },
    { NULL }
};

/**************************************************************************** 
 *
 * Implementation of external functions
 *
 * set_charset - set the charset of the http headers into the document, if 
 * it has no encoding set.
 */

void set_charset(Octstr *document, Octstr *charset)
{
    long gt = 0, enc = 0;
    Octstr *encoding = NULL, *text = NULL, *temp = NULL;

    if (octstr_len(charset) == 0)
	return;

    encoding = octstr_create(" encoding");
    enc = octstr_search(document, encoding, 0);
    gt = octstr_search_char(document, '>', 0);

    if (enc < 0 || enc > gt) {
	gt ++;
	text = octstr_copy(document, gt, octstr_len(document) - gt);
	if (charset_to_utf8(text, &temp, charset) >= 0) {
	    octstr_delete(document, gt, octstr_len(document) - gt);
	    octstr_append_data(document, octstr_get_cstr(temp), 
			       octstr_len(temp));
	}

	octstr_destroy(temp);
	octstr_destroy(text);
    }

    octstr_destroy(encoding);
}

/*
 * only_blanks - checks if a text node contains only white space, when it can 
 * be left out as a element content.
 */

int only_blanks(const char *text)
{
    int blank = 1;
    int j=0;
    int len = strlen(text);

    while ((j<len) && blank) {
	blank = blank && isspace((int)text[j]);
	j++;
    }
 
    return blank;
}

/*
 * Parses the character set of the document. 
 */

int parse_charset(Octstr *charset)
{
    Octstr *number = NULL;
    int i, j, cut = 0, ret = 0;

    /* The charset might be in lower case, so... */
    octstr_convert_range(charset, 0, octstr_len(charset), toupper);

    /*
     * The character set is handled in two parts to make things easier. 
     * The cutting.
     */
    if ((cut = octstr_search_char(charset, '_', 0)) > 0) {
	number = octstr_copy(charset, cut + 1, 
			     (octstr_len(charset) - (cut + 1)));
	octstr_truncate(charset, cut);
    } else if ((cut = octstr_search_char(charset, '-', 0)) > 0) {
	number = octstr_copy(charset, cut + 1, 
			     (octstr_len(charset) - (cut + 1)));
	octstr_truncate(charset, cut);
    }

    /* And table search. */
    for (i = 0; character_sets[i].charset != NULL; i++)
	if (octstr_str_compare(charset, character_sets[i].charset) == 0) {
	    for (j = i; 
		 octstr_str_compare(charset, character_sets[j].charset) == 0; 
		 j++)
		if (octstr_str_compare(number, character_sets[j].nro) == 0) {
		    ret = character_sets[j].MIBenum;
		    break;
		}
	    break;
	}

    /* UTF-8 is the default value */
    if (character_sets[i].charset == NULL)
	ret = character_sets[i-1].MIBenum;

    octstr_destroy(number);

    return ret;
}

/*
 * element_check_content - a helper function for parse_element for checking 
 * if an element has content or attributes. Returns status bit for attributes 
 * (0x80) and another for content (0x40) added into one octet.
 */

unsigned char element_check_content(xmlNodePtr node)
{
    unsigned char status_bits = 0x00;

    if ((node->children != NULL) && 
	!((node->children->next == NULL) && 
	  (node->children->type == XML_TEXT_NODE) && 
	  (only_blanks(node->children->content))))
	status_bits = WBXML_CONTENT_BIT;

    if (node->properties != NULL)
	status_bits = status_bits | WBXML_ATTR_BIT;

    return status_bits;
}


List *wml_charsets(void)
{
    int i;
    List *result;
    Octstr *charset;

    result = list_create();
    for (i = 0; character_sets[i].charset != NULL; i++) {
         charset = octstr_create(character_sets[i].charset);
         octstr_append_char(charset, '-');
         octstr_append(charset, octstr_imm(character_sets[i].nro));
         list_append(result, charset);
    }

    return result;  
}

/*
 * Functions working with simple binary data type (no string table). No 
 * variables are present either. 
 */

simple_binary_t *simple_binary_create(void)
{
    simple_binary_t *binary;

    binary = gw_malloc(sizeof(simple_binary_t));
    
    binary->wbxml_version = 0x00;
    binary->public_id = 0x00;
    binary->charset = 0x00;
    binary->binary = octstr_create("");

    return binary;
}

void simple_binary_destroy(simple_binary_t *binary)
{
    if (binary == NULL)
        return;

    octstr_destroy(binary->binary);
    gw_free(binary);
}

/*
 * Output the wbxml content field after field into octet string os. We add 
 * string table length 0 (meaning no string table) before the content.
 */
void simple_binary_output(Octstr *os, simple_binary_t *binary)
{
    gw_assert(octstr_len(os) == 0);
    octstr_format_append(os, "%c", binary->wbxml_version);
    octstr_format_append(os, "%c", binary->public_id);
    octstr_append_uintvar(os, binary->charset);
    octstr_format_append(os, "%c", 0x00);
    octstr_format_append(os, "%S", binary->binary);
}

void parse_end(simple_binary_t **binary)
{
    output_char(WBXML_END, binary);
}

void output_char(int byte, simple_binary_t **binary)
{
    octstr_append_char((**binary).binary, byte);
}

void parse_octet_string(Octstr *os, simple_binary_t **binary)
{
    output_octet_string(os, binary);
}

/*
 * Add global tokens to the start and to the end of an inline string.
 */ 
void parse_inline_string(Octstr *temp, simple_binary_t **binary)
{
    Octstr *startos;   

    octstr_insert(temp, startos = octstr_format("%c", WBXML_STR_I), 0);
    octstr_destroy(startos);
    octstr_format_append(temp, "%c", WBXML_STR_END);
    parse_octet_string(temp, binary);
}

void output_octet_string(Octstr *os, simple_binary_t **sibxml)
{
    octstr_insert((*sibxml)->binary, os, octstr_len((*sibxml)->binary));
}
