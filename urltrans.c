/*
 * urltrans.c - URL translations
 *
 * Lars Wirzenius for WapIT Ltd.
 */


#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "urltrans.h"
#include "octstr.h"
#include "wapitlib.h"


/***********************************************************************
 * Definitions of data structures. These are not visible to the external
 * world -- they may be accessed only via the functions declared in
 * urltrans.h.
 */


/*
 * Hold one keyword/pattern pair.
 */
struct URLTranslation {
	char *keyword;
	char *pattern;
	char *prefix;
	char *suffix;
	char *faked_sender;
	int max_messages;
        int omit_empty;
	char *split_chars;
	int args;
	int has_catchall_arg;
	URLTranslation *next;
};


/*
 * Hold the list of all translations.
 */
struct URLTranslationList {
	URLTranslation *list;
};


/*
 * Maximum number of encoded characters from one unencoded character.
 * (See encode_for_url.)
 */
#define ENCODED_LEN 3


/***********************************************************************
 * Declarations of internal functions. These are defined at the end of
 * the file.
 */
static URLTranslation *create_onetrans(char *keyword, char *pattern,
				char *prefix, char *suffix, int max_msgs,
				int omit_empty, char *faked_sender, char *split_chars);
static void destroy_onetrans(URLTranslation *ot);
static URLTranslation *find_translation(URLTranslationList *trans, 
					OctstrList *words);
static URLTranslation *find_default_translation(URLTranslationList *trans);
static void encode_for_url(char *buf, char *str);


/***********************************************************************
 * Implementations of the functions declared in urltrans.h. See the
 * header for explanations of what they should do.
 */

URLTranslationList *urltrans_create(void) {
	URLTranslationList *trans;
	
	trans = malloc(sizeof(URLTranslationList));
	if (trans == NULL) {
		error(errno, "Couldn't create URLTranslationList object");
		return NULL;
	}
	
	trans->list = NULL;
	return trans;
}


void urltrans_destroy(URLTranslationList *trans) {
	while (trans->list != NULL) {
		URLTranslation *ot = trans->list;
		trans->list = trans->list->next;
		destroy_onetrans(ot);
	}
}


int urltrans_add_one(URLTranslationList *trans, char *keyword, char *url,
		     char *prefix, char *suffix, int max_messages, int omit_empty,
		     char *faked_sender, char *split_chars)
{
	URLTranslation *ot;
	
	ot = create_onetrans(keyword, url, prefix, suffix, max_messages,
			     omit_empty, faked_sender, split_chars);
	if (ot == NULL)
		return -1;
		
	ot->next = trans->list;
	trans->list = ot;

	return 0;
}


int urltrans_add_cfg(URLTranslationList *trans, Config *cfg) {
	ConfigGroup *grp;
	char *keyword, *url, *prefix, *suffix, *max_msgs, *omit_empty;
	char *faked_sender, *split_chars;
	
	grp = config_first_group(cfg);
	while (grp != NULL) {
		keyword = config_get(grp, "keyword");
		url = config_get(grp, "url");
		prefix = config_get(grp, "prefix");
		suffix = config_get(grp, "suffix");
		max_msgs = config_get(grp, "max-messages");
		omit_empty = config_get(grp, "omit-empty");
		faked_sender = config_get(grp, "faked-sender");
		split_chars = config_get(grp, "split-chars");

		if (keyword != NULL && url != NULL) {
			if (urltrans_add_one(trans, keyword, url, 
					     prefix, suffix, 
					     max_msgs==NULL ? 1 : atoi(max_msgs), 
					     omit_empty==NULL ? 0 : atoi(omit_empty),
					     faked_sender, split_chars) == -1)
				return -1;
		}
		grp = config_next_group(grp);
	}
	
	return 0;
}


URLTranslation *urltrans_find(URLTranslationList *trans, SMSMessage *sms) {
	OctstrList *words;
	URLTranslation *t;
	
	words = octstr_split_words(sms->text);
	if (words == NULL)
		return NULL;

	t = find_translation(trans, words);
	if (t == NULL)
		t = find_default_translation(trans);
	return t;
}


