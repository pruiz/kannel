/*****************************************************************************
* http.c - The implementation of the HTTP subsystem.
* Mikael Gueck (mikael.gueck@wapit.com) for WapIT Ltd.
* Upkeeping Sanna Seppanen <sanna@wapit.com> for WapIT Ltd.
*/


#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <math.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/param.h>

#include "config.h"
#include "gwlib.h"


/*****************************************************************************
* Static Functions
*/

static unsigned char *internal_base6t4(unsigned char *pass);

static double round_to_closest_integer(double x);


/*****************************************************************************
* Implementations
*/

/*****************************************************************************
* Set up a listening socket
*/
int httpserver_setup(int port) {

    return make_server_socket(port);
}

/*****************************************************************************
* Accept a HTTP connection, analyze it and return results
*/

int httpserver_get_request(int socket, char **client_ip, char **path, char **args) {

    int len, connfd = 0;
    struct sockaddr_in cliaddr;
    char accept_ip[NI_MAXHOST];
    
    char *eol = NULL, *ptr = NULL;
    int done_with_looping = 0,  done_with_status = 0, request = -1;
    int tmpint = 0, i = 0, k = 0;
    char *growingbuff = NULL, *newbuff = NULL, *temp = NULL;
    int gbsize = 0, readthisfar = 0;
    URL *url = NULL;
    
    
    /* accept the connection */
    len = sizeof(cliaddr);
    connfd = accept(socket, (struct sockaddr *) &cliaddr, &len);
    if(connfd==-1) {
	error(errno, "could not accept connection to HTTP server socket");
	goto error;
    }

    memset(accept_ip, 0, sizeof(accept_ip));
    getnameinfo((struct sockaddr*)&cliaddr, len,
		accept_ip, sizeof(accept_ip), 
		NULL, 0, NI_NUMERICHOST);
    *client_ip = gw_strdup(accept_ip);
    
    gbsize = 1024;
    growingbuff = gw_malloc(gbsize);
    memset(growingbuff, 0, gbsize);
    errno = 0;

    
    /* Receive the client request. */
    for(;;) {
	if(done_with_looping == 1) break;
	
	/* Read from socket. */
	ptr = growingbuff + readthisfar;
	tmpint = read(connfd, ptr, gbsize-readthisfar);
	    
	if(tmpint == -1) {
	    /* Ignore interrupts. */
	    if (errno == EAGAIN) continue;
	    else if (errno == EINTR) continue;
	    else goto error;
	} else {
	    readthisfar += tmpint;
	}

	/* First comes the status line. */
	if(done_with_status == 0) {

	    eol = strstr(growingbuff, "\r\n");
	    if(eol != NULL) {
		*eol = '\0';
		newbuff = gw_strdup(growingbuff);

		/* check the request method type */
		if( sscanf(growingbuff, "GET %s HTTP/%i.%i",
			   newbuff, &tmpint, &tmpint) == 3 )
		    request = 1; /*designating GET*/ 
		else if( sscanf(growingbuff, "POST %s HTTP/%i.%i",
				newbuff, &tmpint, &tmpint) == 3 )
		    request = 2; /*designating POST*/
		
		url = internal_url_create(newbuff);
		gw_free(newbuff);
		*eol = '\r';
		done_with_status = 1;
		done_with_looping = 1;
		continue;		
	    }
	    
	} /* Statusline found */

	/* see if the buffer needs enlargement */
	
    } /* for */


    if( request == 2 ){/* POST */
	eol = strstr(growingbuff,"Content-Length:");
	/* advance to header value */
	eol = eol+16;

	/* get the content-length*/
	temp = gw_malloc(sizeof(int));
	memset(temp, 0, sizeof(int));
	i = 0;
	while( *eol != '\r' && *eol != '\n' ){
	    temp[i++] = *eol;
	    eol++;
	}

	i=0;
	i = atoi(temp);
	
	/* advance to start of data- cut off the whitespaces */
	eol = strstr(growingbuff, "\r\n\r\n");
	while(isspace(*eol))
	    eol++;
	
	newbuff = gw_malloc(i);
	
	for( k=0; k<i; k++ ){
	    newbuff[k] = *eol;
	    eol++;	
	}

	*args = gw_strdup(newbuff);
	gw_free(newbuff);
    }

    else if( request == 1 ){/* GET */
	if(url->query != NULL) *args = gw_strdup(url->query);
	else *args = NULL;
    }
    
	if(url->abs_path != NULL) *path = gw_strdup(url->abs_path);
	else *path = NULL;    
	
	internal_url_destroy(url);
	gw_free(growingbuff);
	
	return connfd;
error:
    gw_free(growingbuff);
    return -1;
}


/*************************************************************
 */
int httpserver_answer(int socket, char *text) {

    char *bigbuff = NULL;
    int tmpint = 0;
    size_t bufsize = 0;

    assert(socket >= 0);
    assert(text != NULL);

    bufsize = strlen(text) + 1024;
    bigbuff = gw_malloc(bufsize);

    sprintf(bigbuff, "HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nContent-Length: %ul\r\n\r\n", (unsigned int) strlen(text));
    strcat(bigbuff, text);

    tmpint = write_to_socket(socket, bigbuff);

    if(tmpint == -1) {
	error(errno, "error sending to socket");
	gw_free(bigbuff);
	return -1;
    }

    gw_free(bigbuff);
    return close(socket);
}









