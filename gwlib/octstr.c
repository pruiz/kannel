/*
 * octstr.c - Octet strings
 *
 * See octstr.h for explanations of what public functions should do.
 *
 * Lars Wirzenius for WapIT Ltd.
 */


#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "gwassert.h"
#include "gwlib.h"
#include "gwstdint.h"


/***********************************************************************
 * Definitions of data structures. These are not visible to the external
 * world -- they may be accessed only via the functions declared in
 * octstr.h. This ensures they really are abstract.
 */

/*
 * The octet string.
 *
 * `data' is a pointer to dynamically allocated memory are where the 
 * octets in the string. It may be bigger than the actual length of the
 * string.
 *
 * `len' is the length of the string.
 *
 * `size' is the size of the memory area `data' points at.
 *
 * When `size' is greater than zero, it is at least `len+1', and the
 * character at `len' is '\0'. This is so that octstr_get_cstr will
 * always work.
 */
struct Octstr {
	unsigned char *data;
	long len;
	long size;
};


/*
 * Node in list of octet strings.
 */
typedef struct Node Node;
struct Node {
	Octstr *ostr;
	Node *next;
};


/*
 * List of octet strings.
 */
struct OctstrList {
	Node *head, *tail;
};


/***********************************************************************
 * Declarations of internal functions. These are defined at the end of
 * the file.
 */


static void seems_valid(Octstr *ostr);


/***********************************************************************
 * Implementations of the functions declared in octstr.h. See the
 * header for explanations of what they should do.
 */


Octstr *octstr_create_empty(void) {
	Octstr *ostr;
	
	ostr = gw_malloc(sizeof(Octstr));
	ostr->data = NULL;
	ostr->size = 0;
	ostr->len = 0;
	seems_valid(ostr);
	return ostr;
}


/* Reserve space for at least 'size' octets */
static void octstr_grow(Octstr *ostr, long size) {
	seems_valid(ostr);
	gw_assert(size >= 0);
	
	if (size > ostr->size) {
		ostr->data = gw_realloc(ostr->data, size);
		ostr->size = size;
	}
}


Octstr *octstr_create(const char *cstr) {
	gw_assert(cstr != NULL);
	return octstr_create_from_data(cstr, strlen(cstr));
}


Octstr *octstr_create_limited(char *cstr, int max_len) {
	int len;

	gw_assert(cstr != NULL);
	gw_assert(max_len >= 0);

	len = strlen(cstr);
	if (len < max_len)
		return octstr_create_from_data(cstr, len);
	else
		return octstr_create_from_data(cstr, max_len);
}


Octstr *octstr_create_tolower(const char *cstr) {
	int i;
	int len;
	Octstr *ret;
    
	gw_assert(cstr != NULL);

	len = strlen(cstr);
	ret = octstr_create_from_data(cstr, len);
	for (i = 0; i < len; i ++)
		octstr_set_char(ret, i, tolower(octstr_get_char(ret, i)));
	
	seems_valid(ret);
	return ret;
}


Octstr *octstr_create_from_data(const void *data, long len) {
	Octstr *ostr;
	
	ostr = octstr_create_empty();
	if (ostr != NULL && len > 0) {
		ostr->len = len;
		ostr->size = len + 1;
		ostr->data = gw_malloc(ostr->size);
		memcpy(ostr->data, data, len);
		ostr->data[len] = '\0';
	}
	seems_valid(ostr);
	return ostr;
}


void octstr_destroy(Octstr *ostr) {
	if (ostr != NULL) {
		seems_valid(ostr);
		gw_free(ostr->data);
		gw_free(ostr);
	}
}


long octstr_len(Octstr *ostr) {
	seems_valid(ostr);
	return ostr->len;
}


Octstr *octstr_copy(Octstr *ostr, long from, long len) {
	seems_valid(ostr);

	if (from >= ostr->len)
		return octstr_create_empty();
	
	if (from + len > ostr->len)
		len = ostr->len - from;
	
	return octstr_create_from_data(ostr->data + from, len);
}



Octstr *octstr_duplicate(Octstr *ostr) {
	seems_valid(ostr);
	return octstr_create_from_data(ostr->data, ostr->len);
}


Octstr *octstr_cat(Octstr *ostr1, Octstr *ostr2) {
	Octstr *ostr;
    
	seems_valid(ostr1);
	seems_valid(ostr2);

	ostr = octstr_create_empty();
	ostr->len = ostr1->len + ostr2->len;
	ostr->size = ostr->len + 1;
	ostr->data = gw_malloc(ostr->size);
	
	if (ostr1->len > 0)
		memcpy(ostr->data, ostr1->data, ostr1->len);
	if (ostr2->len > 0)
		memcpy(ostr->data + ostr1->len, ostr2->data, ostr2->len);
	ostr->data[ostr->len] = '\0';

	seems_valid(ostr);	
	return ostr;
}


