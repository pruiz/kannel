/*
 * smsc_http.c - interface to various HTTP based content/SMS gateways
 *
 * HTTP based "SMSC Connection" is meant for gateway connections,
 * and has following features:
 *
 * o Kannel listens to certain (HTTP server) port for MO SMS messages.
 *   The exact format of these HTTP calls are defined by type of HTTP based
 *   connection. Kannel replies to these messages as ACK, but does not
 *   support immediate reply. Thus, if Kannel is linked to another Kannel,
 *   only 'max-messages = 0' services are practically supported - any
 *   replies must be done with SMS PUSH (sendsms)
 *
 * o For MT messages, Kannel does HTTP GET or POST to given address, in format
 *   defined by type of HTTP based protocol
 *
 * The 'type' of requests and replies are defined by 'system-type' variable.
 * The only type of HTTP requests currently supported are basic Kannel.
 * If new support is added, smsc_http_create is modified accordingly and new
 * functions added.
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
 * Kalle Marjola for Project Kannel 2001
 * Stipe Tolj <tolj@wapme-systems.de>
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
    HTTPCaller *http_ref;
    long receive_thread;
    long send_cb_thread;
    int shutdown;
    int	port;   /* port for receiving SMS'es */
    Octstr *allow_ip;
    Octstr *send_url;
    long open_sends;
    Octstr *username;   /* if needed */
    Octstr *password;   /* as said */
    int no_sender;      /* ditto */
    int no_coding;      /* this, too */
    int no_sep;         /* not to mention this */

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
 * Thread to listen to HTTP requests from SMSC entity
 */
static void httpsmsc_receiver(void *arg)
{
    SMSCConn *conn = arg;
    ConnData *conndata = conn->data;
    HTTPClient *client;
    Octstr *ip, *url, *body;
    List *headers, *cgivars;
   
    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);
 
    while (conndata->shutdown == 0) {

        /* XXX if conn->is_stopped, do not receive new messages.. */
	
        client = http_accept_request(conndata->port, &ip, &url,
                                     &headers, &body, &cgivars);
        if (client == NULL)
            break;

        debug("smsc.http", 0, "HTTP[%s]: Got request `%s'", 
              octstr_get_cstr(conn->id), octstr_get_cstr(url));

        if (connect_denied(conndata->allow_ip, ip)) {
            info(0, "HTTP[%s]: Connection `%s' tried from denied "
                    "host %s, ignored", octstr_get_cstr(conn->id),
                    octstr_get_cstr(url), octstr_get_cstr(ip));
            http_close_client(client);
        } else
            conndata->receive_sms(conn, client, headers, body, cgivars);

        debug("smsc.http", 0, "HTTP[%s]: Destroying client information",
              octstr_get_cstr(conn->id));
        octstr_destroy(url);
        octstr_destroy(ip);
        octstr_destroy(body);
        http_destroy_headers(headers);
        http_destroy_cgiargs(cgivars);
    }
    debug("smsc.http", 0, "HTTP[%s]: httpsmsc_receiver dying",
          octstr_get_cstr(conn->id));

    conndata->shutdown = 1;
    http_close_port(conndata->port);
    
    /* unblock http_receive_result() if there are no open sends */
    if (conndata->open_sends == 0)
        http_caller_signal_shutdown(conndata->http_ref);
}


/*
 * Thread to handle finished sendings
 */