/*****************************************************************************
* HTTP client GET uses http_get_u with no user defined (NULL) headers
*/

int http_get(char *urltext, char **type, char **data, size_t *size) {

    if( http_get_u(urltext, type, data, size, NULL) == 0 )
	return 0;
    else goto error;
    
error:
    error(errno, "http_get: failed");
    return -1;
}




/**********************************************************************
 * Http client Get with (given) universal headers
 */

int http_get_u(char *urltext, char **type, char **data, size_t *size,  HTTPHeader *header)
{ 
    
    URL *url = NULL;
    HTTPRequest *request = NULL, *response = NULL;
    unsigned char *ptr = NULL, tempbuffer[4];
    unsigned char *authorization = NULL;
    int how_many_moves = 0;
    size_t temp =0;
      
   /* Initializing... */
    /* ..request */
    if( (url = internal_url_create(urltext)) == NULL) {
	error(errno, "http_get_u: Creating URL failed");
	goto error;
    }

    /* ..url */
    if( (request = httprequest_create(url, NULL)) == NULL) {
	error(errno, "http_get_u: Creating HTTPRequest failed");
	internal_url_destroy(url);    
	goto error;
    }
    
    internal_url_destroy(url);    
    
    /* Adding useful stuff */

    if( request->method_type == POST ){
	memset(tempbuffer, 0, strlen(tempbuffer));
	temp = (int) strlen(*data);
	sprintf(tempbuffer, "%d", temp);
	httprequest_add_header( request, "Content-Length", tempbuffer );
    } else {
	request->method_type = GET;
	/* data length zero */
	httprequest_add_header(request, "Content-Length", "0" );
    }
    
    
    
    /* .. the common headers... */
    httprequest_add_header(request, "Host", request->url->host);
    httprequest_add_header(request, "Connection", "close");
    httprequest_add_header(request, "User-Agent",
			   "Mozilla/2.0 (compatible; Open Source WAP Gateway)");
    
    /* ..username */
    if(request->url->username != NULL) {
	ptr = gw_malloc(1024);
	sprintf(ptr, "%s:%s", request->url->username, request->url->password);
	if( (authorization = internal_base6t4(ptr)) == NULL){
	    error(errno, "http_get_u: internal_base6t4 failed");
	    gw_free(ptr);
	    goto error;
	}
	sprintf(ptr, "Basic %s", authorization);
	httprequest_add_header(request, "Authorization", ptr);
	gw_free(authorization);
	gw_free(ptr);
    }
    
    
    /* ..the user defined headers */
    for(;;){
	if(header == NULL)break;
	httprequest_replace_header(request, header->key, header->value);
	header = header->next;
    }
    
    
    
    for(;;) {
	
	debug("gwlib.http", 0, "http_get_u: Making request using headers:");
	header_dump(request->baseheader);

	/* Open connection, send request, get response. */
	response = httprequest_execute(request);
	if(response == NULL){
	    error(errno, "http_get_u: Response failed");
	    httprequest_destroy(request);
	    goto error;
	}
	
	/* Great, the server responded "200 OK" */
	if(response->status == 200) break;
       


	 /* We are redirected to another URL */
	 else if( (response->status == 301) || 
		  (response->status == 302) || 
		  (response->status == 303) ) {

	     url = internal_url_create(httprequest_get_header_value(
		 response, "Location"));
	     if(url==NULL){
		 error(0,"http_get_u: missing 'Location' header in response");
		 httprequest_destroy(response);
		 httprequest_destroy(request);
		 goto error;
	     }

	     httprequest_destroy(request);
	     httprequest_destroy(response);

	     /* Abort if we feel like we might be pushed around in 
		an endless loop. 10 is just an arbitrary number. */
	     if(how_many_moves > 10) {
		 internal_url_destroy(url);
		 error(0, "http_get_u: too many redirects");
		 goto error;
	     }
	     how_many_moves++;

	     /* let's create the request all over again */
	     request = httprequest_create(url, NULL);
	     internal_url_destroy(url);
	     if(request == NULL) goto error;



	     /* .. the common headers... */
	     httprequest_add_header(request, "Host", request->url->host);
	     httprequest_add_header(request, "Connection", "close");
	     httprequest_add_header(
		 request, "User-Agent",
		 "Mozilla/2.0 (compatible; Open Source WAP Gateway)");


	     /* ..username */
	     if(request->url->username != NULL) {
		 ptr = gw_malloc(1024);
		 sprintf(ptr, "%s:%s", request->url->username,
			 request->url->password);
		 if( (authorization = internal_base6t4(ptr)) == NULL){
		     error(errno, "http_get_u: internal_base6t4 failed");
		     gw_free(ptr);
		     goto error;
		 }
		 sprintf(ptr, "Basic %s", authorization);
		 httprequest_add_header(request, "Authorization", ptr);
		 gw_free(authorization);
		 gw_free(ptr);
	     }



	     /* ..the user defined headers */
	     for(;;){
		 if(header == NULL)break;
		 httprequest_add_header(request, header->key, header->value);
		 header = header->next;
	     }
	     
	     
	 } /* redirection handling ends*/


	/* Server returned "404 Authorization Required" */
	else if(response->status == 401) break;
	
	
	/* Server returned "404 Not Found" */
	else if(response->status == 404) break;
	
	
	/* Server returned "501 Not Implemented" */
	else if(response->status == 501) break;
	
	
	/* If we haven't handled the response code this far, abort. */
	else break;
	
    } /* for ends */
    
    httprequest_destroy(request);


    
    
    
/* Check the content of the "Content-Type" header. */
    if( (ptr = httprequest_get_header_value(response,
					    "Content-Type")) != NULL ) {
	*type = gw_strdup(ptr);
    } else {
	*type = NULL;
    }
    
    /* Fill in the variables which we'll return. */
    if( (response->data != NULL) && (response->data_length > 0) ) {
	*data = gw_malloc(response->data_length+1);
	memset(*data, 0, response->data_length+1);
	memcpy(*data, response->data, response->data_length);
	*size = response->data_length;
    } else {
	*data = NULL;
	*size = 0;
    }

    httprequest_destroy(response);
    return 0;


    
error:
    error(0, "http_get_u: failed");
    return -1;
}




