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
 * strings.  These could be used to generate extra warnings when
 * functions are called with wrong arguments.  However, this might not
 * be fully conformable to the WMLScript specification.  I think that
 * the interpreter might do an automatic type conversion so the
 * warnings are less useful.  But, these warnings could be enabled in
 * the `-Wpedantic' mode. */

/********************* Types and definitions ****************************/

/* Calculate the number of function registries in the array `f'. */
#define NF(f) (sizeof(f) / sizeof(f[0]))

/* Information about a standard library function. */
struct WsStdLibFuncRegRec
{
    char *name;

    /* The exact number of arguments. */
    int num_args;
    WsUInt8 function_id;
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
        {"abs", 1, 0},
        {"min", 2, 1},
        {"max", 2, 2},
        {"parseInt", 1, 3},
        {"parseFloat", 1, 4},
        {"isInt", 1, 5},
        {"isFloat", 1, 6},
        {"maxInt", 0, 7},
        {"minInt", 0, 8},
        {"float", 0, 9},
        {"exit", 1, 10},
        {"abort", 1, 11},
        {"random", 1, 12},
        {"seed", 1, 13},
        {"characterSet", 0, 14},
    };

static WsStdLibFuncReg lib_float_functions[] =
    {
        {"int", 1, 0},
        {"floor", 1, 1},
        {"ceil", 1, 2},
        {"pow", 2, 3},
        {"round", 1, 4},
        {"sqrt", 1, 5},
        {"maxFloat", 0, 6},
        {"minFloat", 0, 7},
    };

static WsStdLibFuncReg lib_string_functions[] =
    {
        {"length", 1, 0},
        {"isEmpty", 1, 1},
        {"charAt", 2, 2},
        {"subString", 3, 3},
        {"find", 2, 4},
        {"replace", 3, 5},
        {"elements", 2, 6},
        {"elementAt", 3, 7},
        {"removeAt", 3, 8},
        {"replaceAt", 4, 9},
        {"insertAt", 4, 10},
        {"squeeze", 1, 11},
        {"trim", 1, 12},
        {"compare", 2, 13},
        {"toString", 1, 14},
        {"format", 2, 15},
    };

static WsStdLibFuncReg lib_url_functions[] =
    {
        {"isValid", 1, 0},
        {"getScheme", 1, 1},
        {"getHost", 1, 2},
        {"getPort", 1, 3},
        {"getPath", 1, 4},
        {"getParameters", 1, 5},
        {"getQuery", 1, 6},
        {"getFragment", 1, 7},
        {"getBase", 0, 8},
        {"getReferer", 0, 9},
        {"resolve", 2, 10},
        {"escapeString", 1, 11},
        {"unescapeString", 1, 12},
        {"loadString", 2, 13},
    };

static WsStdLibFuncReg lib_wmlbrowser_functions[] =
    {
        {"getVar", 1, 0},
        {"setVar", 2, 1},
        {"go", 1, 2},
        {"prev", 0, 3},
        {"newContext", 0, 4},
        {"getCurrentCard", 0, 5},
        {"refresh", 0, 6},
    };

static WsStdLibFuncReg lib_dialogs_functions[] =
    {
        {"prompt", 2, 0},
        {"confirm", 3, 1},
        {"alert", 1, 2},
    };

static WsStdLibFuncReg lib_wtapublic_functions[] =
    {
        {"makeCall", 1, 0},
        {"sendDTMF", 1, 1},
    };

static WsStdLibFuncReg lib_wtavoicecall_functions[] =
    {
        {"setup", 2, 0},
        {"accept", 2, 1},
        {"release", 1, 2},
        {"sendDTMF", 1, 3},
    };

static WsStdLibFuncReg lib_wtanettext_functions[] =
    {
        {"send", 2, 0},
        {"read", 1, 1},
        {"remove", 1, 2},
        {"getFieldValue", 2, 3},
    };

static WsStdLibFuncReg lib_phonebook_functions[] =
    {
        {"write", 3, 0},
        {"read", 2, 1},
        {"remove", 1, 2},
        {"getFieldValue", 2, 3},
    };

static WsStdLibFuncReg lib_wtacalllog_functions[] =
    {
        {"dialled", 1, 0},
        {"missed", 1, 1},
        {"received", 1, 2},
        {"getFieldValue", 2, 3},
    };

