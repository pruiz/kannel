/*
 * wml_compiler.h - compiling wml to wml binary
 *
 * This is a header for wml compiler for compiling the wml text 
 * format to wml binary format, which is used for transmitting the 
 * decks to the mobile terminal to decrease the use of the bandwidth.
 *
 * See comments below for explanations on individual functions.
 *
 * Tuomas Luttinen for WapIT Ltd.
 */

#ifndef WML_COMPILER_H
#define WML_COMPILER_H


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

#include "gwlib.h"



/*
 * wml_compile - the interface to wml_compiler
 * 
 * This function compiles the wml to wml binary. The arguments are 
 * the following: 
 *   wml_text: the wml text to be compiled
 *   wml_binary: buffer for the compiled wml binary
 *   wml_scripts: pointer to possible wml scripts buffer
 *
 * The function takes care for memory allocation for the wml_binary and 
 * wml_scripts. The caller is responsible for freeing this space.
 * 
 * Return: 0 for ok, -1 for an error
 * 
 * Errors are logged with a little explanation and error nro.
 */


int wml_compile(Octstr *wml_text,
		Octstr **wml_binary,
		Octstr **wml_scripts);


#endif