int octstr_get_char(Octstr *ostr, long pos) {
	seems_valid(ostr);
	if (pos >= ostr->len)
		return -1;
	return ostr->data[pos];
}


Octstr *octstr_cat_char(Octstr *ostr1, int ch) {
	Octstr *ostr;
	
	seems_valid(ostr1);

	ostr = octstr_create_empty();
	ostr->len = ostr1->len + 1;
	ostr->size = ostr->len + 1;
	ostr->data = gw_malloc(ostr->size);
	
	if (ostr1->len > 0)
		memcpy(ostr->data, ostr1->data, ostr1->len);
	ostr->data[ostr->len-1] = ch;
	ostr->data[ostr->len] = '\0';
	
	seems_valid(ostr);
	return ostr;
}


void octstr_set_char(Octstr *ostr, long pos, int ch) {
	seems_valid(ostr);
	if (pos < ostr->len)
		ostr->data[pos] = ch;
	seems_valid(ostr);
}


void octstr_get_many_chars(void *buf, Octstr *ostr, long pos, long len) {
	gw_assert(buf != NULL);
	seems_valid(ostr);

	if (pos >= ostr->len)
		return;
	if (pos + len > ostr->len)
		len = ostr->len - pos;
	if (len > 0)
		memcpy(buf, ostr->data + pos, len);
}


char *octstr_get_cstr(Octstr *ostr) {
	seems_valid(ostr);
	if (ostr->len == 0)
		return "";
	return ostr->data;
}


int octstr_compare(Octstr *ostr1, Octstr *ostr2) {
	int ret;
	long len;

	seems_valid(ostr1);
	seems_valid(ostr2);

	if (ostr1->len < ostr2->len)
		len = ostr1->len;
	else
		len = ostr2->len;

	if (len == 0)
		return 0;

	ret = memcmp(ostr1->data, ostr2->data, len);
	if (ret == 0) {
		if (ostr1->len < ostr2->len)
			ret = -1;
		else if (ostr1->len > ostr2->len)
			ret = 1;
	}
	return ret;
}


int octstr_ncompare(Octstr *ostr1, Octstr *ostr2, long n) {
	long len;

	seems_valid(ostr1);
	seems_valid(ostr2);

	if ((ostr1->len < ostr2->len) && (ostr1->len < n))
		len = ostr1->len;
	else if ((ostr2->len < ostr1->len) && (ostr2->len < n))
		len = ostr2->len;
	else
		len = n;

	if (len == 0)
		return 0;

	return memcmp(ostr1->data, ostr2->data, len);
}



int octstr_search_char(Octstr *ostr, int ch) {
	return octstr_search_char_from(ostr, ch, 0);
}


int octstr_search_char_from(Octstr *ostr, int ch, long pos) {
	long len;
	int ch_at_pos;
	
	seems_valid(ostr);
	gw_assert(ch >= 0);
	gw_assert(ch <= UCHAR_MAX);

	len = octstr_len(ostr);
	for (; pos < len; ++pos) {
		ch_at_pos = octstr_get_char(ostr, pos);
		gw_assert(ch_at_pos != -1);
		if (ch == ch_at_pos)
			return pos;
	}

	return -1;
}



int octstr_search_str(Octstr *ostr, char *str) {
	long pos_a, pos_b = 0, len_c = 0, len_o =0, a=0, b=0; 
	Octstr *char_to_oct = NULL;
	
	seems_valid(ostr);
	gw_assert(str != NULL);

	len_o = octstr_len(ostr);
	len_c = octstr_len( char_to_oct = octstr_create(str) );
	
	for (pos_a = 0; pos_a < len_o; pos_a++) {
		a=octstr_get_char(ostr, pos_a);
		b=octstr_get_char(char_to_oct, pos_b);
		if (a == b) {
			pos_b++;
			if (pos_b == octstr_len(char_to_oct)) {
				return pos_a - len_c + 1;
				/*returns the start of the found substring */
			}
		} else {
			if (len_o - (pos_a + 1) < len_c)
				break;
			/* is it worth to keep looking */
			pos_b = 0;
		}
	}
	
	/* string wasn't there*/
	return -1;
}



    
int octstr_print(FILE *f, Octstr *ostr) {
	gw_assert(f != NULL);
	seems_valid(ostr);
	
	if (ostr->len == 0)
		return 0;
	if (fwrite(ostr->data, ostr->len, 1, f) != 1) {
		error(errno, "Couldn't write all of octet string to file.");
		return -1;
	}
	return 0;
}


