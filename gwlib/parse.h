/*
 * parse.h - functions for octet-by-octet parsing of an octstr
 *
 * Interface to keep track of position in an octstr, and remember
 * error conditions so that they can be checked after a bunch of
 * calls.  Also allows temporary clipping of the string, so that
 * parsing doesn't go past a boundary until it's explicitly allowed
 * to.  This helps parse strings containing length-defined chunks.
 *
 * The main use of this interface is to simplify code that does
 * this kind of parsing, so that it can pass around a single
 * ParseContext value instead of an octstr and one or more offset
 * and length parameters.
 *
 * Note: The octstr involved MUST NOT change during parsing.
 *
 * Richard Braakman
 */

#ifndef PARSE_H
#define PARSE_H

typedef struct context ParseContext;

/*
 * Return a ParseContext object for this octstr, with parsing starting
 * at position 0 and the limit at the end of the string.
 */
ParseContext *parse_context_create(Octstr *str);

/*
 * Destroy a ParseContext object.  Note that this does not free the string
 * that was parsed.
 */
void parse_context_destroy(ParseContext *context);

/*
 * Return -1 if any error has occurred during parsing, otherwise 0.
 */
int parse_error(ParseContext *context);

/*
 * Clear the error flag for the next call to parse_error.
 */
void parse_clear_error(ParseContext *context);

/*
 * Set the error flag.
 */
void parse_set_error(ParseContext *context);

/*
 * Set a new "end" of the string, for parsing purposes, at length
 * octets from the current position.  Return 0 if it's okay.
 * If it doesn't fit in the current limit, don't do anything and
 * return -1.
 */
int parse_limit(ParseContext *context, long length);

/*
 * Restore the previous "end" of the string.  Limits can be stacked
 * as deeply as needed.  The original limit (end-of-string) can
 * not be popped.  Return -1 and set the error flag if there was
 * nothing to pop, otherwise return 0.
 */
int parse_pop_limit(ParseContext *context);

/*
 * Return the number of octets between the current position and
 * the current limit.
 */
long parse_octets_left(ParseContext *context);

/*
 * Skip count octets.  If that would go past the limit, return -1,
 * skip to the limit, and set the error flag.  Otherwise return 0.
 */
int parse_skip(ParseContext *context, long count);

/*
 * Skip to the current limit.  Cannot fail.
 */
void parse_skip_to_limit(ParseContext *context);

/*
 * Set offset to new position.  If that would go past the limit, return
 * -1, skip to the limit, and set the error flag.  Otherwise return 0.
 */
int parse_skip_to(ParseContext *context, long pos);


/*
 * Return the next octet, but do not skip over it.
 * If already at the limit, return -1 and set the error flag.
 */
int parse_peek_char(ParseContext *context);

/*
 * Return the next octet and skip one position forward.
 * If already at the limit, return -1 and set the error flag.
 */
int parse_get_char(ParseContext *context);

/* 
 * Return "length" octets starting at current position, and skip
 * that many octets forward.  If that would go over the limit,
 * return NULL, do not skip, and set the error flag.
 */
Octstr *parse_get_octets(ParseContext *context, long length);

/*
 * Return the value of an uintvar-encoded integer at current
 * position, then skip over it.  If there's an error in the
 * encoding, or if it would go past the limit, then return 0,
 * do not skip, and set the error flag.  Since 0 is a valid
 * uintvar value, the error flag is only way to detect this error.
 */
unsigned long parse_get_uintvar(ParseContext *context);

/*
 * Look for a NUL-terminated string starting at the current offset,
 * and return it (without the NUL) as an Octstr.  Skip forward past
 * the NUL.  If there is no NUL, return NULL and set the error flag.
 */
Octstr *parse_get_nul_string(ParseContext *context);

/*
 * Return unparsed content. This should be used only after all 
 * headers are parsed (and the headers and content are stored in
 * same octstr).
 */
 
Octstr *parse_get_rest(ParseContext *context);

#endif
