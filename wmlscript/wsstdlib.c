/*
 *
 * wsstdlib.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Standard libraries.
 *
 */

#include "wsint.h"

/* TODO: the function registry could have argument type specifier
   strings.  These could be used to generate extra warnings when
   functions are called with wrong arguments.  However, this might not
   be fully conformable to the WMLScript specification.  I think that
   the interpreter might do an automatic type conversion so the
   warnings are less useful.  But, these warnings could be enabled in
   the `-Wpedantic' mode. */

/********************* Types and definitions ****************************/

/* Calculate the number of function registries in the array `f'. */
#define NF(f) (sizeof(f) / sizeof(f[0]))

/* Information about a standard library function. */
struct WsStdLibFuncRegRec
{
  char *name;

  /* The exact number of arguments. */
  int num_args;
};

typedef struct WsStdLibFuncRegRec WsStdLibFuncReg;

/* Information about a standard library. */
struct WsStdLibRegRec
{
  char *name;

  WsUInt16 library_id;

  /* The number of functions in this library. */
  WsUInt8 num_functions;

  /* The functions are given in their index order. */
  WsStdLibFuncReg *functions;
};

typedef struct WsStdLibRegRec WsStdLibReg;

/********************* Static variables *********************************/

static WsStdLibFuncReg lib_lang_functions[] =
{
  {"abs",		1},
  {"min",		2},
  {"max",		2},
  {"parseInt",		1},
  {"parseFloat",	1},
  {"isInt",		1},
  {"isFloat",		1},
  {"maxInt",		0},
  {"minInt",		0},
  {"float",		0},
  {"exit",		1},
  {"abort",		1},
  {"random",		1},
  {"seed",		1},
  {"characterSet",	0},
};

static WsStdLibFuncReg lib_float_functions[] =
{
  {"int",	1},
  {"floor",	1},
  {"ceil",	1},
  {"pow",	2},
  {"round",	1},
  {"sqrt",	1},
  {"maxFloat",	0},
  {"minFloat",	0},
};

static WsStdLibFuncReg lib_string_functions[] =
{
  {"length",	1},
  {"isEmpty",	1},
  {"charAt",	2},
  {"subString",	3},
  {"find",	2},
  {"replace",	3},
  {"elements",	2},
  {"elementAt",	3},
  {"removeAt",	3},
  {"replaceAt",	4},
  {"insertAt",	4},
  {"squeeze",	1},
  {"trim",	1},
  {"compare",	2},
  {"toString",	1},
  {"format",	2},
};

static WsStdLibFuncReg lib_url_functions[] =
{
  {"isValid",		1},
  {"getScheme",		1},
  {"getHost",		1},
  {"getPort",		1},
  {"getPath",		1},
  {"getParameters",	1},
  {"getQuery",		1},
  {"getFragment",	1},
  {"getBase",		0},
  {"getReferer",	0},
  {"resolve",		2},
  {"escapeString",	1},
  {"unescapeString",	1},
  {"loadString",	2},
};

static WsStdLibFuncReg lib_wmlbrowser_functions[] =
{
  {"getVar",		1},
  {"setVar",		2},
  {"go",		1},
  {"prev",		0},
  {"newContext",	0},
  {"getCurrentCard",	0},
  {"refresh",		0},
};

static WsStdLibFuncReg lib_dialogs_functions[] =
{
  {"prompt",	2},
  {"confirm",	3},
  {"alert",	1},
};

static WsStdLibFuncReg lib_wtapublic_functions[] =
{
  {"makeCall", 1},
  {"sendDTMF", 1},
};

static WsStdLibFuncReg lib_wtavoicecall_functions[] =
{
  {"setup", 2},
  {"accept", 2},
  {"release", 1},
  {"sendDTMF", 1},
};

static WsStdLibFuncReg lib_wtanettext_functions[] =
{
  {"send", 2},
  {"read", 1},
  {"remove", 1},
  {"getFieldValue", 2},
};

static WsStdLibFuncReg lib_phonebook_functions[] =
{
  {"write", 3},
  {"read", 2},
  {"remove", 1},
  {"getFieldValue", 2},
};

static WsStdLibFuncReg lib_wtacalllog_functions[] =
{
  {"dialled", 1},
  {"missed", 1},
  {"received", 1},
  {"getFieldValue", 2},
};

static WsStdLibFuncReg lib_wtamisc_functions[] =
{
  {"indication", 3},
  {"endcontext", 0},
  {"protected", 1},
};

static WsStdLibFuncReg lib_wtagsm_functions[] =
{
  {"reject", 1},
  {"hold", 1},
  {"transfer", 1},
  {"multiparty", 0},
  {"retrieve", 1},
  {"location", 0},
  {"sendUSSD", 4},
};

static WsStdLibReg libraries[] =
{
  {"Lang",		0, NF(lib_lang_functions), lib_lang_functions},
  {"Float",		1, NF(lib_float_functions), lib_float_functions},
  {"String",		2, NF(lib_string_functions), lib_string_functions},
  {"URL",		3, NF(lib_url_functions), lib_url_functions},
  {"WMLBrowser",	4, NF(lib_wmlbrowser_functions),
   lib_wmlbrowser_functions},
  {"Dialogs",		5, NF(lib_dialogs_functions), lib_dialogs_functions},
  {"WTAPublic",         512, NF(lib_wtapublic_functions), lib_wtapublic_functions},
  {"WTAVoiceCall",      513, NF(lib_wtavoicecall_functions), lib_wtavoicecall_functions},
  {"WTANetText",        514, NF(lib_wtanettext_functions), lib_wtanettext_functions},
  {"Phonebook",         515, NF(lib_phonebook_functions), lib_phonebook_functions},
  {"WTAMisc",           516, NF(lib_wtamisc_functions), lib_wtamisc_functions},
  {"WTAGSM",            518, NF(lib_wtagsm_functions), lib_wtagsm_functions},
  {"WTACallLog",        519, NF(lib_wtacalllog_functions), lib_wtacalllog_functions},
  {NULL, 0, 0, NULL}
};

/********************* Global functions *********************************/

WsBool
ws_stdlib_function(const char *library, const char *function,
		   WsUInt16 *lindex_return, WsUInt8 *findex_return,
		   WsUInt8 *num_args_return, WsBool *lindex_found_return,
		   WsBool *findex_found_return)
{
  WsUInt16 l;

  *lindex_found_return = WS_FALSE;
  *findex_found_return = WS_FALSE;

  for (l = 0; libraries[l].name != NULL; l++)
    if (strcmp(libraries[l].name, library) == 0)
      {
	WsUInt8 f;

	*lindex_return = libraries[l].library_id;
	*lindex_found_return = WS_TRUE;

	for (f = 0; f < libraries[l].num_functions; f++)
	  if (strcmp(libraries[l].functions[f].name, function) == 0)
	    {
	      *findex_return = f;
	      *findex_found_return = WS_TRUE;

	      *num_args_return = libraries[l].functions[f].num_args;

	      return WS_TRUE;
	    }
      }

  return WS_FALSE;
}


WsBool
ws_stdlib_function_name(WsUInt16 lindex, WsUInt8 findex,
			const char **library_return,
			const char **function_return)
{
  WsUInt16 l;
  
  *library_return = NULL;
  *function_return = NULL;

  for (l = 0; libraries[l].name != NULL; l++)
    if (libraries[l].library_id == lindex)
      {
        if (findex >= libraries[l].num_functions)
          return WS_FALSE;
	  
        *library_return = libraries[l].name;
        *function_return = libraries[l].functions[findex].name;
        return WS_TRUE;
      }

  return WS_FALSE;
}