/**********************************************************************
 * http_post. User must provide the correct data and headers
 */

int http_post(char *urltext, char **type, char **data, size_t *size,  HTTPHeader *header)
{ 

  URL *url = NULL;
    HTTPRequest *request = NULL, *response = NULL;
    char *ptr = NULL;
    char *authorization = NULL;
    int how_many_moves = 0;
    char temp_buffer[5]="\0\0\0\0\0";

    /* Initializing... */
    /* ..request */
    if( (url = internal_url_create(urltext)) == NULL) {
	error(errno, "http_post: creating URL failed");
	goto error;
    }
    
    /* ..url */
    if( (request = httprequest_create(url, *data)) == NULL) {
	error(errno, "http_post: creating HTTPRequest failed");
	goto error;
    }
    internal_url_destroy(url);    


    /* Adding... */
    request->method_type = POST;
    httprequest_add_header(request, "Connection", "Keep-Alive");
    httprequest_add_header(request,
			   "User-Agent",
			   "Mozilla/2.0 (compatible; Open Source WAP Gateway)");
    httprequest_add_header(request, "Host", request->url->host);

    memset(temp_buffer, 0, strlen(temp_buffer));
    sprintf(temp_buffer, "%d", (int) strlen(*data));
    httprequest_add_header(request, "Content-Length", temp_buffer);


    for(;;){
	if(header == NULL)break;
	httprequest_replace_header(request, header->key, header->value);
	header = header->next;
    }
    
    
    /* ..username */
    if(request->url->username != NULL) {
	ptr = gw_malloc(1024);
	sprintf(ptr, "%s:%s", 
		request->url->username, request->url->password);
	authorization = internal_base6t4(ptr);
	if(authorization == NULL) goto error;
	sprintf(ptr, "Basic %s", authorization);
	httprequest_add_header(request, "Authorization", ptr);
	gw_free(authorization);
	gw_free(ptr);
    }
    

    for(;;) {
	
	/* Open connection, send request, get response. */
	response = httprequest_execute(request);
	if(response == NULL) goto error;
    
	/* Great, the server responded "200 OK" */
	if(response->status == 200) break;
	
	/* "204 No Content" */
	if(response->status == 204) break;
	
	/* We are redirected to another URL */
	else if( (response->status == 301) || 
		 (response->status == 302) || 
		 (response->status == 303) ) {
	    
	    url = internal_url_create(
		httprequest_get_header_value(response, "Location"));
	    if(url==NULL) goto error;
	    httprequest_destroy(request);
	    httprequest_destroy(response);
	    
	    /* Abort if we feel like we might be pushed around in 
	       an endless loop. 10 is just an arbitrary number. */
	    if(how_many_moves > 10) {
		error(0, "http_post: too many redirects");
		goto error;
	    }
	    how_many_moves++;
	    
	    /* let's create the request all over again */
	    request = httprequest_create(url, *data);
	    internal_url_destroy(url);
	    if(request == NULL) goto error;
	    httprequest_add_header(request, "Host", request->url->host);
	    httprequest_add_header(request, "Connection", "close");
	    httprequest_add_header(
		request, "User-Agent",
		"Mozilla/2.0 (compatible; Open Source WAP Gateway)");
	    
	    /*   ..the user defined headers */
	    for(;;){
		if(header == NULL)break;
		httprequest_replace_header(request, header->key, header->value);
		header = header->next;
	    }
	    
	} /* else-if ends*/
	
	
        /* Server returned "404 Authorization Required" */
	else if(response->status == 401) break;
	
	/* Server returned "404 Not Found" */
	else if(response->status == 404) break;
	
	/* Server returned "501 Not Implemented" */
	else if(response->status == 501) break;
	
	/* If we haven't handled the response code this far, abort. */
	else break;
    
    } /* for ends */
    
    /* Check the content of the "Content-Type" header. */
    if( (ptr = httprequest_get_header_value(response,
					    "Content-Type")) != NULL ) {
	*type = gw_strdup(ptr);
    } else {
	*type = NULL;
    }
    
    /* Fill in the variables which we'll return. */
    if( (response->data != NULL) && (response->data_length > 0) ) {
	*data = gw_malloc(response->data_length+1);
	memset(*data, 0, response->data_length+1);
	memcpy(*data, response->data, response->data_length);
	*size = response->data_length;
    } else {
	*data = NULL;
	*size = 0;
    }
    
    /* done, clean up */
    httprequest_destroy(request);
    httprequest_destroy(response);
    return 0;
    
error:
    error(errno, "http_post: failed");
    httprequest_destroy(request);
    httprequest_destroy(response);
    return -1;

    
}





