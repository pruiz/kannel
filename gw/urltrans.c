/*
 * urltrans.c - URL translations
 *
 * Lars Wirzenius for WapIT Ltd.
 */


#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "urltrans.h"
#include "gwlib/gwlib.h"


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
};


/*
 * Hold the list of all translations.
 */
struct URLTranslationList {
	List *list;
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
					List *words);
static URLTranslation *find_default_translation(URLTranslationList *trans);
static void encode_for_url(char *buf, char *str);


/***********************************************************************
 * Implementations of the functions declared in urltrans.h. See the
 * header for explanations of what they should do.
 */

URLTranslationList *urltrans_create(void) {
	URLTranslationList *trans;
	
	trans = gw_malloc(sizeof(URLTranslationList));
	trans->list = list_create();
	return trans;
}


void urltrans_destroy(URLTranslationList *trans) {
	URLTranslation *ot;
	
	if (trans == NULL)
		return;
	while (list_len(trans->list) > 0) {
		ot = list_extract_first(trans->list);
		destroy_onetrans(ot);
	}
	list_destroy(trans->list);
	gw_free(trans);
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
		
    list_append(trans->list, ot);
    return 0;
}


int urltrans_add_cfg(URLTranslationList *trans, Config *cfg) {
	ConfigGroup *grp;

	/*
	 * XXX this is KLUDGE. Should rewrite these one day --Kalle
	 */
	grp = config_find_first_group(cfg, "group", "sms-service");
	while (grp != NULL) {
	    if (urltrans_add_one(trans, grp) == -1)
		return -1;
	    grp = config_find_next_group(grp, "group", "sms-service");
	}
	grp = config_find_first_group(cfg, "group", "sendsms-user");
	while (grp != NULL) {
	    if (urltrans_add_one(trans, grp) == -1)
		return -1;
	    grp = config_find_next_group(grp, "group", "sendsms-user");
	}
	return 0;
}


URLTranslation *urltrans_find(URLTranslationList *trans, Octstr *text) {
	List *words;
	Octstr *word;
	URLTranslation *t;
	
	words = octstr_split_words(text);
	if (words == NULL)
	    return NULL;

	t = find_translation(trans, words);
	while (list_len(words) > 0) {
	    word = list_extract_first(words);
	    octstr_destroy(word);
	}
	list_destroy(words);
	if (t == NULL)
	    t = find_default_translation(trans);
	return t;
}



