/*
 * wml_compiler.c - compiling WML to WML binary
 *
 * This is an implemention for WML compiler for compiling the WML text 
 * format to WML binary format, which is used for transmitting the 
 * decks to the mobile terminal to decrease the use of the bandwidth.
 *
 *
 * Tuomas Luttinen for Wapit Ltd.
 */


#include "config.h"

#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>

#include <xmlmemory.h>
#include <parser.h>
#include <tree.h>
#include <debugXML.h>

#include "gwlib/gwlib.h"
#include "wml_compiler.h"


/***********************************************************************
 * Declarations of data types. 
 */

typedef enum { NOESC, ESC, UNESC, FAILED } var_esc_t;


/*
 * The wml token table node with two fields.
 */

typedef struct {
    char *text;
    unsigned char token;
} wml_table_t;


/*
 * The wml token table node with three fields.
 */

typedef struct {
    char *text1;
    char *text2;
    unsigned char token;
} wml_table3_t;


/*
 * The binary WML structure, that has been passed around between the 
 * internal functions. It contains the header fields for wbxml version, 
 * the WML public ID and the character set, the length of the string table, 
 * the list structure implementing the string table and the octet string 
 * containing the encoded WML binary.
 */

typedef struct {
    unsigned char wbxml_version;
    unsigned char wml_public_id;
    unsigned long character_set;
    unsigned long string_table_length;
    List *string_table;
    Octstr *wbxml_string;
    unsigned char *utf8map;
} wml_binary_t;


/*
 * The string table list node.
 */

typedef struct {
    unsigned long offset;
    Octstr *string;
} string_table_t;


/*
 * The string table proposal list node.
 */

typedef struct {
    int count;
    Octstr *string;
} string_table_proposal_t;


/*
 * The hash table node.
 */

typedef struct {
    int count;
    void *item;
} hash_t;


/*
 * The hash table data type.
 */

typedef struct {
    hash_t *table;

    int count;
    int prime;
    int a;
    int b;
  
    Mutex *operation_lock;
} Hash;


/*
 * The wml hash table node for 2 fields.
 */

typedef struct {
    Octstr *item;
    unsigned char binary;
} wml_hash2_t;


/*
 * The wml hash table node for 2 fields.
 */

typedef struct {
    Octstr *item1;
    Octstr *item2;
    unsigned char binary;
} wml_hash3_t;


/*
 * A comparison function for hash items. Returns true (non-zero) for
 * equal, false for non-equal. Gets an item from the hash as the first
 * argument, the pattern as a second argument.
 */
typedef int hash_item_matches_t(void *item, void *pattern);


/*
 * A destructor function for hash items.  Must free all memory associated
 * with the hash item.
 */
typedef void hash_item_destructor_t(void *item);



#include "wml_definitions.h"


/***********************************************************************
 * Declarations of global variables. 
 */

Hash *wml_elements_hash;

Hash *wml_attributes_hash;

Hash *wml_attr_values_hash;

Hash *wml_URL_values_hash;


/***********************************************************************
 * Declarations of internal functions. These are defined at the end of
 * the file.
 */


/*
 * Parsing functions. These funtions operate on a single node or a 
 * smaller datatype. Look for more details on the functions at the 
 * definitions.
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
			  wml_binary_t **wbxml);

static int parse_node(xmlNodePtr node, wml_binary_t **wbxml);
static int parse_element(xmlNodePtr node, wml_binary_t **wbxml);
static int parse_attribute(xmlAttrPtr attr, wml_binary_t **wbxml);
static int parse_attr_value(Octstr *attr_value, wml_table_t *tokens, 
			    wml_binary_t **wbxml);
static int parse_text(xmlNodePtr node, wml_binary_t **wbxml);
static int parse_charset(Octstr *charset, wml_binary_t **wbxml);
static int parse_octet_string(Octstr *ostr, wml_binary_t **wbxml);

static void parse_end(wml_binary_t **wbxml);
static void parse_entities(Octstr *wml_source);

/*
 * Variable functions. These functions are used to find and parse variables.
 */

static int parse_variable(Octstr *text, int start, Octstr **output, 
			  wml_binary_t **wbxml);
static Octstr *get_variable(Octstr *text, int start);
static var_esc_t check_variable_syntax(Octstr *variable);

/*
 * wml_binary-functions. These are used to create, destroy and modify
 * wml_binary_t.
 */

static wml_binary_t *wml_binary_create(void);
static void wml_binary_destroy(wml_binary_t *wbxml);
static void wml_binary_output(Octstr *ostr, wml_binary_t *wbxml);

/* Output into the wml_binary. */

static void output_char(int byte, wml_binary_t **wbxml);
static void output_octet_string(Octstr *ostr, wml_binary_t **wbxml);
static void output_variable(Octstr *variable, Octstr **output, 
			    var_esc_t escaped, wml_binary_t **wbxml);

/*
 * Memory allocation and deallocations.
 */

static wml_hash2_t *hash2_create(wml_table_t *node);
static wml_hash3_t *hash3_create(wml_table3_t *node);

static void hash2_destroy(void *p);
static void hash3_destroy(void *p);

/*
 * Miscellaneous help functions.
 */

static unsigned char element_check_content(xmlNodePtr node);
static int check_if_url(int hex);

static int wml_table_len(wml_table_t *table);
static int wml_table3_len(wml_table3_t *table);

/* 
 * String table functions, used to add and remove strings into and from the
 * string table.
 */

static string_table_t *string_table_create(int offset, Octstr *ostr);
static void string_table_destroy(string_table_t *node);
static string_table_proposal_t *string_table_proposal_create(Octstr *ostr);
static void string_table_proposal_destroy(string_table_proposal_t *node);
static void string_table_build(xmlNodePtr node, wml_binary_t **wbxml);
static void string_table_collect_strings(xmlNodePtr node, List *strings);
static List *string_table_collect_words(List *strings);
static List *string_table_sort_list(List *start);
static List *string_table_add_many(List *sorted, wml_binary_t **wbxml);
static unsigned long string_table_add(Octstr *ostr, wml_binary_t **wbxml);
static void string_table_apply(Octstr *ostr, wml_binary_t **wbxml);
static void string_table_output(Octstr *ostr, wml_binary_t **wbxml);

