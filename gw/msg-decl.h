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
        OCTSTR(boxc_id);
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
		OCTSTR(account);
		INTEGER(id);
		INTEGER(sms_type);
		INTEGER(mclass);
		INTEGER(mwi);
		INTEGER(coding);
		INTEGER(compress);
		INTEGER(validity);
		INTEGER(deferred);
		INTEGER(dlr_mask);
		OCTSTR(dlr_url);
		INTEGER(pid);
		INTEGER(alt_dcs);
		INTEGER(rpi);
		OCTSTR(charset);
		OCTSTR(boxc_id);
		OCTSTR(binfo);
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
