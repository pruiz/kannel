/*
 * urltrans.c - URL translations
 *
 * Lars Wirzenius
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
    Octstr *keyword;	/* keyword in SMS (similar) query */
    List *aliases;	/* keyword aliases, List of Octstr */
    int type;		/* see enumeration in header file */
    Octstr *pattern;	/* url, text or file-name pattern */
    Octstr *prefix;	/* for prefix-cut */
    Octstr *suffix;	/* for suffix-cut */
    Octstr *faked_sender;/* works only with certain services */
    int max_messages;	/* absolute limit of reply messages */
    int concatenation;	/* send long messages as concatenated SMS's if true */
    Octstr *split_chars;/* allowed chars to be used to split message */
    Octstr *split_suffix;/* chars added to end after each split (not last) */
    int omit_empty;	/* if the reply is empty, is notification send */
    Octstr *header;	/* string to be inserted to each SMS */
    Octstr *footer;	/* string to be appended to each SMS */
    Octstr *accepted_smsc; /* smsc id's allowed to use this service. If not set,
			    all messages can use this service */
    
    Octstr *username;	/* send sms username */
    Octstr *password;	/* password associated */
    Octstr *forced_smsc;/* if smsc id is forcet to certain for this user */
    Octstr *default_smsc; /* smsc id if none given in http send-sms request */
    Octstr *allow_ip;	/* allowed IPs to request send-sms with this account */
    Octstr *deny_ip;	/* denied IPs to request send-sms with this account */
    
    int args;
    int has_catchall_arg;
};


/*
 * Hold the list of all translations.
 */
struct URLTranslationList {
    List *list;
    Dict *dict;
};


/***********************************************************************
 * Declarations of internal functions. These are defined at the end of
 * the file.
 */

static long count_occurences(Octstr *str, Octstr *pat);
static URLTranslation *create_onetrans(ConfigGroup *grp);
static void destroy_onetrans(void *ot);
static URLTranslation *find_translation(URLTranslationList *trans, 
					List *words, Octstr *smsc);
static URLTranslation *find_default_translation(URLTranslationList *trans);
static Octstr *encode_for_url(Octstr *str);


/***********************************************************************
 * Implementations of the functions declared in urltrans.h. See the
 * header for explanations of what they should do.
 */


static void destroy_keyword_list(void *list)
{
    list_destroy(list, NULL);
}


URLTranslationList *urltrans_create(void) 
{
    URLTranslationList *trans;
    
    trans = gw_malloc(sizeof(URLTranslationList));
    trans->list = list_create();
    trans->dict = dict_create(1024, destroy_keyword_list);
    return trans;
}


void urltrans_destroy(URLTranslationList *trans) 
{
    list_destroy(trans->list, destroy_onetrans);
    dict_destroy(trans->dict);
    gw_free(trans);
}


int urltrans_add_one(URLTranslationList *trans, ConfigGroup *grp)
{
    URLTranslation *ot;
    long i;
    List *list;
    Octstr *alias;
	
    if (config_get(grp, "keyword") == NULL &&
	config_get(grp, "username") == NULL)
	return 0;
    
    ot = create_onetrans(grp);
    if (ot == NULL)
	return -1;
		
    list_append(trans->list, ot);
    
    if (ot->keyword == NULL || ot->type == TRANSTYPE_SENDSMS)
    	return 0;

    list = dict_get(trans->dict, ot->keyword);
    if (list == NULL) {
    	list = list_create();
	dict_put(trans->dict, ot->keyword, list);
    }
    list_append(list, ot);

    for (i = 0; i < list_len(ot->aliases); ++i) {
	alias = list_get(ot->aliases, i);
	list = dict_get(trans->dict, alias);
	if (list == NULL) {
	    list = list_create();
	    dict_put(trans->dict, alias, list);
	}
	list_append(list, ot);
    }

    return 0;
}


