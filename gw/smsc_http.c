/*
 * smsc_http.c - interface to various HTTP based content/SMS gateways
 *
 * HTTP based "SMSC Connection" is meant for gateway connections,
 * and has following features:
 *
 * o Kannel listens to certain (HHTP server) port for MO SMS messages.
 *   The exact format of these HTTP calls are defined by type of HTTP based
 *   connection. Kannel replies to these messages as ACK, but does not
 *   support immediate reply. Thus, if Kannel is linked to another Kannel,
 *   only 'max-messages = 0' services are practically supported - any
 *   replies must be done with SMS PUSH (sendsms)
 *
 * o For MT messages, Kannel does HTTP GET or POST to given address, in format
 *   defined by type of HTTP based protocol
 *
 *
 * The 'type' of requests and replies are defined by 'system-type' variable.
 * The only type of HTTP requests currently supported are basic Kannel.
 * If new support is added, smsc_http_create is modified accordingly and new
 * functions added.
 *
 *
 *
 * KANNEL->KANNEL linking: (UDH not supported in MO messages)
 *
 *****
 * FOR CLIENT/END-POINT KANNEL:
 *
 *  group = smsc
 *  smsc = http
 *  system-type = kannel
 *  port = NNN
 *  smsc-username = XXX
 *  smsc-password = YYY
 *  send-url = "server.host:PORT"
 *
 *****
 * FOR SERVER/RELAY KANNEL:
 *
 *  group = smsbox
 *  sendsms-port = PORT
 *  ...
 * 
 *  group = sms-service
 *  keyword = ...
 *  url = "client.host:NNN/sms?user=XXX&pass=YYY&from=%p&to=%P&text=%a"
 *  max-messages = 0
 *
 *  group = send-sms
 *  username = XXX
 *  password = YYY
 *  
 *
 *
 * Kalle Marjola for Project Kannel 2001
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

#include "gwlib/gwlib.h"
#include "smscconn.h"
#include "smscconn_p.h"
#include "bb_smscconn_cb.h"
#include "msg.h"


typedef struct conndata {
    HTTPCaller  *http_ref;
    long	receive_thread;
    long	send_cb_thread;
    int		shutdown;
    int		port;	      	/* port for receiving SMS'es */
    Octstr	*allow_ip;
    Octstr	*send_url;
    long	open_sends;
    Octstr	*username;	/* if needed */
    Octstr	*password;	/* as said */

    /* callback functions set by HTTP-SMSC type */

    void (*send_sms) (SMSCConn *conn, Msg *msg);
    void (*parse_reply) (SMSCConn *conn, Msg *msg, int status,
			 List *headers, Octstr *body);
    void (*receive_sms) (SMSCConn *conn, HTTPClient *client,
			 List *headers, Octstr *body, List *cgivars);
} ConnData;


static void conndata_destroy(ConnData *conndata)
{
    if (conndata == NULL)
	return;
    if (conndata->http_ref)
	http_caller_destroy(conndata->http_ref);
    octstr_destroy(conndata->allow_ip);
    octstr_destroy(conndata->send_url);
    octstr_destroy(conndata->username);
    octstr_destroy(conndata->password);

    gw_free(conndata);
}


/*
 * thread to listen to HTTP requests from other end
 */
static void httpsmsc_receiver(void *arg)
{
    SMSCConn *conn = arg;
    ConnData *conndata = conn->data;
    HTTPClient *client;
    Octstr *ip, *url, *body;
    List *headers, *cgivars;
    
    while(conndata->shutdown == 0) {

	/* XXX if conn->is_stopped, do not receive new messages.. */
	
	client = http_accept_request(conndata->port, &ip, &url,
				     &headers, &body, &cgivars);
	if (client == NULL)
	    break;

	debug("smsc.http", 0, "Got request '%s'", octstr_get_cstr(url));

	if (connect_denied(conndata->allow_ip, ip)) {
	    info(0, "httpsmsc: connection '%s' tried from denied "
		 "host %s, ignored", octstr_get_cstr(url),
		 octstr_get_cstr(ip));
	    http_close_client(client);
	} else
	    conndata->receive_sms(conn, client, headers, body, cgivars);

	debug("smsc.http", 0, "destroying client information");
	octstr_destroy(url);
	octstr_destroy(ip);
	octstr_destroy(body);
	http_destroy_headers(headers);
	http_destroy_cgiargs(cgivars);
    }
    debug("http_smsc", 0, "httpsmsc_receiver dying");

    conndata->shutdown = 1;
    http_close_port(conndata->port);
    http_caller_signal_shutdown(conndata->http_ref);
}