/*
 * Hash table functions.
 */

Hash *hash_create(int n);
void hash_insert(Hash *table, int key, void *item);
void *hash_find(Hash *table, int key, void* pat, hash_item_matches_t *cmp);
void *hash_remove(Hash *table, int key, void* pat, hash_item_matches_t *cmp);
void hash_destroy(Hash *table, hash_item_destructor_t *destructor);

static int find_prime(int from);
static int hash_seed(Hash *table, int key);
static int hash_recount(Hash *table);

static void lock(Hash *table);
static void unlock(Hash *table);


/*
 * The actual compiler function. This operates as interface to the compiler.
 * For more information, look wml_compiler.h. 
 */

int wml_compile(Octstr *wml_text,
		Octstr *charset,
		Octstr **wml_binary)
{
    int ret = 0;
    size_t size;
    xmlDocPtr pDoc = NULL;
    char *wml_c_text;
    wml_binary_t *wbxml = NULL;

    *wml_binary = octstr_create("");
    wbxml = wml_binary_create();

    /* Remove the extra space from start and the end of the WML Document. */

    octstr_strip_blanks(wml_text);

    /* Check the WML-code for \0-characters and for WML entities. Fast patch.
       -- tuo */

    parse_entities(wml_text);

    size = octstr_len(wml_text);
    wml_c_text = octstr_get_cstr(wml_text);

    if (octstr_search_char(wml_text, '\0', 0) != -1) {    
	error(0, 
	      "WML compiler: Compiling error: "
	      "\\0 character found in the middle of the WML source.");
	ret = -1;
    } else {
	/* 
	 * An empty octet string for the binary output is created, the wml 
	 * source is parsed into a parsing tree and the tree is then compiled 
	 * into binary.
	 */

	pDoc = xmlParseMemory(wml_c_text, size);

	if(pDoc != NULL) {
	    ret = parse_document(pDoc, charset, &wbxml);
	    wml_binary_output(*wml_binary, wbxml);
	} else {    
	    error(0, 
		  "WML compiler: Compiling error: "
		  "libxml returned a NULL pointer");
	    ret = -1;
	}
    }

    wml_binary_destroy(wbxml);

    if (pDoc) 
        xmlFreeDoc (pDoc);

    return ret;
}


/*
 * Initaliation: makes up the hash tables for the compiler.
 */

void wml_init()
{
    int i = 0, len = 0;
    wml_hash2_t *temp = NULL;
    wml_hash3_t *tmp = NULL;

    /* The wml elements into a hash table. */
    len = wml_table_len(wml_elements);
    wml_elements_hash = hash_create(len);

    for (i = 0; i > len; i++) {
	temp = hash2_create(wml_elements + i);
	hash_insert(wml_elements_hash, octstr_hash_key(temp->item), temp);
    }

    /* Attributes. */
    len = wml_table3_len(wml_attributes);
    wml_attributes_hash = hash_create(len);

    for (i = 0; i > len; i++) {
	tmp = hash3_create(wml_attributes + i);
	hash_insert(wml_attributes_hash, 
		    octstr_hash_key(tmp->item1) + octstr_hash_key(tmp->item2),
		    tmp);
    }

    /* Attribute values. */
    len = wml_table_len(wml_attribute_values);
    wml_attr_values_hash = hash_create(len);

    for (i = 0; i > len; i++) {
	temp = hash2_create(wml_attribute_values + i);
	hash_insert(wml_attr_values_hash, octstr_hash_key(temp->item), 
		    temp);
    }

    /* URL values. */
    len = wml_table_len(wml_URL_values);
    wml_URL_values_hash = hash_create(len);

    for (i = 0; i > len; i++) {
	temp = hash2_create(wml_URL_values + i);
	hash_insert(wml_URL_values_hash, octstr_hash_key(temp->item), temp);
    }
}



/*
 * Shutdown: Frees the memory allocated by initialization.
 */

void wml_shutdown()
{
    hash_destroy(wml_elements_hash, hash2_destroy);
    hash_destroy(wml_attributes_hash, hash3_destroy);
    hash_destroy(wml_attr_values_hash, hash2_destroy);
    hash_destroy(wml_URL_values_hash, hash2_destroy);
}



/***********************************************************************
 * Internal functions.
 */


/*
 * parse_node - the recursive parsing function for the parsing tree.
 * Function checks the type of the node, calls for the right parse 
 * function for the type, then calls itself for the first child of
 * the current node if there's one and after that calls itself for the 
 * next child on the list.
 */

