/*
 * urltrans.c - URL translations
 *
 * Lars Wirzenius
 */


#include <ctype.h>
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
    long max_messages;	/* absolute limit of reply messages */
    long concatenation;	/* send long messages as concatenated SMS's if true */
    Octstr *split_chars;/* allowed chars to be used to split message */
    Octstr *split_suffix;/* chars added to end after each split (not last) */
    long omit_empty;	/* if the reply is empty, is notification send */
    Octstr *header;	/* string to be inserted to each SMS */
    Octstr *footer;	/* string to be appended to each SMS */
    List *accepted_smsc; /* smsc id's allowed to use this service. If not set,
			    all messages can use this service */
    
    Octstr *username;	/* send sms username */
    Octstr *password;	/* password associated */
    Octstr *forced_smsc;/* if smsc id is forcet to certain for this user */
    Octstr *default_smsc; /* smsc id if none given in http send-sms request */
    Octstr *allow_ip;	/* allowed IPs to request send-sms with this 
    	    	    	   account */
    Octstr *deny_ip;	/* denied IPs to request send-sms with this account */
    
    int args;
    int has_catchall_arg;
};


/*
 * Hold the list of all translations.
 */
struct URLTranslationList {
    List *list;
    Dict *dict;	/* Dict of lowercase Octstr */
};


/***********************************************************************
 * Declarations of internal functions. These are defined at the end of
 * the file.
 */

static long count_occurences(Octstr *str, Octstr *pat);
static URLTranslation *create_onetrans(CfgGroup *grp);
static void destroy_onetrans(void *ot);
static URLTranslation *find_translation(URLTranslationList *trans, 
					List *words, Octstr *smsc);
static URLTranslation *find_default_translation(URLTranslationList *trans);


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


