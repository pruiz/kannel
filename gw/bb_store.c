/*
 * bb_store.c : bearerbox box SMS storage/retrieval module
 *
 * Kalle Marjola 2001 for project Kannel
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "bearerbox.h"


/* passed from bearerbox core */

extern List *incoming_sms;
extern List *outgoing_sms;
extern List *flow_threads;


/* growing number to be given to messages */
static Counter *msg_id;

static FILE *file = NULL;
static Octstr *filename = NULL;
static Octstr *newfile = NULL;
static Octstr *bakfile = NULL;
static Mutex *file_mutex = NULL;
static long cleanup_thread;


static List *sms_store;
static List *ack_store;



static void write_msg(Msg *msg)
{
    Octstr *pack, *line;

    pack = msg_pack(msg);
    line = octstr_duplicate(pack);
    octstr_url_encode(line);

    octstr_print(file, line);
    fprintf(file, "\n");

    octstr_destroy(pack);
    octstr_destroy(line);
    
}    

static int open_file(Octstr *name)
{
    file = fopen(octstr_get_cstr(name), "w");
    if (file == NULL) {
	error(errno, "Failed to open '%s' for writing, cannot create store-file",
	      octstr_get_cstr(name));
	return -1;
    }
    return 0;
}

static int rename_store(void)
{
    if (rename(octstr_get_cstr(filename), octstr_get_cstr(bakfile)) == -1) {
	if (errno != ENOENT) {
	    error(errno, "Failed to rename old store '%s' as '%s'",
	    octstr_get_cstr(filename), octstr_get_cstr(bakfile));
	    return -1;
	}
    }
    if (rename(octstr_get_cstr(newfile), octstr_get_cstr(filename)) == -1) {
	error(errno, "Failed to rename new store '%s' as '%s'",
	      octstr_get_cstr(newfile), octstr_get_cstr(filename));
	return -1;
    }
    return 0;
}


static int do_dump(void)
{
    Msg *msg;
    long l;

    if (filename == NULL)
	return 0;

    /* create a new store-file and save all non-acknowledged
     * messages into it
     */
    if (open_file(newfile)==-1)
	return -1;

    for (l=0; l < list_len(sms_store); l++) {
	msg = list_get(sms_store, l);
	write_msg(msg);
    }    
    for (l=0; l < list_len(ack_store); l++) {
	msg = list_get(ack_store, l);
	write_msg(msg);
    }    
    fflush(file);

    /* rename old storefile as .bak, and then new as regular file
     * without .new ending */

    return rename_store();
}


static int cmp_msgs(void *item, void *pattern) {
    Msg *smsm, *ackm;

    ackm = pattern;
    smsm = item;

    if (   ackm->ack.time == smsm->sms.time
	&& ackm->ack.id == smsm->sms.id)
	return 1;
    else
	return 0;
}


/*
 * thread to cleanup store and to write it to file now and then
 */
static void store_cleanup(void *arg)
{
    Msg *ack;
    List *match;
    time_t last, now;
    long len;
    int cleanup = 0;

    list_add_producer(flow_threads);
    last = time(NULL);
    
    while((ack = list_consume(ack_store)) != NULL) {

	match = list_extract_matching(sms_store, ack, cmp_msgs);
	msg_destroy(ack);

	if (match == NULL) {
	    warning(0, "bb_store: get ACK of message not found "
		    "from store, strange?");
	    continue;
	}
	if (list_len(match) > 1)
	    warning(0, "bb-store cleanup: Found %ld matches!?",
		    list_len(match));
	list_destroy(match, msg_destroy_item);

	len = list_len(ack_store);
	if (len > 100)
	    cleanup = 1;
	now = time(NULL);
	/*
	 * write store to file up to each 10. second, providing
	 * that something happened, of course
	 */
	if (now - last > 10 || (len == 0 && cleanup)) {
	    store_dump();
	    last = now;
	    if (len == 0)
		cleanup = 0;
	}
    }
    store_dump();
    if (file != NULL)
	fclose(file);
    octstr_destroy(filename);
    octstr_destroy(newfile);
    octstr_destroy(bakfile);
    mutex_destroy(file_mutex);
    counter_destroy(msg_id);
    
    list_destroy(ack_store, msg_destroy_item);
    list_destroy(sms_store, msg_destroy_item);

    list_remove_producer(flow_threads);
}



/*------------------------------------------------------*/

