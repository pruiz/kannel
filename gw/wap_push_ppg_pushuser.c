/*
 * wap_push_ppg_pushuser.c: Implementation of wap_push_ppg_pushuser.h header.
 *
 * By Aarno Syvänen for Wiral Ltd.
 */

#include "wap_push_ppg_pushuser.h"
#include "numhash.h"

/***************************************************************************
 *
 * Global data structures
 *
 * Hold authentication data for one ppg user
 */

struct WAPPushUser {
    Octstr *username;                  /* the username of this ppg user */
    Octstr *password;                  /* and password */
    Octstr *allowed_prefix;            /* phone number prefixes allowed by 
                                          this user when pushing*/
    Octstr *denied_prefix;             /* and denied ones */
    Numhash *white_list;               /* phone numbers of this user, used for 
                                          push*/
    Numhash *black_list;               /* numbers should not be used for push*/
    Octstr *user_deny_ip;              /* this user allows pushes from theses 
                                          IPs*/
    Octstr *user_allow_ip;             /* and denies them from these*/
};

typedef struct WAPPushUser WAPPushUser;

/*
 * Hold authentication data of all ppg users
 */

struct WAPPushUserList {
    List *list;
    Dict *names;
};

typedef struct WAPPushUserList WAPPushUserList; 

static WAPPushUserList *users = NULL;

/*
 * This hash table stores time when a specific ip is allowed to try next time.
 */
static Dict *next_try = NULL;

/****************************************************************************
 *
 * Prototypes of internal functions
 */

static void destroy_users_list(void *l);
static WAPPushUserList *pushusers_create(long number_of_users);
static WAPPushUser *create_oneuser(CfgGroup *grp);
static void destroy_oneuser(void *p);
static int oneuser_add(CfgGroup *cfg);
static WAPPushUser *user_find_by_username(Octstr *username);
static int password_matches(WAPPushUser *u, Octstr *password);
static int ip_allowed_by_user(WAPPushUser *u, Octstr *ip);
static int prefix_allowed(WAPPushUser *u, Octstr *number);
static int whitelisted(WAPPushUser *u, Octstr *number);
static int blacklisted(WAPPushUser *u, Octstr *number);
static int wildcarded_ip_found(Octstr *ip, Octstr *needle, Octstr *ip_sep);
static int response(List *push_headers, WAPPushUser **u, Octstr **username, 
                    Octstr **password);
static void challenge(HTTPClient *c, List *push_headers);
static int parse_cgivars(List *cgivars, Octstr **username, Octstr **password);

/****************************************************************************
 *
 * Implementation of external functions
 */

/*
 * Initialize the whole module and fill the push users list.
 */
int wap_push_ppg_pushuser_list_add(List *list, long number_of_pushes, 
                                   long number_of_users)
{
    CfgGroup *grp;

    next_try = dict_create(number_of_pushes, octstr_destroy_item);
    users = pushusers_create(number_of_users);
    gw_assert(list);
    while (list && (grp = list_extract_first(list))) {
        if (oneuser_add(grp) == -1) {
	    list_destroy(list, NULL);
            return -1;
        }
    }
    list_destroy(list, NULL);

    return 0;
}

void wap_push_ppg_pushuser_list_destroy(void)
{
    dict_destroy(next_try);
    if (users == NULL)
        return;

    list_destroy(users->list, destroy_oneuser);
    dict_destroy(users->names);
    gw_free(users);
}


/*
 * This function does authentication possible before compiling the control 
 * document. This means:
 *           a) password authentication by url or by headers (it is, by basic
 *              authentication response, see rfc 2617, chapter 2) 
 *           b) if this does not work, basic authentication by challenge - 
 *              response 
 *           c) enforcing various ip lists
 *
 * Try to find username and password first from the url, then form headers. If
 * both fails, try basic authentication.
 * Then check does this user allow a push from this ip, then check the password.
 *
 * For protection against brute force and partial protection for denial of serv-
 * ice attacks, an exponential backup algorithm is used. Time when a specific ip  
 * is allowed to reconnect, is stored in Dict next_try. If an ip tries to recon-
 * nect before this (because first periods are small, this means the attempt can-
 * not be manual) we drop the connection.
 *
 * Rfc 2617, chapter 1 states that if we do not accept credentials, we must send  
 * a new challenge.
 *
 * Output an authenticated username.
 * This function should be called only when there are a push users list; the 
 * caller is responsible for this.
 */
