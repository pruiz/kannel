/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2003 Kannel Group  
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
 * utils.c - generally useful, non-application specific functions for Gateway
 *
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>

#include "gwlib.h"


/*
 * new datatype functions
 */



MultibyteInt get_variable_value(Octet *source, int *len)
{
    MultibyteInt retval = 0;
    
    for(*len=1;; (*len)++, source++) {
	retval = retval * 0x80 + (*source & 0x7F);
	if (*source < 0x80)  /* if the continue-bit (high bit) is not set */
	    break;
    }
    return retval;
}


int write_variable_value(MultibyteInt value, Octet *dest)
{
    int i, loc = 0;
    Octet revbuffer[20];	/* we write it backwards */
    
    for (;;) {
	revbuffer[loc++] = (value & 0x7F) + 0x80;	
	if (value >= 0x80)
	    value = value >> 7;
	else
	    break;
    }
    for(i=0; i < loc; i++)		/* reverse the buffer */
	dest[i] = revbuffer[loc-i-1];
    
    dest[loc-1] &= 0x7F;	/* remove trailer-bit from last */

    return loc;
}

Octet reverse_octet(Octet source)
{
    Octet	dest;
    dest = (source & 1) <<7;
    dest += (source & 2) <<5;
    dest += (source & 4) <<3;
    dest += (source & 8) <<1;
    dest += (source & 16) >>1;
    dest += (source & 32) >>3;
    dest += (source & 64) >>5;
    dest += (source & 128) >>7;
    
    return dest;
}



int get_and_set_debugs(int argc, char **argv,
		       int (*find_own) (int index, int argc, char **argv))
{
    int i, ret = -1;
    int debug_lvl = -1;
    int file_lvl = GW_DEBUG;
    char *log_file = NULL;
    char *debug_places = NULL;
    
    for(i=1; i < argc; i++) {
	if (strcmp(argv[i],"-v")==0 ||
	    strcmp(argv[i],"--verbosity")==0) {

	    if (i+1 < argc) {
		debug_lvl = atoi(argv[i+1]);
		i++;
	    } else
		fprintf(stderr, "Missing argument for option %s\n", argv[i]); 
	} else if (strcmp(argv[i],"-F")==0 ||
		   strcmp(argv[i],"--logfile")==0) {
	    if (i+1 < argc && *(argv[i+1]) != '-') {
		log_file = argv[i+1];
		i++;
	    } else
		fprintf(stderr, "Missing argument for option %s\n", argv[i]); 
	} else if (strcmp(argv[i],"-V")==0 ||
		   strcmp(argv[i],"--fileverbosity")==0) {
	    if (i+1 < argc) {
		file_lvl = atoi(argv[i+1]);
		i++;
	    } else
		fprintf(stderr, "Missing argument for option %s\n", argv[i]); 
	} else if (strcmp(argv[i],"-D")==0 ||
		   strcmp(argv[i],"--debug")==0) {
	    if (i+1 < argc) {
		debug_places = argv[i+1];
		i++;
	    } else
		fprintf(stderr, "Missing argument for option %s\n", argv[i]); 
	} else if (strcmp(argv[i],"--")==0) {
	    i++;
	    break;
	} else if(*argv[i] != '-') {
	    break;
	} else {
	    if (find_own != NULL) {
		ret = find_own(i, argc, argv);
	    }
	    if (ret < 0) {
		fprintf(stderr, "Unknown option %s, exiting.\n", argv[i]);
		panic(0, "Option parsing failed");
	    }
	    else
		i += ret;	/* advance additional args */
	}
    }
    if (debug_lvl > -1)
	log_set_output_level(debug_lvl);
    if (debug_places != NULL)
        log_set_debug_places(debug_places);
    if (log_file != NULL)
	log_open(log_file, file_lvl, GW_NON_EXCL);

    info(0, "Debug_lvl = %d, log_file = %s, log_lvl = %d",
	  debug_lvl, log_file ? log_file : "<none>", file_lvl);
    if (debug_places != NULL)
	    info(0, "Debug places: `%s'", debug_places);
    
    return i;
}


static int pattern_matches_ip(Octstr *pattern, Octstr *ip)
{
    long i, j;
    long pat_len, ip_len;
    int pat_c, ip_c;
    
    pat_len = octstr_len(pattern);
    ip_len = octstr_len(ip);

    i = 0;
    j = 0;
    while (i < pat_len && j < ip_len) {
	pat_c = octstr_get_char(pattern, i);
	ip_c = octstr_get_char(ip, j);
	if (pat_c == ip_c) {
	    /* The characters match, go to the next ones. */
	    ++i;
	    ++j;
	} else if (pat_c != '*') {
	    /* They differ, and the pattern isn't a wildcard one. */
	    return 0;
	} else {
	    /* We found a wildcard in the pattern. Skip in ip. */
	    ++i;
	    while (j < ip_len && ip_c != '.') {
		++j;
		ip_c = octstr_get_char(ip, j);
	    }
	}
    }
    
    if (i >= pat_len && j >= ip_len)
    	return 1;
    return 0;
}


static int pattern_list_matches_ip(Octstr *pattern_list, Octstr *ip)
{
    List *patterns;
    Octstr *pattern;
    int matches;

    patterns = octstr_split(pattern_list, octstr_imm(";"));
    matches = 0;

    while (!matches && (pattern = list_extract_first(patterns)) != NULL) {
	matches = pattern_matches_ip(pattern, ip);
	octstr_destroy(pattern);
    }
    
    list_destroy(patterns, octstr_destroy_item);
    return matches;
}


