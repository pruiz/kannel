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
#include "new_bb.h"

/* passed from bearerbox core */

extern volatile sig_atomic_t bb_status;

extern List *core_threads;

/* our own thingies */

static volatile sig_atomic_t httpadmin_running;

static int	ha_port;
static char	*ha_password;


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

    
    /* XXX this is WRONG, headers and cgiargs are not of the same
     *   type, but while they are not, ignore password
     */
    /* password = http2_header_find_first(cgivars, "password"); */

    return NULL;

    if ((ha_password && password == NULL) ||
	(ha_password && octstr_str_compare(password, ha_password)!=0))

	reply = octstr_create("Denied");
    else
	reply = NULL;

    octstr_destroy(password);

    return reply;
}

/*
 * check if we still have time to do things
 */
static Octstr *httpd_check_status(void)
{
    if (bb_status == BB_SHUTDOWN || bb_status == BB_DEAD)
	return octstr_create("Avalanche has already started, too late to save the sheeps");
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
    if ((reply = httpd_check_status())!= NULL) return reply;

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




static void *httpd_serve(void *arg)
{
    HTTPSocket *client = arg;
    List *headers, *cgivars;
    Octstr *url, *body;
    Octstr *reply;
    
    debug("bb.thread", 0, "START: httpd_serve");
    list_add_producer(core_threads);
    http2_server_get_request(client, &url, &headers, &body, &cgivars);
    
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
	reply = octstr_create("Unknown command %s"); /* ,url */

    gw_assert(reply != NULL);
    
    /* XXX: how about headers? */

    debug("bb.http", 0, "Result: '%s'", octstr_get_cstr(reply));
    
    if (http2_server_send_reply(client, HTTP_OK, headers, reply) == -1)
	warning(0, "HTTP-admin server_send_reply failed");
    
    octstr_destroy(url);
    octstr_destroy(body);
    octstr_destroy(reply);
    // http2_destroy_headers(headers);
    // http2_destroy_headers(cgivars);
    
    http2_server_close_client(client);
    debug("bb.thread", 0, "EXIT: httpd_serve");
    list_remove_producer(core_threads);
    return NULL;
}

static void *httpadmin_run(void *arg)
{
    HTTPSocket *httpd, *client;
    int port;

    debug("bb.thread", 0, "START: httpadmin_run");
    list_add_producer(core_threads);
    port = (int)arg;
    
    httpd = http2_server_open(port);
    if (httpd == NULL)
	panic(0, "Cannot start without HTTP admin");
    
    /* infinitely wait for new connections;
     */

    while(bb_status != BB_DEAD) {
	if (read_available(http2_socket_fd(httpd), 100000) < 1)
	    continue;
	client = http2_server_accept_client(httpd);
	if (client == NULL)
	    continue;
	if ((int)(start_thread(0, httpd_serve, client, 0)) == -1) {
	    error(0, "Failed to start a new thread to handle HTTP admin command");
	    http2_server_close_client(client);
	}
    }
    http2_server_close(httpd);

    debug("bb.thread", 0, "EXIT: httpadmin_run");
    list_remove_producer(core_threads);
    return NULL;
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
    if ((p = config_get(grp, "admin-port")) == NULL) {
	error(0, "Missing admin-port variable, cannot start HTTP admin");
	return -1;
    }
    ha_password = config_get(grp, "admin-password");
    if (ha_password == NULL)
	warning(0, "No HTTP admin password set");
    
    ha_port = atoi(p);
    
    if ((int)(start_thread(0, httpadmin_run, (void *)ha_port, 0)) == -1)
	panic(0, "Failed to start a new thread for HTTP admin");

    httpadmin_running = 1;
    return 0;
}