static WsStdLibFuncReg lib_wtamisc_functions[] =
    {
        {"indication", 3, 0},
        {"endcontext", 0, 1},
        {"protected", 1, 2},
    };

static WsStdLibFuncReg lib_wtagsm_functions[] =
    {
        {"reject", 1, 0},
        {"hold", 1, 1},
        {"transfer", 1, 2},
        {"multiparty", 0, 3},
        {"retrieve", 1, 4},
        {"location", 0, 5},
        {"sendUSSD", 4, 6},
    };

static WsStdLibFuncReg lib_crypto_functions[] =
    {
        {"signText", 4, 16},
    };

static WsStdLibFuncReg lib_efi_functions[] =
    {
        {"set", 3, 0},
        {"get", 2, 1},
        {"getFirstName", 1, 2},
        {"getNextName", 2, 3},
        {"getAllAttributes", 1, 4},
        {"getAttribute", 2, 5},
        {"getClassProperty", 2, 6},
        {"getUnits", 1, 7},
        {"query", 1, 8},
        {"invoke", 3, 9},
        {"call", 3, 10},
        {"status", 1, 11},
        {"control", 3, 12},
    };

static WsStdLibReg libraries[] =
    {
        {"Lang", 0, NF(lib_lang_functions), lib_lang_functions},
        {"Float", 1, NF(lib_float_functions), lib_float_functions},
        {"String", 2, NF(lib_string_functions), lib_string_functions},
        {"URL", 3, NF(lib_url_functions), lib_url_functions},
        {"WMLBrowser", 4, NF(lib_wmlbrowser_functions),
         lib_wmlbrowser_functions},
        {"Dialogs", 5, NF(lib_dialogs_functions), lib_dialogs_functions},
        {"Crypto", 6, NF(lib_crypto_functions), lib_crypto_functions},
        {"EFI", 7, NF(lib_efi_functions), lib_efi_functions},
        {"WTAPublic", 512, NF(lib_wtapublic_functions), lib_wtapublic_functions},
        {"WTAVoiceCall", 513, NF(lib_wtavoicecall_functions), lib_wtavoicecall_functions},
        {"WTANetText", 514, NF(lib_wtanettext_functions), lib_wtanettext_functions},
        {"PhoneBook", 515, NF(lib_phonebook_functions), lib_phonebook_functions},
        {"WTAMisc", 516, NF(lib_wtamisc_functions), lib_wtamisc_functions},
        {"WTAGSM", 518, NF(lib_wtagsm_functions), lib_wtagsm_functions},
        {"WTACallLog", 519, NF(lib_wtacalllog_functions), lib_wtacalllog_functions},
        {NULL, 0, 0, NULL}
    };

/********************* Global functions *********************************/

WsBool ws_stdlib_function(const char *library, const char *function,
                          WsUInt16 *lindex_return, WsUInt8 *findex_return,
                          WsUInt8 *num_args_return, WsBool *lindex_found_return,
                          WsBool *findex_found_return)
{
    WsUInt16 l;

    *lindex_found_return = WS_FALSE;
    *findex_found_return = WS_FALSE;

    for (l = 0; libraries[l].name != NULL; l++) {
        if (strcmp(libraries[l].name, library) == 0) {
            WsUInt8 f;

            *lindex_return = libraries[l].library_id;
            *lindex_found_return = WS_TRUE;

            for (f = 0; f < libraries[l].num_functions; f++) {
                if (strcmp(libraries[l].functions[f].name, function) == 0) {
                    *findex_return = libraries[l].functions[f].function_id;
                    *findex_found_return = WS_TRUE;

                    *num_args_return = libraries[l].functions[f].num_args;

                    return WS_TRUE;
                }
	    }
        }
    }

    return WS_FALSE;
}


WsBool ws_stdlib_function_name(WsUInt16 lindex, WsUInt8 findex,
                               const char **library_return,
                               const char **function_return)
{
    WsUInt16 l;
    WsUInt8 f;

    *library_return = NULL;
    *function_return = NULL;

    for (l = 0; libraries[l].name != NULL; l++)
        if (libraries[l].library_id == lindex)
            for (f = 0; f < libraries[l].num_functions; f++) {
                if (libraries[l].functions[f].function_id == findex) {
                    *library_return = libraries[l].name;
                    *function_return = libraries[l].functions[f].name;
                    return WS_TRUE;
                }
            }

    return WS_FALSE;
}