int urltrans_add_one(URLTranslationList *trans, CfgGroup *grp)
{
    URLTranslation *ot;
    long i;
    List *list;
    Octstr *alias;
    
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


int urltrans_add_cfg(URLTranslationList *trans, Cfg *cfg) 
{
    CfgGroup *grp;
    List *list;
    
    list = cfg_get_multi_group(cfg, octstr_imm("sms-service"));
    while (list && (grp = list_extract_first(list)) != NULL) {
	if (urltrans_add_one(trans, grp) == -1) {
	    list_destroy(list, NULL);
	    return -1;
	}
    }
    list_destroy(list, NULL);
    
    list = cfg_get_multi_group(cfg, octstr_imm("sendsms-user"));
    while (list && (grp = list_extract_first(list)) != NULL) {
	if (urltrans_add_one(trans, grp) == -1) {
	    list_destroy(list, NULL);
	    return -1;
	}
    }
    list_destroy(list, NULL);

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
	case 'k':
	    enc = octstr_duplicate(list_get(word_list, 0));
	    octstr_url_encode(enc);
	    octstr_append(result, enc);
	    octstr_destroy(enc);
	    break;

	case 's':
	    enc = octstr_duplicate(list_get(word_list, nextarg));
	    octstr_url_encode(enc);
	    octstr_append(result, enc);
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
		enc = octstr_duplicate(list_get(word_list, j));
		octstr_url_encode(enc);
		if (j != nextarg)
		    octstr_append_char(result, '+');
		octstr_append(result, enc);
		octstr_destroy(enc);
	    }
	    break;
    
	/* NOTE: the sender and receiver is already switched in
	 *    message, so that's why we must use 'sender' when
	 *    we want original receiver and vice versa
	 */
	case 'P':
	    enc = octstr_duplicate(request->sms.sender);
    	    octstr_url_encode(enc);
	    octstr_append(result, enc);
	    octstr_destroy(enc);
	    break;

	case 'p':
	    enc = octstr_duplicate(request->sms.receiver);
	    octstr_url_encode(enc);
	    octstr_append(result, enc);
	    octstr_destroy(enc);
	    break;

	case 'Q':
	    if (strncmp(octstr_get_cstr(request->sms.sender), "00", 2) == 0) {
		enc = octstr_copy(request->sms.sender, 2, 
		    	    	  octstr_len(request->sms.sender));
		octstr_url_encode(enc);
		octstr_format_append(result, "%%2B%S", enc);
		octstr_destroy(enc);
	    } else {
		enc = octstr_duplicate(request->sms.sender);
    	    	octstr_url_encode(enc);
		octstr_append(result, enc);
		octstr_destroy(enc);
	    }
	    break;

	case 'q':
	    if (strncmp(octstr_get_cstr(request->sms.receiver),"00",2)==0) {
		enc = octstr_copy(request->sms.receiver, 2, 
		    	    	  octstr_len(request->sms.receiver));
		octstr_url_encode(enc);
		octstr_format_append(result, "%%2B%S", enc);
		octstr_destroy(enc);
	    } else {
		enc = octstr_duplicate(request->sms.receiver);
		octstr_url_encode(enc);
		octstr_append(result, enc);
		octstr_destroy(enc);
	    }
	break;

	case 'a':
	    for (j = 0; j < num_words; ++j) {
		enc = octstr_duplicate(list_get(word_list, j));
		octstr_url_encode(enc);
		if (j > 0)
		    octstr_append_char(result, '+');
		octstr_append(result, enc);
		octstr_destroy(enc);
	    }
	    break;

	case 'b':
	    enc = octstr_duplicate(request->sms.msgdata);
	    octstr_url_encode(enc);
	    octstr_append(result, enc);
	    octstr_destroy(enc);
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
static URLTranslation *create_onetrans(CfgGroup *grp)
{
    URLTranslation *ot;
    Octstr *aliases, *url, *text, *file;
    Octstr *accepted_smsc, *forced_smsc, *default_smsc;
    Octstr *grpname, *sendsms_user, *sms_service;
    int is_sms_service;
    
    grpname = cfg_get_group_name(grp);
    if (grpname == NULL)
    	return NULL;

    sms_service = octstr_imm("sms-service");
    sendsms_user = octstr_imm("sendsms-user");
    if (octstr_compare(grpname, sms_service) == 0)
    	is_sms_service = 1;
    else if (octstr_compare(grpname, sendsms_user) == 0)
    	is_sms_service = 0;
    else {
	octstr_destroy(grpname);
	return NULL;
    }
    octstr_destroy(grpname);

    ot = gw_malloc(sizeof(URLTranslation));

    ot->keyword = NULL;
    ot->aliases = NULL;
    ot->pattern = NULL;
    ot->prefix = NULL;
    ot->suffix = NULL;
    ot->faked_sender = NULL;
    ot->split_chars = NULL;
    ot->split_suffix = NULL;
    ot->footer = NULL;
    ot->header = NULL;
    ot->username = NULL;
    ot->password = NULL;
    ot->omit_empty = 0;
    ot->accepted_smsc = NULL;
    ot->forced_smsc = NULL;
    ot->default_smsc = NULL;
    ot->allow_ip = NULL;
    ot->deny_ip = NULL;
    
    if (is_sms_service) {
	url = cfg_get(grp, octstr_imm("url"));
	file = cfg_get(grp, octstr_imm("file"));
	text = cfg_get(grp, octstr_imm("text"));
	if (url != NULL) {
	    ot->type = TRANSTYPE_URL;
	    ot->pattern = url;
	} else if (file != NULL) {
	    ot->type = TRANSTYPE_FILE;
	    ot->pattern = file;
	} else if (text != NULL) {
	    ot->type = TRANSTYPE_TEXT;
	    ot->pattern = text;
	} else {
	    error(0, "Configuration group `sms-service' "
	    	     "did not specify url, file or text.");
    	    goto error;
	}

	ot->keyword = cfg_get(grp, octstr_imm("keyword"));
	if (ot->keyword == NULL) {
	    error(0, "Group 'sms-service' must include 'keyword'.");
	    goto error;
	}
	octstr_convert_range(ot->keyword, 0, octstr_len(ot->keyword), 
	    	    	     tolower);

	aliases = cfg_get(grp, octstr_imm("aliases"));
	if (aliases == NULL)
	    ot->aliases = list_create();
	else {
	    long i;
	    Octstr *os;

	    ot->aliases = octstr_split(aliases, octstr_imm(";"));
	    octstr_destroy(aliases);
	    for (i = 0; i < list_len(ot->aliases); ++i) {
		os = list_get(ot->aliases, i);
	    	octstr_convert_range(os, 0, octstr_len(os), tolower);
	    }
	}

	accepted_smsc = cfg_get(grp, octstr_imm("accepted-smsc"));
	if (accepted_smsc != NULL) {
	    ot->accepted_smsc = octstr_split(accepted_smsc, octstr_imm(";"));
	    octstr_destroy(accepted_smsc);
	}

	ot->prefix = cfg_get(grp, octstr_imm("prefix"));
	ot->suffix = cfg_get(grp, octstr_imm("suffix"));
	
	ot->args = count_occurences(ot->pattern, octstr_imm("%s"));
	ot->args += count_occurences(ot->pattern, octstr_imm("%S"));
	ot->has_catchall_arg = 
	    (count_occurences(ot->pattern, octstr_imm("%r")) > 0) ||
	    (count_occurences(ot->pattern, octstr_imm("%a")) > 0);
    } else {
	ot->type = TRANSTYPE_SENDSMS;
	ot->pattern = octstr_create("");
	ot->args = 0;
	ot->has_catchall_arg = 0;
	ot->username = cfg_get(grp, octstr_imm("username"));
	ot->password = cfg_get(grp, octstr_imm("password"));
	if (ot->password == NULL) {
	    error(0, "Password required for send-sms user");
	    goto error;
	}

	forced_smsc = cfg_get(grp, octstr_imm("forced-smsc"));
	default_smsc = cfg_get(grp, octstr_imm("default-smsc"));
	if (forced_smsc != NULL) {
	    if (default_smsc != NULL) {
		info(0, "Redundant default-smsc for send-sms user %s", 
		     octstr_get_cstr(ot->username));
	    }
	    ot->forced_smsc = forced_smsc;
	    octstr_destroy(default_smsc);
	} else  if (default_smsc != NULL)
	    ot->default_smsc = default_smsc;

	ot->deny_ip = cfg_get(grp, octstr_imm("user-deny-ip"));
	ot->allow_ip = cfg_get(grp, octstr_imm("user-allow-ip"));
    }

    if (cfg_get_integer(&ot->max_messages, grp, 
    	    	    	octstr_imm("max-messages")) == -1)
	ot->max_messages = 1;
    if (cfg_get_integer(&ot->concatenation, grp, 
    	    	    	octstr_imm("concatenation")) == -1)
	ot->concatenation = 0;
    if (cfg_get_integer(&ot->omit_empty, grp, 
			octstr_imm("omit-empty")) == -1)
	ot->omit_empty = 0;

    ot->header = cfg_get(grp, octstr_imm("header"));
    ot->footer = cfg_get(grp, octstr_imm("footer"));
    ot->faked_sender = cfg_get(grp, octstr_imm("faked-sender"));
    ot->split_chars = cfg_get(grp, octstr_imm("split-chars"));
    ot->split_suffix = cfg_get(grp, octstr_imm("split-suffix"));
    
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
	octstr_destroy(ot->keyword);
	list_destroy(ot->aliases, octstr_destroy_item);
	octstr_destroy(ot->pattern);
	octstr_destroy(ot->prefix);
	octstr_destroy(ot->suffix);
	octstr_destroy(ot->faked_sender);
	octstr_destroy(ot->split_chars);
	octstr_destroy(ot->split_suffix);
	octstr_destroy(ot->username);
	octstr_destroy(ot->password);
	list_destroy(ot->accepted_smsc, octstr_destroy_item);
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
    keyword = octstr_duplicate(keyword);
    octstr_convert_range(keyword, 0, octstr_len(keyword), tolower);
    
    list = dict_get(trans->dict, keyword);
    t = NULL;
    for (i = 0; i < list_len(list); ++i) {
	t = list_get(list, i);

	/* if smsc_id set and accepted_smsc exist, accept
	 * translation only if smsc id is in accept string
	 */
	if (smsc && t->accepted_smsc) {
	    if (list_search(t->accepted_smsc, smsc, octstr_item_match)) {
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

    octstr_destroy(keyword);    
    return t;
}


static URLTranslation *find_default_translation(URLTranslationList *trans)
{
    URLTranslation *t;
    int i;
	
    for (i = 0; i < list_len(trans->list); ++i) {
	t = list_get(trans->list, i);
	if (t->keyword != NULL && t->type != TRANSTYPE_SENDSMS
	    && octstr_compare(octstr_imm("default"), 
	    	    	      t->keyword) == 0)
	    return t;
    }
    return NULL;
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