Octstr *store_status(int status_type)
{
    char *frmt;
    char buf[1024], p[22];
    Octstr *ret, *str, *t;
    unsigned long l;
    struct tm tm;
    Msg *msg;

    ret = octstr_create("");

    /* set the type based header */
    if (status_type == BBSTATUS_HTML) {
        octstr_append_cstr(ret, "<table border=1>\n"
            "<tr><td>SMS ID</td><td>Sender</td><td>Receiver</td>"
            "<td>SMSC ID</td><td>UDH</td><td>Message</td>"
            "<td>Time</td></tr>\n");
    } else if (status_type == BBSTATUS_TEXT) {
        octstr_append_cstr(ret, "[SMS ID] [Sender] [Receiver] [SMSC ID] [UDH] [Message] [Time]\n");
    }
   
    for (l = 0; l < list_len(sms_store); l++) {
        msg = list_get(sms_store, l);

        if (msg_type(msg) == sms) {
            
            if (status_type == BBSTATUS_HTML) {
                frmt = "<tr><td>%d</td><td>%s</td><td>%s</td><td>%s</td>"
                       "<td>%s</td><td>%s</td><td>%s</td></tr>\n";
            } else if (status_type == BBSTATUS_XML) {
                frmt = "<message>\n\t<id>%d</id>\n\t<sender>%s</sender>\n\t"
                       "<receiver>%s</receiver>\n\t<smsc-id>%s</smsc-id>\n\t"
                       "<udh-data>%s</udh-data>\n\t<msg-data>%s</msg-data>\n\t"
                       "<time>%s</time>\n</message>\n";
            } else {
                frmt = "[%d] [%s] [%s] [%s] [%s] [%s] [%s]\n";
            }

            /* transform the time value */
            tm = gw_gmtime(msg->sms.time);
            sprintf(p, "%04d-%02d-%02d %02d:%02d:%02d",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec);
            t = octstr_create(p);

            sprintf(buf, frmt,
                msg->sms.id,
                octstr_get_cstr(msg->sms.sender),
                octstr_get_cstr(msg->sms.receiver),
                octstr_get_cstr(msg->sms.smsc_id),
                octstr_get_cstr(msg->sms.udhdata),
                octstr_get_cstr(msg->sms.msgdata),
                octstr_get_cstr(t));
            octstr_destroy(t);
            str = octstr_create(buf);
            octstr_append(ret, str);
        }
    }

    /* set the type based footer */
    if (status_type == BBSTATUS_HTML) {
        octstr_append_cstr(ret,"</table>");
    }

    return ret;
}


long store_messages(void)
{
    return list_len(sms_store);
}


int store_save(Msg *msg)
{
    Msg *copy;
    
    if (file == NULL)
	return 0;

    if (msg_type(msg) == sms) {
	msg->sms.id = counter_increase(msg_id);
	if (counter_value(msg_id) >= 1000000)   /* limit to 1,000,000
						 * distinct msg/s */
	    counter_set(msg_id, 1);

	copy = msg_duplicate(msg);
	list_produce(sms_store, copy);
    }
    else if (msg_type(msg) == ack) {
	copy = msg_duplicate(msg);
	list_produce(ack_store, copy);
    }
    else
	return -1;

    /* write to file, too */
    mutex_lock(file_mutex);
    write_msg(msg);
    fflush(file);
    mutex_unlock(file_mutex);

    return 0;
}



