/*
 * bb_http.c : bearerbox http adminstration commands
 *
 * NOTE: this is a special bearerbox module - it does call
 *   functions from core module! (other modules are fully
 *    encapsulated, and only called outside)
 *
 * Kalle Marjola <rpr@wapit.com> 2000 for project Kannel
 */

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "gwlib/gwlib.h"
#include "bearerbox.h"

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;

/* our own thingies */

static volatile sig_atomic_t httpadmin_running;

static int	ha_port;
static char	*ha_password;
static char	*ha_allow_ip;
static char	*ha_deny_ip;


/*---------------------------------------------------------
 * static functions
 */

/*
 * check if the password matches. Return NULL if
 * it does (or is not required)
 */
static Octstr *httpd_check_authorization(List *cgivars)
{
    Octstr *password;
    Octstr *reply;

    password = http_cgi_variable(cgivars, "password");

    if ((ha_password && password == NULL) ||
	(ha_password && octstr_str_compare(password, ha_password)!=0))
    {
	reply = octstr_create("Denied");
    } else
	reply = NULL;

    return reply;
}

/*
 * check if we still have time to do things
 */
static Octstr *httpd_check_status(void)
{
    if (bb_status == BB_SHUTDOWN || bb_status == BB_DEAD)
	return octstr_create("Avalanche has already started, too late to "
	    	    	     "save the sheeps");
    return NULL;
}



static Octstr *httpd_status(List *cgivars)
{
    return bb_print_status();
}

static Octstr *httpd_shutdown(List *cgivars)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars))!= NULL) return reply;
    if (bb_status == BB_SHUTDOWN)
	bb_status = BB_DEAD;
    else
	bb_shutdown();
    return octstr_create("Bringing system down");
}

static Octstr *httpd_isolate(List *cgivars)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    if (bb_isolate() == -1)
	return octstr_create("Already isolated");
    else
	return octstr_create("Kannel isolated from message providers");
}

static Octstr *httpd_suspend(List *cgivars)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    if (bb_suspend() == -1)
	return octstr_create("Already suspended");
    else
	return octstr_create("Kannel suspended");
}

static Octstr *httpd_resume(List *cgivars)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;
 
    if (bb_resume() == -1)
	return octstr_create("Already running");
    else
	return octstr_create("Running resumed");
}




static void httpd_serve(void *arg)
{
    HTTPSocket *client = arg;
    List *headers, *cgivars;
    Octstr *url, *body;
    Octstr *reply;
    
    if (http_server_get_request(client, &url, &headers, &body, &cgivars) < 1)
    {
	warning(0, "Malformed line from client, ignored");
	http_server_close_client(client);
	return;
    }
    
    if (octstr_str_compare(url, "/cgi-bin/status")==0)
	reply = httpd_status(cgivars);
    else if (octstr_str_compare(url, "/cgi-bin/shutdown")==0)
	reply = httpd_shutdown(cgivars);
    else if (octstr_str_compare(url, "/cgi-bin/suspend")==0)
	reply = httpd_suspend(cgivars);
    else if (octstr_str_compare(url, "/cgi-bin/isolate")==0)
	reply = httpd_isolate(cgivars);
    else if (octstr_str_compare(url, "/cgi-bin/resume")==0)
	reply = httpd_resume(cgivars);
    /*
     * reconfig? restart?
     */
    else 
	reply = octstr_format("Unknown command %S", url);

    gw_assert(reply != NULL);
    
    /* XXX: how about headers? */

    debug("bb.http", 0, "Result: '%s'", octstr_get_cstr(reply));
    
    if (http_server_send_reply(client, HTTP_OK, headers, reply) == -1)
	warning(0, "HTTP-admin server_send_reply failed");

    octstr_destroy(url);
    octstr_destroy(body);
    octstr_destroy(reply);
    http_destroy_headers(headers);
    http_destroy_cgiargs(cgivars);

    http_server_close_client(client);
}

static void httpadmin_run(void *arg)
{
    HTTPSocket *httpd, *client;
    int port;

    port = (int)arg;
    
    httpd = http_server_open(port);
    if (httpd == NULL)
	panic(0, "Cannot start without HTTP admin");
    
    /* infinitely wait for new connections;
     */

    while(bb_status != BB_DEAD) {
	if (bb_status == BB_SHUTDOWN)
	    bb_shutdown();
	if (read_available(http_socket_fd(httpd), 100000) < 1)
	    continue;
	client = http_server_accept_client(httpd);
	if (client == NULL)
	    continue;
	if (is_allowed_ip(ha_allow_ip,ha_deny_ip,http_socket_ip(client))==0) {
	    info(0, "HTTP admin tried from denied host <%s>, disconnected",
		 octstr_get_cstr(http_socket_ip(client)));
	    http_server_close_client(client);
	    continue;
	}
        if (gwthread_create(httpd_serve, client) == -1) {
	    error(0, 
	    	"Failed to start a new thread to handle HTTP admin command");
	    http_server_close_client(client);
	}
    }
    http_server_close(httpd);

    httpadmin_running = 0;
}


/*-------------------------------------------------------------
 * public functions
 *
 */

int httpadmin_start(Config *config)
{
    char *p;
    ConfigGroup *grp;
    
    if (httpadmin_running) return -1;


    grp = config_find_first_group(config, "group", "core");
    if ((p = config_get(grp, "admin-port")) == NULL)
	panic(0, "Missing admin-port variable, cannot start HTTP admin");

    ha_password = config_get(grp, "admin-password");
    if (ha_password == NULL)
	warning(0, "No HTTP admin password set");
    
    ha_port = atoi(p);
    ha_allow_ip = config_get(grp, "admin-allow-ip");
    ha_deny_ip = config_get(grp, "admin-deny-ip");
    
    if (gwthread_create(httpadmin_run, (void *)ha_port) == -1)
	panic(0, "Failed to start a new thread for HTTP admin");

    httpadmin_running = 1;
    return 0;
}