static void httpsmsc_send_cb(void *arg)
{
    SMSCConn *conn = arg;
    ConnData *conndata = conn->data;
    Msg *msg;
    int status;
    List *headers;
    Octstr *final_url, *body;

    /* Make sure we log into our own log-file if defined */
    log_thread_to(conn->log_idx);

    while (conndata->shutdown == 0 || conndata->open_sends) {

        msg = http_receive_result(conndata->http_ref, &status,
                                  &final_url, &headers, &body);

        if (msg == NULL)
            break;  /* they told us to die, by unlocking */

        /* Handle various states here. */

        /* request failed and we are not in shutdown mode */
        if (status == -1 && conndata->shutdown == 0) { 
            error(0, "HTTP[%s]: Couldn't connect to SMS center "
                     "(retrying in %ld seconds).",
                     octstr_get_cstr(conn->id), conn->reconnect_delay);
            conn->status = SMSCCONN_RECONNECTING; 
            gwthread_sleep(conn->reconnect_delay);
            debug("smsc.http.kannel", 0, "HTTP[%s]: Re-sending request",
                  octstr_get_cstr(conn->id));
            conndata->send_sms(conn, msg);
            continue; 
        } 
        /* request failed and we *are* in shutdown mode, drop the message */ 
        else if (status == -1 && conndata->shutdown == 1) {
        }
        /* request succeeded */    
        else {
            /* we received a response, so this link is considered online again */
            if (status && conn->status != SMSCCONN_ACTIVE) {
                conn->status = SMSCCONN_ACTIVE;
            }
            conndata->parse_reply(conn, msg, status, headers, body);
        }
   
        conndata->open_sends--;

        http_destroy_headers(headers);
        octstr_destroy(final_url);
        octstr_destroy(body);
    }
    debug("smsc.http", 0, "HTTP[%s]: httpsmsc_send_cb dying",
          octstr_get_cstr(conn->id));
    conndata->shutdown = 1;

    if (conndata->open_sends) {
        warning(0, "HTTP[%s]: Shutdown while <%ld> requests are pending.",
                octstr_get_cstr(conn->id), conndata->open_sends);
    }

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

/*----------------------------------------------------------------
 * Kannel
 */

enum {HEX_NOT_UPPERCASE = 0};

static void kannel_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url;
    List *headers;

    if (!conndata->no_sep) {
        url = octstr_format("%S?"
			    "username=%E&password=%E&to=%E&text=%E",
			     conndata->send_url,
			     conndata->username, conndata->password,
			     sms->sms.receiver, sms->sms.msgdata);
    } else {
        octstr_binary_to_hex(sms->sms.msgdata, HEX_NOT_UPPERCASE);
        url = octstr_format("%S?"
			    "username=%E&password=%E&to=%E&text=%S",
			     conndata->send_url,
			     conndata->username, conndata->password,
			     sms->sms.receiver, 
                             sms->sms.msgdata); 
    }   

    if (octstr_len(sms->sms.udhdata)) {
        if (!conndata->no_sep) {
	    octstr_format_append(url, "&udh=%E", sms->sms.udhdata);
        } else {
	    octstr_binary_to_hex(sms->sms.udhdata, HEX_NOT_UPPERCASE);
            octstr_format_append(url, "&udh=%S", sms->sms.udhdata);
	}
    }

    if (!conndata->no_sender)
        octstr_format_append(url, "&from=%E", sms->sms.sender);
    if (sms->sms.mclass)
	octstr_format_append(url, "&mclass=%d", sms->sms.mclass);
    if (!conndata->no_coding && sms->sms.coding)
	octstr_format_append(url, "&coding=%d", sms->sms.coding);
    if (sms->sms.mwi)
	octstr_format_append(url, "&mwi=%d", sms->sms.mwi);
    if (sms->sms.account) /* prepend account with local username */
	octstr_format_append(url, "&account=%E:%E", sms->sms.service, sms->sms.account);
    if (sms->sms.smsc_id) /* proxy the smsc-id to the next instance */
	octstr_format_append(url, "&smsc=%S", sms->sms.smsc_id);

    headers = list_create();
    debug("smsc.http.kannel", 0, "HTTP[%s]: Start request",
          octstr_get_cstr(conn->id));
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers, 
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    http_destroy_headers(headers);
    
}

static void kannel_parse_reply(SMSCConn *conn, Msg *msg, int status,
			       List *headers, Octstr *body)
{
    /* Test on three cases:
     * 1. an smsbox reply of an remote kannel instance
     * 2. an smsc_http response (if used for MT to MO looping)
     * 3. an smsbox reply of partly sucessfull sendings */
    if ((status == HTTP_OK || status == HTTP_ACCEPTED) 
        && (octstr_case_compare(body, octstr_imm("Sent.")) == 0 ||
            octstr_case_compare(body, octstr_imm("Ok.")) == 0 || 
            octstr_ncompare(body, octstr_imm("Result: OK"),10) == 0)) {
        bb_smscconn_sent(conn, msg);
    } else {
        bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_MALFORMED);
    }
}

