/*
 * test_msg.c - test message manipulation
 * 
 * This file is a test program for the message manipulation functions in 
 * msg.h and msg.c.
 * 
 * Lars Wirzenius <liw@wapit.com> 
 */


#include "msg.h"
#include "octstr.h"
#include "wapitlib.h"

int main(void) {
	Msg *msg, *msg2;
	Octstr *os;
	
	info(0, "Creating msg.");
	msg = msg_create(heartbeat);
	msg->heartbeat.load = 42;
	msg_dump(msg);
	
	info(0, "Packing msg.");
	os = msg_pack(msg);
	octstr_dump(os);
	
	info(0, "Unpacking msg to msg2.");
	msg2 = msg_unpack(os);
	info(0, "msg2->heartbeat.load: %ld", (long) msg2->heartbeat.load);

	info(0, "Destroying msg and msg2.");
	msg_destroy(msg);
	msg_destroy(msg2);
	
	info(0, "Creating plain_sms.");
	msg = msg_create(plain_sms);
	msg->plain_sms.sender = octstr_create("123");
	msg->plain_sms.receiver = octstr_create("456");
	msg->plain_sms.text = octstr_create("hello, world");
	
	info(0, "Packing plain_sms.");
	os = msg_pack(msg);
	octstr_dump(os);
	
	info(0, "Duplicating msg.");
	msg2 = msg_duplicate(msg);
	msg_dump(msg2);
	msg_destroy(msg2);

	info(0, "Unpacking plain_sms.");
	msg2 = msg_unpack(os);
	info(0, "msg2:");
	info(0, "  sender: %s", octstr_get_cstr(msg->plain_sms.sender));
	info(0, "  receiv: %s", octstr_get_cstr(msg->plain_sms.receiver));
	info(0, "  text  : %s", octstr_get_cstr(msg->plain_sms.text));

	return 0;
}