/*********************************************************************
* Create a URL structure (see RFC2616/3.2.2)
* Does NOT validate. Defaults to http://HOSTNAME:80/
*/
URL* internal_url_create(char *text) {

    char *mycopy = NULL;
    char *scheme_start = NULL, *scheme_end = NULL;
    char *username_start = NULL, *username_end = NULL;
    char *password_start = NULL, *password_end = NULL;
    char *host_start = NULL;
    char *host_end = NULL;
    char *cport_start = NULL;
    char *abs_path_start = NULL;
    char *query_start = NULL;
    URL *tmpURL = NULL;

    int has_scheme = 0, has_username = 0, has_server = 0, has_port = 0;
    int has_abs_path = 0, has_query = 0;

    has_scheme = has_username = has_server = has_port =
	has_abs_path = has_query;

    if(text == NULL) {
	debug("gwlib.http", 0,
	      "url_create: someone just called me with NULL input");
	goto error;
    }

    /* make a copy of the input string for myself */
    mycopy = gw_strdup(text);
    if(mycopy == NULL) {
	goto error;
    }
	
    tmpURL = gw_malloc(sizeof(struct URL));
    memset(tmpURL, 0, sizeof(struct URL));

    tmpURL->scheme = NULL;
    tmpURL->host = NULL;
    tmpURL->abs_path = NULL;
    tmpURL->query = NULL;
    tmpURL->username = NULL;
    tmpURL->password = NULL;

    /* cut off query - stuff after '?' */
    /* before: "scheme://username:password@server:port/abs_path?query" */
    /* after: "scheme://username:password@server:port/abs_path" */
    query_start = strchr(mycopy, '?');
    if(query_start == NULL) { /* no query this time */
	tmpURL->query = NULL;
    } else { /* cut off the query part */
	tmpURL->query = gw_strdup(query_start+1);
	*query_start = '\0';
    }

    /* Now see if we're using HTTP or FTP or gopher... */
    /* before: "scheme://username:password@server:port/abs_path" */
    /* after: "username:password@server:port/abs_path" */
    scheme_end = strstr(mycopy, "://");
    if (scheme_end != NULL) { /* awrighty! */
	scheme_start = mycopy;
	*scheme_end   = '\0';
	tmpURL->scheme = gw_strdup(scheme_start);
	host_start = scheme_end + 3;
    } else { /* no scheme found, default to http */
	tmpURL->scheme = gw_strdup("http");
	host_start = mycopy;
    }

    /* cut off the abs_path - stuff after and including the first / */
    /* before: "username:password@server:port/abs_path" */
    /* after: "username:password@server:port" */
    abs_path_start = strchr(host_start, '/');
    if(abs_path_start != NULL) { /* the hostname is followed by path */
	tmpURL->abs_path = gw_strdup(abs_path_start);
	*abs_path_start = '\0';
    } else { /* default per RFC2616/3.2.2 */
	tmpURL->abs_path = gw_strdup("/");
    }

    /* cut off the cport */
    /* before: "username:password@server:port" */
    /* after: "username:password@server" */

    password_end = strchr(host_start, '@');
    if(password_end != NULL) {
	username_end = strchr(host_start, ':');
	username_start = host_start;
	host_start = password_end + 1;
    }

    cport_start = strrchr(host_start, ':');

    if( (cport_start != NULL) && (cport_start != username_end)) {
	/* special but legal case http://hostname:/ */
    /* see RFC2616/3.2.2 and 3.2.3 */
    if(strlen(cport_start) == 1) { 
	tmpURL->port = 80;
    } else {
	tmpURL->port = atoi(cport_start+1);
	host_end = cport_start - 1;
    }
    *cport_start = '\0';

    } else { /* default to the http service port 80 */
	tmpURL->port = 80;
    }

    /* before: "username:password@server" */
    /* after: "username:password" */
    if(username_end != NULL) {
	if(password_end != NULL) {
	    password_start = username_end + 1;

	    if( (password_start != NULL) && (password_end!=NULL) ) {
		*password_end = '\0';
		tmpURL->password = gw_strdup(password_start);
	    }

	    if( (username_start != NULL) && (username_end!=NULL) ) {
		*username_end = '\0';
		tmpURL->username = gw_strdup(username_start);
	    }


	    host_start = password_end + 1;
	} else {
	    host_start = username_end + 1;
	}
    }

    /* before: "server" */
    /* after: "" */
    tmpURL->host = gw_strdup(host_start);

    gw_free(mycopy);
    return tmpURL;

error:
    debug("gwlib.http", errno, "url_create: failed");
    gw_free(mycopy);
    internal_url_destroy(tmpURL);
    return NULL;
}


