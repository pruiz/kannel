/*
 *
 * wsutf8.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Functions to manipulate UTF-8 encoded strings.
 *
 * Specification: RFC-2279
 *
 */

#ifndef WSUTF8_H
#define WSUTF8_H

/********************* Types and defintions *****************************/

/* UTF-8 string handle. */
struct WsUtf8StringRec
{
  /* The length of the UTF-8 encoded `data'. */
  size_t len;

  /* The UTF-8 encoded data. */
  unsigned char *data;

  /* The number of characters in the string. */
  size_t num_chars;
};

typedef struct WsUtf8StringRec WsUtf8String;

/********************* Global functions *********************************/

/* Allocate an empty UTF-8 string.  The function returns NULL if the
   allocation failed (out of memory). */
WsUtf8String *ws_utf8_alloc(void);

/* Free an UTF-8 encoded string. */
void ws_utf8_free(WsUtf8String *string);

/* Append the character `ch' to the string `string'.  The function
   returns 1 if the operation was successful or 0 otherwise (out of
   memory). */
int ws_utf8_append_char(WsUtf8String *string, unsigned long ch);

/* Verify the UTF-8 encoded string `data' containing `len' bytes of
   data.  The function returns 1 if the `data' is correctly encoded
   and 0 otherwise.  If the argument `strlen_return' is not NULL, it
   is set to the number of characters in the string. */
int ws_utf8_verify(const unsigned char *data, size_t len,
		   size_t *strlen_return);

/* Set UTF-8 encoded data `data', `len' to the string `string'.  The
   function returns 1 if the data was UTF-8 encoded and 0 otherwise
   (malformed data or out of memory).  The function frees the possible
   old data from `string'. */
int ws_utf8_set_data(WsUtf8String *string, const unsigned char *data,
		     size_t len);

/* Get a character from the UTF-8 string `string'.  The argument
   `posp' gives the index of the character in the UTF-8 encoded data.
   It is not the sequence number of the character.  It is its starting
   position within the UTF-8 encoded data.  The argument `posp' is
   updated to point to the beginning of the next character within the
   data.  The character is returned in `ch_return'.  The function
   returns 1 if the operation was successful or 0 otherwise (index
   `posp' was invalid or there were no more characters in the
   string). */
int ws_utf8_get_char(const WsUtf8String *string, unsigned long *ch_return,
		     size_t *posp);

/* Convert the UTF-8 encoded string `string' to null-terminated ISO
   8859/1 (ISO latin1) string.  Those characters of `string' which can
   not be presented in latin1 are replaced with the character
   `unknown_char'.  If the argument `len_return' is not NULL, it is
   set to contain the length of the returned string (excluding the
   trailing null-character).  The function returns a pointer to the
   string or NULL if the operation failed (out of memory).  The
   returned string must be freed with the ws_utf8_free_data()
   function. */
unsigned char *ws_utf8_to_latin1(const WsUtf8String *string,
				 unsigned char unknown_char,
				 size_t *len_return);

/* Free a string, returned by the ws_utf8_to_latin1_cstr()
   function. */
void ws_utf8_free_data(char *data);

#endif /* not WSUTF8_H */