int octstr_pretty_print(FILE *f, Octstr *ostr) {
	unsigned char *p;
	long i;
	
	gw_assert(f != NULL);
	seems_valid(ostr);
	
	p = ostr->data;
	for (i = 0; i < ostr->len; ++i, ++p) {
		if (isprint(*p))
			fprintf(f, "%c", *p);
		else
			fprintf(f, "\\x%02x", *p);
	}
	if (ferror(f))
		return -1;
	return 0;
}


int octstr_write_to_socket(int socket, Octstr *ostr) {
	long len;
	unsigned char *data;
	int ret;

	gw_assert(socket >= 0);
	seems_valid(ostr);

	data = ostr->data;
	len = ostr->len;
	while (len > 0) {
		ret = write(socket, data, len);
		if (ret == -1) {
			error(errno, "Writing to socket failed");
			return -1;
		}
		/* ret may be less than len, if the writing was interrupted
		   by a signal. */
		len -= ret;
		data += ret;
	}
	return 0;
}


void octstr_insert(Octstr *ostr1, Octstr *ostr2, long pos) {
	seems_valid(ostr1);
	seems_valid(ostr2);

	if (ostr2->len == 0)
		return;
	
	octstr_grow(ostr1, ostr1->len + ostr2->len + 1);
	memmove(ostr1->data + pos + ostr2->len, ostr1->data + pos,
		ostr1->len - pos);
	memcpy(ostr1->data + pos, ostr2->data, ostr2->len);
	ostr1->len += ostr2->len;
	ostr1->data[ostr1->len] = '\0';
	
	seems_valid(ostr1);
}


void octstr_replace(Octstr *ostr, char *data, long len) {
	seems_valid(ostr);
	gw_assert(data != NULL);

	octstr_grow(ostr, len + 1);
	if (len > 0)
		memcpy(ostr->data, data, len);
	ostr->len = len;
	ostr->data[len] = '\0';

	seems_valid(ostr);
}


void octstr_truncate(Octstr *ostr, int new_len) {
	seems_valid(ostr);
	gw_assert(new_len >= 0);
	if (new_len >= ostr->len)
	    return;

	ostr->len = new_len;
	ostr->data[new_len] = '\0';
	
	seems_valid(ostr);
}


void octstr_strip_blank(Octstr *text) {
        int start = 0, end, len = 0;

	seems_valid(text);

	/* Remove white space from the beginning of the text */
	while (isspace(octstr_get_char(text, start)))
		start ++;

	if (start > 0)
		octstr_delete(text, 0, start);

	/* and from the end. */

	if ((len = octstr_len(text)) > 0) {
		end = len = len - 1;
		while (isspace(octstr_get_char(text, end)))
			end--;
		octstr_delete(text, end + 1, len - end);
	}

	seems_valid(text);
}


void octstr_shrink_blank(Octstr *text) {
	int i, j, end;
	
	seems_valid(text);
	
	end = octstr_len(text);
	
	/* Shrink white spaces to one  */
	for (i = 0; i < end; i++) {
		if (isspace(octstr_get_char(text, i))) {
			/* Change the remaining space into single space. */
			if (octstr_get_char(text, i) != ' ')
				octstr_set_char(text, i, ' ');
	
			j = i = i + 1;
			while (isspace(octstr_get_char(text, j)))
				j ++;
			if (j - i > 1)
				octstr_delete(text, i, j - i);
		}
	}
	
	seems_valid(text);
}


void octstr_insert_data(Octstr *ostr, long pos, char *data, long len) {
	seems_valid(ostr);
	gw_assert(pos <= ostr->len);

	if (len == 0)
		return;
	
	octstr_grow(ostr, ostr->len + len + 1);
	if (ostr->len > pos) {	/* only if neccessary */
		memmove(ostr->data+pos+len, ostr->data+pos, ostr->len - pos);
	}
	memcpy(ostr->data + pos, data, len);
	ostr->len += len;
	ostr->data[ostr->len] = '\0';
	
	seems_valid(ostr);
}


void octstr_append_data(Octstr *ostr, char *data, long len) {
	octstr_insert_data(ostr, ostr->len, data, len);
}


void octstr_append_cstr(Octstr *ostr, char *cstr) {
	octstr_insert_data(ostr, ostr->len, cstr, strlen(cstr));
}