static void kannel_receive_sms(SMSCConn *conn, HTTPClient *client,
			       List *headers, Octstr *body, List *cgivars)
{
    ConnData *conndata = conn->data;
    Octstr *user, *pass, *from, *to, *text, *udh, *account, *tmp_string;
    Octstr *retmsg;
    int	mclass, mwi, coding, validity, deferred; 
    List *reply_headers;
    int ret;

    mclass = mwi = coding = validity = deferred = 0;

    user = http_cgi_variable(cgivars, "username");
    pass = http_cgi_variable(cgivars, "password");
    from = http_cgi_variable(cgivars, "from");
    to = http_cgi_variable(cgivars, "to");
    text = http_cgi_variable(cgivars, "text");
    udh = http_cgi_variable(cgivars, "udh");
    account = http_cgi_variable(cgivars, "account");

    tmp_string = http_cgi_variable(cgivars, "flash");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &mclass);
    }
    tmp_string = http_cgi_variable(cgivars, "mclass");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &mclass);
    }
    tmp_string = http_cgi_variable(cgivars, "mwi");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &mwi);
    }
    tmp_string = http_cgi_variable(cgivars, "coding");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &coding);
    }
    tmp_string = http_cgi_variable(cgivars, "validity");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &validity);
    }
    tmp_string = http_cgi_variable(cgivars, "deferred");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &deferred);
    }
    debug("smsc.http.kannel", 0, "HTTP[%s]: Received an HTTP request",
          octstr_get_cstr(conn->id));
    
    if (user == NULL || pass == NULL ||
	    octstr_compare(user, conndata->username) != 0 ||
	    octstr_compare(pass, conndata->password) != 0) {

        error(0, "HTTP[%s]: Authorization failure",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Authorization failed for sendsms");
    }
    else if (from == NULL || to == NULL || text == NULL) {
	
        error(0, "HTTP[%s]: Insufficient args",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Insufficient args, rejected");
    }
    else {
	Msg *msg;
	msg = msg_create(sms);

	debug("smsc.http.kannel", 0, "HTTP[%s]: Constructing new SMS",
          octstr_get_cstr(conn->id));
	
	msg->sms.sender = octstr_duplicate(from);
	msg->sms.receiver = octstr_duplicate(to);
	msg->sms.msgdata = octstr_duplicate(text);
	msg->sms.udhdata = octstr_duplicate(udh);

	msg->sms.smsc_id = octstr_duplicate(conn->id);
	msg->sms.time = time(NULL);
	msg->sms.mclass = mclass;
	msg->sms.mwi = mwi;
	msg->sms.coding = coding;
	msg->sms.validity = validity;
	msg->sms.deferred = deferred;
    msg->sms.account = octstr_duplicate(account);
	ret = bb_smscconn_receive(conn, msg);
	if (ret == -1)
	    retmsg = octstr_create("Not accepted");
	else
	    retmsg = octstr_create("Ok.");
    }
    reply_headers = list_create();
    http_header_add(reply_headers, "Content-Type", "text/plain");
    debug("smsc.http.kannel", 0, "HTTP[%s]: Sending reply",
          octstr_get_cstr(conn->id));
    http_send_reply(client, HTTP_OK, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}


/*----------------------------------------------------------------
 * Brunet - A german aggregator (mainly doing T-Mobil D1 connections)
 *
 *  o bruHTT v1.3L (for MO traffic) 
 *  o bruHTP v2.1  (for MT traffic)
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

/* MT related function */
static void brunet_send_sms(SMSCConn *conn, Msg *sms)
{
    ConnData *conndata = conn->data;
    Octstr *url, *tid;
    List *headers;

    /* 
     * Construct TransactionId like this: <timestamp>-<receiver msisdn>-<msg.id> 
     * to garantee uniqueness. 
     */
    tid = octstr_format("%d-%S-%d", time(NULL), sms->sms.receiver, sms->sms.id);

    /* format the URL for call */
    url = octstr_format("%S?"
        "CustomerId=%E&MsIsdn=%E&Originator=%E&MessageType=%E"
        "&Text=%E&TransactionId=%E"
        "&SMSCount=1&ActionType=A&ServiceDeliveryType=P", /* static parts */
        conndata->send_url,
        conndata->username, sms->sms.receiver, sms->sms.sender,
        (octstr_len(sms->sms.udhdata) ? octstr_imm("B") : octstr_imm("S")),
        sms->sms.msgdata, tid);

    if (octstr_len(sms->sms.udhdata)) {
        octstr_format_append(url, "&XSer=%E", sms->sms.udhdata);
    }

    /* 
     * We use &account=<foobar> from sendsms interface to encode any additionaly
     * proxied parameters, ie. billing information.
     */
    if (octstr_len(sms->sms.account)) {
        octstr_format_append(url, "&%E", sms->sms.account);
    }

    headers = list_create();
    debug("smsc.http.brunet", 0, "HTTP[%s]: Sending request <%s>",
          octstr_get_cstr(conn->id), octstr_get_cstr(url));

    /* 
     * Brunet requires an SSL-enabled HTTP client call, this is handled
     * transparently by the Kannel HTTP layer module.
     */
    http_start_request(conndata->http_ref, HTTP_METHOD_GET, url, headers, 
                       NULL, 0, sms, NULL);

    octstr_destroy(url);
    octstr_destroy(tid);
    http_destroy_headers(headers);
}

static void brunet_parse_reply(SMSCConn *conn, Msg *msg, int status,
                               List *headers, Octstr *body)
{
    if (status == HTTP_OK || status == HTTP_ACCEPTED) {
        if (octstr_case_compare(body, octstr_imm("Status=0")) == 0) {
            bb_smscconn_sent(conn, msg);
        } else {
            error(0, "HTTP[%s]: Message was malformed. SMSC response `%s'.", 
                  octstr_get_cstr(conn->id), octstr_get_cstr(body));
            bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_MALFORMED);
        }
    } else {
        error(0, "HTTP[%s]: Message was rejected. SMSC reponse `%s'.", 
              octstr_get_cstr(conn->id), octstr_get_cstr(body));
        bb_smscconn_send_failed(conn, msg, SMSCCONN_FAILED_REJECTED);
    }
}

/* MO related function */
static void brunet_receive_sms(SMSCConn *conn, HTTPClient *client,
                               List *headers, Octstr *body, List *cgivars)
{
    ConnData *conndata = conn->data;
    Octstr *user, *from, *to, *text, *udh, *date, *type;
    Octstr *retmsg;
    int	mclass, mwi, coding, validity, deferred; 
    List *reply_headers;
    int ret;

    mclass = mwi = coding = validity = deferred = 0;

    user = http_cgi_variable(cgivars, "CustomerId");
    from = http_cgi_variable(cgivars, "MsIsdn");
    to = http_cgi_variable(cgivars, "Recipient");
    text = http_cgi_variable(cgivars, "SMMO");
    udh = http_cgi_variable(cgivars, "XSer");
    date = http_cgi_variable(cgivars, "DateReceived");
    type = http_cgi_variable(cgivars, "MessageType");

    /*
    tmp_string = http_cgi_variable(cgivars, "flash");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &mclass);
    }
    tmp_string = http_cgi_variable(cgivars, "mclass");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &mclass);
    }
    tmp_string = http_cgi_variable(cgivars, "mwi");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &mwi);
    }
    tmp_string = http_cgi_variable(cgivars, "coding");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &coding);
    }
    tmp_string = http_cgi_variable(cgivars, "validity");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &validity);
    }
    tmp_string = http_cgi_variable(cgivars, "deferred");
    if(tmp_string) {
	sscanf(octstr_get_cstr(tmp_string),"%d", &deferred);
    }
    */

    debug("smsc.http.brunet", 0, "HTTP[%s]: Received a request",
          octstr_get_cstr(conn->id));
    
    if (user == NULL || octstr_compare(user, conndata->username) != 0) {
        error(0, "HTTP[%s]: Authorization failure. CustomerId was <%s>.",
              octstr_get_cstr(conn->id), octstr_get_cstr(user));
        retmsg = octstr_create("Authorization failed for MO submission.");
    }
    else if (from == NULL || to == NULL || text == NULL) {
        error(0, "HTTP[%s]: Insufficient args.",
              octstr_get_cstr(conn->id));
        retmsg = octstr_create("Insufficient arguments, rejected.");
    }
    else {
        Msg *msg;
        msg = msg_create(sms);

        debug("smsc.http.brunet", 0, "HTTP[%s]: Received new MO SMS.",
              octstr_get_cstr(conn->id));
	
        msg->sms.sender = octstr_duplicate(from);
        msg->sms.receiver = octstr_duplicate(to);
        msg->sms.msgdata = octstr_duplicate(text);
        msg->sms.udhdata = octstr_duplicate(udh);

        msg->sms.smsc_id = octstr_duplicate(conn->id);
        msg->sms.time = time(NULL); /* XXX maybe extract from DateReceived */ 
        msg->sms.mclass = mclass;
        msg->sms.mwi = mwi;
        msg->sms.coding = coding;
        msg->sms.validity = validity;
        msg->sms.deferred = deferred;

        ret = bb_smscconn_receive(conn, msg);
        if (ret == -1)
            retmsg = octstr_create("Status=1");
        else
            retmsg = octstr_create("Status=0");
    }

    reply_headers = list_create();
    http_header_add(reply_headers, "Content-Type", "text/plain");
    debug("smsc.http.brunet", 0, "HTTP[%s]: Sending reply `%s'.",
          octstr_get_cstr(conn->id), octstr_get_cstr(retmsg));
    http_send_reply(client, HTTP_OK, reply_headers, retmsg);

    octstr_destroy(retmsg);
    http_destroy_headers(reply_headers);
}



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

    return (conndata ? (conn->status != SMSCCONN_DEAD ? 
            conndata->open_sends : 0) : 0);
}


