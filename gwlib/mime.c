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
 * mime.c - Implement MIME multipart/related handling 
 * 
 * References:
 *   RFC 2387 (The MIME Multipart/Related Content-type)
 *   RFC 2025 (Multipurpose Internet Mail Extensions [MIME])
 *
 * See gwlib/mime.h for more details on the implementation.
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include <string.h>
#include <limits.h>
#include <ctype.h>

#include "gwlib/gwlib.h"
#include "gwlib/mime.h"


/********************************************************************
 * Creation and destruction routines.
 */

MIMEEntity *mime_entity_create(void) 
{
    MIMEEntity *e;

    e = gw_malloc(sizeof(MIMEEntity));
    e->headers = http_create_empty_headers();
    e->multiparts = list_create();
    e->body = NULL;
    e->start = NULL;

    return e;
}

void static mime_entity_destroy_item(void *e)
{
    mime_entity_destroy(e);
}

void mime_entity_destroy(MIMEEntity *e) 
{
    gw_assert(e != NULL);

    if (e->headers != NULL)
        list_destroy(e->headers, octstr_destroy_item);
    if (e->multiparts != NULL)
        list_destroy(e->multiparts, mime_entity_destroy_item);
    octstr_destroy(e->body);
    e->start = NULL; /* will be destroyed on it's own via list_destroy */
  
    gw_free(e);
}    


/********************************************************************
 * Helper routines. Some are derived from gwlib/http.[ch]
 */

/*
 * Read some headers, i.e., until the first empty line (read and discard
 * the empty line as well). Return -1 for error, 0 for all headers read.
 */
static int read_mime_headers(ParseContext *context, List *headers)
{
    Octstr *line, *prev;

    if (list_len(headers) == 0)
        prev = NULL;
    else
        prev = list_get(headers, list_len(headers) - 1);

    for (;;) {
        line = parse_get_line(context);
        if (line == NULL) {
	    	return -1;
        }
        if (octstr_len(line) == 0) {
            octstr_destroy(line);
            break;
        }
        if (isspace(octstr_get_char(line, 0)) && prev != NULL) {
            octstr_append(prev, line);
            octstr_destroy(line);
        } else {
            list_append(headers, line);
            prev = line;
        }
    }

    return 0;
}


/********************************************************************
 * Mapping function from other data types, mainly Octstr and HTTP.
 */

static Octstr *mime_entity_to_octstr_real(MIMEEntity *m, unsigned int level)
{
    Octstr *mime, *value, *boundary;
    List *headers;
    long i;

    gw_assert(m != NULL && m->headers != NULL);

    mime = octstr_create("");

    /* 
     * First of all check if we have further MIME entity dependencies,
     * which means we have further MIMEEntities in our m->multiparts
     * list. If no, then add headers and body and return. This is the
     * easy case. Otherwise we have to loop inside our entities.
     */
    if (list_len(m->multiparts) == 0) {
        for (i = 0; i < list_len(m->headers); i++) {
            octstr_append(mime, list_get(m->headers, i));
            octstr_append(mime, octstr_imm("\r\n"));
        }
        octstr_append(mime, octstr_imm("\r\n"));
        if (m->body != NULL)
            octstr_append(mime, m->body);
        goto finished;
    }

    /* 
     * Check if we have an boundary parameter already in the 
     * Content-Type header. If no, add one, otherwise parse which one 
     * we should use.
     * XXX this can be astracted as function in gwlib/http.[ch].
     */
    headers = http_header_duplicate(m->headers);
    value = http_header_value(headers, octstr_imm("Content-Type"));
    boundary = http_get_header_parameter(value, octstr_imm("boundary"));
    if (boundary == NULL) {
      boundary = octstr_format("_MIME_boundary-%d-%ld_%c_%c_bd%d", 
			random(), (long)time(NULL), 'A' + (random()%26), 
			       'a'+(random() % 26), random());
        octstr_append(value, octstr_imm("; boundary="));
        octstr_append(value, boundary);
        http_header_remove_all(headers, "Content-Type");
        http_header_add(headers, "Content-Type", octstr_get_cstr(value));
        http_header_add(headers, "MIME-Version", "1.0");
    }
    octstr_destroy(value);

    /* headers */
    for (i = 0; i < list_len(headers); i++) {
        octstr_append(mime, list_get(headers, i));
        octstr_append(mime, octstr_imm("\r\n"));
    }
    http_destroy_headers(headers);

    /* loop through all MIME multipart entities of this entity */
    for (i = 0; i < list_len(m->multiparts); i++) {
        MIMEEntity *e = list_get(m->multiparts, i);
        Octstr *body;

        if (i != 0)
            octstr_append(mime, octstr_imm("\r\n"));
        octstr_append(mime, octstr_imm("\r\n--"));
        octstr_append(mime, boundary);
        octstr_append(mime, octstr_imm("\r\n"));

        /* call ourself to produce the MIME entity body */
        body = mime_entity_to_octstr_real(e, level + 1);
        octstr_append(mime, body);

        octstr_destroy(body);
    }

    /* add the last boundary statement, but hive an EOL 
     * if we are on the top of the recursion stack. */
    /* if (level > 0) */
        octstr_append(mime, octstr_imm("\r\n"));
    octstr_append(mime, octstr_imm("\r\n--"));
    octstr_append(mime, boundary);
    octstr_append(mime, octstr_imm("--\r\n"));

    octstr_destroy(boundary);

finished:

    return mime;
}