char *urltrans_get_url(URLTranslation *t, SMSMessage *request)
{
	char *buf, *enc, *s, *p, *pattern, *tilde;
	int nextarg, j;
	size_t len, maxword;
	struct tm *tm;
	char *words[161];
	int max_words;
	OctstrList *word_list;
	int n;
	
	word_list = octstr_split_words(request->text);
	if (word_list == NULL)
		return NULL;
	n = octstr_list_len(word_list);
	max_words = sizeof(words) / sizeof(words[0]);
	if (n > max_words)
		n = max_words;
	for (j = 0; j < n; ++j)
		words[j] = octstr_get_cstr(octstr_list_get(word_list, j));

	maxword = 0;
	for (j = 0; j < n; ++j) {
		len = strlen(words[j]);
		if (len > maxword)
			maxword = len;
	}

	pattern = t->pattern;
	len = strlen(pattern);
	len += count_occurences(pattern, "%s") * maxword * ENCODED_LEN;
	len += count_occurences(pattern, "%S") * maxword * ENCODED_LEN;
	len += count_occurences(pattern, "%a") * 
			(maxword + 1) * n * ENCODED_LEN;
	len += count_occurences(pattern, "%r") * 
			(maxword + 1) * n * ENCODED_LEN;
	len += count_occurences(pattern, "%p") * 
			strlen(request->sender) * ENCODED_LEN;
	len += count_occurences(pattern, "%P") * 
			strlen(request->receiver) * ENCODED_LEN;
	len += count_occurences(pattern, "%t") * strlen("YYYY-MM-DD+HH:MM");

	buf = malloc(len + 1);
	enc = malloc(len + 1);
	if (buf == NULL || enc == NULL) {
		free(buf);
		free(enc);
		return NULL;
	}

	*buf = '\0';
	s = buf;
	nextarg = 1;
	for (;;) {
		s = strchr(s, '\0');
		p = strstr(pattern, "%");
		if (p == NULL || p[1] == '\0') {
			strcpy(s, pattern);
			break;
		}

		sprintf(s, "%.*s", (int) (p - pattern), pattern);
		s = strchr(s, '\0');
		switch (p[1]) {
		case 's':
			encode_for_url(enc, words[nextarg]);
			sprintf(s, "%s", enc);
			++nextarg;
			break;
		case 'S':
			sprintf(s, "%s", words[nextarg]);
			++nextarg;
			while ((tilde = strchr(s, '*')) != NULL) {
				*tilde++ = '~';
				s = tilde;
			}
			break;
		case 'r':
			for (j = nextarg; j < n; ++j) {
				encode_for_url(enc, words[j]);
				if (j == nextarg)
					sprintf(s, "%s", enc);
				else
					sprintf(s, "+%s", enc);
				s = strchr(s, '\0');
			}
			break;
		case 'p':
			encode_for_url(enc, request->sender);
			sprintf(s, "%s", enc);
			break;
		case 'P':
			encode_for_url(enc, request->receiver);
			sprintf(s, "%s", enc);
			break;
		case 'q':
			if (strncmp(request->sender, "00", 2) == 0) {
				encode_for_url(enc, request->sender + 2);
				sprintf(s, "%%2B%s", enc);
			} else {
				encode_for_url(enc, request->sender);
				sprintf(s, "%s", enc);
			}
			break;
		case 'Q':
			if (strncmp(request->receiver, "00", 2) == 0) {
				encode_for_url(enc, request->receiver + 2);
				sprintf(s, "%%2B%s", enc);
			} else {
				encode_for_url(enc, request->receiver);
				sprintf(s, "%s", enc);
			}
			break;
		case 'a':
			for (j = 0; j < n; ++j) {
				encode_for_url(enc, words[j]);
				if (j > 0)
					sprintf(s, "+%s", enc);
				else
					sprintf(s, "%s", enc);
				s = strchr(s, '\0');
			}
			break;
		case 't':
			tm = gmtime(&request->time);
			sprintf(s, "%04d-%02d-%02d+%02d:%02d",
				tm->tm_year + 1900,
				tm->tm_mon + 1,
				tm->tm_mday,
				tm->tm_hour,
				tm->tm_min);
			break;
		case '%':
			*s = '%';
			break;
		default:
			sprintf(s, "%%%c", p[1]);
			break;
		}
		pattern = p + 2;
	}
	
	free(enc);
	return buf;
}


char *urltrans_prefix(URLTranslation *t) {
	return t->prefix;
}

char *urltrans_suffix(URLTranslation *t) {
	return t->suffix;
}

int urltrans_max_messages(URLTranslation *t) {
	return t->max_messages;
}

