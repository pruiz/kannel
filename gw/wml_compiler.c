/*
 * wml_compiler.c - compiling WML to WML binary
 *
 * This is an implemention for WML compiler for compiling the WML text 
 * format to WML binary format, which is used for transmitting the 
 * decks to the mobile terminal to decrease the use of the bandwidth.
 *
 *
 * Tuomas Luttinen for WapIT Ltd.
 */


#include "config.h"



#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include <gnome-xml/xmlmemory.h>
#include <gnome-xml/parser.h>
#include <gnome-xml/tree.h>
#include <gnome-xml/debugXML.h>

#include "gwlib/gwlib.h"
#include "wml_compiler.h"

/***********************************************************************
 * Declarations of global tokens. 
 */

#define SWITCH_PAGE 0x00
#define END         0x01
#define ENTITY      0x02
#define STR_I       0x03
#define LITERAL     0x04
#define EXT_I_0     0x40
#define EXT_I_1     0x41
#define EXT_I_2     0x42
#define PI          0x43
#define LITERAL_C   0x44
#define EXT_T_0     0x80
#define EXT_T_1     0x81
#define EXT_T_2     0x82
#define STR_T       0x83
#define LITERAL_A   0x84
#define EXT_0       0xC0
#define EXT_1       0xC1
#define EXT_2       0xC2
#define OPAQUE      0xC3
#define LITERAL_AC  0xC4

#define STR_END     0x00

#define CHILD_BIT   0x40
#define ATTR_BIT    0x80


/***********************************************************************
 * Declarations of data types. 
 */

typedef enum { NO, YES, NOT_CHECKED } var_allow_t;

typedef enum { NOESC, ESC, UNESC, FAILED } var_esc_t;



/***********************************************************************
 * Declarations of global variables. 
 */

/*
 * Elements as defined by tag code page 0.
 */

struct {
  char *element;
  unsigned char token;
} wml_elements[] = {
  { "wml", 0x3F },
  { "card", 0x27 },
  { "do", 0x28 },
  { "onevent", 0x33 },
  { "head", 0x2C },
  { "template", 0x3B },
  { "access", 0x23 },
  { "meta", 0x30 },
  { "go", 0x2B },
  { "prev", 0x32 },
  { "refresh", 0x36 },
  { "noop", 0x31 },
  { "postfield", 0x21 },
  { "setvar", 0x3E },
  { "select", 0x37 },
  { "optgroup", 0x34 },
  { "option", 0x35 },
  { "input", 0x2F },
  { "fieldset", 0x2A },
  { "timer", 0x3C },
  { "img", 0x2E },
  { "anchor", 0x22 },
  { "a", 0x1C },
  { "table", 0x1F },
  { "tr", 0x1E },
  { "td", 0x1D },
  { "em", 0x29 },
  { "strong", 0x39 },
  { "b", 0x24 },
  { "i", 0x2D },
  { "u", 0x3D },
  { "big", 0x25 },
  { "small", 0x38 },
  { "p", 0x20 },
  { "br", 0x26 },
  { NULL }
};


/*
 * Attributes as defined by attribute code page 0.
 */