/*
 *   thread to handle finished sendings
 */
static void httpsmsc_send_cb(void *arg)
{
    SMSCConn *conn = arg;
    ConnData *conndata = conn->data;
    Msg *msg;
    int status;
    List *headers;
    Octstr *final_url, *body;

    while(conndata->shutdown == 0 || conndata->open_sends) {

	msg = http_receive_result(conndata->http_ref, &status,
				 &final_url, &headers, &body);
	if (msg == NULL)
	    break;	/* they told us to die */

	conndata->open_sends--;

	conndata->parse_reply(conn, msg, status, headers, body);

	http_destroy_headers(headers);
	octstr_destroy(final_url);
	octstr_destroy(body);
    }
    debug("http-smsc", 0, "httpsmsc_send_cb dying");
    conndata->shutdown = 1;

    gwthread_join(conndata->receive_thread);

    conn->data = NULL;
    conndata_destroy(conndata);

    conn->status = SMSCCONN_DEAD;
    bb_smscconn_killed();
}


/*----------------------------------------------------------------
 * SMSC-type specific functions
 *
 * 3 functions are needed for each:
 *
 *   1) send SMS
 *   2) parse send SMS result
 *   3) receive SMS (and send reply)
 *
 *   These functions do not return anything and do not destroy
 *   arguments. They must handle everything that happens therein
 *   and must call appropriate bb_smscconn functions
 */

/*xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
 * Kannel
 */

static void kannel_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url;
    List *headers;

    url = octstr_format("%S/cgi-bin/sendsms?"
			"user=%E&pass=%E&to=%E&from=%E&text=%E",
			conndata->send_url,
			conndata->username, conndata->password,
			sms->sms.receiver, sms->sms.sender,
			sms->sms.msgdata);

    if (sms->sms.flag_udh && sms->sms.udhdata)
	octstr_format_append(url, "&udh=%E", sms->sms.udhdata);
    
    if (sms->sms.flag_flash)
	octstr_format_append(url, "&flash=%d", sms->sms.flag_flash);
  
    headers = list_create();
    debug("smsc.http.kannel", 0, "start request");
    http_start_request(conndata->http_ref, url, headers, NULL, 0, sms, NULL);

    octstr_destroy(url);
    http_destroy_headers(headers);
    
}

static void kannel_parse_reply(SMSCConn *conn, Msg *msg, int status,
			       List *headers, Octstr *body)
{
    if (status == HTTP_OK && octstr_case_compare(body, octstr_imm("Sent."))==0)
	bb_smscconn_sent(conn, msg);
    else
	bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_MALFORMED);
}

