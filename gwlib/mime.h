/*
 * mime.h - Implement MIME multipart/related handling 
 * 
 * References:
 *   RFC 2387 (The MIME Multipart/Related Content-type)
 *   RFC 2025 (Multipurpose Internet Mail Extensions [MIME])
 *
 * Here we implement generic parsing functions to handle MIME multipart/related
 * messages. Our representation is made by the struct MIMEEntity where each
 * entity of the MIME scope is hold. Every entity should at least have headers.
 * If the body is a "normal" content-type, then it is stored in Octstr *body.
 * if the body is a sub-MIME multipart/related again, then it's single entities
 * are parsed and chained into the List *multiparts. Hence items in the list
 * are of type MIMEEntity again. We result in a recursive linked represenation
 * of the MIME multipart/related structure.
 *
 * In order to provide a mapping facility between the MIMEEntity representation
 * and an Octstr that holds the MIME document we have the two major functions
 * mime_octstr_to_entity() and mime_entity_to_octstr() that act as converters.
 *
 * Using the MIMEEntity representation structure it is easier to handle the 
 * subsequent MIME entities in case we have a cubed structure in the document.
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#ifndef MIME_H
#define MIME_H

#include "gwlib/gwlib.h"


/* Define an generic MIME entity structure to be 
 * used for MIME multipart parsing. */
typedef struct {
    List *headers;
    List *multiparts;
    Octstr *body;
} MIMEEntity;


/* create and destroy MIME multipart entity */
MIMEEntity *mime_entity_create(void);
void mime_entity_destroy(MIMEEntity *e);

/*
 * Parse the given Octstr that contains a normative text representation
 * of the MIME multipart document into our representation strucuture.
 * Will return parts of it in case an error occures in the recursive
 * call, otherwise will return NULL in case of a fatal error while parsing. 
 */
MIMEEntity *mime_octstr_to_entity(Octstr *mime);

/*
 * Convert a given MIME multipart representation structure to
 * it's Octstr counterpart and return it. Will return parts of it,
 * in case an error occures in a deeper level, otherwise a NULL
 * is return in case of a fatal error while convertion.
 */
Octstr *mime_entity_to_octstr(MIMEEntity *m);

/*
 * Take a MIME multipart representation and return the global header
 * for it. Return a pointer to a HTTP header List.
 */
List *mime_entity_header(MIMEEntity *m);

/*
 * Take a MIME multipart representation and return the highest level
 * body encoded to an Octstr to tbe used as HTTP POST body.
 * Rerturns an Octstr with the body.
 */
Octstr *mime_entity_body(MIMEEntity *m);

/*
 * Dump the structure (hicharchical view) of the MIME representation
 * structure into our DEBUG log level facility.
 */
void mime_entity_dump(MIMEEntity *m);


#endif