int wap_push_ppg_pushuser_authenticate (HTTPClient *c, List *cgivars, 
                                        Octstr *ip, List *push_headers, 
                                        Octstr **username) {
        time_t now;
	time_t *next_time;
        static time_t addition = 0.1;  /* used only for this thread, and this
                                          function */
        static long multiplier = 0L;   /* and again */
        WAPPushUser *u;
        Octstr *copy,
               *password;

        next_time = NULL;
        copy = octstr_duplicate(ip);

        if (parse_cgivars(cgivars, username, &password)) {
	    if (!response(push_headers, &u, username, &password)) {
	      debug("wap.push.ppg", 0, "no username/password for client %s", 
                     octstr_get_cstr(copy));
	        goto not_listed;
            }
        }

        if (!ip_allowed_by_user(u, ip)) {
	    error(0, "ip %s is not allowed by %s", octstr_get_cstr(copy), 
                  octstr_get_cstr(*username));
	    goto not_listed;
        }

        if ((next_time = dict_get(next_try, ip)) != NULL) {
            time(&now);
            if (difftime(now, *next_time) < 0.0) {
	        error(0, "another try from %s, not much time used", 
                      octstr_get_cstr(copy));
	        goto listed;
            }
        }

        if (!password_matches(u, password)) {
	    error(0, "wrong password in request from %s", octstr_get_cstr(copy));
            goto listed;
        }

        dict_remove(next_try, ip);       /* no restrictions after authentica-
                                            tion */
        octstr_destroy(copy);
        return 1;

listed:
        challenge(c, push_headers);
        addition *= multiplier;
        next_time += addition;
        dict_put(next_try, ip, next_time);

        if (multiplier)
            multiplier <<= 1;
        else
	    ++multiplier;

        http_close_client(c);
        octstr_destroy(copy);
        return 0;

not_listed:
        challenge(c, push_headers);
        http_close_client(c);
        octstr_destroy(copy);
        return 0;
}

/*
 * This function checks phone number for allowed prefixes, black lists and white
 * lists. Note that the phone number necessarily follows the international 
 * format (a requirement by our pap compiler).
 */
int wap_push_ppg_pushuser_client_phone_number_acceptable(Octstr *username, 
        Octstr *number)
{
    WAPPushUser *u;

    u = user_find_by_username(username);
    if (!prefix_allowed(u, number)) {
        error(0, "Number %s not allowed by user %s (wrong prefix)", 
              octstr_get_cstr(number), octstr_get_cstr(username));
        return 0;
    }

    if (blacklisted(u, number)) {
        error(0, "Number %s not allowed by user %s (blacklisted)", 
              octstr_get_cstr(number), octstr_get_cstr(username) );
        return 0;
    }

    if (!whitelisted(u, number)) {
        error(0, "Number %s not allowed by user %s (not whitelisted)", 
              octstr_get_cstr(number), octstr_get_cstr(username) );
        return 0;
    }

    return 1;
}

int wap_push_ppg_pushuser_search_ip_from_wildcarded_list(Octstr *haystack, 
        Octstr *needle, Octstr *list_sep, Octstr *ip_sep)
{
    List *ips;
    long i;
    Octstr *ip;

    gw_assert(haystack);
    gw_assert(list_sep);
    gw_assert(ip_sep);
    if (octstr_search_char(haystack, '*', 0) < 0)
        return octstr_search(haystack, needle, 0);
    
    ips = octstr_split(haystack, list_sep);
    for (i = 0; i < list_len(ips); ++i) {
        ip = list_get(ips, i);
        if (wildcarded_ip_found(ip, needle, ip_sep))
	    goto found;
        octstr_destroy(ip);
    }

    list_destroy(ips, octstr_destroy_item);
    return 0;

found:
    list_destroy(ips, octstr_destroy_item);
    octstr_destroy(ip);
    return 1;
}


/***************************************************************************
 *
 * Implementation of internal functions
 */

static void destroy_users_list(void *l)
{
    list_destroy(l, NULL);
}

static WAPPushUserList *pushusers_create(long number_of_users) 
{
    users = gw_malloc(sizeof(WAPPushUserList));
    users->list = list_create();
    users->names = dict_create(number_of_users, destroy_users_list);

    return users;
}

/*
 * Allocate memory for one push user and read configuration data to it. We initial-
 * ize all fields to NULL, because the value NULL means that the configuration did
 * not have this variable. 
 * Return NULL when failure, a pointer to the data structure otherwise.
 */