int store_load(void)
{
    List *keys;
    Octstr *store_file, *pack, *key;
    Dict *msg_hash;
    Msg *msg, *dmsg, *copy;
    int retval, msgs;
    long end, pos;
    long store_size;
    
    if (filename == NULL)
	return 0;

    list_lock(sms_store);
    list_lock(ack_store);

    while((msg = list_extract_first(sms_store))!=NULL)
	msg_destroy(msg);

    while((msg = list_extract_first(ack_store))!=NULL)
	msg_destroy(msg);
	
    mutex_lock(file_mutex);
    if (file != NULL) {
	fclose(file);
	file = NULL;
    }
    
    store_file = octstr_read_file(octstr_get_cstr(filename));
    if (store_file != NULL)
	info(0, "Loading store file %s", octstr_get_cstr(filename));
    else {
	store_file = octstr_read_file(octstr_get_cstr(newfile));
	if (store_file != NULL)
	    info(0, "Loading store file %s", octstr_get_cstr(newfile));
	else {
	    store_file = octstr_read_file(octstr_get_cstr(bakfile));
	    if (store_file != NULL)
		info(0, "Loading store file %s", octstr_get_cstr(bakfile));
	    else {
		info(0, "Cannot open any store file, starting new one");
		retval = open_file(filename);
		list_unlock(sms_store);
		list_unlock(ack_store);
		mutex_unlock(file_mutex);
		return retval;
	    }
	}
    }

    info(0, "Store-file size %ld, starting to unpack%s", octstr_len(store_file),
	 octstr_len(store_file) > 10000 ? " (may take awhile)" : "");

    msg_hash = dict_create(101, NULL);  /* XXX should be different? */
	
    pos = 0;
    msgs = 0;
    
    while ((end = octstr_search(store_file, octstr_imm("\n"), pos)) != -1) {

	pack = octstr_copy(store_file, pos, end-pos);
	pos = end+1;
	
	if (octstr_url_decode(pack) == -1) {
	    debug("bb.store", 0, "Garbage at store-file, skipped");
	    continue;
	}

	msg = msg_unpack(pack);
	if (msg == NULL) 
	    continue;

	if (msg_type(msg) == sms) {
	    if (msg->sms.sms_type == report) {
		octstr_destroy(pack);
		continue;
	    }
	    key = octstr_format("%d-%d", msg->sms.time, msg->sms.id);
	    dict_put(msg_hash, key, msg);
	    octstr_destroy(key);
	    msgs++;
	}
	else if (msg_type(msg) == ack) {
	    if (msg->sms.sms_type == report) {
		octstr_destroy(pack);
		continue;
	    }
	    key = octstr_format("%d-%d", msg->ack.time, msg->ack.id);
	    dmsg = dict_remove(msg_hash, key);
	    if (dmsg != NULL) 
		msg_destroy(dmsg);
	    else
		info(0, "Acknowledge of non-existant message found '%s', "
		   "discarded", octstr_get_cstr(key));
	    msg_destroy(msg);
	    octstr_destroy(key);
	} else {
	    warning(0, "Strange message in store-file, discarded, "
		    "dump follows:");
	    msg_dump(msg, 0);
	    msg_destroy(msg);
	}
	octstr_destroy(pack);
    }
    octstr_destroy(store_file);

    store_size = dict_key_count(msg_hash);
    info(0, "Retrieved %d messages, non-acknowledged messages: %ld",
	 msgs, store_size);

    /* now create a new sms_store out of messages left
     */

    keys = dict_keys(msg_hash);
    while((key = list_extract_first(keys))!=NULL) {
	msg = dict_get(msg_hash, key);

	if (msg_type(msg) != sms) {
	    msg_destroy(msg);
	    octstr_destroy(key);
	    continue;
	}
	copy = msg_duplicate(msg);
	list_produce(sms_store, copy);

	if (msg->sms.sms_type == mo)
	    list_produce(incoming_sms, msg);
	if (msg->sms.sms_type == mt_push ||
	    msg->sms.sms_type == mt_reply)
	    list_produce(outgoing_sms, msg);
	
	octstr_destroy(key);
    }
    list_destroy(keys, NULL);

    /* Finally, generate new store file out of left messages
     */
    retval = do_dump();
    
    mutex_unlock(file_mutex);

    /* destroy the hash */
    dict_destroy(msg_hash);

    list_unlock(sms_store);
    list_unlock(ack_store);

    return retval;
}



int store_dump(void)
{
    int retval;

    list_lock(sms_store);
    list_lock(ack_store);
    debug("bb.store", 0, "Dumping %ld messages and %ld acks to store",
	  list_len(sms_store), list_len(ack_store));
    mutex_lock(file_mutex);
    if (file != NULL) {
	fclose(file);
	file = NULL;
    }
    retval = do_dump();
    mutex_unlock(file_mutex);
    list_unlock(ack_store);
    list_unlock(sms_store);

    return retval;
}


int store_init(Octstr *fname)
{
    if (octstr_len(fname) > (FILENAME_MAX-5)) {
        error(0, "Store file filename too long: `%s', failed to init.",
	      octstr_get_cstr(fname));
	return -1;
    }
    filename = octstr_duplicate(fname);
    newfile = octstr_format("%s.new", octstr_get_cstr(filename));
    bakfile = octstr_format("%s.bak", octstr_get_cstr(filename));

    msg_id = counter_create();
    counter_set(msg_id, 1);
    sms_store = list_create();
    ack_store = list_create();

    file_mutex = mutex_create();
    list_add_producer(ack_store);
    
    if ((cleanup_thread = gwthread_create(store_cleanup, NULL))==-1)
	panic(0, "Failed to create a cleanup thread!");

    return 0;
}


void store_shutdown(void)
{
    if (filename == NULL)
	return;

    list_remove_producer(ack_store);
}
