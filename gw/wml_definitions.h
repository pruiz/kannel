/*
 * wml_definitions.h - definitions needed by WML compiler
 *
 * This file contains fefinitions for global tokens and structures containing 
 * element and attribute tokens for the code page 1.
 *
 *
 * Tuomas Luttinen for Wapit Ltd.
 */


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
  { "multiple", "false", 0x1F },
  { "multiple", "true", 0x20 },
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

/*
 * Character sets.
 */

static unsigned char utf8map_iso8859_7[] = {
#include "utf8map_iso8859-7.h"
} ;

static unsigned char utf8map_win1251[] = {
#include "utf8map_win1251.h"
} ;

static unsigned char utf8map_win1253[] = {
#include "utf8map_win1253.h"
} ;

static unsigned char utf8map_win1257[] = {
#include "utf8map_win1257.h"
} ;

static unsigned char utf8map_koi8r[] = {
#include "utf8map_koi8r.h"
} ;

struct {
  char *charset;
  char *nro;
  unsigned int MIBenum;
  unsigned char *utf8map;
} character_sets[] = {
  { "ISO", "8859-1", 4, NULL }, /* ISOLatin1 */
  { "ISO", "8859-2", 5, NULL }, /* ISOLatin2 */
  { "ISO", "8859-3", 6, NULL }, /* ISOLatin3 */
  { "ISO", "8859-4", 7, NULL }, /* ISOLatin4 */
  { "ISO", "8859-5", 8, NULL }, /* ISOLatinCyrillic */
  { "ISO", "8859-6", 9, NULL }, /* ISOLatinArabic */
  { "ISO", "8859-7", 10, utf8map_iso8859_7 }, /* ISOLatinGreek */
  { "ISO", "8859-8", 11, NULL }, /* ISOLatinHebrew */
  { "ISO", "8859-9", 12, NULL }, /* ISOLatin5 */
  /* Russian, uses UTF-8 but with remapping */
  { "WINDOWS", "1251", 106, utf8map_win1251 }, 
  /* Windows-1253, uses UTF-8 but with remapping */
  { "WINDOWS", "1253", 106, utf8map_win1253 }, 
  /* Latvian, uses UTF-8 but with remapping */
  { "WINDOWS", "1257", 106, utf8map_win1257 }, 
  /* Russian, uses UTF-8 but with remapping */
  { "KOI8", "R", 106, utf8map_koi8r }, 
  /* Note!! If you want to add character sets, put them above this line. */
  { "UTF", "8", 106, NULL }, /* UTF-8, the default. */
  {NULL}
};
