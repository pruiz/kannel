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
 * Attribute value code structure.
 */

typedef struct {
  char *attr_value;
  unsigned char token;
} wml_attr_value_t;

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


#include "wml_definitions.h"


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
static int parse_attr_value(Octstr *attr_value, wml_attr_value_t *tokens, 
			    wml_binary_t **wbxml);
static int parse_text(xmlNodePtr node, wml_binary_t **wbxml);
static int parse_charset(Octstr *charset, wml_binary_t **wbxml);
static int parse_octet_string(Octstr *ostr, wml_binary_t **wbxml);

static void parse_end(wml_binary_t **wbxml);

static unsigned char element_check_content(xmlNodePtr node);
static int check_if_url(int hex);

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
static int output_terminated_string(Octstr *ostr, wml_binary_t **wbxml);
static int output_convertable_string(Octstr *ostr, wml_binary_t **wbxml);
static int output_plain_string(Octstr *ostr, wml_binary_t **wbxml);
void output_variable(Octstr *variable, Octstr **output, var_esc_t escaped,
		     wml_binary_t **wbxml);

/* 
 * String table functions, used to add and remove strings into and from the
 * string table.
 */

static string_table_t *string_table_create(int offset, Octstr *ostr);
static void string_table_destroy(string_table_t *node);
static unsigned long string_table_add(Octstr *ostr, wml_binary_t **wbxml);
static void string_table_output(Octstr *ostr, wml_binary_t **wbxml);

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

  *wml_binary = octstr_create_empty();
  wbxml = wml_binary_create();

  /* Remove the extra space from start and the end of the WML Document. */

  octstr_strip_blank(wml_text);

  /* Check the WML-code for \0-characters. */

  size = octstr_len(wml_text);
  wml_c_text = octstr_get_cstr(wml_text);

  if (octstr_search_char(wml_text, '\0') != -1)
    {    
      error(0, 
	    "WML compiler: Compiling error: "
	    "\\0 character found in the middle of the WML source.");
      ret = -1;
    }
  else
    {
      /* 
       * An empty octet string for the binary output is created, the wml 
       * source is parsed into a parsing tree and the tree is then compiled 
       * into binary.
       */

#if HAVE_LIBXML_1_8_6
      /* XXX this seems to work around a bug in libxml, which is even in
	 1.8.6. --liw */
      pDoc = xmlParseMemory(wml_c_text, size + 1);
#else
      /* XXX this should be the correct version. --liw */
      pDoc = xmlParseMemory(wml_c_text, size);
#endif

      if(pDoc != NULL)
	{
	  ret = parse_document(pDoc, charset, &wbxml);
	  wml_binary_output(*wml_binary, wbxml);
	}
      else
	{    
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

#if defined(LIBXML_VERSION) && LIBXML_VERSION >= 20000

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

#else

    if (node->childs != NULL)
      if (parse_node(node->childs, wbxml) == -1)
	return -1;
    break;
  case 1:
    if (node->childs != NULL)
      if (parse_node(node->childs, wbxml) == -1)
	return -1;
    parse_end(wbxml);
    break;

#endif

  case -1: /* Something went wrong in the parsing. */
    return -1;
  default:
    error(0, "WML compiler: undefined return value in a parse function.");
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

  if (document == NULL)
    {
      error(0, "WML compiler: XML parsing failed, no parsed document.");
      error(0, "Most probably an error in the WML source.");
      return -1;
    }

  /* A bad hack, WBXML version is assumed to be 1.1. */
  (*wbxml)->wbxml_version = 0x01; /* WBXML Version number 1.1 */
  (*wbxml)->wml_public_id = 0x04; /* WML 1.1 Public ID */
  (*wbxml)->string_table_length = 0x00; /* String table length=0 */

  if (document->encoding != NULL)
    {
      chars = octstr_create(document->encoding);
      (*wbxml)->character_set = parse_charset(chars, wbxml);
      octstr_destroy(chars);
    }
  else if (charset != NULL && octstr_len(charset) > 0)
    (*wbxml)->character_set = parse_charset(charset, wbxml);
  else
    {
      chars = octstr_create("UTF-8");
      (*wbxml)->character_set = parse_charset(chars, wbxml);
      octstr_destroy(chars);
    }

  return parse_node(xmlDocGetRootElement(document), wbxml);
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
  
  for (i = 0; wml_elements[i].element != NULL; i++)
    if (octstr_str_compare(name, wml_elements[i].element) == 0)
      {
	wbxml_hex = wml_elements[i].token;
	if ((status_bits = element_check_content(node)) > 0)
	  {
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

  if (wml_elements[i].element == NULL)
    { /* Unknown tags not yet implemented. They will need a string table. */
      error(0, "WML compiler: unknown tag.");
      return -1;
    }

  /* Encode the attribute list for this node and add end tag after the list. */

  if(node->properties != NULL)
    {
      attribute = node->properties;
      while (attribute != NULL)
	{
	  parse_attribute(attribute, wbxml);
	  attribute = attribute->next;
	}
      parse_end(wbxml);
    }

  octstr_destroy(name);
  return add_end_tag;
}


/*
 * element_check_content - a helper function for parse_element for checking 
 * if an element has content or attributes. Returns status bit for attributes 
 * (0x80) and another for content (0x40) added into one octet.
 */

static unsigned char element_check_content(xmlNodePtr node)
{
  unsigned char status_bits = 0x00;

#if defined(LIBXML_VERSION) && LIBXML_VERSION >= 20000

  if (node->children != NULL)

#else 

  if (node->childs != NULL)

#endif

    status_bits = CHILD_BIT;

  if (node->properties != NULL)
    status_bits = status_bits | ATTR_BIT;

  return status_bits;
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

#if defined(LIBXML_VERSION) && LIBXML_VERSION >= 20000

  if (attr->children != NULL)
    value = octstr_create(attr->children->content);

#else

  if (attr->val != NULL)
    value = octstr_create(attr->val->content);

#endif

  else 
    value = NULL;

  /* Check if the attribute is found on the code page. */

  for (i = 0; wml_attributes[i].attribute != NULL; i++)
    if (octstr_str_compare(attribute, wml_attributes[i].attribute) == 0)
      {
	/* Check if there's an attribute start token with good value on 
	   the code page. */
        for (j = i; (wml_attributes[j].attribute != NULL) &&
	       (strcmp(wml_attributes[i].attribute, 
		       wml_attributes[j].attribute)
		== 0); j++)
	  if (wml_attributes[j].a_value != NULL && value != NULL)	      
	    {
	      val_j = octstr_create(wml_attributes[j].a_value);

	      if (octstr_ncompare(val_j, value, 
				  coded_length = octstr_len(val_j)) == 0)
		{
		  wbxml_hex = wml_attributes[j].token;
		  octstr_destroy(val_j);
		  break;
		}
	      else
		{
		  octstr_destroy(val_j);
		  coded_length = 0;
		}
	    }
	  else
	    {
	      wbxml_hex = wml_attributes[i].token;
	      coded_length = 0;
	    }
	break;
      }

  output_char(wbxml_hex, wbxml);

  /* The rest of the attribute is coded as a inline string. */
  if (value != NULL && coded_length < (int) octstr_len(value))
    {
      if (coded_length == 0)

#if defined(LIBXML_VERSION) && LIBXML_VERSION >= 20000

        p = octstr_create(attr->children->content); 

#else

	p = octstr_create(attr->val->content); 

#endif

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
	      "WML compiler: could not output attribute value as a string.");
      octstr_destroy(p);
    }

  /* Memory cleanup. */
  octstr_destroy(attribute);
  if (value != NULL)
    octstr_destroy(value);

  /* Return the status. */
  if (wml_attributes[i].attribute == NULL)
    {
      error(0, "WML compiler: unknown attribute.");
      return -1;
    } 
  else 
    return status;
}



/*
 * parse_attr_value - parses an attributes value using WML value codes.
 */

static int parse_attr_value(Octstr *attr_value, wml_attr_value_t *tokens,
			     wml_binary_t **wbxml)
{
  int i, pos, wbxml_hex;
  Octstr *cut_text = NULL;

  /*
   * The attribute value is search for text strings that can be replaced 
   * with one byte codes. Note that the algorith is not foolproof; seaching 
   * is done in an order and the text before first hit is not checked for 
   * those tokens that are after the hit in the order. Most likely it would 
   * be waste of time anyway.
   */

  for (i = 0; tokens[i].attr_value != NULL; i++)
    {
      pos = octstr_search_cstr(attr_value, tokens[i].attr_value);
      switch (pos) {
      case -1:
	break;
      case 0:
	wbxml_hex = tokens[i].token;
	output_char(wbxml_hex, wbxml);	
	octstr_delete(attr_value, 0, strlen(tokens[i].attr_value));	
	break;
      default:
	/* There is some text before the first hit, that has to handled too. */
	gw_assert(pos <= octstr_len(attr_value));
	
	cut_text = octstr_copy(attr_value, 0, pos);
	if (parse_octet_string(cut_text, wbxml) != 0)
	  return -1;
	octstr_destroy(cut_text);

	wbxml_hex = tokens[i].token;
	output_char(wbxml_hex, wbxml);	

	octstr_delete(attr_value, 0, pos + strlen(tokens[i].attr_value));
	break;
      }
    }

  /* 
   * If no hits, then the attr_value is handled as a normal text, otherwise
   * the remaining part is searched for other hits too. 
   */

  if ((int) octstr_len(attr_value) > 0)
    {
      if (tokens[i].attr_value != NULL)
	parse_attr_value(attr_value, tokens, wbxml);
      else
	if (parse_octet_string(attr_value, wbxml) != 0)
	  return -1;
    }

  return 0;
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

  octstr_shrink_blank(temp);
  octstr_strip_blank(temp);

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
  if ((cut = octstr_search_char(charset, '_')) > 0)
    {
      number = octstr_copy(charset, cut + 1, 
			   (octstr_len(charset) - (cut + 1)));
      octstr_truncate(charset, cut);
    }
  else if ((cut = octstr_search_char(charset, '-')) > 0)
    {
      number = octstr_copy(charset, cut + 1, 
			   (octstr_len(charset) - (cut + 1)));
      octstr_truncate(charset, cut);
    }

  /* And table search. */
  for (i = 0; character_sets[i].charset != NULL; i++)
    if (octstr_str_compare(charset, character_sets[i].charset) == 0)
      {
	for (j = i; 
	     octstr_str_compare(charset, character_sets[j].charset) == 0; j++)
	  if (octstr_str_compare(number, character_sets[j].nro) == 0)
	    {
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

  if (octstr_get_char(variable, 0) == '$')
    {
      octstr_append_char(*output, '$');
      octstr_destroy(variable);
      ret = 2;
    }
  else
    {
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

  if (ch == '$')
    {
      var = octstr_create("$");
    }
  else if (ch == '(')
    {
      start ++;
      end = octstr_search_char_from(text, ')', start);
      if (end == -1)
	error(0, "WML compiler: braces opened, but not closed for a "
	      "variable.");
      else if (end - start == 0)
	error(0, "WML compiler: empty braces without variable.");
      else
	var = octstr_copy(text, start, end - start);
    }
  else
    {
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

  if ((pos = octstr_search_char(variable, ':')) > 0)
    {
      len = octstr_len(variable) - pos;
      escape = octstr_copy(variable, pos + 1, len - 1);
      octstr_truncate(variable, pos);
      octstr_truncate(escape, len);
      octstr_convert_range(escape, 0, octstr_len(escape), tolower);

      if (octstr_str_compare(escape, "noesc") == 0)
	ret = NOESC;
      else if (octstr_str_compare(escape, "unesc") == 0)
	ret = UNESC;
      else if (octstr_str_compare(escape, "escape") == 0)
	ret = ESC;
      else
	{
	  error(0, "WML compiler: syntax error in variable escaping.");
	  octstr_destroy(escape);
	  return FAILED;
	}
      octstr_destroy(escape);
    }
  else
    ret = NOESC;

  ch = octstr_get_char(variable, 0);
  if (!(isalpha((int)ch)) && ch != '_')
    {
      buf = gw_malloc(70);
      if (sprintf
	  (buf, 
	   "WML compiler: syntax error in variable; name starting with %c.",
	   ch) < 1)
	error(0, "WML compiler: could not format error log output!");
      error(0, buf);
      gw_free(buf);
      return FAILED;
    }
  else
    for (i = 1; i < (int) octstr_len(variable); i++)
      if (!isalnum((int)(ch = octstr_get_char(variable, 0))) && ch != '_')
	{
	  error(0, "WML compiler: syntax error in variable.");
	  return FAILED;
	}

  return ret;
}



/*
 * parse_octet_string - parse an octet string into wbxml_string, the string 
 * is checked for variables. Returns 0 for success, -1 for error.
 */

static int parse_octet_string(Octstr *ostr, wml_binary_t **wbxml)
{
  Octstr *output, *var, *temp = NULL;
  int var_len;
  int start = 0, pos = 0, len;

  /* No variables? Ok, let's take the easy way... */

  if ((pos = octstr_search_char(ostr, '$')) < 0)
    return output_terminated_string(ostr, wbxml);

  len = octstr_len(ostr);
  output = octstr_create_empty();
  var = octstr_create_empty();

  while (pos < len)
    {
      if (octstr_get_char(ostr, pos) == '$')
	{
	  if (pos > start)
	    {
	      temp = octstr_copy(ostr, start, pos - start);
	      octstr_insert(output, temp, octstr_len(output));
	      octstr_destroy(temp);
	    }
	  
	  if ((var_len = parse_variable(ostr, pos, &var, wbxml)) > 0)
	    {
	      if (octstr_get_char(var, 0) == '$')
		/* No, it's not actually variable, but $-character escaped as
		   "$$". So everything should be packed into one string. */
		octstr_insert(output, var, octstr_len(output));
	      else
		/* The string is output as a inline string and the variable 
		   as a inline variable reference. */
		{
		  if (octstr_len(output) > 0)
		    if (output_terminated_string(output, wbxml) == -1)
		      return -1;
		  octstr_truncate(output, 0);
		  output_plain_string(var, wbxml);
		}

	      pos = pos + var_len;
	      start = pos;
	    }
	  else
	    return -1;
	}
      else
	pos ++;
    }

  /* Was there still something after the last variable? */
  if (start < pos)
    {
      if (octstr_len(output) == 0)
	{
	  octstr_destroy(output);
	  output = octstr_copy(ostr, start, pos - start);
	}
      else
	{
	  temp = octstr_copy(ostr, start, pos - start);
	  octstr_insert(output, temp, octstr_len(output));
	  octstr_destroy(temp);
	}
    }

  if (octstr_len(output) > 0)
    if (output_terminated_string(output, wbxml) == -1)
      return -1;
  
  octstr_destroy(output);
  octstr_destroy(var);
  
  return 0;
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
  wbxml->wbxml_string = octstr_create_empty();
  wbxml->utf8map = NULL ;

  return wbxml;
}



/*
 * wml_binary_destroy - frees the memory allocated for the wml_binary_t.
 */

static void wml_binary_destroy(wml_binary_t *wbxml)
{
  if (wbxml != NULL)
    {
      list_destroy(wbxml->string_table);
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
 * output_terminated_string - output an octet string into wbxml_string as a 
 * inline string. Returns 0 for success, -1 for an error.
 */

static int output_terminated_string(Octstr *ostr, wml_binary_t **wbxml)
{
  output_char(STR_I, wbxml);
  if (output_convertable_string(ostr, wbxml) == 0)
    {      
      output_char(STR_END, wbxml);
      return 0;
    }
  return -1;
}



/*
 * output_plain_octet_utf8map - remap ascii to utf8 and
 * output an octet string into wbxml.
 * Returns 0 for success, -1 for an error.
 */

static int output_plain_octet_utf8map(Octstr *ostr, wml_binary_t **wbxml)
{
  long i ;

  for (i = 0; i < octstr_len(ostr); i++) 
    {
      int ch = octstr_get_char(ostr, i) ;
      /* copy chars  [0-127] interval and transform [128-255] */
      if (ch < 0x80) 
	octstr_append_char((*wbxml)->wbxml_string, ch) ;
      else 
	octstr_append_cstr((*wbxml)->wbxml_string, 
			   (*wbxml)->utf8map + ((ch - 128) * 4)) ;
    }
    
  return 0 ;
}



/*
 * output_convertable_string - output an octet string into wbxml.
 * Returns 0 for success, -1 for an error. The content is converted 
 * into UTF-8 if needed.
 */

static int output_convertable_string(Octstr *ostr, wml_binary_t **wbxml)
{
  if ((*wbxml)->utf8map) 
    output_plain_octet_utf8map(ostr, wbxml) ;
  else
    octstr_insert((*wbxml)->wbxml_string, ostr, 
		  octstr_len((*wbxml)->wbxml_string));
  return 0;
}



/*
 * output_plain_string - output an octet string into wbxml.
 * Returns 0 for success, -1 for an error. No conversions.
 */

static int output_plain_string(Octstr *ostr, wml_binary_t **wbxml)
{
  octstr_insert((*wbxml)->wbxml_string, ostr, 
		octstr_len((*wbxml)->wbxml_string));
  return 0;
}



/*
 * output_variable - output a variable reference into an octet string or
 * the string table.
 */

void output_variable(Octstr *variable, Octstr **output, var_esc_t escaped, 
		     wml_binary_t **wbxml)
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

#if NO_STRING_TABLE
  octstr_insert(*output, variable, octstr_len(*output));
  octstr_append_char(*output, STR_END);
  octstr_destroy (variable);
#else
  octstr_append_uintvar(*output, string_table_add(variable, wbxml));
#endif
}



/*
 * string_table_create - reserves memory for the string_table_t and sets the 
 * fields to zeroes and NULLs.
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
 * string_table_add - adds a string to the string table. Duplicates are
 * discarded. The function returns the offset of the string in the 
 * string table; if the string is already in the table then the offset 
 * of the first copy.
 */

static unsigned long string_table_add(Octstr *ostr, wml_binary_t **wbxml)
{
  string_table_t *item = NULL;
  unsigned long i, offset = 0;

  octstr_append_char(ostr, STR_END);

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
    (*wbxml)->string_table_length + octstr_len(ostr);
  list_append((*wbxml)->string_table, item);

  return offset;
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