/*********************************************************************
* Destroy a URL structure
*/
int internal_url_destroy(URL *url) {

    if(url==NULL) return -1;

    gw_free(url->username);
    gw_free(url->password);
    gw_free(url->scheme);
    gw_free(url->host);
    gw_free(url->abs_path);
    gw_free(url->query);
    gw_free(url);

    return 0;

}


/*********************************************************************
* Create an exact duplicate of an URL structure
*/
URL* url_duplicate(URL* url) {

    URL *newurl = NULL;

    newurl = gw_malloc(sizeof(struct URL));
    memset(newurl, 0, sizeof(struct URL));

    if(url->username != NULL) {
	newurl->username = gw_strdup(url->username);
    }

    if(url->password != NULL) {
	newurl->password = gw_strdup(url->password);
    }

    if(url->scheme != NULL) {
	newurl->scheme = gw_strdup(url->scheme);
    }

    if(url->host != NULL) {
	newurl->host = gw_strdup(url->host);
    }

    if(url->abs_path != NULL) {
	newurl->abs_path = gw_strdup(url->abs_path);
    }

    if(url->query != NULL) {
	newurl->query = gw_strdup(url->query);
    }

    newurl->port = url->port;

    return newurl;
}


/************************************************************
 */
URL* internal_url_relative_to_absolute(URL* baseURL, char *relativepath) {

    return NULL;
}

/*****************************************************************************
* Create a raw request from a URL
*/
HTTPRequest* httprequest_create(URL *url, char *payload) {

    HTTPRequest *request = NULL;

    /* initialize the variable to be returned */
    request = gw_malloc(sizeof(struct HTTPRequest));
    memset(request, 0, sizeof(struct HTTPRequest));

    if(url == NULL)
	request->url = NULL;
    else 
	request->url = url_duplicate(url);
    
    if(payload == NULL)
	request->data = NULL;
    else
	request->data = gw_strdup(payload);
    
    request->baseheader = NULL;
    
    
    return request;
}


/*****************************************************************************
* Free a HTTPRequest structure
*/
int httprequest_destroy(HTTPRequest *request) {

    HTTPHeader *thisheader = NULL, *nextheader = NULL;

    if(request==NULL)
	return 0;


    if(request->url != NULL)	
	internal_url_destroy(request->url);

    /* free headers */
    thisheader = request->baseheader;
    for(;;) {
	if(thisheader == NULL) break;
	nextheader = thisheader->next;
	gw_free(thisheader->key);
	gw_free(thisheader->value);
	gw_free(thisheader);
	thisheader = nextheader;
    }
	
    /* free data */
    gw_free(request->data);

    /* free the structure */
    gw_free(request);
	
    return 0;
}