static WAPPushUser *create_oneuser(CfgGroup *grp)
{
    WAPPushUser *u;
    Octstr *grpname,
           *os;

    grpname = cfg_get(grp, octstr_imm("wap-push-user"));
    if (grpname == NULL)
        return NULL;
   
    u = gw_malloc(sizeof(WAPPushUser));
    u->username = NULL;                  
    u->allowed_prefix = NULL;           
    u->denied_prefix = NULL;             
    u->white_list = NULL;               
    u->black_list = NULL;              
    u->user_deny_ip = NULL;              
    u->user_allow_ip = NULL;

    u->username = cfg_get(grp, octstr_imm("ppg-username"));
    u->password = cfg_get(grp, octstr_imm("ppg-password"));
    if (u->password == NULL) {
        error(0, "password for user %s missing", octstr_get_cstr(u->username));
        goto error;
    }

    u->user_deny_ip = cfg_get(grp, octstr_imm("deny-ip"));
    u->user_allow_ip = cfg_get(grp, octstr_imm("allow-ip"));
    u->allowed_prefix = cfg_get(grp, octstr_imm("allowed-prefix"));
    u->denied_prefix = cfg_get(grp, octstr_imm("denied-prefix"));

    os = cfg_get(grp, octstr_imm("white-list"));
    if (os != NULL) {
	u->white_list = numhash_create(octstr_get_cstr(os));
	octstr_destroy(os);
    }
    os = cfg_get(grp, octstr_imm("black-list"));
    if (os != NULL) {
	u->black_list = numhash_create(octstr_get_cstr(os));
	octstr_destroy(os);
    }

    return u;

error:
    destroy_oneuser(u);
    return NULL;
}

static void destroy_oneuser(void *p) 
{
     WAPPushUser *u;

     u = p;
     if (u == NULL)
         return;

     octstr_destroy(u->username);                  
     octstr_destroy(u->allowed_prefix);           
     octstr_destroy(u->denied_prefix);             
     numhash_destroy(u->white_list);               
     numhash_destroy(u->black_list);              
     octstr_destroy(u->user_deny_ip);              
     octstr_destroy(u->user_allow_ip);
     gw_free(u);             
}

/*
 * Add an user to the push users list
 */
static int oneuser_add(CfgGroup *grp)
{
    WAPPushUser *u;
    List *list;

    u = create_oneuser(grp);
    if (u == NULL)
        return -1;

    list_append(users->list, u);

    list = dict_get(users->names, u->username);
    if (list == NULL) {
        list = list_create();
        dict_put(users->names, u->username, list);
    }

    return 0;
}

static WAPPushUser *user_find_by_username(Octstr *username)
{
    WAPPushUser *u;
    long i;
    List *list;

    gw_assert(username);
    if ((list = dict_get(users->names, username)) == NULL)
         return NULL;

    for (i = 0; i < list_len(users->list); ++i) {
         u = list_get(users->list, i);
         if (octstr_compare(u->username, username) == 0)
	   return u;
    }

    return NULL;
}

static int password_matches(WAPPushUser *u, Octstr *password)
{
    gw_assert(password);
    return u->password == password;
}

static int wildcarded_ip_found(Octstr *ip, Octstr *needle, Octstr *ip_sep)
{
    List *ip_fragments,
         *needle_fragments;
    long i;
    Octstr *ip_fragment,
           *needle_fragment;

    ip_fragments = octstr_split(ip, ip_sep);
    needle_fragments = octstr_split(needle, ip_sep);

    gw_assert(list_len(ip_fragments) == list_len(needle_fragments));
    for (i = 0; i < list_len(ip_fragments); ++i) {
        ip_fragment = list_get(ip_fragments, i);
        needle_fragment = list_get(needle_fragments, i);
        if (octstr_compare(ip_fragment, needle_fragment) != 0 && 
                octstr_compare(ip_fragment, octstr_imm("*")) != 0)
	    goto not_found;
        octstr_destroy(needle_fragment);
        octstr_destroy(ip_fragment);
    }

    list_destroy(ip_fragments, octstr_destroy_item);
    list_destroy(needle_fragments, octstr_destroy_item);
    return 1;

not_found:
    octstr_destroy(needle_fragment);
    octstr_destroy(ip_fragment);
    list_destroy(ip_fragments, octstr_destroy_item);
    list_destroy(needle_fragments, octstr_destroy_item);
    return 0;
}

/*
 * User_deny_ip = '*.*.*.*' is here taken literally: no ips allowed by this 
 * user (definitely strange, but not a fatal error). 
 */
static int ip_allowed_by_user(WAPPushUser *u, Octstr *ip)
{
    Octstr *copy;

    copy = octstr_duplicate(u->username);
    if (u->user_deny_ip == NULL && u->user_allow_ip == NULL)
        goto allowed;

    if (octstr_compare(u->user_deny_ip, octstr_imm("*.*.*.*")) == 0) {
        warning(0, "no ips allowed for %s", octstr_get_cstr(copy));
        goto denied;
    }

    if (octstr_compare(u->user_allow_ip, octstr_imm("*.*.*.*")) == 0)
        goto allowed;

    if (wap_push_ppg_pushuser_search_ip_from_wildcarded_list(u->user_deny_ip, 
            ip, octstr_imm(";"), octstr_imm(".")) == 0)
        goto denied;

    if (wap_push_ppg_pushuser_search_ip_from_wildcarded_list(u->user_allow_ip, 
            ip, octstr_imm(";"), octstr_imm(".")) != 0)
        goto allowed;

    octstr_destroy(copy);
    return 0;

allowed:
    octstr_destroy(copy);
    return 1;

denied:
    octstr_destroy(copy);
    return 0;
}