URLTranslation *urltrans_find_username(URLTranslationList *trans, 
				       char *name)
{
    URLTranslation *t;
    int i;

    for (i = 0; i < list_len(trans->list); ++i) {
	t = list_get(trans->list, i);
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
	struct tm tm;
	char *words[161];
	int max_words;
	List *word_list;
	int n;

	if (t->type == TRANSTYPE_SENDSMS)
	    return gw_strdup("");
	
	word_list = octstr_split_words(request->smart_sms.msgdata);
	n = list_len(word_list);
	max_words = sizeof(words) / sizeof(words[0]);
	if (n > max_words)
		n = max_words;
	for (j = 0; j < n; ++j)
		words[j] = octstr_get_cstr(list_get(word_list, j));

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

	buf = gw_malloc(len + 1);
	enc = gw_malloc(len + 1);

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
			tm = gw_gmtime(request->smart_sms.time);
			sprintf(s, "%04d-%02d-%02d+%02d:%02d",
				tm.tm_year + 1900,
				tm.tm_mon + 1,
				tm.tm_mday,
				tm.tm_hour,
				tm.tm_min);
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
	
	while (list_len(word_list) > 0)
		octstr_destroy(list_extract_first(word_list));
	list_destroy(word_list);
	gw_free(enc);
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
    
    ot = gw_malloc(sizeof(URLTranslation));

    ot->keyword = ot->aliases = ot->pattern = NULL;
    ot->prefix = ot->suffix = ot->faked_sender = NULL;
    ot->split_chars = ot->split_suffix = NULL;
    ot->footer = ot->header = NULL;
    ot->username = ot->password = NULL;
    ot->omit_empty = 0;
    
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
	ot->pattern = gw_strdup(url);
    } else if (file) {
	ot->type = TRANSTYPE_FILE;
	ot->pattern = gw_strdup(file);
    } else if (text) {
	ot->type = TRANSTYPE_TEXT;
	ot->pattern = gw_strdup(text);
    } else if (username) {
	ot->type = TRANSTYPE_SENDSMS;
	ot->pattern = gw_strdup("");
	ot->username = gw_strdup(username);
	if (password)
	    ot->password = gw_strdup(password);
    } else {
	error(0, "No url, file or text spesified");
	goto error;
    }
    if (keyword)
	ot->keyword = gw_strdup(keyword);

    if (aliases) {
	ot->aliases = gw_malloc(strlen(aliases)+2);
	sprintf(ot->aliases, "%s;", aliases);
    }
    else
	ot->aliases = gw_strdup("");
    
    if ((ot->keyword == NULL && (ot->username == NULL || ot->password == NULL)) ||
	ot->pattern == NULL || ot->aliases == NULL)
	goto error;
    if (prefix != NULL && suffix != NULL) {

	ot->prefix = gw_strdup(prefix);
	ot->suffix = gw_strdup(suffix);
	if (ot->prefix == NULL || ot->suffix == NULL)
	    goto error;
    }
    if (faked_sender != NULL) {
	ot->faked_sender = gw_strdup(faked_sender);
	if (ot->faked_sender == NULL)
	    goto error;
    }

    if (max_msgs != NULL) {
	ot->max_messages = atoi(max_msgs);
	if (split_chars != NULL) {
	    ot->split_chars = gw_strdup(split_chars);
	    if (ot->split_chars == NULL)
		goto error;
	}
	if (split_suffix != NULL) {
	    ot->split_suffix = gw_strdup(split_suffix);
	    if (ot->split_suffix == NULL)
		goto error;
	}
    }
    else
	ot->max_messages = 1;

    if (header != NULL)
	if ((ot->header = gw_strdup(header)) == NULL)
	    goto error;
    if (footer != NULL)
	if ((ot->footer = gw_strdup(footer)) == NULL)
	    goto error;

    if (omit_empty != NULL)
	ot->omit_empty = atoi(omit_empty);
    
    ot->args = count_occurences(ot->pattern, "%s");
    ot->args += count_occurences(ot->pattern, "%S");
    ot->has_catchall_arg = (count_occurences(ot->pattern, "%r") > 0) ||
	(count_occurences(ot->pattern, "%a") > 0);

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
		gw_free(ot->keyword);
		gw_free(ot->aliases);
		gw_free(ot->pattern);
		gw_free(ot->prefix);
		gw_free(ot->suffix);
		gw_free(ot->faked_sender);
		gw_free(ot->split_chars);
		gw_free(ot->split_suffix);
		gw_free(ot->username);
		gw_free(ot->password);
		gw_free(ot);
	}
}


/*
 * Find the appropriate translation 
 */
static URLTranslation *find_translation(URLTranslationList *trans, 
	List *words)
{
	char *keyword;
	char alias_keyword[1024];
	int i, n;
	URLTranslation *t;

	n = list_len(words);
	if (n == 0)
		return NULL;
	keyword = octstr_get_cstr(list_get(words, 0));
	sprintf(alias_keyword, "%s;", keyword);

        t = NULL;
	for (i = 0; i < list_len(trans->list); ++i) {
	    t = list_get(trans->list, i);
	    if (t->type != TRANSTYPE_SENDSMS && t->keyword != NULL)
	    {
		if ((strcasecmp(keyword, t->keyword) == 0 &&
		     strlen(keyword) == strlen(t->keyword))
		    ||
		    str_case_str(t->aliases, alias_keyword) != NULL) {

		    if (n - 1 == t->args)
			break;
		    if (t->has_catchall_arg && n - 1 >= t->args)
			break;
		}
	    }
	    t = NULL;
	}
	
	return t;
}


static URLTranslation *find_default_translation(URLTranslationList *trans)
{
    URLTranslation *t;
    int i;
	
    t = NULL;
    for (i = 0; i < list_len(trans->list); ++i) {
	t = list_get(trans->list, i);
	if (t->keyword != NULL && t->type != TRANSTYPE_SENDSMS
	    && strcasecmp("default", t->keyword) == 0)
	    break;
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