/*****************************************************************************
* Take the raw input data and turn it into a HTTPRequest structure
*/
HTTPRequest* httprequest_wrap(char *from, size_t size) {

    char *eol = NULL, *mycopy = NULL, *ptr = NULL, *midptr = NULL;
    char *tmpbuff = NULL;
    int tmpint = 0, tmpint2 = 0;
    HTTPRequest *request = NULL;

    if( (from==NULL) || (size==0) ) {
	error(0, "httprequest_wrap: faulty input");
	goto error;
    }

    mycopy = gw_malloc(size);
    memcpy(mycopy, from, size);

    request = gw_malloc(sizeof(HTTPRequest));
    tmpbuff = gw_malloc(10*1024);

    memset(request, 0, sizeof(request));
    memset(tmpbuff, 0, 10*1024);

    request->url = NULL;
    request->baseheader = NULL;
    request->data = NULL;

    /* parse the status line */
    eol = strstr(mycopy, "\r\n");
    if(eol==NULL) { /* This ain't HTTP, mate */
	error(0, "httprequest_wrap: no HTTP header found");
	goto error;
    }
    /* replace the '\r\n' sequence with '\0'*/
    *eol = '\0';
    
    /* maybe we are acting as a client? */

    tmpint = sscanf(mycopy, "HTTP/%i.%i %i %s", 
		    &request->http_version_major,
		    &request->http_version_minor, 
		    &request->status, 
		    tmpbuff);    
    
    
    /* perhaps we are acting as a server? */
    if(tmpint < 4) {
	tmpint = sscanf(mycopy, "GET %s HTTP/%i.%i",
			tmpbuff,
			&request->http_version_major,
			&request->http_version_minor);
	request->url = internal_url_create(tmpbuff);
    }

    /* One way or another, parse the headers. */
    for(;;) {
	ptr = eol+2;
	midptr = strchr(ptr, ':');
	eol = strstr(ptr, "\r\n");
		
	/* Kludge for a stupid nonconforming HTTP server */
	if(eol == NULL) {
	    eol = strstr(ptr, "\n\n");
	    if(eol==NULL) {
		ptr += 2;
		break;
	    }
	}

	if(ptr == eol) {    /* found "\r\n\r\n" - end of headers */
	    ptr += 2; 
	    break;
	}

	if(eol != NULL) *eol = '\0';
	if(midptr != NULL) *midptr = '\0';
	midptr++;
	while(isspace(*midptr)) midptr++;
	/* advance to start of data- cut off the whitespaces */
	if( (eol!=NULL) && (midptr!=NULL) )
	    httprequest_add_header(request, ptr, midptr);
    }

    request->data_length = size - (int) (ptr - mycopy);








    
    /* The data might be chucked according to RFC2616/3.6.1, and in
       practise quite often is. We detect this by checking the value of
       the "Transfer-Encoding" header. */

    
    midptr = httprequest_get_header_value(request, "Transfer-Encoding");


    if(midptr == NULL) {
	/* no transfer encoding - a simple case */
	if(httprequest_get_header_value(request, "Content-Length") != NULL)
	    request->data_length = atoi(
		httprequest_get_header_value(request, "Content-Length"));
	
	/* No need to check Content-Length, just read it all in. */
	request->data = gw_malloc(request->data_length+1);
	memset(request->data, 0, request->data_length);
	memcpy(request->data, ptr, request->data_length);
	request->data[request->data_length] = '\0';






    } else if(strstr(midptr, "chunked") != NULL) {
	/* yepp, data has encoding */	
	/* Get enough space to hold all the data. */
	request->data = gw_malloc(request->data_length);
	memset(request->data, 0,  request->data_length);
	
	

	/* Convert RFC2616/3.6.1 chunked data to normal. */
	for(;;){

	    /* Get the chunk size. */
	    tmpint = strtol(ptr, NULL, 16);
	    if( (tmpint2+tmpint) > size) {
		error(0, "httprequest_wrap: chunk size too big");
		break;
	    }
	    debug("gwlib.http",0, "chunk size:<%d>",tmpint);
	    debug("gwlib.http",0, "chunk:<%s>",ptr);	    
	    if(tmpint == 0) break;
	    midptr = strstr(ptr, "\r\n");
	    if(midptr == NULL) break;
	    memcpy(request->data+tmpint2, midptr+2, tmpint);
	    ptr = midptr + 2 /* CRLF */ + tmpint + 2 /* CRLF */;
	    tmpint2 += tmpint;
	    debug("gwlib.http",0, "loopstop\n\n\n\n\n");
	} /* for loop */


	
	/* Get the real data size */
	request->data_length = tmpint2;
	
	/* Shrink the buffer to the real size */
	request->data = gw_realloc(request->data, request->data_length+1);
	request->data[request->data_length] = '\0';
	
    }




    else { /* Transfer-Encoding set to an unknown value */
	/* Someone at the server end seems to have botched
	   the job of implementing HTTP/1.1. 
	   The "Transfer-Encoding" header should hold 
	   either "chunked" or NULL. */
	error(0, "Broken HTTP implementation on the server side.");
    }


    
    /* done */
    gw_free(tmpbuff);
    gw_free(mycopy);
    
    return request;
    
error:
    error(0, "httprequest_wrap: failed");
    httprequest_destroy(request);
    gw_free(mycopy);
    gw_free(tmpbuff);
    return NULL;
}


/*****************************************************************************
 * Convert a HTTPRequest structure to data writable to a socket
 */
char* httprequest_unwrap(HTTPRequest *request) {
    
    char *method_used = NULL, *tmpbuff = NULL, *tmpline = NULL, *bigbuff = NULL, *finalbuff = NULL;
    HTTPHeader *header = NULL;
    
    if(request==NULL) goto error; /* PEBKaC */
    
    bigbuff = gw_malloc(16*1024);
    tmpbuff = gw_malloc(1024);
    tmpline = gw_malloc(1024);
    
    memset(bigbuff, 0, 16*1024);
    memset(tmpbuff, 0, 1024);
    memset(tmpline, 0,  1024);
    
    if(request->action == HTTP_SERVER)
	strcat(bigbuff, "HTTP/1.1 200 OK\r\n");
    else if(request->action == HTTP_CLIENT){
	
	switch(request->method_type){
	    
	case GET:
	    method_used = "GET";
	    if(request->url->query != NULL) 
		sprintf(tmpbuff,
			"%s?%s", request->url->abs_path, request->url->query);
	    else
		sprintf(tmpbuff, "%s", request->url->abs_path);
	    break;
	    
	case POST:
	    method_used = "POST";
	    sprintf(tmpbuff, "%s", request->url->abs_path);
	    break;
	    
	default:
	    error(errno, "http_request_unwrap: method_type not defined");
	    goto error;
	}
	
	sprintf(tmpline, "%s %s HTTP/1.1\r\n", method_used, tmpbuff);
	strcat(bigbuff, tmpline);
    }
    
/* write the http headers out */
    header = request->baseheader;
    for(;;) {
	if(header == NULL) break;
	sprintf(tmpline, "%s: %s\r\n", header->key, header->value);
	strcat(bigbuff, tmpline);
	header = header->next;
    }
    
/* terminate headers */
    strcat(bigbuff, "\r\n");
    
    debug("gwlib.http", 0,
	  "request_unwrap: preparing to put entity: <%s>", request->data);
    
/* data */
    if(request->data != NULL) 
	strcat(bigbuff, request->data);
    
    /* done */
    finalbuff = gw_strdup(bigbuff);
    gw_free(bigbuff);
    gw_free(tmpbuff);
    gw_free(tmpline);
    return finalbuff;
    
    
error:
    debug("gwlib.http", errno, "httprequest_unwrap: failed");
    gw_free(bigbuff);
    gw_free(tmpbuff);
    gw_free(tmpline);
    return NULL;
}


