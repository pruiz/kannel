#include "config.h"

/* XXX The #ifdef HAVE_LIBXML is a stupid hack to make this not break things
until libxml is installed everywhere we do development. --liw */

#ifndef HAVE_LIBXML
int wml_compiler_not_implemented = 1;
#else

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


/***********************************************************************
 * Declarations of data types. 
 */

typedef enum { NO, YES, NOT_CHECKED } var_allow_t;



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

struct {
  char *attr_value;
  unsigned char token;
} wml_attribute_values[] = {
  { ".com/", 0x85 },
  { ".edu/", 0x86 },
  { ".net/", 0x67 },
  { ".org/", 0x88 },
  { "accept", 0x89 },
  { "bottom", 0x8A },
  { "clear", 0x8B },
  { "delete", 0x8C },
  { "help", 0x8D },
  { "http://", 0x8E },
  { "http://www.", 0x8F },
  { "https://", 0x90 },
  { "https://www.", 0x91 },
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
  { "www.", 0xA1 },
  { NULL }
};



/*
 * Octstr *wbxml_string is the final output in WBXML format.
 */

Octstr *wbxml_string;


/*
 * int wml_state_attr_code_page keeps track of the current attribute 
 * code page.
 *

int wml_state_attr_code_page;


 *
 * int wml_state_tag_code_page keeps track of the current tag
 * code page.
 *

int wml_state_tag_code_page;
*/




/***********************************************************************
 * Declarations of internal functions. These are defined at the end of
 * the file.
 */


/*
 * Parsing functions. These funtions operate on a single node or a 
 * smaller datatype. Look for more details on the functions at the 
 * definitions.
 */


int parse_document(xmlDocPtr document);

int parse_node(xmlNodePtr node);
int parse_element(xmlNodePtr node);
int parse_attribute(xmlAttrPtr attr);
int parse_text(xmlNodePtr node);

int parse_end(void);


unsigned char element_check_content(xmlNodePtr node);

/*
 * Variable functions. These functions are used to find and parse variables.
 */

int parse_variable(Octstr *text, int start, var_allow_t allowed,
		   Octstr **output);


/* Functions to get rid of extra white space. */

Octstr *text_strip_blank(Octstr *text);
void text_shrink_blank(Octstr *text);

/* Output into the wbxml_string. */

int output_char(char byte);
int output_octet_string(Octstr *ostr);

/*
void parse_cdata(xmlNodePtr node);
void parse_entity_ref(xmlNodePtr node);
void parse_entity(xmlNodePtr node);
void parse_pi(xmlNodePtr node);
void parse_comment(xmlNodePtr node);
void parse_document_type(xmlNodePtr node);
void parse_document_frag(xmlNodePtr node);
void parse_notation(xmlNodePtr node);
void parse_html(xmlNodePtr node);
*/



/*
 * The actual compiler function. This operates as interface to the compiler.
 * For more information, look wml_compiler.h. 
 */

