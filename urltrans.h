/*
 * urltrans.h - URL translations
 *
 * The SMS gateway receives service requests sent as SMS messages and uses
 * a web server to actually perform the requests. The first word of the
 * SMS message usually specifies the service, and for each service there is
 * a URL that specifies the web page or cgi-bin that performs the service. 
 * Thus, in effect, the gateway `translates' SMS messages to URLs.
 *
 * urltrans.h and urltrans.c implement a data structure for holding a list
 * of translations and formatting a SMS request into a URL. It is used as
 * follows:
 *
 * 1. Create a URLTranslation object with urltrans_create.
 * 2. Add translations into it with urltrans_add_one or urltrans_add_cfg.
 * 3. Receive SMS messages, and translate them into URLs with urltrans_get_url.
 * 4. When you are done, free the object with urltrans_destroy.
 *
 * See below for more detailed instructions for using the functions.
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef URLTRANS_H
#define URLTRANS_H

#include "config.h"
#include "sms_msg.h"

/*
 * This is the data structure that holds the list of translations. It is
 * opaque and is defined in and usable only within urltrans.c.
 */
typedef struct URLTranslationList URLTranslationList;


/*
 * This is the data structure that holds one translation. It is also
 * opaque, and is accessed via some of the functions below.
 */
typedef struct URLTranslation URLTranslation;


/*
 * Create a new URLTranslationList object. Return NULL if the creation failed,
 * or a pointer to the object if it succeded.
 *
 * The object is empty: it contains no translations.
 */
URLTranslationList *urltrans_create(void);


/*
 * Destroy a URLTranslationList object.
 */
void urltrans_destroy(URLTranslationList *list);


/*
 * Add a translation to the object. The `keyword' is the first word of
 * the SMS message, `url' is the URL pattern it should use. See 
 * urltrans_get_url below for a description of the patterns. The keyword
 * and pattern strings are copied, so the caller does not have to keep
 * them around.
 *
 * There can be several patterns for the same keyword, but with different
 * patterns. urltrans_get_url will pick the pattern that best matches the
 * actual SMS message. (See urltrans_get_url for a description of the
 * algorithm.)
 *
 * There can only be one pattern with keyword "default", however.
 *
 * `max_messages' defines the maximum number of SMS messages that can
 * be generated from one URL. If 'split_chars' is set, these are used for
 * sms message splitting. They have no effect if max_messages is 1 or less
 * or if the entire reply fits in one sms message
 *
 * 'imit_empty', if set, prevents sending of '<empty reply from server>'
 * text.
 *
 * 'faked_sender' is number that is sent back
 * to SMSC, masking the system. This works only in EMI systems
 *
 * Return -1 for error, or 0 for OK.
 */
int urltrans_add_one(URLTranslationList *trans, char *keyword, char *url,
	char *prefix, char *suffix, int max_messages, int omit_empty,
	char *faked_sender, char *split_chars);


/*
 * Add translations to a URLTranslation object from a Config object
 * (see config.h). Translations are added from groups in `cfg' that
 * contain variables called "keyword" and "url". For each such group,
 * urltrans_add_one is called.
 *
 * Return -1 for error, 0 for OK. If -1 is returned, the URLTranslation
 * object may have been partially modified.
 */
int urltrans_add_cfg(URLTranslationList *trans, Config *cfg);


/*
 * Find the translation that corresponds to a given SMS request.
 */
URLTranslation *urltrans_find(URLTranslationList *trans, SMSMessage *sms);


/*
 * Return a URL given contents of an SMS message. Find the appropriate
 * translation pattern and fill in the missing parts from the contents of
 * the SMS message.
 *
 * The contents of the SMS message is delivered in `words', which is an
 * array of strings, each string being a word of the message. That is,
 * the caller needs to break up the message into words (using, say,
 * split_words from wapitlib.h). `n' is the number of words.
 *
 * `orig' is the SMS message that is being translated.
 *
 * Use the pattern whose keyword is the same as the first word of the SMS
 * message and that has the number of `%s' fields as the SMS message has
 * words after the first one. If no such pattern exists, use the pattern
 * whose keyword is "default". If there is no such pattern, either, return
 * NULL.
 *
 * Return NULL if there is a failure. Otherwise, return a pointer to the URL,
 * which is stored in dynamically allocated memory that the caller should
 * free when the URL is no longer needed.
 */
char *urltrans_get_url(URLTranslation *t, SMSMessage *sms);


/*
 * Return prefix and suffix of translations, if they have been set.
 */
char *urltrans_prefix(URLTranslation *t);
char *urltrans_suffix(URLTranslation *t);


/*
 * Return maximum number of SMS messages that should be generated from
 * the web page directed by the URL translation.
 */
int urltrans_max_messages(URLTranslation *t);

/*
 * Return if set that should not send 'empty reply' messages
 */
int urltrans_omit_empty(URLTranslation *t);

/*
 * Return (a recommended) faked sender number, or NULL if not set.
 */
char *urltrans_faked_sender(URLTranslation *t);

/*
 * Return (recommended) delimiter characters when splitting long
 * replies into several messages
 */
char *urltrans_split_chars(URLTranslation *t);


#endif