/*****************************************************************************
 * For client use. Does TCP connection and returns whatever the server sez.
 */

HTTPRequest* httprequest_execute(HTTPRequest *request) {
    
    char *datareceive = NULL;
    char *datasend = NULL;
    int s = -1;
    size_t size = 0;
    HTTPRequest *result = NULL;
    
    if(request==NULL) goto error; /* PEBKaC */
    
/* prepare data to be sent to server */
    request->action = HTTP_CLIENT;
    datasend = httprequest_unwrap(request);
    if(datasend==NULL){
	gw_free(datasend);
	goto error;
    }
    
/* open socket */
    s = tcpip_connect_to_server(request->url->host, request->url->port);
    if (s == -1) {
	warning(0, "Couldn't open socket.");
	gw_free(datasend);
	goto error;
    }
    
/* write data to socket */
    if (write_to_socket(s, datasend) == -1) {
	warning(0, "Couldn't write to socket.");
	gw_free(datasend);
	goto close_socket;
    }
    gw_free(datasend);
    
/* make socket readonly */
    if (shutdown(s, 1) == -1) {
	error(errno, "Error closing writing end of socket.");
	goto close_socket;
    }
    
/* read from socket */
    if (read_to_eof(s, &datareceive, &size) == -1) {
	warning(errno, "Couldn't read from socket.");
	gw_free(datareceive);
	goto close_socket;
    }
    
/* close socket */
    if (close(s) == -1) {
	error(errno, "Error closing connection to HTTP server.");
	gw_free(datareceive);
	goto error;
    }

/* parse the results */
    result = httprequest_wrap(datareceive, size);
    gw_free(datareceive);
    /* assert(result != NULL); */
    
/* return the result */	
    return result;
    


    
close_socket:
    /* close socket */
    if (close(s) == -1) 
	error(errno, "Closing connection to an HTTP server failed.");
    goto error;
    
error:
    warning(0, "httprequest_execute: failed");
    return NULL;   
}




/*****************************************************************************
 * Add a HTTPHeader to the linked list HTTPRequst structure.
 */
int httprequest_add_header(HTTPRequest *request, char *key, char *value) {
    
    HTTPHeader *hdr = NULL;
    
    if(request == NULL || key == NULL || value == NULL){
	error(errno, "httprequest_add_header: Bad input.");
	return -1;
    }
    hdr = request->baseheader;
    while(hdr != NULL)
	hdr = hdr->next;

    hdr = header_create(key, value);
    assert(hdr!=NULL);
    return 0;
}

/*****************************************************************************
 * httprequest_replace_header - replace the existing header and return 0. If
 * header doesn't exist create it. Return -1 for errors.
 */
int httprequest_replace_header(HTTPRequest *request, char *key, char *value)
{
    unsigned char *old = NULL;
    HTTPHeader *hdr = NULL;
    
    if(request == NULL || key == NULL || value == NULL){
	error(0, "httprequest_replace_header: Bad input.");
	return -1;
    }
    hdr = request->baseheader;
    while(hdr != NULL ){
	if(!(strcasecmp(hdr->key, key))){
	    /* strcasecmp return 0 for match */
	    old = hdr->value;
	    hdr->value = gw_strdup(value);
	    gw_free(old);
	    return 0;
       	}
	hdr = hdr->next;
    }
    hdr = header_create(key, value);
    assert(hdr!=NULL);
    return 0;
}




/**********************************************
 * header-functions
 */
HTTPHeader *header_create(char *key, char *value) {
	HTTPHeader *h;
	
	h = gw_malloc(sizeof(HTTPHeader));
	if(h == NULL)return NULL;
	h->key = gw_strdup(key);
	h->value = gw_strdup(value);
	h->next = NULL;
	return h;
}

/************/

void header_dump(HTTPHeader *hdr)
{
    while(hdr != NULL) {
	debug("gwlib.http", 0, "%s: %s", hdr->key, hdr->value);
	hdr = hdr->next;
    }
}


/************/

int header_destroy(HTTPHeader *hdr)
{
    HTTPHeader *ptr;
    while(hdr != NULL) {
	ptr = hdr;
	hdr = hdr->next;
	gw_free(ptr->key);
	gw_free(ptr->value);
	gw_free(ptr);
    }
    return 0;
}