int urltrans_omit_empty(URLTranslation *t) {
	return t->omit_empty;
}

char *urltrans_faked_sender(URLTranslation *t) {
	return t->faked_sender;
}

char *urltrans_split_chars(URLTranslation *t) {
	return t->split_chars;
}


/***********************************************************************
 * Internal functions.
 */


/*
 * Create one URLTranslation. Return NULL for failure, pointer to it for OK.
 */
static URLTranslation *create_onetrans(char *keyword, char *pattern,
	    char *prefix, char *suffix, int max_msgs, int omit_empty,
	    char *faked_sender, char *split_chars)
{
	URLTranslation *ot;
	
	ot = malloc(sizeof(URLTranslation));
	if (ot == NULL)
		goto error;
	
	ot->keyword = strdup(keyword);
	ot->pattern = strdup(pattern);
	if (ot->keyword == NULL || ot->pattern == NULL)
		goto error;
	if (prefix != NULL && suffix != NULL) {
		ot->prefix = strdup(prefix);
		ot->suffix = strdup(suffix);
		if (ot->prefix == NULL || ot->suffix == NULL)
			goto error;
	} else {
		ot->prefix = NULL;
		ot->suffix = NULL;
	}
	if (faked_sender != NULL) {
		ot->faked_sender = strdup(faked_sender);
		if (ot->faked_sender == NULL)
			goto error;
	} else
		ot->faked_sender = NULL;
	if (split_chars != NULL) {
		ot->split_chars = strdup(split_chars);
		if (ot->split_chars == NULL)
			goto error;
	} else
		ot->split_chars = NULL;
	ot->args = count_occurences(pattern, "%s");
	ot->args += count_occurences(pattern, "%S");
	ot->has_catchall_arg = (count_occurences(pattern, "%r") > 0) ||
				(count_occurences(pattern, "%a") > 0);
	ot->max_messages = max_msgs;
	ot->omit_empty = omit_empty;

	ot->next = NULL;
	return ot;
error:
	error(errno, "Couldn't create a URLTranslation.");
	destroy_onetrans(ot);
	return NULL;
}


/*
 * Free one URLTranslation.
 */
static void destroy_onetrans(URLTranslation *ot) {
	if (ot != NULL) {
		free(ot->keyword);
		free(ot->pattern);
		free(ot->prefix);
		free(ot->suffix);
		free(ot->faked_sender);
		free(ot->split_chars);
		free(ot);
	}
}


/*
 * Find the appropriate translation 
 */
static URLTranslation *find_translation(URLTranslationList *trans, 
	OctstrList *words)
{
	char *keyword;
	int n;
	URLTranslation *t;

	n = octstr_list_len(words);
	if (n == 0)
		return NULL;
	keyword = octstr_get_cstr(octstr_list_get(words, 0));

	for (t = trans->list; t != NULL; t = t->next) {
		if (strcasecmp(keyword, t->keyword) == 0) {
			if (n - 1 == t->args)
				break;
			if (t->has_catchall_arg && n - 1 >= t->args)
				break;
		}
	}
	
	return t;
}


static URLTranslation *find_default_translation(URLTranslationList *trans) {
	URLTranslation *t;
	
	for (t = trans->list; t != NULL; t = t->next) {
		if (strcasecmp("default", t->keyword) == 0)
			break;
		t = t->next;
	}
	return t;
}



/*
 * Encode `str' for insertion into a URL. Put the result into `buf',
 * which must be long enough (at most strlen(str) * ENCODED_LEN + 1.
 * 
 * RFC 2396 defines the list of characters that need to be encoded.
 */
static void encode_for_url(char *buf, char *str) {
	static unsigned char *unsafe = ";/?:@&=+$,-_.!~*'()";
	static char is_unsafe[UCHAR_MAX];
	static char hexdigits[] = "0123456789ABCDEF";
	unsigned char *ustr;
	
	if (unsafe != NULL) {
		for (; *unsafe != '\0'; ++unsafe)
			is_unsafe[*unsafe] = 1;
		unsafe = NULL;
	}
	
	ustr = str;
	while (*ustr != '\0') {
		if (!is_unsafe[*ustr])
			*buf++ = *ustr++;
		else {
			*buf++ = '%';
			*buf++ = hexdigits[*ustr / 16];
			*buf++ = hexdigits[*ustr % 16];
			++ustr;
		}
	}
	*buf = '\0';
}