/*
 * HTTP basic authentication response is defined in rfc 2617
 */
static int response(List *push_headers, WAPPushUser **u, Octstr **username, 
                   Octstr **password)
{
    Octstr *header_value,
           *basic;
    size_t basic_len;
    List *auth_list;

    header_value = http_header_find_first(push_headers, "Authorization"); 
    octstr_strip_blanks(header_value);
    basic = octstr_imm("Basic");
    basic_len = octstr_len(basic);

    if (octstr_ncompare(header_value, basic, basic_len) != 0)
        goto no_response1;

    octstr_delete(header_value, 0, basic_len);
    octstr_strip_blanks(header_value);
    octstr_base64_to_binary(header_value);
    auth_list = octstr_split(header_value, octstr_imm(":"));

    if (list_len(auth_list) != 2)
        goto no_response2;
    
    *username = list_get(auth_list, 0);
    *password = list_get(auth_list, 1);

    if (username == NULL || (*u = user_find_by_username(*username)) == NULL) {
        goto no_response2;
    }

    if (!password_matches(*u, *password)) {
        goto no_response2;
    }

    list_destroy(auth_list, octstr_destroy_item);
    octstr_destroy(header_value);
    return 1;

no_response1:
    octstr_destroy(header_value);
    return 0;

no_response2:   
    list_destroy(auth_list, octstr_destroy_item);
    octstr_destroy(header_value);
    return 0;
}

/*
 * HTTP basic authentication challenge is defined in rfc 2617.
 */
static void challenge(HTTPClient *c, List *push_headers)
{
    Octstr *challenge;
    int http_status;

    http_header_add(push_headers, "WWW-Authenticate", 
                    "Basic realm=\"wap-push\"");
    http_status = 401;
    challenge = octstr_imm("You must show your credentials");
    http_send_reply(c, http_status, push_headers, challenge);
}

/*
 * Note that the phone number necessarily follows the international 
 * format (this is checked by our pap compiler).
 */
static int prefix_allowed(WAPPushUser *u, Octstr *number)
{
    List *allowed,
         *denied;
    long i;
    Octstr *listed_prefix,
           *sure;
    size_t sure_len;

    allowed = NULL;
    denied = NULL;

    if (u->allowed_prefix == NULL && u->denied_prefix == NULL)
        goto no_configuration;

    sure = octstr_imm("+358");
    sure_len = octstr_len(sure);
    gw_assert(octstr_ncompare(number, sure, sure_len) == 0);
    octstr_delete(number, 0, sure_len);

    if (u->denied_prefix != NULL) {
        denied = octstr_split(u->denied_prefix, octstr_imm(";"));
        for (i = 0; i < list_len(denied); ++i) {
             listed_prefix = list_get(denied, i);
             if (octstr_ncompare(number, listed_prefix,  
                     octstr_len(listed_prefix)) == 0) {
	         goto denied;
             }
        }
    }

    if (u->allowed_prefix == NULL) {
        goto no_allowed_config;
    }

    allowed = octstr_split(u->allowed_prefix, octstr_imm(";"));
    for (i = 0; i < list_len(allowed); ++i) {
         listed_prefix = list_get(denied, i);
         if (octstr_ncompare(number, listed_prefix, 
                 octstr_len(listed_prefix)) == 0) {
	     goto allowed;
         }
    }

/*
 * Here we have an intentional fall-through. It will removed when memory cleaning
 * functions are implemented.
 */
denied:         
    list_destroy(allowed, octstr_destroy_item);
    list_destroy(denied, octstr_destroy_item);
    return 0;

allowed:         
    list_destroy(allowed, octstr_destroy_item);
    list_destroy(denied, octstr_destroy_item);
    return 1;

no_configuration:
    return 1;

no_allowed_config:
    list_destroy(denied, octstr_destroy_item);
    return 1;
}

static int whitelisted(WAPPushUser *u, Octstr *number)
{
    if (u->white_list == NULL)
        return 1;

    return numhash_find_number(u->white_list, number);
}

static int blacklisted(WAPPushUser *u, Octstr *number)
{
    if (u->black_list == NULL)
        return 1;

    return numhash_find_number(u->black_list, number);
}

/*
 * Return 1 when we found password and username, 0 otherwise.
 */
static int parse_cgivars(List *cgivars, Octstr **username, Octstr **password)
{
    *username = http_cgi_variable(cgivars, "username");
    *password = http_cgi_variable(cgivars, "password");

    if (*username == NULL || *password == NULL)
        return 0;

    return 1;
}