static int parse_node(xmlNodePtr node, wml_binary_t **wbxml)
{
    int status = 0;
    
    /* Call for the parser function of the node type. */
    switch (node->type) {
    case XML_ELEMENT_NODE:
	status = parse_element(node, wbxml);
	break;
    case XML_TEXT_NODE:
	status = parse_text(node, wbxml);
	break;
    case XML_COMMENT_NODE:
	/* Comments are ignored. */
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
	    if (parse_node(node->children, wbxml) == -1)
		return -1;
	break;
    case 1:
	if (node->children != NULL)
	    if (parse_node(node->children, wbxml) == -1)
		return -1;
	parse_end(wbxml);
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
	if (parse_node(node->next, wbxml) == -1)
	    return -1;

    return 0;
}


/*
 * parse_document - the parsing function for the document node.
 * The function outputs the WBXML version, WML public id and the
 * character set values into start of the wbxml.
 */

static int parse_document(xmlDocPtr document, Octstr *charset, 
			  wml_binary_t **wbxml)
{
    Octstr *chars;
    xmlNodePtr node;

    if (document == NULL) {
	error(0, "WML compiler: XML parsing failed, no parsed document.");
	error(0, "Most probably an error in the WML source.");
	return -1;
    }

    /* XXX - A bad hack, WBXML version is assumed to be 1.1. -tuo */
    (*wbxml)->wbxml_version = 0x01; /* WBXML Version number 1.1 */
    (*wbxml)->wml_public_id = 0x04; /* WML 1.1 Public ID */
    (*wbxml)->string_table_length = 0x00; /* String table length=0 */

    chars = octstr_create("UTF-8");
    (*wbxml)->character_set = parse_charset(chars, wbxml);
    octstr_destroy(chars);

    node = xmlDocGetRootElement(document);
    string_table_build(node, wbxml);

    return parse_node(node, wbxml);
}


/*
 * parse_element - the parsing function for an element node.
 * The element tag is encoded into one octet hexadecimal value, 
 * if possible. Otherwise it is encoded as text. If the element 
 * needs an end tag, the function returns 1, for no end tag 0
 * and -1 for an error.
 */

static int parse_element(xmlNodePtr node, wml_binary_t **wbxml)
{
    int i, add_end_tag = 0;
    unsigned char wbxml_hex, status_bits;
    xmlAttrPtr attribute;
    Octstr *name;

    name = octstr_create(node->name);

    /* Check, if the tag can be found from the code page. */
    
    for (i = 0; wml_elements[i].text != NULL; i++)
	if (octstr_str_compare(name, wml_elements[i].text) == 0) {
	    wbxml_hex = wml_elements[i].token;
	    if ((status_bits = element_check_content(node)) > 0) {
		wbxml_hex = wbxml_hex | status_bits;
		/* If this node has children, the end tag must be added after 
		   them. */
		if ((status_bits & CHILD_BIT) == CHILD_BIT)
		    add_end_tag = 1;
	    }
	    output_char(wbxml_hex, wbxml);
	    break;
	}

    /* The tag was not on the code page, it has to be encoded as a string. */

    if (wml_elements[i].text == NULL) { 
	/* Unknown tags not yet implemented. They will need a string table. */
	error(0, "WML compiler: unknown tag.");
	return -1;
    }

    /* Encode the attribute list for this node and add end tag after the list. */

    if(node->properties != NULL) {
	attribute = node->properties;
	while (attribute != NULL) {
	    parse_attribute(attribute, wbxml);
	    attribute = attribute->next;
	}
	parse_end(wbxml);
    }

    octstr_destroy(name);
    return add_end_tag;
}


/*
 * parse_attribute - the parsing function for attributes. The function 
 * encodes the attribute (and probably start of the value) as a one 
 * hexadecimal octet. The value (or the rest of it) is coded as a string 
 * maybe using predefined attribute value tokens to reduce the length
 * of the output. Returns 0 for success, -1 for error.
 */


static int parse_attribute(xmlAttrPtr attr, wml_binary_t **wbxml)
{
    int i, j, status = 0;
    int coded_length = 0;
    unsigned char wbxml_hex = 0x00;
    Octstr *attribute = NULL, *value = NULL, *val_j = NULL, 
	*p = NULL;

    attribute = octstr_create(attr->name);

    if (attr->children != NULL)
	value = octstr_create(attr->children->content);

    else 
	value = NULL;

    /* Check if the attribute is found on the code page. */

    for (i = 0; wml_attributes[i].text1 != NULL; i++)
	if (octstr_str_compare(attribute, wml_attributes[i].text1) == 0) {
	    /* Check if there's an attribute start token with good value on 
	       the code page. */
	    for (j = i; (wml_attributes[j].text1 != NULL) &&
		     (strcmp(wml_attributes[i].text1, 
			     wml_attributes[j].text1)
		      == 0); j++)
		if (wml_attributes[j].text2 != NULL && value != NULL) {
		    val_j = octstr_create(wml_attributes[j].text2);

		    if (octstr_ncompare(val_j, value, 
					coded_length = octstr_len(val_j)) 
			== 0) {			
			wbxml_hex = wml_attributes[j].token;
			octstr_destroy(val_j);
			break;
		    } else {
			octstr_destroy(val_j);
			coded_length = 0;
		    }
		} else {
		    wbxml_hex = wml_attributes[i].token;
		    coded_length = 0;
		}
	    break;
	}

    output_char(wbxml_hex, wbxml);

    /* The rest of the attribute is coded as a inline string. */
    if (value != NULL && coded_length < (int) octstr_len(value)) {
	if (coded_length == 0)
	    p = octstr_create(attr->children->content); 
	else
	    p = octstr_copy(value, coded_length, octstr_len(value) - 
			    coded_length); 

	if (check_if_url(wbxml_hex))
	    status = parse_attr_value(p, wml_URL_values,
				      wbxml);
	else
	    status = parse_attr_value(p, wml_attribute_values,
				      wbxml);
	if (status != 0)
	    error(0, 
		  "WML compiler: could not output attribute "
		  "value as a string.");
	octstr_destroy(p);
    }

    /* Memory cleanup. */
    octstr_destroy(attribute);
    if (value != NULL)
	octstr_destroy(value);

    /* Return the status. */
    if (wml_attributes[i].text1 == NULL) {
	error(0, "WML compiler: unknown attribute.");
	return -1;
    } else 
	return status;
}



/*
 * parse_attr_value - parses an attributes value using WML value codes.
 */

static int parse_attr_value(Octstr *attr_value, wml_table_t *tokens,
			    wml_binary_t **wbxml)
{
    int i, pos, wbxml_hex;
    Octstr *cut_text = NULL;

    /*
     * The attribute value is search for text strings that can be replaced 
     * with one byte codes. Note that the algorith is not foolproof; seaching 
     * is done in an order and the text before first hit is not checked for 
     * those tokens that are after the hit in the order. Most likely it would 
     * be waste of time anyway. String table is not used here, since at least 
     * Nokia 7110 doesn't seem to understand string table references here.
     */

    for (i = 0; tokens[i].text != NULL; i++) {
	pos = octstr_search_cstr(attr_value, tokens[i].text, 0);
	switch (pos) {
	case -1:
	    break;
	case 0:
	    wbxml_hex = tokens[i].token;
	    output_char(wbxml_hex, wbxml);	
	    octstr_delete(attr_value, 0, strlen(tokens[i].text));	
	    break;
	default:
	    /* 
	     *  There is some text before the first hit, that has to 
	     *  be handled too. 
	     */
	    gw_assert(pos <= octstr_len(attr_value));
	
	    cut_text = octstr_copy(attr_value, 0, pos);
	    if (parse_octet_string(cut_text, wbxml) != 0)
		return -1;
	    octstr_destroy(cut_text);
	    
	    wbxml_hex = tokens[i].token;
	    output_char(wbxml_hex, wbxml);	

	    octstr_delete(attr_value, 0, pos + strlen(tokens[i].text));
	    break;
	}
    }

    /* 
     * If no hits, then the attr_value is handled as a normal text, otherwise
     * the remaining part is searched for other hits too. 
     */

    if ((int) octstr_len(attr_value) > 0) {
	if (tokens[i].text != NULL)
	    parse_attr_value(attr_value, tokens, wbxml);
	else
	    if (parse_octet_string(attr_value, wbxml) != 0)
		return -1;
    }

    return 0;
}



/*
 * parse_end - adds end tag to an element.
 */

static void parse_end(wml_binary_t **wbxml)
{
    output_char(END, wbxml);
}



/*
 * parse_text - a text string parsing function.
 * This function parses a text node. 
 */

static int parse_text(xmlNodePtr node, wml_binary_t **wbxml)
{
    int ret;
    Octstr *temp;

    temp = octstr_create(node->content);

    octstr_shrink_blanks(temp);
    octstr_strip_blanks(temp);

    if (octstr_len(temp) == 0)
	ret = 0;
    else
	ret = parse_octet_string(temp, wbxml);

    /* Memory cleanup. */
    octstr_destroy(temp);

    return ret;
}



/*
 * parse_charset - a character set parsing function.
 * This function parses the character set of the document. 
 */

static int parse_charset(Octstr *charset, wml_binary_t **wbxml)
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
		    if (character_sets[j].utf8map)
			(*wbxml)->utf8map = character_sets[j].utf8map ;
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
 * parse_variable - a variable parsing function. 
 * Arguments:
 * - text: the octet string containing a variable
 * - start: the starting position of the variable not including 
 *   trailing &
 * Returns: lenth of the variable for success, -1 for failure, 0 for 
 * variable syntax error, when it will be ignored. 
 * Parsed variable is returned as an octet string in Octstr **output.
 */

static int parse_variable(Octstr *text, int start, Octstr **output, 
			  wml_binary_t **wbxml)
{
    var_esc_t esc;
    int ret;
    Octstr *variable;

    variable = get_variable(text, start + 1);
    octstr_truncate(*output, 0);

    if (variable == NULL)
	return 0;

    if (octstr_get_char(variable, 0) == '$') {
	octstr_append_char(*output, '$');
	octstr_destroy(variable);
	ret = 2;
    } else {
	if (octstr_get_char(text, start + 1) == '(')
	    ret = octstr_len(variable) + 3;
	else
	    ret = octstr_len(variable) + 1;

	if ((esc = check_variable_syntax(variable)) == FAILED)
	    return -1;
	else
	    output_variable(variable, output, esc, wbxml);
    }

    return ret;
}



/*
 * get_variable - get the variable name from text.
 * Octstr *text contains the text with a variable name starting at point 
 * int start.
 */

static Octstr *get_variable(Octstr *text, int start)
{
    Octstr *var = NULL;
    long end;
    int ch;

    gw_assert(text != NULL);
    gw_assert(start >= 0 && start <= (int) octstr_len(text));

    ch = octstr_get_char(text, start);

    if (ch == '$') {
	var = octstr_create("$");
    } else if (ch == '(') {
	start ++;
	end = octstr_search_char(text, ')', start);
	if (end == -1)
	    error(0, "WML compiler: braces opened, but not closed for a "
		  "variable.");
	else if (end - start == 0)
	    error(0, "WML compiler: empty braces without variable.");
	else
	    var = octstr_copy(text, start, end - start);
    } else {
	end = start + 1;
	while (isalnum(ch = octstr_get_char(text, end)) || (ch == '_'))
	    end ++;

	var = octstr_copy(text, start, end - start);
    }

    return var;
}



/*
 * check_variable_syntax - checks the variable syntax and the possible 
 * escape mode it has. Octstr *variable contains the variable string.
 */

static var_esc_t check_variable_syntax(Octstr *variable)
{
    Octstr *escape;
    char *buf;
    char ch;
    int pos, len, i;
    var_esc_t ret;

    if ((pos = octstr_search_char(variable, ':', 0)) > 0) {
	len = octstr_len(variable) - pos;
	escape = octstr_copy(variable, pos + 1, len - 1);
	octstr_truncate(variable, pos);
	octstr_truncate(escape, len);
	octstr_convert_range(escape, 0, octstr_len(escape), tolower);

	if (octstr_str_compare(escape, "noesc") == 0 ||
	    octstr_str_compare(escape, "n") == 0 )
	    ret = NOESC;
	else if (octstr_str_compare(escape, "unesc") == 0 ||
		 octstr_str_compare(escape, "u") == 0 )
	    ret = UNESC;
	else if (octstr_str_compare(escape, "escape") == 0 ||
		 octstr_str_compare(escape, "e") == 0 )
	    ret = ESC;
	else {
	    error(0, "WML compiler: syntax error in variable escaping.");
	    octstr_destroy(escape);
	    return FAILED;
	}
	octstr_destroy(escape);
    } else
	ret = NOESC;

    ch = octstr_get_char(variable, 0);
    if (!(isalpha((int)ch)) && ch != '_') {
	buf = gw_malloc(70);
	if (sprintf
	    (buf, 
	     "WML compiler: syntax error in variable; name starting with %c.",
	     ch) < 1)
	    error(0, "WML compiler: could not format error log output!");
	error(0, buf);
	gw_free(buf);
	return FAILED;
    } else
	for (i = 1; i < (int) octstr_len(variable); i++)
	    if (!isalnum((int)(ch = octstr_get_char(variable, 0))) && 
		ch != '_') {
		error(0, "WML compiler: syntax error in variable.");
		return FAILED;
	    }

    return ret;
}



/*
 * parse_octet_string - parse an octet string into wbxml_string, the string 
 * is checked for variables. If string is string table applicable, it will 
 * be checked for string insrtances that are in the string table, otherwise 
 * not. Returns 0 for success, -1 for error.
 */

static int parse_octet_string(Octstr *ostr, wml_binary_t **wbxml)
{
    Octstr *output, *var, *temp = NULL;
    int var_len;
    int start = 0, pos = 0, len;

    /* No variables? Ok, let's take the easy way... */

    if ((pos = octstr_search_char(ostr, '$', 0)) < 0) {
	string_table_apply(ostr, wbxml);
	return 0;
    }

    len = octstr_len(ostr);
    output = octstr_create("");
    var = octstr_create("");

    while (pos < len) {
	if (octstr_get_char(ostr, pos) == '$') {
	    if (pos > start) {
		temp = octstr_copy(ostr, start, pos - start);
		octstr_insert(output, temp, octstr_len(output));
		octstr_destroy(temp);
	    }
	  
	    if ((var_len = parse_variable(ostr, pos, &var, wbxml)) > 0)	{
		if (octstr_get_char(var, 0) == '$')
		    /*
		     * No, it's not actually variable, but $-character escaped 
		     * as "$$". So everything should be packed into one string. 
		     */
		    octstr_insert(output, var, octstr_len(output));
		else {
		    /* The string is output as a inline string and the variable
		       as a string table variable reference. */
		    if (octstr_len(output) > 0)
			string_table_apply(output, wbxml);
		    octstr_truncate(output, 0);
		    output_octet_string(var, wbxml);
		}

		pos = pos + var_len;
		start = pos;
	    } else
		return -1;
	} else
	    pos ++;
    }

    /* Was there still something after the last variable? */
    if (start < pos) {
	if (octstr_len(output) == 0) {
	    octstr_destroy(output);
	    output = octstr_copy(ostr, start, pos - start);
	} else {
	    temp = octstr_copy(ostr, start, pos - start);
	    octstr_insert(output, temp, octstr_len(output));
	    octstr_destroy(temp);
	}
    }

    if (octstr_len(output) > 0)
	string_table_apply(output, wbxml);
  
    octstr_destroy(output);
    octstr_destroy(var);
  
    return 0;
}




/*
 * parse_entities - replaces WML entites in the WML source with equivalent
 * numerical entities. A fast patch for WAP 1.1 compliance.
 */

static void parse_entities(Octstr *wml_source)
{
    static char entity_nbsp[] = "&nbsp;";
    static char entity_shy[] = "&shy;";
    static char nbsp[] = "&#160;";
    static char shy[] = "&#173;";
    int pos = 0;
    Octstr *temp;

    if ((pos = octstr_search_cstr(wml_source, entity_nbsp, pos)) >= 0) {
	temp = octstr_create(nbsp);
	while (pos >= 0) {
	    octstr_delete(wml_source, pos, strlen(entity_nbsp));
	    octstr_insert(wml_source, temp, pos);
	    pos = octstr_search_cstr(wml_source, entity_nbsp, pos);
	}
	octstr_destroy(temp);
    }

    pos = 0;
    if ((pos = octstr_search_cstr(wml_source, entity_shy, pos)) >= 0) {
	temp = octstr_create(shy);
	while (pos >= 0) {
	    octstr_delete(wml_source, pos, strlen(entity_shy));
	    octstr_insert(wml_source, temp, pos);
	    pos = octstr_search_cstr(wml_source, entity_shy, pos);
	}
	octstr_destroy(temp);
    }	
}



/*
 * wml_binary_create - reserves memory for the wml_binary_t and sets the 
 * fields to zeroes and NULLs.
 */

static wml_binary_t *wml_binary_create(void)
{
    wml_binary_t *wbxml;

    wbxml = gw_malloc(sizeof(wml_binary_t));
    wbxml->wbxml_version = 0x00;
    wbxml->wml_public_id = 0x00;
    wbxml->character_set = 0x00;
    wbxml->string_table_length = 0x00;
    wbxml->string_table = list_create();
    wbxml->wbxml_string = octstr_create("");
    wbxml->utf8map = NULL ;

    return wbxml;
}



/*
 * wml_binary_destroy - frees the memory allocated for the wml_binary_t.
 */

static void wml_binary_destroy(wml_binary_t *wbxml)
{
    if (wbxml != NULL) {
	list_destroy(wbxml->string_table, NULL);
	octstr_destroy(wbxml->wbxml_string);
	gw_free(wbxml);
    }
}



/*
 * wml_binary_output - outputs all the fiels of wml_binary_t into ostr.
 */

static void wml_binary_output(Octstr *ostr, wml_binary_t *wbxml)
{
    octstr_append_char(ostr, wbxml->wbxml_version);
    octstr_append_char(ostr, wbxml->wml_public_id);
    octstr_append_uintvar(ostr, wbxml->character_set);
    octstr_append_uintvar(ostr, wbxml->string_table_length);

    if (wbxml->string_table_length > 0)
	string_table_output(ostr, &wbxml);

    octstr_insert(ostr, wbxml->wbxml_string, octstr_len(ostr));
}



/*
 * output_char - output a character into wbxml_string.
 * Returns 0 for success, -1 for error.
 */

static void output_char(int byte, wml_binary_t **wbxml)
{
    octstr_append_char((*wbxml)->wbxml_string, byte);
}



/*
 * output_octet_string - output an octet string into wbxml.
 * Returns 0 for success, -1 for an error. No conversions.
 */

static void output_octet_string(Octstr *ostr, wml_binary_t **wbxml)
{
    octstr_insert((*wbxml)->wbxml_string, ostr, 
		  octstr_len((*wbxml)->wbxml_string));
}



/*
 * output_variable - output a variable reference into the string table.
 */

static void output_variable(Octstr *variable, Octstr **output, 
			    var_esc_t escaped, wml_binary_t **wbxml)
{
  switch (escaped)
    {
    case ESC:
      octstr_append_char(*output, EXT_T_0);
      break;
    case UNESC:
      octstr_append_char(*output, EXT_T_1);
      break;
    default:
      octstr_append_char(*output, EXT_T_2);
      break;
    }

  octstr_append_uintvar(*output, string_table_add(variable, wbxml));
}



/*
 * hash2_create - allocates memory for a 2 field hash table node.
 */

static wml_hash2_t *hash2_create(wml_table_t *node)
{
    wml_hash2_t *table_node;

    table_node = gw_malloc(sizeof(wml_hash2_t));
    table_node->item = octstr_create(node->text);
    table_node->binary = node->token;

    return table_node;
}



/*
 * hash3_create - allocates memory for a 3 field hash table node.
 */

static wml_hash3_t *hash3_create(wml_table3_t *node)
{
    wml_hash3_t *table_node;

    table_node = gw_malloc(sizeof(wml_hash3_t));
    table_node->item1 = octstr_create(node->text1);
    table_node->item2 = octstr_create(node->text2);
    table_node->binary = node->token;

    return table_node;
}



/*
 * hash2_destroy - deallocates memory of a 2 field hash table node.
 */

static void hash2_destroy(void *p)
{
    wml_hash2_t *node;

    if (p == NULL)
        return;

    node = p;

    octstr_destroy(node->item);
    gw_free(node);
}



/*
 * hash3_destroy - deallocates memory of a 3 field hash table node.
 */

static void hash3_destroy(void *p)
{
    wml_hash3_t *node;

    if (p == NULL)
	return;

    node = p;

    octstr_destroy(node->item1);
    octstr_destroy(node->item2);
    gw_free(node);
}



/*
 * element_check_content - a helper function for parse_element for checking 
 * if an element has content or attributes. Returns status bit for attributes 
 * (0x80) and another for content (0x40) added into one octet.
 */

static unsigned char element_check_content(xmlNodePtr node)
{
    unsigned char status_bits = 0x00;

    if (node->children != NULL)
	status_bits = CHILD_BIT;

    if (node->properties != NULL)
	status_bits = status_bits | ATTR_BIT;

    return status_bits;
}


/*
 * check_if_url - checks whether the attribute value is an URL or some other 
 * kind of value. Returns 1 for an URL and 0 otherwise.
 */

static int check_if_url(int hex)
{
    switch ((unsigned char) hex) {
    case 0x4A: case 0x4B: case 0x4C: /* href, href http://, href https:// */
    case 0x32: case 0x58: case 0x59: /* src, src http://, src https:// */
	return 1;
	break;
    }
    return 0;
}



/*
 * wml_table_len - returns the length of a wml_table_t array.
 */

static int wml_table_len(wml_table_t *table)
{
    int i = 0;

    while (table[i].text != NULL)
	i++;

    return i;
}



/*
 * wml_table3_len - returns the length of a wml_table3_t array.
 */

static int wml_table3_len(wml_table3_t *table)
{
    int i = 0;

    while (table[i].text1 != NULL)
	i++;

    return i;
}



/*
 * string_table_create - reserves memory for the string_table_t and sets the 
 * fields.
 */

static string_table_t *string_table_create(int offset, Octstr *ostr)
{
  string_table_t *node;

  node = gw_malloc(sizeof(string_table_t));
  node->offset = offset;
  node->string = ostr;

  return node;
}



/*
 * string_table_destroy - frees the memory allocated for the string_table_t.
 */

static void string_table_destroy(string_table_t *node)
{
  if (node != NULL)
    {
      octstr_destroy(node->string);
      gw_free(node);
    }
}



/*
 * string_table_proposal_create - reserves memory for the 
 * string_table_proposal_t and sets the fields.
 */

static string_table_proposal_t *string_table_proposal_create(Octstr *ostr)
{
  string_table_proposal_t *node;

  node = gw_malloc(sizeof(string_table_proposal_t));
  node->count = 1;
  node->string = ostr;

  return node;
}



/*
 * string_table_proposal_destroy - frees the memory allocated for the 
 * string_table_proposal_t.
 */

static void string_table_proposal_destroy(string_table_proposal_t *node)
{
  if (node != NULL)
    {
      octstr_destroy(node->string);
      gw_free(node);
    }
}



/*
 * string_table_build - collects the strings from the WML source into a list, 
 * adds those strings that appear more than once into string table. The rest 
 * of the strings are sliced into words and the same procedure is executed to 
 * the list of these words.
 */

static void string_table_build(xmlNodePtr node, wml_binary_t **wbxml)
{
  string_table_proposal_t *item = NULL;
  List *list = NULL;

  list = list_create();

  string_table_collect_strings(node, list);

  list = string_table_add_many(string_table_sort_list(list), wbxml);

  list =  string_table_collect_words(list);

  /* Don't add strings if there aren't any. (no NULLs please) */
  if (list)
    {
      list = 
        string_table_add_many(string_table_sort_list(list), wbxml);
    }

  /* Memory cleanup. */
  while (list_len(list))
    {
      item = list_extract_first(list);
      string_table_proposal_destroy(item);
    }

  list_destroy(list, NULL);
}



/*
 * string_table_collect_strings - collects the strings from the WML 
 * ocument into a list that is then further processed to build the 
 * string table for the document.
 */

static void string_table_collect_strings(xmlNodePtr node, List *strings)
{
    Octstr *string;

    switch (node->type) {
    case XML_TEXT_NODE:
	if (strlen(node->content) > STRING_TABLE_MIN) {
	    string = octstr_create(node->content);

	    octstr_shrink_blanks(string);
	    octstr_strip_blanks(string);

	    if (octstr_len(string) > STRING_TABLE_MIN)
		list_append(strings, string);
	    else 
		octstr_destroy(string);
	}
	break;
    default:
	break;
    }

    if (node->children != NULL)
	string_table_collect_strings(node->children, strings);

    if (node->next != NULL)
	string_table_collect_strings(node->next, strings);
}



/*
 * string_table_sort_list - takes a list of octet strings and returns a list
 * of string_table_proposal_t:s that contains the same strings with number of 
 * instants of every string in the input list.
 */

static List *string_table_sort_list(List *start)
{
  int i;
  Octstr *string = NULL;
  string_table_proposal_t *item = NULL;
  List *sorted = NULL;

  sorted = list_create();

  while (list_len(start))
    {
      string = list_extract_first(start);
      
      /* Check whether the string is unique. */
      for (i = 0; i < list_len(sorted); i++)
	{
	  item = list_get(sorted, i);
	  if (octstr_compare(item->string, string) == 0)
	    {
	      octstr_destroy(string);
	      string = NULL;
	      item->count ++;
	      break;
	    }
	}
      
      if (string != NULL)
	{
	  item = string_table_proposal_create(string);
	  list_append(sorted, item);
	}
    }

  list_destroy(start, NULL);

  return sorted;
}



/*
 * string_table_add_many - takes a list of string with number of instants and
 * adds those whose number is greater than 1 into the string table. Returns 
 * the list ofrejected strings for memory cleanup.
 */

static List *string_table_add_many(List *sorted, wml_binary_t **wbxml)
{
  string_table_proposal_t *item = NULL;
  List *list = NULL;

  list = list_create();

  while (list_len(sorted))
    {
      item = list_extract_first(sorted);

      if (item->count > 1 && octstr_len(item->string) > STRING_TABLE_MIN)
	{
	  string_table_add(octstr_duplicate(item->string), wbxml);
	  string_table_proposal_destroy(item);
	}
      else
	list_append(list, item);
    }

  list_destroy(sorted, NULL);

  return list;
}



/*
 * string_table_collect_words - takes a list of strings and returns a list 
 * of words contained by those strings.
 */

static List *string_table_collect_words(List *strings)
{
  Octstr *word = NULL;
  string_table_proposal_t *item = NULL;
  List *list = NULL, *temp_list = NULL;

  while (list_len(strings))
    {
      item = list_extract_first(strings);

      if (list == NULL)
	{
	  list = octstr_split_words(item->string);
	  string_table_proposal_destroy(item);
	}
      else
	{
	  temp_list = octstr_split_words(item->string);

	  while ((word = list_extract_first(temp_list)) != NULL)
	    list_append(list, word);

	  list_destroy(temp_list, NULL);
	  string_table_proposal_destroy(item);
	}
    }

  list_destroy(strings, NULL);

  return list;
}



/*
 * string_table_add - adds a string to the string table. Duplicates are
 * discarded. The function returns the offset of the string in the 
 * string table; if the string is already in the table then the offset 
 * of the first copy.
 */

static unsigned long string_table_add(Octstr *ostr, wml_binary_t **wbxml)
{
  string_table_t *item = NULL;
  unsigned long i, offset = 0;

  /* Check whether the string is unique. */
  for (i = 0; i < (unsigned long)list_len((*wbxml)->string_table); i++)
    {
      item = list_get((*wbxml)->string_table, i);
      if (octstr_compare(item->string, ostr) == 0)
	{
	  octstr_destroy(ostr);
	  return item->offset;
	}
    }

  /* Create a new list item for the string table. */
  offset = (*wbxml)->string_table_length;

  item = string_table_create(offset, ostr);

  (*wbxml)->string_table_length = 
    (*wbxml)->string_table_length + octstr_len(ostr) + 1;
  list_append((*wbxml)->string_table, item);

  return offset;
}



/*
 * string_table_apply - takes a octet string of WML bnary and goes it 
 * through searching for substrings that are in the string table and 
 * replaces them with string table references.
 */

static void string_table_apply(Octstr *ostr, wml_binary_t **wbxml)
{
  Octstr *input = NULL;
  string_table_t *item = NULL;
  long i = 0, word_s = 0, str_e = 0;

  input = octstr_create("");

  for (i = 0; i < list_len((*wbxml)->string_table); i++)
    {
      item = list_get((*wbxml)->string_table, i);

      if (octstr_len(item->string) > STRING_TABLE_MIN)
	/* No use to replace 1 to 3 character substring, the reference 
	   will eat the saving up. A variable will be in the string table 
	   even though it's only 1 character long. */
	if ((word_s = octstr_search(ostr, item->string, 0)) >= 0)
	  {
	    /* Check whether the octet string are equal if they are equal 
	       in length. */
	    if (octstr_len(ostr) == octstr_len(item->string))
	      {
		if ((word_s = octstr_compare(ostr, item->string)) == 0)
		  {
		    octstr_truncate(ostr, 0);
		    octstr_append_char(ostr, STR_T);
		    octstr_append_uintvar(ostr, item->offset);
		    str_e = 1;
		  }
	      }
	    /* Check the possible substrings. */
	    else if (octstr_len(ostr) > octstr_len(item->string))
	      {
		if (word_s + octstr_len(item->string) == octstr_len(ostr))
		    str_e = 1;

		octstr_delete(ostr, word_s, octstr_len(item->string));

		octstr_truncate(input, 0);
		/* Substring in the start? No STR_END then. */
		if (word_s > 0)
		  octstr_append_char(input, STR_END);
                  
		octstr_append_char(input, STR_T);
		octstr_append_uintvar(input, item->offset);

		/* Subtring ending the string? No need to start a new one. */
		if ( word_s < octstr_len(ostr))
		  octstr_append_char(input, STR_I);

		octstr_insert(ostr, input, word_s);
	      }
	    /* If te string table entry is longer than the string, it can 
	       be skipped. */
	  }
    }

  octstr_destroy(input);

  if (octstr_get_char(ostr, 0) != STR_T)
    output_char(STR_I, wbxml);
  if (!str_e)
    octstr_append_char(ostr, STR_END);    

  output_octet_string(ostr, wbxml);
}



/*
 * string_table_output - writes the contents of the string table 
 * into an octet string that is sent to the phone.
 */

static void string_table_output(Octstr *ostr, wml_binary_t **wbxml)
{
  string_table_t *item;

  while ((item = list_extract_first((*wbxml)->string_table)) != NULL)
    {
      octstr_insert(ostr, item->string, octstr_len(ostr));
      octstr_append_char(ostr, STR_END);
      string_table_destroy(item);
    }
}


List *wml_charsets(void) {
	int i;
	List *result;
	Octstr *charset;

	result = list_create();
	for (i = 0; character_sets[i].charset != NULL; i++) {
		charset = octstr_create(character_sets[i].charset);
		octstr_append_char(charset, '-');
		octstr_append_cstr(charset, character_sets[i].nro);
		list_append(result, charset);
	}
	return result;
}



/*
 * hash_create - creates a hash table. This function reserves space for a hash
 * table and initializes a hash function to be used in the table. It takes an 
 * estimate of the nodes as an argument. NOTE: At the moment a static table, 
 * at some point a check must be added whether adding a new node requires 
 * allocating more memory at the hash_insert.
 */

Hash *hash_create(int n)
{
    Hash *hash;
    int i = 0;

    hash = gw_malloc(sizeof(Hash));

    if (n == 0)
	n = START_NUM;

    hash->count = 0;
    hash->prime = find_prime(n);
    hash->table = gw_malloc(hash->prime * sizeof(hash_t));

    for (i = 0; i < hash->prime; i++) {
	hash->table[i].count = 0;
	hash->table[i].item = NULL;
    }

    hash->a = 1 + (gw_rand() % n); /* Must not be 0! */
    hash->b = gw_rand() % n;

    hash->operation_lock = mutex_create();

    return hash;
}



/*
 * hash_insert - adds a node into the hash_table. This funcion adds a node 
 * to the hash table. If a conlict occurs the pointer is replaced with a list 
 * of nodes that have the same hash key. This should add also allocate more 
 * space when necessary and do the initialization of that table, like count a 
 * new prime that is at least 2* the old one, and recount the hash keys for 
 * every node in the table and add them into the new table.
 */

void hash_insert(Hash *table, int key, void *item)
{
    void *temp = NULL;
    int hash;

    hash = hash_seed(table, key);

    lock(table);

    switch (table->table[hash].count) {
    case 0:
	table->table[hash].item = item;
	table->table[hash].count = 1;
	hash_recount(table);
	break;
    case 1:
	temp = table->table[hash].item;
	table->table[hash].item = list_create();
	list_append(table->table[hash].item, temp);
	list_append(table->table[hash].item, item);
	table->table[hash].count = 2;
	hash_recount(table);
	break;
    default:
	list_append(table->table[hash].item, item);
	table->table[hash].count ++;
	hash_recount(table);
	break;
    }

    unlock(table);    
}



/*
 * hash_find - searches for a node in the table and returns a pointer to it.
 * The arguments for the function are:
 * - table: the hash table to be used
 * - key: the hash key value to be searched.
 * - pat: an object where to compare if a conflict occurs
 * - cmp: a comparison function for a conflict. Takes two of the type of 
 *   objects as arguments and returns true (ie an integer > 0) for two similar
 *   ones.
 * Returns either an object found or in a conflict a list of objects that have 
 * the same hash key.
 * NOTE: This functions does not copy the object, so if the object is altered
 * or destroyed, the table becomes unconsistent.
 */

void *hash_find(Hash *table, int key, void* pat, hash_item_matches_t *cmp)
{
    void *item = NULL;
    int hash;

    hash = hash_seed(table, key);

    switch (table->table[hash].count) {
    case 0:
	break;
    case 1:
	item = table->table[hash].item;
	break;
    default:
	if (pat != NULL && cmp != NULL)
	    item = list_search(table->table[hash].item, pat, cmp);
	else
	    item = table->table[hash].item;
	break;
    }
    
    return item;
}



/*
 * hash_remove - searches for a node in the table and returns it. This funtion 
 * acts almost the same as the previous one, but also removes the node from the
 * hash table when returning it.
 */

void *hash_remove(Hash *table, int key, void* pat, hash_item_matches_t *cmp)
{
    void *item = NULL, *temp = NULL;
    int hash;

    hash = hash_seed(table, key);

    lock(table);

    switch (table->table[hash].count) {
    case 0:
	break;
    case 1:
	item = table->table[hash].item;
	table->table[hash].item = NULL;
	table->table[hash].count = 0;
	break;
    default:
	temp = list_extract_matching(table->table[hash].item, pat, cmp);
	item = list_extract_first(temp);
	list_destroy(temp, NULL);

	if (list_len(table->table[hash].item) == 1) {
	    temp = list_extract_first(table->table[hash].item);
	    list_destroy(table->table[hash].item, NULL);
	    table->table[hash].item = temp;
	    table->table[hash].count = 1;
	} else
	    table->table[hash].count = table->table[hash].count - 1;
	
	break;
    }
    
    unlock(table);

    return item;
}



/*
 * hash_destroy - destroys an hash table. The function takes as arguments the 
 * hash table to be destroyed and a function pointer to a function that frees
 * all the memory allocated by a node in the table. If the second argument is 
 * NULL, it's up to the caller to make sure that the table is empty before it
 * is destroyed.
 */

void hash_destroy(Hash *table, hash_item_destructor_t *destructor)
{
    int i;
    
    lock(table);

    mutex_destroy(table->operation_lock);

    if (destructor != NULL)
	for (i = 0; i < table->prime; i ++) {
	    switch (table->table[i].count) {
	    case 0:
		break;
	    case 1:
		destructor(table->table[i].item);
		break;
	    default:
		list_destroy(table->table[i].item, destructor);
		break;
	    }
	}

    gw_free(table->table);
    gw_free(table);  
}



/*
 * find_prime - searches for a prime starting from the argument from.
 */

static int find_prime(int from)
{
    int num = 0, i;

    /* Primes are not dividable with 2. */
    if ((from % 2) == 0)
	num = from + 1;
    else 
	num = from;

    for (i = 3; i < sqrt(num); i = i + 2) {
	if ((num % i) == 0) {
	    i = 3;
	    num = num + 2;
	}
    }

    return num;
}



/*
 * hash_seed - returns the seed from the hash function from the table *table 
 * with the key key.
 */

static int hash_seed(Hash *table, int key)
{
    return (table->a * key + table->b) % table->prime;
}



/*
 * lock - locks the hash table.
 */

static void lock(Hash *table) 
{
    gw_assert(table != NULL);
    mutex_lock(table->operation_lock);
}



/*
 * unlock - unlocks the hash table.
 */

static void unlock(Hash *table) 
{
    gw_assert(table != NULL);
    mutex_unlock(table->operation_lock);
}



/*
 * hash_recount - recounts the number of the nodes in the hash table.
 */

static int hash_recount(Hash *table)
{
    int i, sum = 0;

    for (i = 0; i < table->prime; i++)
	sum = sum + table->table[i].count;

    return sum;
}