void octstr_delete(Octstr *ostr1, long pos, long len) {
	seems_valid(ostr1);

	if (pos > ostr1->len)
		pos = ostr1->len;
	if (pos + len > ostr1->len)
		len = ostr1->len - pos;
	if (len > 0) {
		memmove(ostr1->data + pos, ostr1->data + pos + len,
			ostr1->len - pos - len);
		ostr1->len -= len;
	        ostr1->data[ostr1->len] = '\0';
	}

	seems_valid(ostr1);
}



Octstr *octstr_read_file(const char *filename) {
	FILE *f;
	Octstr *os;
	char buf[128*1024];
	long n;
	
	gw_assert(filename != NULL);

	f = fopen(filename, "r");
	if (f == NULL) {
		error(errno, "fopen failed: couldn't open `%s'", filename);
		return NULL;
	}

	os = octstr_create_empty();
	if (os == NULL)
		goto error;

	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		octstr_insert_data(os, octstr_len(os), buf, n);
	
	(void) fclose(f);
	return os;

error:
	(void) fclose(f);
	octstr_destroy(os);
	return NULL;
}



OctstrList *octstr_list_create(void) {
	OctstrList *list;
	
	list = gw_malloc(sizeof(OctstrList));
	list->head = NULL;
	list->tail = NULL;
	return list;
}


void octstr_list_destroy(OctstrList *list, int strings_also) {
	while (list->head != NULL) {
		Node *n = list->head;
		list->head = list->head->next;
		if (strings_also)
			octstr_destroy(n->ostr);
		gw_free(n);
	}
	gw_free(list);
}


long octstr_list_len(OctstrList *list) {
	Node *n;
	long len;
	
	for (len = 0, n = list->head; n != NULL; n = n->next, ++len)
		;
	return len;
}


void octstr_list_append(OctstrList *list, Octstr *ostr) {
	Node *n;
	
	n = gw_malloc(sizeof(Node));

	n->ostr = ostr;
	n->next = NULL;
	if (list->head == NULL) {
		list->head = n;
		list->tail = n;
	} else {
		list->tail->next = n;
		list->tail = n;
	}
}


Octstr *octstr_list_get(OctstrList *list, long index) {
	Node *n;
	
	for (n = list->head; index > 0 && n != NULL; n = n->next, --index)
		;
	if (n == NULL)
		return NULL;
	return n->ostr;
}


OctstrList *octstr_split_words(Octstr *ostr) {
	unsigned char *p;
	OctstrList *list;
	Octstr *word;
	long i, start, end;
	
	seems_valid(ostr);

	list = octstr_list_create();

	p = ostr->data;
	i = 0;
	for (;;) {
		while (i < ostr->len && isspace(*p)) {
			++p;
			++i;
		}
		start = i;
		
		while (i < ostr->len && !isspace(*p)) {
			++p;
			++i;
		}
		end = i;
		
		if (start == end)
			break;
			
		word = octstr_create_from_data(ostr->data + start, 
						end - start);
		octstr_list_append(list, word);
	}
	
	return list;
}


void octstr_dump(Octstr *ostr, int level) {
	unsigned char *p, *d, buf[1024], charbuf[256];
	long pos;
	const int octets_per_line = 8;
	int c, this_line_begins_at;

	if (ostr == NULL)
		return;

	debug("gwlib.octstr", 0, "%*sOctet string at %p:", level, "", 
				  (void *) ostr);
	debug("gwlib.octstr", 0, "%*s  len:  %lu", level, "",
				  (unsigned long) ostr->len);
	debug("gwlib.octstr", 0, "%*s  size: %lu", level, "",
				  (unsigned long) ostr->size);

	buf[0] = '\0';
	p = buf;
	d = charbuf;
	this_line_begins_at = 0;
	for (pos = 0; pos < octstr_len(ostr); ) {
	    c = octstr_get_char(ostr, pos);
	    sprintf(p, "%02x ", c);
	    p = strchr(p, '\0');
	    if (isprint(c))
		*d++ = c;
	    else
		*d++ = '.';
	    ++pos;
	    if (pos - this_line_begins_at == octets_per_line) {
		*d = '\0';
		debug("gwlib.octstr", 0, "%*s  data: %s  %s", level, "",
				          buf, charbuf);
		buf[0] = '\0';
		charbuf[0] = '\0';
		p = buf;
		d = charbuf;
		this_line_begins_at = pos;
	    }
	}
	if (pos - this_line_begins_at > 0) {
	    *d = '\0';
	    debug("gwlib.octstr", 0, "%*s  data: %-*.*s  %s", level, "",
	    				octets_per_line*3,
		  octets_per_line*3, buf, charbuf);
	}

	debug("gwlib.octstr", 0, "%*sOctet string dump ends.", level, "");
}

