/*
 * wml_compiler.h - compiling WML to WML binary
 *
 * This is a header for WML compiler for compiling the WML text 
 * format to WML binary format, which is used for transmitting the 
 * decks to the mobile terminal to decrease the use of the bandwidth.
 *
 * See comments below for explanations on individual functions.
 *
 * Tuomas Luttinen for Wapit Ltd.
 */


#ifndef WML_COMPILER_H
#define WML_COMPILER_H

/*
 * wml_compile - the interface to wml_compiler
 * 
 * This function compiles the WML to WML binary. The arguments are 
 * the following: 
 *   wml_text: the WML text to be compiled
 *   charset: the character set as HTTP headers declare it
 *   wml_binary: buffer for the compiled WML binary
 *
 * The wml_text and charset are allocated by the caller and should also be
 * freed by the caller. The function takes care for memory allocation for
 * the wml_binary. The caller is responsible for freeing this space. 
 * 
 * Return: 0 for ok, -1 for an error
 * 
 * Errors are logged with a little explanation and error number.
 */
int wml_compile(Octstr *wml_text,
		Octstr *charset,
		Octstr **wml_binary);

/*
 * A function to initialize the wml compiler for use. Allocates memory 
 * for the tables wml compiler uses.
 */
void wml_init(void);

/*
 * A function for shutting down the wml_compiler. Frees the memory used 
 * by the wml compiler.
 */
void wml_shutdown(void);


#endif