static int httpsmsc_shutdown(SMSCConn *conn, int finish_sending)
{
    ConnData *conndata = conn->data;

    debug("httpsmsc_shutdown", 0, "HTTP[%s]: Shutting down",
          octstr_get_cstr(conn->id));
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
    int ssl = 0;   /* indicate if SSL-enabled server should be used */

    if (cfg_get_integer(&portno, cfg, octstr_imm("port")) == -1) {
        error(0, "HTTP[%s]: 'port' invalid in smsc 'http' record.",
              octstr_get_cstr(conn->id));
        return -1;
    }
    if ((type = cfg_get(cfg, octstr_imm("system-type")))==NULL) {
        error(0, "HTTP[%s]: 'type' missing in smsc 'http' record.",
              octstr_get_cstr(conn->id));
        octstr_destroy(type);
        return -1;
    }
    conndata = gw_malloc(sizeof(ConnData));
    conndata->http_ref = NULL;

    conndata->allow_ip = cfg_get(cfg, octstr_imm("connect-allow-ip"));
    conndata->send_url = cfg_get(cfg, octstr_imm("send-url"));
    conndata->username = cfg_get(cfg, octstr_imm("smsc-username"));
    conndata->password = cfg_get(cfg, octstr_imm("smsc-password"));
    cfg_get_bool(&conndata->no_sender, cfg, octstr_imm("no-sender"));
    cfg_get_bool(&conndata->no_coding, cfg, octstr_imm("no-coding"));
    cfg_get_bool(&conndata->no_sep, cfg, octstr_imm("no-sep"));

    if (conndata->send_url == NULL)
        panic(0, "HTTP[%s]: Sending not allowed. No 'send-url' specified.",
              octstr_get_cstr(conn->id));

    if (octstr_case_compare(type, octstr_imm("kannel")) == 0) {
        if (conndata->username == NULL || conndata->password == NULL) {
            error(0, "HTTP[%s]: 'username' and 'password' required for Kannel http smsc",
                  octstr_get_cstr(conn->id));
            goto error;
        }
        conndata->receive_sms = kannel_receive_sms;
        conndata->send_sms = kannel_send_sms;
        conndata->parse_reply = kannel_parse_reply;
    }
    else if (octstr_case_compare(type, octstr_imm("brunet")) == 0) {
        if (conndata->username == NULL) {
            error(0, "HTTP[%s]: 'username' required for Brunet http smsc",
                  octstr_get_cstr(conn->id));
            goto error;
        }
        conndata->receive_sms = brunet_receive_sms;
        conndata->send_sms = brunet_send_sms;
        conndata->parse_reply = brunet_parse_reply;
    }
    /*
     * ADD NEW HTTP SMSC TYPES HERE
     */
    else {
	error(0, "HTTP[%s]: system-type '%s' unknown smsc 'http' record.",
          octstr_get_cstr(conn->id), octstr_get_cstr(type));

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

    if (http_open_port_if(portno, ssl, conn->our_host)==-1)
	goto error;

    conndata->port = portno;
    conndata->shutdown = 0;
    
    if ((conndata->receive_thread =
	 gwthread_create(httpsmsc_receiver, conn)) == -1)
	goto error;

    if ((conndata->send_cb_thread =
	 gwthread_create(httpsmsc_send_cb, conn)) == -1)
	goto error;

    info(0, "HTTP[%s]: Initiated and ready", octstr_get_cstr(conn->id));
    
    octstr_destroy(type);
    return 0;

error:
    error(0, "HTTP[%s]: Failed to create http smsc connection",
          octstr_get_cstr(conn->id));

    conn->data = NULL;
    conndata_destroy(conndata);
    conn->why_killed = SMSCCONN_KILLED_CANNOT_CONNECT;
    conn->status = SMSCCONN_DEAD;
    octstr_destroy(type);
    return -1;
}