struct {
  char *attribute;
  char *a_value;
  unsigned char token;
} wml_attributes[] = {
  { "accept_charset", NULL, 0x05 },
  { "align", NULL, 0x52 },
  { "align", "bottom", 0x06 },
  { "align", "center", 0x07 },
  { "align", "left", 0x08 },
  { "align", "middle", 0x09 },
  { "align", "right", 0x0A },
  { "align", "top", 0x0B },
  { "alt", NULL, 0x0C },
  { "class", NULL, 0x54 },
  { "columns", NULL, 0x53 },
  { "content", NULL, 0x0D },
  { "content", "application/vnd.wap.wmlc;charset=", 0x5C },
  { "domain", NULL, 0x0F },
  { "emptyok", "false", 0x10 },
  { "emptyok", "true", 0x11 },
  { "format", NULL, 0x12 },
  { "forua", "false", 0x56 },
  { "forua", "true", 0x57 },
  { "height", NULL, 0x13 },
  { "href", NULL, 0x4A },
  { "href", "http://", 0x4B },
  { "href", "https://", 0x4C },
  { "hspace", NULL, 0x14 },
  { "http-equiv", NULL, 0x5A },
  { "http-equiv", "content-type", 0x5B },
  { "http-equiv", "expires", 0x5D },
  { "id", NULL, 0x55 },
  { "ivalue", NULL, 0x15 },
  { "iname", NULL, 0x16 },
  { "label", NULL, 0x18 },
  { "localsrc", NULL, 0x19 },
  { "maxlength", NULL, 0x1A },
  { "method", "get", 0x1B },
  { "method", "post", 0x1C },
  { "mode", "nowrap", 0x1D },
  { "mode", "wrap", 0x1E },
  { "multible", "false", 0x1F },
  { "multible", "true", 0x20 },
  { "name", NULL, 0x21 },
  { "newcontext", "false", 0x22 },
  { "newcontext", "true", 0x23 },
  { "onenterbackward", NULL, 0x25 },
  { "onenterforward", NULL, 0x26 },
  { "onpick", NULL, 0x24 },
  { "ontimer", NULL, 0x27 },
  { "optional", "false", 0x28 },
  { "optional", "true", 0x29 },
  { "path", NULL, 0x2A },
  { "scheme", NULL, 0x2E },
  { "sendreferer", "false", 0x2F },
  { "sendreferer", "true", 0x30 },
  { "size", NULL, 0x31 },
  { "src", NULL, 0x32 },
  { "src", "http://", 0x58 },
  { "src", "https://", 0x59 },
  { "ordered", "false", 0x33 },
  { "ordered", "true", 0x34 },
  { "tabindex", NULL, 0x35 },
  { "title", NULL, 0x36 },
  { "type", NULL, 0x37 },
  { "type", "accept", 0x38 },
  { "type", "delete", 0x39 },
  { "type", "help", 0x3A },
  { "type", "password", 0x3B },
  { "type", "onpick", 0x3C },
  { "type", "onenterbackward", 0x3D },
  { "type", "onenterforward", 0x3E },
  { "type", "ontimer", 0x3F },
  { "type", "options", 0x45 },
  { "type", "prev", 0x46 },
  { "type", "reset", 0x47 },
  { "type", "text", 0x48 },
  { "type", "vnd.", 0x49 },
  { "value", NULL, 0x4D },
  { "vspace", NULL, 0x4E },
  { "width", NULL, 0x4F },
  { "xml:lang", NULL, 0x50 },
  { NULL }
};


/*
 * Attribute value codes.
 */

typedef struct {
  char *attr_value;
  unsigned char token;
} wml_attr_value_t;

static
wml_attr_value_t wml_attribute_values[] = {
  { "accept", 0x89 },
  { "bottom", 0x8A },
  { "clear", 0x8B },
  { "delete", 0x8C },
  { "help", 0x8D },
  { "middle", 0x93 },
  { "nowrap", 0x94 },
  { "onenterbackward", 0x96 },
  { "onenterforward", 0x97 },
  { "onpick", 0x95 },
  { "ontimer", 0x98 },
  { "options", 0x99 },
  { "password", 0x9A },
  { "reset", 0x9B },
  { "text", 0x9D },
  { "top", 0x9E },
  { "unknown", 0x9F },
  { "wrap", 0xA0 },
  { NULL }
};

/*
 * URL value codes.
 */

static
wml_attr_value_t wml_URL_values[] = {
  { "www.", 0xA1 },
  { ".com/", 0x85 },
  { ".edu/", 0x86 },
  { ".net/", 0x67 },
  { ".org/", 0x88 },
  { NULL }
};


#if 0
/*
 * int wml_state_attr_code_page keeps track of the current attribute 
 * code page.
 */

int wml_state_attr_code_page;


/*
 * int wml_state_tag_code_page keeps track of the current tag
 * code page.
 */

int wml_state_tag_code_page;

#endif



/***********************************************************************
 * Declarations of internal functions. These are defined at the end of
 * the file.
 */


/*
 * Parsing functions. These funtions operate on a single node or a 
 * smaller datatype. Look for more details on the functions at the 
 * definitions.
 */


