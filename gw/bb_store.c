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



/* growing number to be given to messages */
static int msg_id = 1;

/* approximation of store size (how many messages in it) */
static long store_size = 0;

static FILE *file = NULL;
static Octstr *filename = NULL;
static Octstr *newfile = NULL;
static Octstr *bakfile = NULL;
static Mutex *store_mutex = NULL;


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
	error(errno, "Failed to rename old store '%s' as '%s'",
	      octstr_get_cstr(filename), octstr_get_cstr(bakfile));
	return -1;
    }
    if (rename(octstr_get_cstr(newfile), octstr_get_cstr(filename)) == -1) {
	error(errno, "Failed to rename new store '%s' as '%s'",
	      octstr_get_cstr(newfile), octstr_get_cstr(filename));
	return -1;
    }
    return 0;
}


/*------------------------------------------------------*/

long store_messages(void)
{
    return store_size;
}

int store_save(Msg *msg)
{
    if (file == NULL)
	return 0;

    if (msg_type(msg) == sms) {
	msg->sms.id = msg_id;
	if (msg_id == 1000000)   /* limit to 1,000,000 distinct msg/s */
	    msg_id = 1;
	else
	    msg_id++;

	store_size++;
    } else if (msg_type(msg) != ack)
	return -1;

    mutex_lock(store_mutex);
    write_msg(msg);
    fflush(file);
    mutex_unlock(store_mutex);
    
    return 0;
}


int store_load(void)
{
    List *keys;
    Octstr *store_file, *pack, *key;
    Dict *msg_hash;
    Msg *msg, *dmsg;
    int retval, msgs;
    long end, pos;

    if (filename == NULL)
	return 0;

    mutex_lock(store_mutex);

    if (file != NULL) {
	fclose(file);
	file = NULL;
    }
    
    store_file = octstr_read_file(octstr_get_cstr(filename));
    if (store_file == NULL)
	store_file = octstr_read_file(octstr_get_cstr(newfile));
    if (store_file == NULL)
	store_file = octstr_read_file(octstr_get_cstr(bakfile));
    if (store_file == NULL) {
	info(0, "Cannot open any store file, starting new one");
	retval = open_file(filename);
	mutex_unlock(store_mutex);
	return retval;
    }

    info(0, "Store-file size %ld, starting to unpack%s", octstr_len(store_file),
	 octstr_len(store_file) > 10000 ? " (may take awhile)" : "");

    if (store_size == 0)
	msg_hash = dict_create(101, NULL);  /* XXX */
    else
	msg_hash = dict_create(store_size, NULL);
	
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
	    key = octstr_format("%d-%d", msg->sms.time, msg->sms.id);
	    dict_put(msg_hash, key, msg);
	    octstr_destroy(key);
	    msgs++;
	}
	else if (msg_type(msg) == ack) {
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

    /* now create a new store-file and save all non-acknowledged messages
     * into it
     */

    if (open_file(newfile)==-1)
	return -1;

    keys = dict_keys(msg_hash);
    while((key = list_extract_first(keys))!=NULL) {
	msg = dict_get(msg_hash, key);
	write_msg(msg);		/* write to new file */
	octstr_destroy(key);
    }
    list_destroy(keys, NULL);
    fflush(file);

    /* rename old storefile as .bak, and then new as regular file
     * without .new ending */

    retval = rename_store();
    

    /* Finally, add all non-acknowledged messages to correct queues
     * so that they can get the answer at some point
     *
     * If retval = -1 (errors in renaming), do not add messages,
     * just clean up memory
     */
    
    keys = dict_keys(msg_hash);
    while((key = list_extract_first(keys))!=NULL) {
	msg = dict_remove(msg_hash, key);
	if (msg_type(msg) == sms && retval == 0) {
	    if (msg->sms.sms_type == mo)
		list_produce(incoming_sms, msg);
	    else
		list_produce(outgoing_sms, msg);
	} else
	    msg_destroy(msg);
	octstr_destroy(key);
    }
    list_destroy(keys, NULL);

    /* destroy the hash */
    dict_destroy(msg_hash);
    
    mutex_unlock(store_mutex);
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
    store_mutex = mutex_create();

    return 0;
}


void store_shutdown(void)
{
    if (filename == NULL)
	return;
    if (file != NULL)
	fclose(file);
    octstr_destroy(filename);
    octstr_destroy(newfile);
    octstr_destroy(bakfile);
    mutex_destroy(store_mutex);
}
