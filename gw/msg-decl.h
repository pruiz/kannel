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
		OCTSTR(udhdata);
		OCTSTR(msgdata);
		INTEGER(time);
		OCTSTR(smsc_id);
		OCTSTR(service);
		INTEGER(id);
		INTEGER(sms_type);
		INTEGER(class);
		INTEGER(mwi);
		INTEGER(coding);
		INTEGER(compress);
		INTEGER(validity);
		INTEGER(deferred);
		OCTSTR(dlr_id);
		OCTSTR(dlr_keyword);
		INTEGER(dlr_mask);
	})

MSG(ack,
	{
		INTEGER(nack);
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
