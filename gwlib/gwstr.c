
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gwstr.h"
#include "log.h"


int split_words(char *buf, int max, char **words) {
        int n;

        n = 0;
        while (n < max - 1 && *buf != '\0') {
                while (*buf == ' ')
                        ++buf;
                if (*buf != '\0') {
                        words[n++] = buf;
                        while (*buf != ' ' && *buf != '\0')
                                ++buf;
                        if (*buf == ' ')
                                *buf++ = '\0';
                }
        }
        while (*buf == ' ')
                ++buf;
        if (*buf != '\0')
                words[n++] = buf;
        return n;
}


char *trim_ends(char *str) {
	char *end;
	
	while (isspace(*str))
		++str;
	end = strchr(str, '\0');
	while (str < end && isspace(end[-1]))
		--end;
	*end = '\0';
	return str;
}


int count_occurences(char *str, char *pat) {
	int count;
	size_t len;
	
	count = 0;
	len = strlen(pat);
	while ((str = strstr(str, pat)) != NULL) {
		++count;
		str += len;
	}
	return count;
}


char *strndup(char *str, size_t n) {
	char *p;
	
	p = malloc(n + 1);
	if (p == NULL)
		return NULL;
	memcpy(p, str, n);
	p[n] = '\0';
	return p;
}


int url_decode(char *string)
{
    long value;
    char *dptr = string;
    char buf[3];		/* buffer for strtol conversion */
    buf[2] = '\0';
    
    do {
	if (*string == '%') {
	    if (*(string+1) == '\0' || *(string+2) == '\0')
		goto error;
	    buf[0] = *(string+1);
	    buf[1] = *(string+2);
	    value =  strtol(buf, NULL, 16);
	    if (value > 0) {
		*dptr = (unsigned char)value;
		string += 3;
		dptr++;
		continue;
	    }
	}
	if (*string == '+') {
	    *dptr++ = ' ';
	    string++;
	}
	else
	    *dptr++ = *string++;
    } while(*string);
    *dptr = '\0';

    return 0;

error:
    *dptr = '\0';
    error(0, "url_decode: corrupted end-of-string <%s>", string);
    return -1;
}


char *str_case_str(char *str, char *pat) {
	char *p, *s;
	
	while (*str != '\0') {
		for (p = pat, s = str; *p != '\0' && *s != '\0'; ++p, ++s)
			if (tolower(*p) != tolower(*s))
				break;
		if (*p == '\0')
			return str;
		++str;
	}
	return NULL;
}


/*
 * seek string 's' backward from offset 'start_offset'. Return offset of
 * the first occurance of any character in 'accept' string, or -1 if not
 * found  */
int str_reverse_seek(const char *s, int start_offset, const char *accept)
{
    char	*other;

    for(;start_offset >= 0; start_offset--) {
	for(other = (char *)accept; *other != '\0'; other++) {
	    if (*other == s[start_offset])
		return start_offset;
	}
    }
    return -1;		/* not found */
}


/* as above but ignoring case */
int str_reverse_case_seek(const char *s, int start_offset, const char *accept)
{
    char	*other;

    for(;start_offset >= 0; start_offset--) {
	for(other = (char *)accept; *other != '\0'; other++) {
	    if (toupper(*other) == toupper(s[start_offset]))
		return start_offset;
	}
    }
    return -1;		/* not found */
}