static void kannel_receive_sms(SMSCConn *conn, HTTPClient *client,
			       List *headers, Octstr *body, List *cgivars)
{
    ConnData *conndata = conn->data;
    Octstr *user, *pass, *from, *to, *text, *udh, *flash_string;
    Octstr *retmsg;
    int	flash;
    List *reply_headers;
    int ret;

    user = http_cgi_variable(cgivars, "user");
    pass = http_cgi_variable(cgivars, "pass");
    from = http_cgi_variable(cgivars, "from");
    to = http_cgi_variable(cgivars, "to");
    text = http_cgi_variable(cgivars, "text");
    udh = http_cgi_variable(cgivars, "udh");
    flash_string = http_cgi_variable(cgivars, "flash");
    if(flash_string) {
	sscanf(octstr_get_cstr(flash_string),"%d",&flash);
	octstr_destroy(flash_string);
    }
    debug("smsc.http.kannel", 0, "Received an HTTP request");
    
    if (   user == NULL || pass == NULL
	   || octstr_compare(user, conndata->username)!= 0
	   || octstr_compare(pass, conndata->password)!= 0) {

	debug("smsc.http.kannel", 0, "Authorization failure");
	retmsg = octstr_create("Authorization failed for sendsms");
    }
    else if (from == NULL || to == NULL || text == NULL) {
	
	debug("smsc.http.kannel", 0, "Insufficient args");
	retmsg = octstr_create("Insufficient args, rejected");
    }
    else {
	Msg *msg;
	msg = msg_create(sms);

	debug("smsc.http.kannel", 0, "Constructing new SMS");
	
	msg->sms.sender = octstr_duplicate(from);
	msg->sms.receiver = octstr_duplicate(to);
	msg->sms.msgdata = octstr_duplicate(text);
	msg->sms.udhdata = octstr_duplicate(udh);
	if (udh)
	    msg->sms.flag_8bit = msg->sms.flag_udh = 1;
	else
	    msg->sms.flag_8bit = msg->sms.flag_udh = 0;

	msg->sms.smsc_id = octstr_duplicate(conn->id);
	msg->sms.time = time(NULL);
	msg->sms.flag_flash = flash;

	ret = bb_smscconn_receive(conn, msg);
	if (ret == -1)
	    retmsg = octstr_create("Not accepted");
	else
	    retmsg = octstr_create("Ok.");
    }
    reply_headers = list_create();
    http_header_add(reply_headers, "Content-Type", "text/plain");
    debug("smsc.http.kannel", 0, "sending reply");
    http_send_reply(client, HTTP_OK, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}


/*xxxxxxxxxxxxxxxxxxxxxxx
 *
 * ADD NEW CONTENT GATEWAY/HTTP SMSC CALLBACKS HERE
 */




/*-----------------------------------------------------------------
 * functions to implement various smscconn operations
 */

static int httpsmsc_send(SMSCConn *conn, Msg *msg)
{
    ConnData *conndata = conn->data;
    Msg *sms = msg_duplicate(msg);

    conndata->open_sends++;
    conndata->send_sms(conn, sms);

    return 0;
}


static long httpsmsc_queued(SMSCConn *conn)
{
    ConnData *conndata = conn->data;
    return conndata->open_sends;
}


static int httpsmsc_shutdown(SMSCConn *conn, int finish_sending)
{
    ConnData *conndata = conn->data;

    debug("httpsmsc_shutdown", 0, "httpsmsc: shutting down");
    conn->why_killed = SMSCCONN_KILLED_SHUTDOWN;
    conndata->shutdown = 1;

    http_close_port(conndata->port);
    return 0;
}


int smsc_http_create(SMSCConn *conn, CfgGroup *cfg)
{
    ConnData *conndata = NULL;
    Octstr *type;
    long portno;   /* has to be long because of cfg_get_integer */

    if (cfg_get_integer(&portno, cfg, octstr_imm("port")) == -1) {
	error(0, "'port' invalid in smsc 'http' record.");
	return -1;
    }
    if ((type = cfg_get(cfg, octstr_imm("system-type")))==NULL) {
	error(0, "'type' missing in smsc 'http' record.");
	octstr_destroy(type);
	return -1;
    }
    conndata = gw_malloc(sizeof(ConnData));
    conndata->http_ref = NULL;

    conndata->allow_ip = cfg_get(cfg, octstr_imm("connect-allow-ip"));
    conndata->send_url = cfg_get(cfg, octstr_imm("send-url"));
    conndata->username = cfg_get(cfg, octstr_imm("smsc-username"));
    conndata->password = cfg_get(cfg, octstr_imm("smsc-password"));

    if (conndata->send_url == NULL)
	panic(0, "Sending not allowed");

    if (octstr_case_compare(type, octstr_imm("kannel"))==0) {
	if (conndata->username == NULL || conndata->password == NULL) {
	    error(0, "username and password required for Kannel http smsc");
	    goto error;
	}
	conndata->receive_sms = kannel_receive_sms;
	conndata->send_sms = kannel_send_sms;
	conndata->parse_reply = kannel_parse_reply;
    }
    /*
     * ADD NEW HTTP SMSC TYPES HERE
     */
    else {
	error(0, "system-type '%s' unknown smsc 'http' record.",
	      octstr_get_cstr(type));

	goto error;
    }	
    conndata->open_sends = 0;
    conndata->http_ref = http_caller_create();
    
    conn->data = conndata;
    conn->name = octstr_format("HTTP:%S", type);
    conn->status = SMSCCONN_ACTIVE;
    conn->connect_time = time(NULL);

    conn->shutdown = httpsmsc_shutdown;
    conn->queued = httpsmsc_queued;
    conn->send_msg = httpsmsc_send;

    if (http_open_port(portno)==-1)
	goto error;

    conndata->port = portno;
    conndata->shutdown = 0;
    
    if ((conndata->receive_thread =
	 gwthread_create(httpsmsc_receiver, conn)) == -1)
	goto error;

    if ((conndata->send_cb_thread =
	 gwthread_create(httpsmsc_send_cb, conn)) == -1)
	goto error;

    info(0, "httpsmsc '%s' initiated and ready", octstr_get_cstr(conn->name));
    
    octstr_destroy(type);
    return 0;

error:
    error(0, "Failed to create http smsc connection");

    conn->data = NULL;
    conndata_destroy(conndata);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    octstr_destroy(type);
    return -1;
}