/*
 * header_pack
 */

int header_pack(HTTPHeader *hdr)
{
    HTTPHeader *ptr = NULL, *prev = NULL;
    char *buf = NULL;
    int ret = 0;
    size_t len = 0;
    
    while(hdr != NULL) {
	/* find identical headers and merge them */
	for(prev = hdr, ptr = hdr->next; ptr != NULL; ptr = ptr->next) {
	    if (strcasecmp(hdr->key, ptr->key)==0) {
		len = strlen(hdr->value) + 2 + strlen(ptr->value); 
		buf = gw_malloc(len);
		ret = sprintf(buf, "%s, %s", hdr->value, ptr->value);
		debug("gwlib.http",0,"header_pack: ret:<%d> len:<%d>",
		      ret, len);
		if(ret < len) return -1;
		gw_free(hdr->value);
		hdr->value = gw_strdup(buf);
		gw_free(buf);
		prev->next = ptr->next;
		gw_free(ptr->key);
		gw_free(ptr->value);
		gw_free(ptr);
		
		ptr = prev;     /* rewind */
	    }
	    else
		prev = ptr;
	}	    
	hdr = hdr->next;
    }
    return 0;
}



/*
 * httprequest_remove_header - remove header from http request. Search for
 * multiple occurrences.  Return 0 for ok or if header not found, -1 for
 * errors. 
 */
int httprequest_remove_header(HTTPRequest *request, char *key) {

    HTTPHeader *hdr = NULL, *prev = NULL;

    if(request==NULL || key==NULL){
	error(0, "httprequest_remove_header: Bad input.");
	return -1;
    }
    hdr = request->baseheader;
    while(hdr!=NULL){
	if(!(strcasecmp(hdr->key, key))){
	    /* strcasecmp return 0 for match */
	    prev->next = hdr->next;
	    gw_free(hdr->key);
	    gw_free(hdr->value);
	    gw_free(hdr);
	    hdr = prev->next;
	}
	prev = hdr;
	hdr = hdr->next;
    }
    return 0;
}


/*
 * httprequets_get_header_value - find the given header and return the value.
 * If header doesn't exist or function fails return NULL.
 */
char* httprequest_get_header_value(HTTPRequest *request, char *key) {

    HTTPHeader *hdr = NULL;

    if(request==NULL || key==NULL){
	error(0, "httprequest_get_header_value: Bad input.");
	return NULL;
    }
    hdr = request->baseheader;
    while(hdr!=NULL){
	if(!(strcasecmp(hdr->key, key)))
	    /* strcasecmp return 0 for match */
	    return hdr->value;
	hdr = hdr->next;
    }
    return NULL;
}



/*
 * internal_base6t4 - encode character string to base 64. Return the
 * encoded string.
 */

static unsigned char *internal_base6t4(unsigned char *pass) {

    int ictr,ctr2;
    unsigned char onec, *result, temp[10];
    long twentyfour;
    unsigned char base64[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    result = gw_malloc(strlen(pass) + strlen(pass) / 4 + strlen(pass) % 4 + 5);
    result[0] = 0;

    twentyfour = 0;
    for (ictr=0;ictr<strlen(pass);ictr++) {
	switch(ictr%3) {
	case 0:
	    twentyfour=0l;
	    twentyfour+=(long)pass[ictr] * 256l * 256l;
	    break;
	case 1:
	    twentyfour+=(long)pass[ictr] * 256l;
	    break;
	case 2:
	    twentyfour+=(long)pass[ictr];
	}
	if (ictr%3 == 2) {
	    for(ctr2=3;ctr2>=0;ctr2--) {
		onec = base64[(int)(twentyfour /
				    round_to_closest_integer(pow(64,ctr2)))];
		sprintf(temp, "%c", onec);
		strcat(result, temp);
		twentyfour = twentyfour %
		    (long)round_to_closest_integer(pow(64,ctr2));
	    } 
	}
    }
    switch (ictr%3) {
    case 1:
	for(ctr2=3;ctr2>1;ctr2--) {
	    onec = base64[(int)(twentyfour /
				(long)round_to_closest_integer(pow(64,ctr2)))];
	    sprintf(temp, "%c", onec);
	    strcat(result, temp);
	    twentyfour = twentyfour %
		(long)round_to_closest_integer(pow(64,ctr2));
	}
	strcat(result,"==");
	break;
    case 2:
	for(ctr2=3;ctr2>0;ctr2--) {
	    onec = base64[(int)(twentyfour /
				(long)round_to_closest_integer(pow(64,ctr2)))];
	    sprintf(temp, "%c", onec);
	    strcat(result, temp);
	    twentyfour = twentyfour %
		(long)round_to_closest_integer(pow(64,ctr2));
	}
	strcat(result,"=");
    default:
	break;
    }
    
    return result;
}


/*
 * rint - a new implementation for porting kannel to alpha.
 */
static double round_to_closest_integer(double x)
{
    int int_x = x;
    
    if((x - int_x) < 0.5) 
	return int_x;
    else
	return (int) x+1;
}
