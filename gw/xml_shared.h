/*
 * xml_shared.h - Common xml tokenizer interface
 * This file contains mainly character set functions and binary manipulating
 * functions used with a binary without a string table.
 *
 * Tuomas Luttinen for Wapit Ltd and Aarno Syvänen for Wiral Ltd.
 */

#ifndef XML_SHARED_H
#define XML_SHARED_H

/*
 * Charset type is used by WML, SI and SL.
 */
typedef struct charset_t charset_t;

/*
 * XML binary type not containing a string table. This is used for SI and SL.
 */

typedef struct simple_binary_t simple_binary_t;

#include "gwlib/gwlib.h"

/*
 * XML binary type not containing a string table. This is used for SI and SL.
 */
struct simple_binary_t {
    unsigned char wbxml_version;
    unsigned char public_id;
    unsigned long charset;
    Octstr *binary;
};

/*
 * Prototypes of common functions. First functions common with wml, si and sl
 * compilers.
 *
 * set_charset - set the charset of the http headers into the document, if 
 * it has no encoding set.
 */
void set_charset(Octstr *document, Octstr *charset);

/*
 * element_check_content - a helper function for checking if an element has 
 * content or attributes. Returns status bit for attributes (0x80) and another
 * for content (0x40) added into one octet.
 */
unsigned char element_check_content(xmlNodePtr node);

/*
 * only_blanks - checks if a text node contains only white space, when it can 
 * be left out as a element content.
 */

int only_blanks(const char *text);

/*
 * Parses the character set of the document. 
 */
int parse_charset(Octstr *charset);

List *wml_charsets(void);

/*
 * Macro for creating an octet string from a node content. This has two 
 * versions for different libxml node content implementation methods. 
 */

#ifdef XML_USE_BUFFER_CONTENT
#define create_octstr_from_node(node) (octstr_create(node->content->content))
#else
#define create_octstr_from_node(node) (octstr_create(node->content))
#endif

#endif

/*
 * Functions working with simple binary type (no string table)
 */

simple_binary_t *simple_binary_create(void);

void simple_binary_destroy(simple_binary_t *bxml);

/*
 * Output the sibxml content field after field into octet string os. We add 
 * string table length 0 (no string table) before the content.
 */
void simple_binary_output(Octstr *os, simple_binary_t *bxml);

void parse_end(simple_binary_t **bxml);

void output_char(int byte, simple_binary_t **bxml);

void parse_octet_string(Octstr *os, simple_binary_t **bxml);

/*
 * Add global tokens to the start and to the end of an inline string.
 */ 
void parse_inline_string(Octstr *temp, simple_binary_t **bxml);

void output_octet_string(Octstr *os, simple_binary_t **bxml);