int wml_compile(Octstr *wml_text,
		Octstr **wml_binary,
		Octstr **wml_scripts)
{
  int i, ret = 0;
  size_t size;
  char *wml_c_text;

  /* Check the WML-code for \0-characters. */

  size = octstr_len(wml_text);
  wml_c_text = octstr_get_cstr(wml_text);

  for (i = 0; i < size; i++)
    {
      if (wml_c_text[i] == '\0')
	{
	  error(0, 
		"WML compiler: Compiling error in WML text.\n\\0 character found in the middle of the buffer.");
	  return -1;
	}
    }

  /* 
   * An empty octet string for the binary output is created, the wml source
   * is parsed into a parsing tree and the tree is then parsed into binary.
   */

  wbxml_string = octstr_create_empty();

  ret = parse_document(xmlParseMemory(wml_c_text, size+1));

  *wml_binary = octstr_duplicate(wbxml_string);
  *wml_scripts = octstr_create_empty();

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
 * next child of the node one level upwards.
 */

int parse_node(xmlNodePtr node)
{
  int status = 0;

  /* Call for the parser function of the node type. */
  switch (node->type) {
  case XML_ELEMENT_NODE:
    status = parse_element(node);
    break;
  case XML_TEXT_NODE:
    status = parse_text(node);
    break;
    /* ignored at this state of development 
  case XML_DOCUMENT_NODE:
    debug("wap.wml", 0, "WML compiler: Parsing document node.");
    status = parse_document(node);
    break;
  case XML_ATTRIBUTE_NODE:
    debug("wap.wml", 0, "WML compiler: Parsing attribute node.");
    status = parse_attribute(node);
    break;
  case XML_CDATA_SECTION_NODE:
    parse_cdata(node);
    break;
  case XML_ENTITY_REF_NODE:
    parse_entity_ref(node);
    break;
  case XML_ENTITY_NODE:
    parse_entity(node);
    break;
  case XML_PI_NODE:
    parse_pi(node);
    break;
  case XML_COMMENT_NODE:
    * status = parse_comment(node);
       Comments are ignored. *
    break;
  case XML_DOCUMENT_TYPE_NODE:
    parse_document_type(node);
    break;
  case XML_DOCUMENT_FRAG_NODE:
    parse_document_frag(node);
    break;
  case XML_NOTATION_NODE:
    parse_notation(node);
    break;
  case XML_HTML_DOCUMENT_NODE:
    parse_html(node);
    break; */
  default:
    break;
  }

  /* 
   * If node is an element with content, it will need an end tag after it's
   * children. The status for it is returned by parse_element.
   */
  switch (status) {
  case 0:
    if (node->childs != NULL)
      if (parse_node(node->childs) == -1)
	return -1;
    break;
  case 1:
    if (node->childs != NULL)
      if (parse_node(node->childs) == -1)
	return -1;
    if (parse_end() != 0) 
      {
	error(0, "WML compiler: adding end tag failed.");
	return -1;
      }
    break;
  case -1: /* Something went wrong in the parsing. */
    return -1;
  default:
    error(0, "WML compiler: undefined return value in a parse function.");
    return -1;
    break;
  }

  if (node->next != NULL)
    if (parse_node(node->next) ==-1)
      return -1;

  return 0;
}


/*
 * parse_document - the parsing function for the document node.
 * The function outputs the WBXML version, WML public id and the
 * character set values into start of the wbxml_string.
 */

int parse_document(xmlDocPtr document)
{
  int ret = 0;

  /* 
   * A bad hack, WBXML version is assumed to be 1.1, charset is assumed 
   * to be ISO-8859-1!
   */
  if (output_char(0x01) == 0) /* WBXML Version number 1.1 */
   {
     if (output_char(0x04) == 0) /* WML 1.1 Public ID */
       {
	 if (output_char(0x04) == 0) /* Charset=ISO-8859-1 */
	   ret = output_char(0x00);  /* String table length=0 */
	 else
	   error(0, "WML compiler: could not output string table length.");	   
       }
     else
    error(0, "WML compiler: could not output WML public ID.");       
   }
  else
    error(0, "WML compiler: could not output WBXML version number.");

  if (ret == 0)
    return parse_node(xmlDocGetRootElement(document));
  else
    {
      error(0, "WML compiler: could not output charset.");
      return -1;
    }
}


/*
 * parse_element - the parsing function for an element node.
 * The element tag is encoded into one octet hexadecimal value, 
 * if possible. Otherwise it is encoded as text. If the element 
 * needs an end tag, the function returns 1, for no end tag 0
 * and -1 for an error.
 */

int parse_element(xmlNodePtr node)
{
  int i, add_end_tag = 0;
  unsigned char wbxml_hex, status_bits;
  xmlAttrPtr attribute;
  Octstr *name;

  name = octstr_create_tolower(node->name);

  /* Check, if the tag can be found from the code page. */

  for (i = 0; wml_elements[i].element != NULL; i++)
    {
      if (octstr_compare(name, octstr_create(wml_elements[i].element)) == 0)
	{
	  wbxml_hex = wml_elements[i].token;
	  if ((status_bits = element_check_content(node)) > 0)
	    {
	      wbxml_hex = wbxml_hex + status_bits;
	      /* If this node has children, the end tag must be added after 
		 them. */
	      if ((status_bits & 0x40) == 0x40)
		add_end_tag = 1;
	    }
	  if (output_char(wbxml_hex) != 0)
	    {
	      error(0, "WML compiler: could not output WML tag.");	      
	      return -1;
	    }
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
	  parse_attribute(attribute);
	  attribute = attribute->next;
	}
      if (parse_end() != 0) 
	error(0, "WML compiler: adding end tag to attribute list failed.");
    }

  octstr_destroy(name);
  return add_end_tag;
}


/*
 * element_check_content - a helper function for parse_element for checking 
 * if an element has content or attributes. Returns status bit for attributes 
 * (0x80) and another for content (0x40) added into one octet.
 */

unsigned char element_check_content(xmlNodePtr node)
{
  unsigned char status_bits = 0x00;

  if (node->childs != NULL)
    status_bits = 0x40;

  if (node->properties != NULL)
    status_bits = status_bits + 0x80;

  return status_bits;
}


/*
 * parse_attribute - the parsing function for attributes. The function 
 * encodes the attribute (and probably start of the value) as a one 
 * hexadecimal octet. The value (or the rest of it) is coded as a string 
 * maybe using predefined attribute value tokens to reduce the length
 * of the output. Returns 0 for success, -1 for error.
 */


int parse_attribute(xmlAttrPtr attr)
{
  int i, j, status = 0, coded_length = 0;
  unsigned char wbxml_hex = 0x00;
  Octstr *attribute, *value, *attr_i, *val_j;

  attribute = octstr_create_tolower(attr->name);
  if (attr->val != NULL)
    value = octstr_create_tolower(attr->val->content);
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
	       octstr_compare(attr_i,
			      octstr_create(wml_attributes[j].attribute))
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
	  break;
	}
      octstr_destroy(attr_i);      
    }
  output_char(wbxml_hex);

  /*
   * The rest of the attribute is coded as a inline string. Not as 
   * compressed as it could be... This will be enchanced later.
   */
  if (value != NULL && coded_length < octstr_len(value))
    {
      if (coded_length == 0) 
	{
	  if ((status = output_octet_string(octstr_create(attr->val->content))) 
	      != 0)
	    error(0, 
		  "WML compiler: could not output attribute value as a string.");
	}
      else
	{
	  if ((status = output_octet_string(octstr_copy(value, coded_length, 
							octstr_len(value) -
							coded_length))) != 0)
	    error(0, 
		  "WML compiler: could not output attribute value as a string.");
	}
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
 * parse_end - adds end tag to an element. Returns 0 success, -1 for error.
 */

int parse_end(void)
{
  return output_char(END);
}



/*
 * parse_text - a text string parsing function.
 * This function parses a text node. The text is searched for variables that
 * are them passed to function that parses them with information if the 
 * variables are allowed in this text context.
 */

int parse_text(xmlNodePtr node)
{
  int i;
  var_allow_t var_allowed = NOT_CHECKED;
  Octstr *temp1, *temp2;

  temp1 = octstr_create(node->content);

  text_shrink_blank(temp1);
  text_strip_blank(temp1);

  if (octstr_len(temp1) == 0)
    return 0;

  if (octstr_search_char(temp1, '$') > -1)
    return -1;

  if (output_char(STR_I) == -1)
      error(0, "WML compiler: couldn't output STR_I before a text field.");
  temp2 = octstr_duplicate(wbxml_string);
  octstr_destroy(wbxml_string);
  wbxml_string = octstr_cat(temp2, temp1);
  if (output_char(STR_END) == -1)
      error(0, "WML compiler: couldn't output STR_END after a text field.");

  /* Memory cleanup. */
  octstr_destroy(temp1);
  octstr_destroy(temp2);

  return 0;
}



/*
 * parse_variable - a variable parsing function. 
 * Arguments:
 * - text: the octet string containing a variable
 * - start: the starting position of the variable not including 
 *   trailing &
 * - allowed: iformation if the variables are allowed in this context
 * - output: octet string that returns the encoded variable
 * Returns: lenth of the variable for success, -1 for failure. A variable 
 * encoutered in a context that doesn't allow them is considered a failure.
 */

int parse_variable(Octstr *text, int start, var_allow_t allowed,
		   Octstr **output)
{
  return -1;
}



/*
int parse_pi(xmlNodePtr node)
{
  output_char(PI);

  return 1;
}
*/


/*
 * output_char - output a character into wbxml_string.
 * Returns 0 for success, -1 for error.
 */

int output_char(char byte)
{
  if ((wbxml_string = octstr_cat_char(wbxml_string, byte)) == NULL)
    return -1;

  return 0;
}



/*
 * output_octet_string - output an octet string into wbxml_string.
 * Returns 0 for success, -1 for error.
 */

int output_octet_string(Octstr *ostr)
{
  if (output_char(STR_I) == 0)
    if ((wbxml_string = octstr_cat(wbxml_string, ostr)) != NULL)
      if (output_char(STR_END) == 0)
	return 0;

  return -1;
}



/*
 * text_strip_blank - strips white space from start and end of a octet
 * string.
 */

Octstr *text_strip_blank(Octstr *text)
{
  int start = 0, end;

  /* Remove white space from the beginning of the text */
  while (isspace(octstr_get_char(text, start)))
    start ++;

  /* and from the end. */
  end = octstr_len(text) - 1;

  while (isspace(octstr_get_char(text, end)))
    end--;

  return octstr_copy(text, start, end);
}


/*
 * text_shrink_blank - shrinks following white space characters into one 
 * white space.
 */

void text_shrink_blank(Octstr *text)
{
  int i, j, end;

  end = octstr_len(text);

  /* Shrink white spaces to one  */
  for(i = 0; i < end; i++)
    {
      if(isspace(octstr_get_char(text, i)))
	{
	  j = i + 1;
	  while (isspace(octstr_get_char(text, j)))
	    j ++;
	  if (j - i > 1)
	    octstr_delete(text, i, j - i);
	}
    }

  return;
}


#endif
