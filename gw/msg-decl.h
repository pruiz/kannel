/*
 * msg-decl.h - message declarations
 *
 * This file contains declarations of the message types. See the
 * architecture document to see how these should be interpreted and
 * modified.
 *
 * This file is included by a number of other files.
 *
 * Lars Wirzenius
 */

MSG(heartbeat,
	{
		INTEGER(load);
	})

MSG(admin,
        {
	        INTEGER(command);
	})
    
MSG(sms,
	{
		OCTSTR(sender);
		OCTSTR(receiver);
		INTEGER(flag_8bit);
		INTEGER(flag_udh);
		OCTSTR(udhdata);
		OCTSTR(msgdata);
		INTEGER(time);
		OCTSTR(smsc_id);
		INTEGER(id);
		INTEGER(sms_type);
		INTEGER(flag_flash);
		INTEGER(flag_mwi);
		INTEGER(mwimessages);
		INTEGER(flag_unicode);
		INTEGER(validity);
		INTEGER(deferred);
	})

MSG(ack,
	{
		INTEGER(time);
		INTEGER(id);
	})
    
MSG(wdp_datagram,
	{
		OCTSTR(source_address);
		INTEGER(source_port);
		OCTSTR(destination_address);
		INTEGER(destination_port);
		OCTSTR(user_data);
	})

#undef MSG
#undef INTEGER
#undef OCTSTR