int is_allowed_ip(Octstr *allow_ip, Octstr *deny_ip, Octstr *ip)
{
    if (ip == NULL)
	return 0;

    if (octstr_len(deny_ip) == 0)
	return 1;

    if (allow_ip != NULL && pattern_list_matches_ip(allow_ip, ip))
	return 1;

    if (pattern_list_matches_ip(deny_ip, ip))
    	return 0;

    return 1;
}


int connect_denied(Octstr *allow_ip, Octstr *ip)
{
    if (ip == NULL)
	return 1;

    /* If IP not set, allow from Localhost */
    if (allow_ip == NULL) { 
	if (pattern_list_matches_ip(octstr_imm("127.0.0.1"), ip))
	    return 0;
    } else {
	if (pattern_list_matches_ip(allow_ip, ip))
	    return 0;
    }
    return 1;
}


int does_prefix_match(Octstr *prefix, Octstr *number)
{
    /* XXX modify to use just octstr operations
     */
    char *b, *p, *n;

    gw_assert(prefix != NULL);
    gw_assert(number != NULL);

    p = octstr_get_cstr(prefix);
    n = octstr_get_cstr(number);
    

    while (*p != '\0') {
        b = n;
        for (b = n; *b != '\0'; b++, p++) {
            if (*p == ';' || *p == '\0') {
                return 1;
            }
            if (*p != *b) break;
        }
        if (*p == ';' || *p == '\0') {
            return 1;
        }
        while (*p != '\0' && *p != ';')
            p++;
        while (*p == ';') p++;
    }
    return 0;
}


int normalize_number(char *dial_prefixes, Octstr **number)
{
    char *t, *p, *official, *start;
    int len, official_len;
    
    if (dial_prefixes == NULL || dial_prefixes[0] == '\0')
        return 0;

    t = official = dial_prefixes;
    official_len = 0;

    gw_assert(number != NULL);
    
    while(1) {

    	p = octstr_get_cstr(*number);
        for(start = t, len = 0; ; t++, p++, len++)
	{
            if (*t == ',' || *t == ';' || *t == '\0') {
                if (start != official) {
                    Octstr *nstr;
		    long n;
		    
		    if ( official[0] == '-' ) official_len=0;
		    n = official_len;
		    if (strlen(official) < (size_t) n)
		    	n = strlen(official);
                    nstr = octstr_create_from_data(official, n);
                    octstr_insert_data(nstr, official_len,
                                           octstr_get_cstr(*number) + len,
                                           octstr_len(*number) - len);
                    octstr_destroy(*number);
                    *number = nstr;
                }
                return 1;
            }
            if (*p == '\0' || *t != *p)
                break;          /* not matching */
        }
        for(; *t != ',' && *t != ';' && *t != '\0'; t++, len++)
            ;
        if (*t == '\0') break;
        if (start == official) official_len = len;
        if (*t == ';') official = t+1;
        t++;
    }
    return 0;
}





long decode_network_long(unsigned char *data) {
        return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}


void encode_network_long(unsigned char *data, unsigned long value) {
        data[0] = (value >> 24) & 0xff;
        data[1] = (value >> 16) & 0xff;
        data[2] = (value >> 8) & 0xff;
        data[3] = value & 0xff;
}

/* Something that does the same as GNU cfmakeraw. We don't use cfmakeraw
   so that we always know what it does, and also to reduce configure.in
   complexity. */

void kannel_cfmakeraw (struct termios *tio){
    /* Block until a charactor is available, but it only needs to be one*/
    tio->c_cc[VMIN]    = 1;
    tio->c_cc[VTIME]   = 0;

    /* GNU cfmakeraw sets these flags so we had better too...*/

    /* Control modes */
    /* Mask out character size (CSIZE), then set it to 8 bits (CS8).
     * Enable parity bit generation in both directions (PARENB).
     */
    tio->c_cflag      &= ~(CSIZE|PARENB);
    tio->c_cflag      |= CS8;

    /* Input Flags,*/
    /* Turn off all input flags that interfere with the byte stream:
     * BRKINT - generate SIGINT when receiving BREAK, ICRNL - translate
     * NL to CR, IGNCR - ignore CR, IGNBRK - ignore BREAK,
     * INLCR - translate NL to CR, IXON - use XON/XOFF flow control,
     * ISTRIP - strip off eighth bit.
     */
    tio->c_iflag &= ~(BRKINT|ICRNL|IGNCR|IGNBRK|INLCR|IXON|ISTRIP);

    /* Other flags,*/
    /* Turn off all local flags that interpret the byte stream:
     * ECHO - echo input chars, ECHONL - always echo NL even if ECHO is off,
     * ICANON - enable canonical mode (basically line-oriented mode),
     * IEXTEN - enable implementation-defined input processing,
     * ISIG - generate signals when certain characters are received. */
    tio->c_lflag      &= ~(ECHO|ECHONL|ICANON|IEXTEN|ISIG);

    /* Output flags,*/
    /* Disable implementation defined processing on the output stream*/
    tio->c_oflag      &= ~OPOST;
}


int gw_isdigit(int c)
{
    return isdigit(c);
}

int gw_isalnum(int c)
{
    return isalnum(c);
}

int gw_isxdigit(int c)
{
    return isxdigit(c);
}


/* Rounds up the result of a division */
int roundup_div(int a, int b)
{
    int t;
	
    t = a / b;
    if (t * b != a)
	t += 1;

    return t;
}


unsigned long long gw_generate_id(void)
{
    /* create a 64 bit unique Id by putting a 32 bit epoch time value
     * and a 32 bit random value together */
    unsigned long random, timer;
     
    random = gw_rand();
    timer = (unsigned long)time(NULL);
    
    return ((unsigned long long)timer << 32) + random;
}