Octstr *mime_entity_to_octstr(MIMEEntity *m)
{
    Octstr *mime;

    /* mapping function required to pass recurssion level */
    mime = mime_entity_to_octstr_real(m, 0);

    return mime;
}


/*
 * This routine is used for mime_[http|octstr]_to_entity() in order to
 * reduce code duplication. Basically the only difference is how the headers
 * are parsed or passed to the resulting MIMEEntity representation.
 */
static MIMEEntity *mime_something_to_entity(Octstr *mime, List *headers)
{
    MIMEEntity *e;
    ParseContext *context;
    Octstr *value, *boundary, *start;
    int len = 0;

    gw_assert(mime != NULL);

    value = boundary = start = NULL;
    context = parse_context_create(mime);
    e = mime_entity_create();
    
    /* parse the headers up to the body. If we have headers already passed 
     * from our caller, then duplicate them and continue */
    if (headers != NULL) {
        /* duplicate existing headers */
        e->headers = http_header_duplicate(headers);
    } else {
        /* parse the headers out of the mime block */
        if ((read_mime_headers(context, e->headers) != 0) || e->headers == NULL) {
            debug("mime.parse",0,"Failed to read MIME headers in Octstr block:");
            octstr_dump(mime, 0);
            mime_entity_destroy(e);
            parse_context_destroy(context);
            return NULL;
        }
    }

    /* 
     * Now check if the body is a multipart. This is indicated by an 'boundary'
     * parameter in the 'Content-Type' value. If yes, call ourself for the 
     * multipart entities after parsing them.
     */
    value = http_header_value(e->headers, octstr_imm("Content-Type"));
    boundary = http_get_header_parameter(value, octstr_imm("boundary"));
    start = http_get_header_parameter(value, octstr_imm("start"));

    /* Beware that we need *unquoted* strings to compare against in the
     * following parsing sections. */
    if (boundary && (len = octstr_len(boundary)) > 0 &&
        octstr_get_char(boundary, 0) == '"' && octstr_get_char(boundary, len-1) == '"') {
        octstr_delete(boundary, 0, 1);
        octstr_delete(boundary, len-2, 1);

    }
    if (start && (len = octstr_len(start)) > 0 &&
        octstr_get_char(start, 0) == '"' && octstr_get_char(start, len-1) == '"') {
        octstr_delete(start, 0, 1);
        octstr_delete(start, len-2, 1);

    }

    if (boundary != NULL) {
        /* we have a multipart block as body, parse the boundary blocks */
        Octstr *entity, *seperator, *os;
        
        /* loop by all boundary blocks we have in the body */
        seperator = octstr_create("--");
        octstr_append(seperator, boundary);
        while ((entity = parse_get_seperated_block(context, seperator)) != NULL) {
            MIMEEntity *m;
            Octstr *cid = NULL;

            /* we have still two linefeeds at the beginning and end that we 
             * need to remove, these are from the separator. 
             * We check if it is \n or \r\n?! */
	    if (octstr_get_char(entity, 0) == '\r')
		 octstr_delete(entity, 0, 2);	      
	    else
		 octstr_delete(entity, 0, 1);

	    if (octstr_get_char(entity, octstr_len(entity) - 2) == '\r')
		 octstr_delete(entity, octstr_len(entity) - 4, 4);
	    else
		 octstr_delete(entity, octstr_len(entity) - 2, 2);


            debug("mime.parse",0,"MIME multipart: Parsing entity:");
            octstr_dump(entity, 0);

            /* call ourself for this MIME entity and inject to list */
            m = mime_octstr_to_entity(entity);
            list_append(e->multiparts, m);

            /* check if this entity is our start entity (in terms of related)
             * and set our start pointer to it */
            if (start != NULL && 
                (cid = http_header_value(m->headers, octstr_imm("Content-ID"))) != NULL &&
                octstr_compare(start, cid) == 0) {
                /* set only if none has been set before */
                e->start = (e->start == NULL) ? m : e->start;
            }

            octstr_destroy(cid);
            octstr_destroy(entity);
        }
        /* ok, we parsed all blocks, we expect to see now the end boundary */
        octstr_append_cstr(seperator, "--");
        os = parse_get_line(context);
        if (os != NULL && octstr_compare(os, seperator) != 0) {
            debug("mime.parse",0,"Failed to see end boundary, parsed line is '%s'.",
                  octstr_get_cstr(os));
        }

        octstr_destroy(seperator);
        octstr_destroy(os);
    }
    else {

        /* we don't have boundaries, so this is no multipart block, 
         * pass the body to the MIME entity. */
        e->body = parse_get_rest(context);

    }

    parse_context_destroy(context);
    octstr_destroy(value);
    octstr_destroy(boundary);
    octstr_destroy(start);

    return e;
}