int octstr_send(int fd, Octstr *ostr) {
	uint32_t length;
	int ret = 0, written = 0, datalength = 0;
	char *data = NULL;

	gw_assert(fd >= 0);
	seems_valid(ostr);

	length = htonl(octstr_len(ostr));
	datalength = octstr_len(ostr)+sizeof(uint32_t);
	data = gw_malloc(datalength);
	memcpy(data, &length, sizeof(uint32_t));
	octstr_get_many_chars(data+sizeof(uint32_t), ostr, 0, octstr_len(ostr));
	
	while(written < datalength) {
		ret = send(fd, data+written, datalength-written, 0);
		if(ret == 0)
			goto error;
		else if(ret == -1) {
			if(errno==EINTR) continue;
			if(errno==EAGAIN) continue;
			goto error;
		} else {
			written += ret;
		}
	}

	gw_free(data);
	return 0;

error:
	gw_free(data);
	error(errno, "octstr_send: failed");
	return -1;	
}

int octstr_recv(int fd, Octstr **ostr) {
	long length, nlength;
	char *data = NULL;
	Octstr *newostr = NULL;
	int ret = 0, readlength = 0;
	
	gw_assert(fd >= 0);

	nlength = 0;
	*ostr = NULL;
	
	/* How many octets in incomint Octstr. */
	readlength = 0;
	while(readlength < (int) sizeof(uint32_t)) {
		ret = recv(fd, (&nlength)+readlength, sizeof(uint32_t)-readlength, 0);
		if(ret == 0)
			goto eof;
		else if(ret == -1) {
			if(errno==EINTR) continue;
			if(errno==EAGAIN) continue;
			goto error;
		} else {
			readlength += ret;
		}
	}
	length = ntohl(nlength);
	if (length > 256*256*256) {
	    unsigned char *u;
	    u = (unsigned char *) &nlength;
	    warning(0, "Possible garbage received by octsr_recv, length %ld "
		    "data %02x %02x %02x %02x ...", length,
		    u[0], u[1], u[2], u[3]);
	    return -1;
	}
	data = gw_malloc(length);

	/* Read the real data. */
	readlength = 0;
	while(readlength < length) {
		ret = recv(fd, data+readlength, length-readlength, 0);
		if(ret == 0)
		        goto eof;
		else if(ret == -1) {
			if(errno==EINTR) continue;
			if(errno==EAGAIN) continue;
			goto error;
		} else {
			readlength += ret;
		}
	}

	newostr = octstr_create_from_data(data, length);

	*ostr = newostr;
	gw_free(data);
	return 1;
eof:
	gw_free(data);
	return 0;
error:
	gw_free(data);
	return -1;
}


int octstr_url_decode(Octstr *ostr)
{
    long value;
    unsigned char *string = ostr->data;
    unsigned char *dptr = ostr->data;
    unsigned char buf[3];   	/* buffer for strtol conversion */
    buf[2] = '\0';
    
    do {
	if (*string == '%') {
	    if (*(string+1) == '\0' || *(string+2) == '\0')
		goto error;
	    buf[0] = *(string+1);
	    buf[1] = *(string+2);
	    value =  strtol(buf, NULL, 16);

	    if (value >= 0 && value < 256) {
		*dptr = (unsigned char)value;
		string += 3;
		dptr++;
		continue;
	    }
	    warning(0, "Garbage encoded (value = %ld)", value);
	}
	if (*string == '+') {
	    *dptr++ = ' ';
	    string++;
	}
	else
	    *dptr++ = *string++;
    } while(*string);	/* we stop here because it terimates encoded string */
    *dptr = '\0';

    ostr->len = (dptr - ostr->data);
    return 0;

error:
    *dptr = '\0';
    ostr->len = (dptr - ostr->data);
    warning(0, "octstr_url_decode: corrupted end-of-string <%s>", string);
    return -1;
}



/**********************************************************************
 * Local functions.
 */

static void seems_valid(Octstr *ostr) {
	gw_assert(ostr != NULL);
	gw_assert(ostr->len < ostr->size || 
	         (ostr->size == 0 && ostr->len == 0));
	gw_assert(ostr->size == 0 || 
	         (ostr->data !=  NULL && ostr->data[ostr->len] == '\0'));
}