static int parse_document(xmlDocPtr document, Octstr **wbxml_string);

static int parse_node(xmlNodePtr node, Octstr **wbxml_string);
static int parse_element(xmlNodePtr node, Octstr **wbxml_string);
static int parse_attribute(xmlAttrPtr attr, Octstr **wbxml_string);
static int parse_attr_value(Octstr *attr_value, wml_attr_value_t *tokens, 
			    Octstr **wbxml_string);
static int parse_text(xmlNodePtr node, Octstr **wbxml_string);
static int parse_octet_string(Octstr *ostr, Octstr **wbxml_string);

static void parse_end(Octstr **wbxml_string);

static unsigned char element_check_content(xmlNodePtr node);
static int check_if_url(int hex);

/*
 * Variable functions. These functions are used to find and parse variables.
 */

static int parse_variable(Octstr *text, int start, Octstr **output, 
			  Octstr **wbxml_string);
static Octstr *get_variable(Octstr *text, int start);
static var_esc_t check_variable_syntax(Octstr *variable);


/* Output into the wbxml_string. */

static void output_char(char byte, Octstr **wbxml_string);
static int output_octet_string(Octstr *ostr, Octstr **wbxml_string);
static int output_plain_octet_string(Octstr *ostr, Octstr **wbxml_string);
static Octstr *output_variable(Octstr *variable, var_esc_t escaped, 
			       Octstr **wbxml_string);



/*
 * The actual compiler function. This operates as interface to the compiler.
 * For more information, look wml_compiler.h. 
 */

