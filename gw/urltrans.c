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
#include "gwlib.h"


/***********************************************************************
 * Definitions of data structures. These are not visible to the external
 * world -- they may be accessed only via the functions declared in
 * urltrans.h.
 */


/*
 * Hold one keyword/options entity
 */
struct URLTranslation {
    char *keyword;	/* keyword in SMS (similar) query */
    char *aliases;	/* separated with ':', after each (inc. last one) */
    int type;		/* see enumeration in header file */
    char *pattern;	/* url, text or file-name pattern */
    char *prefix;	/* for prefix-cut */
    char *suffix;	/* for suffix-cut */
    char *faked_sender;	/* works only with certain services */
    int max_messages;	/* absolute limit of reply messages */
    char *split_chars;	/* allowed chars to be used to split message */
    char *split_suffix;	/* chars added to end after each split (not last) */
    int omit_empty;	/* if the reply is empty, is notification send */
    char *header;	/* string to be inserted to each SMS */
    char *footer;	/* string to be appended to each SMS */

    
    char *username;	/* send sms username */
    char *password;	/* password associated */
    
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
static URLTranslation *create_onetrans(ConfigGroup *grp);
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


int urltrans_add_one(URLTranslationList *trans, ConfigGroup *grp)
{
    URLTranslation *ot;
	
    if (config_get(grp, "keyword") == NULL &&
	config_get(grp, "username") == NULL)
	return 0;
    
    ot = create_onetrans(grp);
    if (ot == NULL)
	return -1;
		
    ot->next = trans->list;
    trans->list = ot;

    return 0;
}


int urltrans_add_cfg(URLTranslationList *trans, Config *cfg) {
	ConfigGroup *grp;
	
	grp = config_first_group(cfg);
	while (grp != NULL) {
	    if (urltrans_add_one(trans, grp) == -1)
		return -1;
	    grp = config_next_group(grp);
	}
	return 0;
}


URLTranslation *urltrans_find(URLTranslationList *trans, Octstr *text) {
	OctstrList *words;
	URLTranslation *t;
	
	words = octstr_split_words(text);
	if (words == NULL)
	    return NULL;

	t = find_translation(trans, words);
	if (t == NULL)
	    t = find_default_translation(trans);
	return t;
}



URLTranslation *urltrans_find_username(URLTranslationList *trans, 
				       char *name)
{
    URLTranslation *t;

    for (t = trans->list; t != NULL; t = t->next) {
	if (t->type == TRANSTYPE_SENDSMS) {
	    if (strcmp(name, t->username) == 0)
		return t;
	}
    }
    return NULL;
}



char *urltrans_get_pattern(URLTranslation *t, Msg *request)
{
	char *buf, *enc, *s, *p, *pattern, *tilde;
	int nextarg, j;
	size_t len, maxword;
	struct tm *tm;
	char *words[161];
	int max_words;
	OctstrList *word_list;
	int n;

	if (t->type == TRANSTYPE_SENDSMS)
	    return strdup("");
	
	word_list = octstr_split_words(request->smart_sms.msgdata);
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
			octstr_len(request->smart_sms.sender) * ENCODED_LEN;
	len += count_occurences(pattern, "%P") * 
			octstr_len(request->smart_sms.receiver) * ENCODED_LEN;
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
		case 'P':
			encode_for_url(enc, octstr_get_cstr(request->smart_sms.sender));
			sprintf(s, "%s", enc);
			break;
		case 'p':
			encode_for_url(enc, octstr_get_cstr(request->smart_sms.receiver));
			sprintf(s, "%s", enc);
			break;
		case 'Q':
			if (strncmp(octstr_get_cstr(request->smart_sms.sender),
				    "00", 2) == 0) {
				encode_for_url(enc, octstr_get_cstr(request->smart_sms.sender) + 2);
				sprintf(s, "%%2B%s", enc);
			} else {
				encode_for_url(enc, octstr_get_cstr(request->smart_sms.sender));
				sprintf(s, "%s", enc);
			}
			break;
		case 'q':
			if (strncmp(octstr_get_cstr(request->smart_sms.receiver), "00", 2) == 0) {
				encode_for_url(enc, octstr_get_cstr(request->smart_sms.receiver) + 2);
				sprintf(s, "%%2B%s", enc);
			} else {
				encode_for_url(enc, octstr_get_cstr(request->smart_sms.receiver));
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
			tm = gmtime(&request->smart_sms.time);
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


int urltrans_type(URLTranslation *t) {
	return t->type;
}

char *urltrans_prefix(URLTranslation *t) {
	return t->prefix;
}

char *urltrans_suffix(URLTranslation *t) {
	return t->suffix;
}

char *urltrans_faked_sender(URLTranslation *t) {
	return t->faked_sender;
}

int urltrans_max_messages(URLTranslation *t) {
	return t->max_messages;
}

char *urltrans_split_chars(URLTranslation *t) {
	return t->split_chars;
}

char *urltrans_split_suffix(URLTranslation *t) {
	return t->split_suffix;
}

int urltrans_omit_empty(URLTranslation *t) {
	return t->omit_empty;
}

char *urltrans_header(URLTranslation *t) {
	return t->header;
}
char *urltrans_footer(URLTranslation *t) {
	return t->footer;
}

char *urltrans_password(URLTranslation *t) {
	return t->password;
}





/***********************************************************************
 * Internal functions.
 */


/*
 * Create one URLTranslation. Return NULL for failure, pointer to it for OK.
 */
static URLTranslation *create_onetrans(ConfigGroup *grp)
{
    URLTranslation *ot;
    char *keyword, *aliases, *url, *text, *file;
    char *prefix, *suffix, *faked_sender, *max_msgs;
    char *split_chars, *split_suffix, *omit_empty;
    char *username, *password;
    char *header, *footer;
    
    ot = malloc(sizeof(URLTranslation));
    if (ot == NULL)
	goto error;

    ot->keyword = ot->aliases = ot->pattern = NULL;
    ot->prefix = ot->suffix = ot->faked_sender = NULL;
    ot->split_chars = ot->split_suffix = NULL;
    ot->username = ot->password = NULL;
    
    keyword = config_get(grp, "keyword");
    aliases = config_get(grp, "aliases");
    url = config_get(grp, "url");
    text = config_get(grp, "text");
    file = config_get(grp, "file");
    prefix = config_get(grp, "prefix");
    suffix = config_get(grp, "suffix");
    faked_sender = config_get(grp, "faked-sender");
    max_msgs = config_get(grp, "max-messages");
    split_chars = config_get(grp, "split-chars");
    split_suffix = config_get(grp, "split-suffix");
    omit_empty = config_get(grp, "omit-empty");
    header = config_get(grp, "header");
    footer = config_get(grp, "footer");
    username = config_get(grp, "username");
    password = config_get(grp, "password");

    if (url) {
	ot->type = TRANSTYPE_URL;
	ot->pattern = strdup(url);
    } else if (file) {
	ot->type = TRANSTYPE_FILE;
	ot->pattern = strdup(file);
    } else if (text) {
	ot->type = TRANSTYPE_TEXT;
	ot->pattern = strdup(text);
    } else if (username) {
	ot->type = TRANSTYPE_SENDSMS;
	ot->pattern = strdup("");
	ot->username = strdup(username);
	if (password)
	    ot->password = strdup(password);
    } else {
	error(0, "No url, file or text spesified");
	goto error;
    }
    if (keyword)
	ot->keyword = strdup(keyword);

    if (aliases) {
	ot->aliases = malloc(strlen(aliases)+2);
	if (ot->aliases != NULL)
	    sprintf(ot->aliases, "%s;", aliases);
    }
    else
	ot->aliases = strdup("");
    
    if ((ot->keyword == NULL && (ot->username == NULL || ot->password == NULL)) ||
	ot->pattern == NULL || ot->aliases == NULL)
	goto error;
    if (prefix != NULL && suffix != NULL) {

	ot->prefix = strdup(prefix);
	ot->suffix = strdup(suffix);
	if (ot->prefix == NULL || ot->suffix == NULL)
	    goto error;
    }
    if (faked_sender != NULL) {
	ot->faked_sender = strdup(faked_sender);
	if (ot->faked_sender == NULL)
	    goto error;
    }

    if (max_msgs != NULL) {
	ot->max_messages = atoi(max_msgs);
	if (split_chars != NULL) {
	    ot->split_chars = strdup(split_chars);
	    if (ot->split_chars == NULL)
		goto error;
	}
	if (split_suffix != NULL) {
	    ot->split_suffix = strdup(split_suffix);
	    if (ot->split_suffix == NULL)
		goto error;
	}
    }
    else
	ot->max_messages = 1;

    if (header != NULL)
	if ((ot->header = strdup(header)) == NULL)
	    goto error;
    if (footer != NULL)
	if ((ot->footer = strdup(footer)) == NULL)
	    goto error;

    if (omit_empty != NULL)
	ot->omit_empty = atoi(omit_empty);
    
    ot->args = count_occurences(ot->pattern, "%s");
    ot->args += count_occurences(ot->pattern, "%S");
    ot->has_catchall_arg = (count_occurences(ot->pattern, "%r") > 0) ||
	(count_occurences(ot->pattern, "%a") > 0);

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
		free(ot->aliases);
		free(ot->pattern);
		free(ot->prefix);
		free(ot->suffix);
		free(ot->faked_sender);
		free(ot->split_chars);
		free(ot->split_suffix);
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
	char alias_keyword[1024];
	int n;
	URLTranslation *t;

	n = octstr_list_len(words);
	if (n == 0)
		return NULL;
	keyword = octstr_get_cstr(octstr_list_get(words, 0));
	sprintf(alias_keyword, "%s;", keyword);

	for (t = trans->list; t != NULL; t = t->next) {
	    if (t->keyword != NULL) {
		if (strcasecmp(keyword, t->keyword) == 0 ||
		    str_case_str(t->aliases, alias_keyword) != NULL) {

		    if (n - 1 == t->args)
			break;
		    if (t->has_catchall_arg && n - 1 >= t->args)
			break;
		}
	    }
	}
	
	return t;
}


static URLTranslation *find_default_translation(URLTranslationList *trans)
{
    URLTranslation *t;
	
    for (t = trans->list; t != NULL; t = t->next) {
	if (t->keyword != NULL && strcasecmp("default", t->keyword) == 0)
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