MIMEEntity *mime_octstr_to_entity(Octstr *mime)
{
    gw_assert(mime != NULL);

    return mime_something_to_entity(mime, NULL);
}


MIMEEntity *mime_http_to_entity(List *headers, Octstr *body)
{
    gw_assert(headers != NULL && body != NULL);

    return mime_something_to_entity(body, headers);
}


List *mime_entity_headers(MIMEEntity *m)
{
    List *headers;

    gw_assert(m != NULL && m->headers != NULL);

    headers = http_header_duplicate(m->headers);

    return headers;
}


Octstr *mime_entity_body(MIMEEntity *m)
{
    Octstr *os, *body;
    ParseContext *context;
    MIMEEntity *e;

    gw_assert(m != NULL && m->headers != NULL);

    os = mime_entity_to_octstr(m);
    context = parse_context_create(os);
    e = mime_entity_create();

    /* parse the headers up to the body */
    if ((read_mime_headers(context, e->headers) != 0) || e->headers == NULL) {
        debug("mime.parse",0,"Failed to read MIME headers in Octstr block:");
        octstr_dump(os, 0);
        mime_entity_destroy(e);
        parse_context_destroy(context);
        return NULL;
    }

    /* the rest is the body */
    body = parse_get_rest(context);

    octstr_destroy(os);
    mime_entity_destroy(e);
    parse_context_destroy(context);

    return body;
}


/********************************************************************
 * Routines for debugging purposes.
 */

static void mime_entity_dump_real(MIMEEntity *m, unsigned int level)
{
    long i, items;
    Octstr *prefix, *type, *charset;
    unsigned int j;

    gw_assert(m != NULL && m->headers != NULL);

    prefix = octstr_create("");
    for (j = 0; j < level * 2; j++)
        octstr_append_cstr(prefix, " ");

    http_header_get_content_type(m->headers, &type, &charset);
    debug("mime.dump",0,"%sContent-Type `%s'", octstr_get_cstr(prefix),
          octstr_get_cstr(type));
    if (m->start != NULL) {
        Octstr *cid = http_header_value(m->start->headers, octstr_imm("Content-ID"));
        debug("mime.dump",0,"%sRelated to Content-ID <%s> MIMEEntity at address `%p'", 
              octstr_get_cstr(prefix), octstr_get_cstr(cid), m->start);
        octstr_destroy(cid);
    }
    items = list_len(m->multiparts);
    debug("mime.dump",0,"%sBody contains %ld MIME entities, size %ld", octstr_get_cstr(prefix),
          items, (items == 0 && m->body) ? octstr_len(m->body) : -1);

    octstr_destroy(prefix);
    octstr_destroy(type);
    octstr_destroy(charset);

    for (i = 0; i < items; i++) {
        MIMEEntity *e = list_get(m->multiparts, i);

        mime_entity_dump_real(e, level + 1);
    }

}


void mime_entity_dump(MIMEEntity *m)
{
    gw_assert(m != NULL && m->headers != NULL);

    debug("mms",0,"Dumping MIMEEntity at address %p", m);
    mime_entity_dump_real(m, 0);
}