int wml_compile(Octstr *wml_text,
		Octstr **wml_binary)
{
  int ret = 0;
  size_t size;
  xmlDocPtr pDoc = NULL;
  char *wml_c_text;

  *wml_binary = octstr_create_empty();

  /* Remove the extra space from start and the end of the WML Document. */

  octstr_strip_blank(wml_text);

  /* Check the WML-code for \0-characters. */

  size = octstr_len(wml_text);
  wml_c_text = octstr_get_cstr(wml_text);

  if (octstr_search_char(wml_text, '\0') != -1)
    {    
      error(0, 
	    "WML compiler: Compiling error in WML text. "
	    "\\0 character found in the middle of the WML source.");
      return -1;
    }

  /* 
   * An empty octet string for the binary output is created, the wml source
   * is parsed into a parsing tree and the tree is then compiled into binary.
   */

#if HAVE_LIBXML_1_8_6
  /* XXX this seems to work around a bug in libxml, which is even in
     1.8.6. --liw */
  pDoc = xmlParseMemory(wml_c_text, size + 1);
#else
  /* XXX this should be the correct version. --liw */
  pDoc = xmlParseMemory(wml_c_text, size);
#endif

  ret = parse_document(pDoc, wml_binary);
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

static int parse_node(xmlNodePtr node, Octstr **wbxml_string)
{
  int status = 0;

  /* Call for the parser function of the node type. */
  switch (node->type) {
  case XML_ELEMENT_NODE:
    status = parse_element(node, wbxml_string);
    break;
  case XML_TEXT_NODE:
    status = parse_text(node, wbxml_string);
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
    if (node->childs != NULL)
      if (parse_node(node->childs, wbxml_string) == -1)
	return -1;
    break;
  case 1:
    if (node->childs != NULL)
      if (parse_node(node->childs, wbxml_string) == -1)
	return -1;
    parse_end(wbxml_string);
    break;
  case -1: /* Something went wrong in the parsing. */
    return -1;
  default:
    error(0, "WML compiler: undefined return value in a parse function.");
    return -1;
    break;
  }

  if (node->next != NULL)
    if (parse_node(node->next, wbxml_string) == -1)
      return -1;

  return 0;
}


/*
 * parse_document - the parsing function for the document node.
 * The function outputs the WBXML version, WML public id and the
 * character set values into start of the wbxml_string.
 */

static int parse_document(xmlDocPtr document, Octstr **wbxml_string)
{
  if (document == NULL)
    {
      error(0, "WML compiler: XML parsing failed, no parsed document.");
      error(0, "Most probably an error in the WML source.");
      return -1;
    }

  /* 
   * A bad hack, WBXML version is assumed to be 1.1, charset is assumed 
   * to be ISO-8859-1!
   */
  output_char(0x01, wbxml_string); /* WBXML Version number 1.1 */
  output_char(0x04, wbxml_string); /* WML 1.1 Public ID */
  output_char(0x04, wbxml_string); /* Charset=ISO-8859-1 */
  output_char(0x00, wbxml_string); /* String table length=0 */

  return parse_node(xmlDocGetRootElement(document), wbxml_string);
}


/*
 * parse_element - the parsing function for an element node.
 * The element tag is encoded into one octet hexadecimal value, 
 * if possible. Otherwise it is encoded as text. If the element 
 * needs an end tag, the function returns 1, for no end tag 0
 * and -1 for an error.
 */

static int parse_element(xmlNodePtr node, Octstr **wbxml_string)
{
  int i, add_end_tag = 0;
  unsigned char wbxml_hex, status_bits;
  xmlAttrPtr attribute;
  Octstr *name;

  name = octstr_create(node->name);

  /* Check, if the tag can be found from the code page. */

  for (i = 0; wml_elements[i].element != NULL; i++)
    {
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
	  output_char(wbxml_hex, wbxml_string);
	  break;
	}
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
	  parse_attribute(attribute, wbxml_string);
	  attribute = attribute->next;
	}
      parse_end(wbxml_string);
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

  if (node->childs != NULL)
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


static int parse_attribute(xmlAttrPtr attr, Octstr **wbxml_string)
{
  int i, j, status = 0;
  int coded_length = 0;
  unsigned char wbxml_hex = 0x00;
  Octstr *attribute = NULL, *value = NULL, *attr_i = NULL, *val_j = NULL, 
    *p = NULL;

  attribute = octstr_create(attr->name);
  if (attr->val != NULL)
    value = octstr_create(attr->val->content);
  else 
    value = NULL;

  /* Check if the attribute is found on the code page. */

  for (i = 0; wml_attributes[i].attribute != NULL; i++)
    {
      if (octstr_compare(attribute, 
			 (attr_i = octstr_create(wml_attributes[i].attribute)))
			 == 0)
	{
	  /* Check if there's an attribute start token with good value on 
	     the code page. */
	  for (j = i; 
	       octstr_str_compare(attr_i, wml_attributes[j].attribute)
		 == 0; j++)
	    {
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
	    }
	  octstr_destroy(attr_i);      
	  break;
	}
      octstr_destroy(attr_i);      
    }

  output_char(wbxml_hex, wbxml_string);

  /* The rest of the attribute is coded as a inline string. */
  if (value != NULL && coded_length < (int) octstr_len(value))
    {
      if (coded_length == 0) 
	p = octstr_create(attr->val->content); 
      else
	p = octstr_copy(value, coded_length, octstr_len(value) - 
			coded_length); 

      if (check_if_url(wbxml_hex))
	status = parse_attr_value(p, wml_URL_values,
				  wbxml_string);
      else
	status = parse_attr_value(p, wml_attribute_values,
				  wbxml_string);
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
			     Octstr **wbxml_string)
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
	output_char(wbxml_hex, wbxml_string);	
	octstr_delete(attr_value, 0, strlen(tokens[i].attr_value));	
	break;
      default:
	/* There is some text before the first hit, that has to handled too. */
	gw_assert(pos <= octstr_len(attr_value));
	
	cut_text = octstr_copy(attr_value, 0, pos);
	if (parse_octet_string(cut_text, wbxml_string) != 0)
	  return -1;
	octstr_destroy(cut_text);

	wbxml_hex = tokens[i].token;
	output_char(wbxml_hex, wbxml_string);	

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
	parse_attr_value(attr_value, tokens, wbxml_string);
      else
	if (parse_octet_string(attr_value, wbxml_string) != 0)
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

static void parse_end(Octstr **wbxml_string)
{
  output_char(END, wbxml_string);
}



/*
 * parse_text - a text string parsing function.
 * This function parses a text node. 
 */

static int parse_text(xmlNodePtr node, Octstr **wbxml_string)
{
  int ret;
  /*  var_allow_t var_allowed = NOT_CHECKED; */
  Octstr *temp;

  temp = octstr_create(node->content);

  octstr_shrink_blank(temp);
  octstr_strip_blank(temp);

  if (octstr_len(temp) == 0)
    ret = 0;
  else
    ret = parse_octet_string(temp, wbxml_string);

  /* Memory cleanup. */
  octstr_destroy(temp);

  return ret;
}



/*
 * parse_variable - a variable parsing function. 
 * Arguments:
 * - text: the octet string containing a variable
 * - start: the starting position of the variable not including 
 *   trailing &
 * Returns: lenth of the variable for success, -1 for failure. A variable 
 * encoutered in a context that doesn't allow them is considered a failure.
 * Parsed variable is returned as an octet string in Octstr **output.
 */

static int parse_variable(Octstr *text, int start, Octstr **output, 
			  Octstr **wbxml_string)
{
  var_esc_t esc;
  int ret;
  Octstr *variable;

  variable = get_variable(text, start + 1);

  if (octstr_get_char(variable, 0) == '$')
    {
      *output = octstr_create("$");
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
	if ((*output = output_variable(variable, esc, wbxml_string)) == NULL)
	  return -1;
    }

  octstr_destroy (variable);
  return ret;
}



/*
 * get_variable - get the variable name from text.
 * Octstr *text contains the text with a ariable name starting at point 
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
	error(0, "WML compiler: braces opened, but not closed for a variable.");
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

  gw_assert(var != NULL);

  return var;
}



/*
 * check_variable_syntax - checks the variable syntax and the possible 
 * escepe mode it has. Octstr *variable contains the variable string.
 */

static var_esc_t check_variable_syntax(Octstr *variable)
{
  Octstr *escaped, *noesc, *unesc, *escape;
  char *buf;
  char ch;
  int pos, len, i;
  var_esc_t ret;

  if ((pos = octstr_search_char(variable, ':')) > 0)
    {
      buf = gw_malloc((len = (octstr_len(variable) - pos) - 1));
      octstr_get_many_chars(buf, variable, pos + 1, len);
      escaped = octstr_create_tolower(buf);
      octstr_truncate(variable, pos);
      octstr_truncate(escaped, len);

      noesc = octstr_create("noesc");
      unesc = octstr_create("unesc");
      escape = octstr_create("escape");

      if (octstr_compare(escaped, noesc) == 0)
	ret = NOESC;
      else if (octstr_compare(escaped, unesc) == 0)
	ret = UNESC;
      else if (octstr_compare(escaped, escape) == 0)
	ret = ESC;
      else
	{
	  error(0, "WML compiler: syntax error in variable escaping.");
	  octstr_destroy(escaped);
	  octstr_destroy(escape);
	  octstr_destroy(noesc);
	  octstr_destroy(unesc);
	  gw_free(buf);
	  return FAILED;
	}
      octstr_destroy(escaped);
      octstr_destroy(escape);
      octstr_destroy(noesc);
      octstr_destroy(unesc);
      gw_free(buf);
    }
  else
    ret = NOESC;

  ch = octstr_get_char(variable, 0);
  if (!(isalpha((int)ch)) && ch != '_')
    {
      buf = gw_malloc(70);
      if (sprintf(buf, 
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

static int parse_octet_string(Octstr *ostr, Octstr **wbxml_string)
{
  Octstr *output, *temp1, *temp2 = NULL, *var;
  int var_len;
  int start = 0, pos = 0, len;

  /* No variables? Ok, let's take the easy way... */

  if (octstr_search_char(ostr, '$') < 0)
    return output_octet_string(ostr, wbxml_string);

  len = octstr_len(ostr);
  output = octstr_create_empty();
  var = octstr_create_empty();

  for (pos = 0; pos < len; pos ++)
    {
      if (octstr_get_char(ostr, pos) == '$')
	{
	  temp1 = octstr_copy(ostr, start, pos - start);
	  if ((var_len = parse_variable(ostr, pos, &var, wbxml_string)) > 0)
	    {
	      if (octstr_get_char(var, 0) == '$')
		/* No, it's not actually variable, but $-character escaped as
		   "$$". So everything should be packed into one string. */
		{
		  temp2 = octstr_cat(temp1, var);
		  octstr_destroy(temp1);

		  if (octstr_len(output) == 0)
		    {	
		      output = octstr_duplicate(temp2);
		      octstr_destroy(temp2);
		    }
		  else
		    {
		      temp1 = octstr_cat(output, temp2);
		      output = octstr_duplicate(temp1);
		      octstr_destroy(temp1);
		    }
		}
	      else
		/* The string is output as a inline string and the variable 
		   as a inline variable reference. */
		{
		  output_octet_string(temp1, wbxml_string);
		  octstr_destroy(temp1);
		  output_plain_octet_string(var, wbxml_string);
		}

	      pos = pos + var_len;
	      start = pos;
	    }
	  else
	    return -1;
	}
    }
  

  /* Was there still something after the last variable? */
  if (start < pos - 1)
    {
      if (octstr_len(output) != 0)
	{
	  temp2 = octstr_copy(ostr, start, pos - start);
	  temp1 = octstr_cat(output, temp2);
	  output = octstr_duplicate(temp1);
	  octstr_destroy(temp1);
	}
      else
	{
	  temp1 = octstr_copy(ostr, start, pos - start);
	  output_octet_string(temp1, wbxml_string);
	  octstr_destroy(temp1);
	}
    }

  if (octstr_len(output) > 0)
    if (output_octet_string(output, wbxml_string) == -1)
      return -1;
  
  octstr_destroy(output);
  octstr_destroy(var);
  
  return 0;
}



/*
 * output_char - output a character into wbxml_string.
 * Returns 0 for success, -1 for error.
 */

static void output_char(char byte, Octstr **wbxml_string)
{
  octstr_append_char(*wbxml_string, byte);
}



/*
 * output_octet_string - output an octet string into wbxml_string as a 
 * inline string. Returns 0 for success, -1 for an error.
 */

static int output_octet_string(Octstr *ostr, Octstr **wbxml_string)
{
  output_char(STR_I, wbxml_string);
  if (output_plain_octet_string(ostr, wbxml_string) == 0)
    {      
      output_char(STR_END, wbxml_string);
      return 0;
    }
  return -1;
}



/*
 * output_plain_octet_string - output an octet string into wbxml_string.
 * Returns 0 for success, -1 for an error.
 */

static int output_plain_octet_string(Octstr *ostr, Octstr **wbxml_string)
{
  Octstr *temp;
  if ((temp = octstr_cat(*wbxml_string, ostr)) == NULL)
    return -1;

  octstr_destroy(*wbxml_string);

  *wbxml_string = octstr_duplicate(temp);

  octstr_destroy(temp);

  return 0;
}



/*
 * output_variable - output a variable reference into a octet string
 * that is returned to the caller. Return NULL for an error.
 */

static Octstr *output_variable(Octstr *variable, var_esc_t escaped, 
			       Octstr **wbxml_string)
{
  char ch;
  char cha[2];
  Octstr *ret, *temp;

  switch (escaped)
    {
    case ESC:
      ch = EXT_I_0;
      sprintf(cha, "%c", ch);
      if ((ret = octstr_create(cha)) == NULL)
	error(0, "WML compiler: could not output EXT_I_0.");
      break;
    case UNESC:
      ch = EXT_I_1;
      sprintf(cha, "%c", ch);
      if ((ret = octstr_create(cha)) == NULL)
	error(0, "WML compiler: could not output EXT_I_1.");
      break;
    default:
      ch = EXT_I_2;
      sprintf(cha, "%c", ch);
      if ((ret = octstr_create(cha)) == NULL)
	error(0, "WML compiler: could not output EXT_I_2.");
      break;
    }

  temp = octstr_cat(ret, variable);
  octstr_destroy(ret);
  ret = octstr_cat_char(temp, STR_END);
  octstr_destroy(temp);

  if (ret == NULL)
    error(0, "WML compiler: could not output variable name.");

  return ret;
}