int urltrans_add_cfg(URLTranslationList *trans, Config *cfg) 
{
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


URLTranslation *urltrans_find(URLTranslationList *trans, Octstr *text,
			      Octstr *smsc) 
{
    List *words;
    URLTranslation *t;
    
    words = octstr_split_words(text);
    
    t = find_translation(trans, words, smsc);
    list_destroy(words, octstr_destroy_item);
    if (t == NULL)
	t = find_default_translation(trans);
    return t;
}



URLTranslation *urltrans_find_username(URLTranslationList *trans, 
				       Octstr *name)
{
    URLTranslation *t;
    int i;

    gw_assert(name != NULL);
    for (i = 0; i < list_len(trans->list); ++i) {
	t = list_get(trans->list, i);
	if (t->type == TRANSTYPE_SENDSMS) {
	    if (octstr_compare(name, t->username) == 0)
		return t;
	}
    }
    return NULL;
}



Octstr *urltrans_get_pattern(URLTranslation *t, Msg *request)
{
    Octstr *enc;
    int nextarg, j;
    struct tm tm;
    int num_words;
    List *word_list;
    Octstr *result;
    long pattern_len;
    long pos;
    int c;
    long i;
    Octstr *temp;
    
    if (t->type == TRANSTYPE_SENDSMS)
	return octstr_create("");
    
    word_list = octstr_split_words(request->sms.msgdata);
    num_words = list_len(word_list);
    
    result = octstr_create("");
    pattern_len = octstr_len(t->pattern);
    nextarg = 1;
    pos = 0;
    for (;;) {
    	while (pos < pattern_len) {
	    c = octstr_get_char(t->pattern, pos);
	    if (c == '%' && pos + 1 < pattern_len)
	    	break;
	    octstr_append_char(result, c);
	    ++pos;
	}

    	if (pos == pattern_len)
	    break;

	switch (octstr_get_char(t->pattern, pos + 1)) {
	case 's':
	    enc = encode_for_url(list_get(word_list, nextarg));
	    octstr_format_append(result, "%s", enc);
	    octstr_destroy(enc);
	    ++nextarg;
	    break;

	case 'S':
	    temp = list_get(word_list, nextarg);
	    for (i = 0; i < octstr_len(temp); ++i) {
		if (octstr_get_char(temp, i) == '*')
		    octstr_append_char(result, '~');
		else
		    octstr_append_char(result, octstr_get_char(temp, i));
	    }
	    ++nextarg;
	    break;

	case 'r':
	    for (j = nextarg; j < num_words; ++j) {
		enc = encode_for_url(list_get(word_list, j));
		if (j == nextarg)
		    octstr_format_append(result, "%s", enc);
		else
		    octstr_format_append(result, "+%s", enc);
		octstr_destroy(enc);
	    }
	    break;
    
	/* NOTE: the sender and receiver is already switched in
	 *    message, so that's why we must use 'sender' when
	 *    we want original receiver and vice versa
	 */
	case 'P':
	    enc = encode_for_url(request->sms.sender);
	    octstr_format_append(result, "%s", enc);
	    octstr_destroy(enc);
	    break;

	case 'p':
	    enc = encode_for_url(request->sms.receiver);
	    octstr_format_append(result, "%s", enc);
	    octstr_destroy(enc);
	    break;

	case 'Q':
	    if (strncmp(octstr_get_cstr(request->sms.sender), "00", 2) == 0) {
		temp = octstr_copy(request->sms.sender, 2, 
		    	    	  octstr_len(request->sms.sender));
		enc = encode_for_url(temp);
		octstr_format_append(result, "%%2B%s", enc);
		octstr_destroy(enc);
		octstr_destroy(temp);
	    } else {
		enc = encode_for_url(request->sms.sender);
		octstr_format_append(result, "%s", enc);
		octstr_destroy(enc);
	    }
	    break;

	case 'q':
	    if (strncmp(octstr_get_cstr(request->sms.receiver), "00", 2) == 0) {
		temp = octstr_copy(request->sms.receiver, 2, 
		    	    	  octstr_len(request->sms.receiver));
		enc = encode_for_url(temp);
		octstr_format_append(result, "%%2B%s", enc);
		octstr_destroy(enc);
		octstr_destroy(temp);
	    } else {
		enc = encode_for_url(request->sms.receiver);
		octstr_format_append(result, "%s", enc);
		octstr_destroy(enc);
	    }
	break;

	case 'a':
	    for (j = 0; j < num_words; ++j) {
		enc = encode_for_url(list_get(word_list, j));
		if (j > 0)
		    octstr_format_append(result, "+%s", enc);
		else
		    octstr_format_append(result, "%s", enc);
		octstr_destroy(enc);
	    }
	    break;

	case 't':
	    tm = gw_gmtime(request->sms.time);
	    octstr_format_append(result, "%04d-%02d-%02d+%02d:%02d",
				 tm.tm_year + 1900,
				 tm.tm_mon + 1,
				 tm.tm_mday,
				 tm.tm_hour,
				 tm.tm_min);
	    break;

	case '%':
	    octstr_format_append(result, "%%");
	    break;

	default:
	    octstr_format_append(result, "%%%c",
	    	    	    	 octstr_get_char(t->pattern, pos));
	    break;
	}

	pos += 2;
    }
    
    list_destroy(word_list, octstr_destroy_item);
    return result;
}


int urltrans_type(URLTranslation *t) 
{
    return t->type;
}

Octstr *urltrans_prefix(URLTranslation *t) 
{
    return t->prefix;
}

Octstr *urltrans_suffix(URLTranslation *t) 
{
    return t->suffix;
}

Octstr *urltrans_faked_sender(URLTranslation *t) 
{
    return t->faked_sender;
}

int urltrans_max_messages(URLTranslation *t) 
{
    return t->max_messages;
}

int urltrans_concatenation(URLTranslation *t) 
{
    return t->concatenation;
}

Octstr *urltrans_split_chars(URLTranslation *t) 
{
    return t->split_chars;
}

Octstr *urltrans_split_suffix(URLTranslation *t) 
{
    return t->split_suffix;
}

int urltrans_omit_empty(URLTranslation *t) 
{
    return t->omit_empty;
}

Octstr *urltrans_header(URLTranslation *t) 
{
    return t->header;
}

Octstr *urltrans_footer(URLTranslation *t) 
{
    return t->footer;
}

Octstr *urltrans_username(URLTranslation *t) 
{
    return t->username;
}

Octstr *urltrans_password(URLTranslation *t) 
{
    return t->password;
}

Octstr *urltrans_forced_smsc(URLTranslation *t) 
{
    return t->forced_smsc;
}

Octstr *urltrans_default_smsc(URLTranslation *t) 
{
    return t->default_smsc;
}

Octstr *urltrans_accepted_smsc(URLTranslation *t) 
{
    return t->accepted_smsc;
}

Octstr *urltrans_allow_ip(URLTranslation *t) 
{
    return t->allow_ip;
}

Octstr *urltrans_deny_ip(URLTranslation *t) 
{
    return t->deny_ip;
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
    char *prefix, *suffix, *faked_sender, *max_msgs, *concatenation;
    char *split_chars, *split_suffix, *omit_empty;
    char *username, *password;
    char *header, *footer;
    char *accepted_smsc, *forced_smsc, *default_smsc;
    char *allow_ip, *deny_ip;
    
    ot = gw_malloc(sizeof(URLTranslation));

    ot->keyword = NULL;
    ot->aliases = NULL;
    ot->pattern = NULL;
    ot->prefix = ot->suffix = NULL;
    ot->faked_sender = NULL;
    ot->split_chars = ot->split_suffix = NULL;
    ot->footer = ot->header = NULL;
    ot->username = ot->password = NULL;
    ot->omit_empty = 0;
    ot->accepted_smsc = ot->forced_smsc = ot->default_smsc = NULL;
    ot->allow_ip = ot->deny_ip = NULL;
    
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
    concatenation = config_get(grp, "concatenation");

    accepted_smsc = config_get(grp, "accepted-smsc");
    forced_smsc = config_get(grp, "forced-smsc");
    default_smsc = config_get(grp, "default-smsc");

    allow_ip = config_get(grp, "user-allow-ip");
    deny_ip = config_get(grp, "user-deny-ip");
    
    if (url) {
	ot->type = TRANSTYPE_URL;
	ot->pattern = octstr_create(url);
    } else if (file) {
	ot->type = TRANSTYPE_FILE;
	ot->pattern = octstr_create(file);
    } else if (text) {
	ot->type = TRANSTYPE_TEXT;
	ot->pattern = octstr_create(text);
    } else if (username) {
	ot->type = TRANSTYPE_SENDSMS;
	ot->pattern = octstr_create("");
	ot->username = octstr_create(username);
	if (password)
	    ot->password = octstr_create(password);
	else {
	    error(0, "Password required for send-sms user");
	    goto error;
	}
	if (forced_smsc) {
	    if (default_smsc)
		info(0, "Redundant default-smsc for send-sms user %s", username);
	    ot->forced_smsc = octstr_create(forced_smsc);
	} else if (default_smsc) {
	    ot->default_smsc = octstr_create(default_smsc);
	}
	if (allow_ip)
	    ot->allow_ip = octstr_create(allow_ip);
	if (deny_ip)
	    ot->deny_ip = octstr_create(deny_ip);
	
    } else {
	error(0, "No url, file or text spesified");
	goto error;
    }

    if (ot->type != TRANSTYPE_SENDSMS) {	/* sms-service */
	if (keyword)
	    ot->keyword = octstr_create(keyword);
	else {
	    error(0, "keyword required for sms-service");
	    goto error;
	}
	if (aliases) {
	    Octstr *temp = octstr_create(aliases);
	    ot->aliases = octstr_split(temp, 
	    	    	    	       octstr_create_immutable(";"));
    	    octstr_destroy(temp);
	} else
	    ot->aliases = list_create();

	if (accepted_smsc)
	    ot->accepted_smsc = octstr_create(accepted_smsc);
	    
	if (prefix != NULL && suffix != NULL) {
	    ot->prefix = octstr_create(prefix);
	    ot->suffix = octstr_create(suffix);
	}

	ot->args = count_occurences(ot->pattern, 
	    	    	    	    octstr_create_immutable("%s"));
	ot->args += count_occurences(ot->pattern, 
	    	    	    	     octstr_create_immutable("%S"));
	ot->has_catchall_arg = 
	    (count_occurences(ot->pattern, octstr_create_immutable("%r")) > 0) ||
	    (count_occurences(ot->pattern, octstr_create_immutable("%a")) > 0);
    }
    else { 		/* send-sms user */
	ot->args = 0;
	ot->has_catchall_arg = 0;
    }

    /* things that apply to both */
    
    if (faked_sender != NULL)
	ot->faked_sender = octstr_create(faked_sender);

    if (max_msgs != NULL) {
	ot->max_messages = atoi(max_msgs);
	if (split_chars != NULL)
	    ot->split_chars = octstr_create(split_chars);

	if (split_suffix != NULL)
	    ot->split_suffix = octstr_create(split_suffix);
    }
    else
	ot->max_messages = 1;

    if (concatenation != NULL) {
	ot->concatenation = atoi(concatenation);
    }
    else
    	 ot->concatenation =  0;
    	 
    if (header != NULL)
	ot->header = octstr_create(header);

    if (footer != NULL)
	ot->footer = octstr_create(footer);

    if (omit_empty != NULL)
	ot->omit_empty = atoi(omit_empty);

    
    return ot;
error:
    error(errno, "Couldn't create a URLTranslation.");
    destroy_onetrans(ot);
    return NULL;
}


/*
 * Free one URLTranslation.
 */
static void destroy_onetrans(void *p) 
{
    URLTranslation *ot;
    
    ot = p;
    if (ot != NULL) {
	gw_free(ot->keyword);
	gw_free(ot->aliases);
	octstr_destroy(ot->pattern);
	octstr_destroy(ot->prefix);
	octstr_destroy(ot->suffix);
	octstr_destroy(ot->faked_sender);
	octstr_destroy(ot->split_chars);
	octstr_destroy(ot->split_suffix);
	octstr_destroy(ot->username);
	octstr_destroy(ot->password);
	octstr_destroy(ot->accepted_smsc);
	octstr_destroy(ot->forced_smsc);
	octstr_destroy(ot->default_smsc);
	octstr_destroy(ot->allow_ip);
	octstr_destroy(ot->deny_ip);
	gw_free(ot);
    }
}


/*
 * Find the appropriate translation 
 */
static URLTranslation *find_translation(URLTranslationList *trans, 
	List *words, Octstr *smsc)
{
    Octstr *keyword;
    int i, n;
    URLTranslation *t;
    List *list;
    
    n = list_len(words);
    if (n == 0)
	return NULL;
    keyword = list_get(words, 0);
    
    list = dict_get(trans->dict, keyword);
    t = NULL;
    for (i = 0; i < list_len(list); ++i) {
	t = list_get(list, i);

	/* if smsc_id set and accepted_smsc exist, accept
	 * translation only if smsc id is in accept string
	 */
	if (smsc && t->accepted_smsc) {
	    if (str_find_substr(octstr_get_cstr(t->accepted_smsc),
				octstr_get_cstr(smsc), ";")==0)
	    {
		t = NULL;
		continue;
	    }
	}
	if (n - 1 == t->args)
	    break;
	if (t->has_catchall_arg && n - 1 >= t->args)
	    break;
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
	    && octstr_compare(octstr_create_immutable("default"), 
	    	    	      t->keyword) == 0)
	    break;
    }
    return t;
}



/*
 * Encode `str' for insertion into a URL. Return result.
 * 
 * RFC 2396 defines the list of characters that need to be encoded.
 */
static Octstr *encode_for_url(Octstr *str) {
    static unsigned char *safe = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "abcdefghijklmnopqrstuvwxyz-_.!~*'()";
    static char is_safe[UCHAR_MAX + 1];
    static int init = 0;
    Octstr *enc;
    long i, len;
    int c;
    
    if (!init) {
	for (i = 0; safe[i] != '\0'; ++i)
	    is_safe[safe[i]] = 1;
	init = 1;
    }
	
    enc = octstr_create("");
    len = octstr_len(str);
    for (i = 0; i < len; ++i) {
	c = octstr_get_char(str, i);
	if (is_safe[c])
	    octstr_append_char(enc, c);
	else
	    octstr_format_append(enc, "%%%02x", c);
    }
    return enc;
}



/*
 * Count the number of times `pat' occurs in `str'.
 */
static long count_occurences(Octstr *str, Octstr *pat)
{
    long count;
    long pos;
    long len;
    
    count = 0;
    pos = 0;
    len = octstr_len(pat);
    while ((pos = octstr_search(str, pat, pos)) != -1) {
    	++count;
	pos += len;
    }
    return count;
}
