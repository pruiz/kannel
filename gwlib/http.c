/*****************************************************************************
* http.c - The implementation of the HTTP subsystem.
* Mikael Gueck (mikael.gueck@wapit.com) for WapIT Ltd.
*/


#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/param.h>

#include "gwlib.h"


/*****************************************************************************
* Static Functions
*/

static char *internal_base6t4(char *pass);

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
* Accept a HTTP connection, analyze if and return results
*/
int httpserver_get_request(int socket, char **client_ip, char **path, char **args) {

	socklen_t len;
	struct sockaddr_in cliaddr;
	int connfd = 0;
	char *accept_ip;
	int   accept_port;

	char *eol = NULL, *ptr = NULL;
	int done_with_looping = 0;
	int done_with_status = 0, tmpint = 0;
	
	char *growingbuff = NULL, *newbuff = NULL;
	int gbsize = 0, readthisfar = 0;

	URL *url = NULL;

	/* accept the connection */
	len = sizeof(cliaddr);
	connfd = accept(socket, (struct sockaddr *) &cliaddr, &len);
	if(connfd==-1) {
		error(errno, "could not accept connection to HTTP server socket");
		goto error;
	}

	if(gw_getnameinfo(&cliaddr, &accept_ip, &accept_port)==-1) goto error;
	*client_ip = accept_ip;

	gbsize = 1024;
	growingbuff = malloc(gbsize);
	if(growingbuff==NULL) {
		error(errno, "memory allocation error");
		goto error;
	}
	
	bzero(growingbuff, gbsize);

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
				newbuff = strdup(growingbuff);
				sscanf(growingbuff, "GET %s HTTP/%i.%i", 
					newbuff, &tmpint, &tmpint);
				url = internal_url_create(newbuff);
				free(newbuff);
				*eol = '\r';
				done_with_status = 1;
				done_with_looping = 1;
				continue;
			}
			
		} /* if */

		/* see if the buffer needs enlargement */
		
	} /* for */

	if(url->abs_path != NULL)
		*path = strdup(url->abs_path);
	else
		*path = NULL;

	if(url->query != NULL)
		*args = strdup(url->query);
	else
		*args = NULL;

	internal_url_destroy(url);
	free(growingbuff);
	return connfd;

error:
	free(growingbuff);
	return -1;

}

int httpserver_answer(int socket, char *text) {

	char *bigbuff = NULL;
	int tmpint = 0;
	size_t bufsize = 0;

	if( (socket < 0) || (text == NULL) )
		goto error;

	bufsize = strlen(text) + 1024;
	bigbuff = malloc(bufsize);

	if(bigbuff == NULL) {
		error(errno, "memory allocation error");
		goto error;
	}

	sprintf(bigbuff, "HTTP/1.1 200 OK\r\nContent-Type:text/html\r\nContent-Length: %i\r\n\r\n", strlen(text));
	strcat(bigbuff, text);

	tmpint = write_to_socket(socket, bigbuff);

	if(tmpint == -1) {
		error(errno, "error sending to socket");
		goto error;
	}

	free(bigbuff);
	return close(socket);
	
error:
	free(bigbuff);
	return -1;
	
}

/*****************************************************************************
* HTTP client GET
*/
int http_get(char *urltext, char **type, char **data, size_t *size) {

	URL *url = NULL;
	HTTPRequest *request = NULL, *response = NULL;
	char *ptr = NULL;
	char *authorization = NULL;
	int how_many_moves = 0;

/*	debug(0, "http_get: starting"); */

/*	*data = NULL; */

	/* Create the request */
	url = internal_url_create(urltext);
	if(url==NULL) {
		error(errno, "http_get: creating URL failed");
		goto error;
	}

	request = httprequest_create(url, NULL);
	internal_url_destroy(url);
	if(request==NULL) {
		error(errno, "http_get: creating HTTPRequest failed");
		goto error;
	}

/*
	info(0, "url->scheme   = <%s>", request->url->scheme);
	info(0, "url->host     = <%s>", request->url->host);
	info(0, "url->port     = <%i>", request->url->port);
	info(0, "url->abs_path = <%s>", request->url->abs_path);
	info(0, "url->username = <%s>", request->url->username);
	info(0, "url->password = <%s>", request->url->password);
*/
	
	httprequest_add_header(request, "Host", request->url->host);
	httprequest_add_header(request, "Connection", "close");
	httprequest_add_header(request, "User-Agent", "Mozilla/2.0 (compatible; Open Source WAP Gateway)");
	if(request->url->username!=NULL) {
		ptr = malloc(1024);
		if(ptr==NULL) goto error;
		sprintf(ptr, "%s:%s", 
			request->url->username, request->url->password);
		authorization = internal_base6t4(ptr);
		if(authorization==NULL) goto error;
		sprintf(ptr, "Basic %s", authorization);
		httprequest_add_header(request, "Authorization", ptr);
		free(authorization);
		free(ptr);
	}

	for(;;) {

		/* Open connection, send request, get response. */
		response = httprequest_execute(request);
		if(response == NULL) goto error;

		/* Great, the server responded "200 OK" */
		if(response->status == 200) break;

		/* We are redirected to another URL */
		else if( (response->status == 301) || 
			 (response->status == 302) || 
			 (response->status == 303) ) 
		{

			url = internal_url_create(httprequest_get_header_value(response, "Location"));
			if(url==NULL) goto error;
			httprequest_destroy(request);
			httprequest_destroy(response);

			/* Abort if we feel like we might be pushed around in 
			   an endless loop. 10 is just an arbitrary number. */
			if(how_many_moves > 10) {
				error(0, "http_get: too many redirects");
				goto error;
			}
			how_many_moves++;
			
			request = httprequest_create(url, NULL);
			internal_url_destroy(url);
			if(request==NULL) goto error;
			httprequest_add_header(request, "Host", request->url->host);
			httprequest_add_header(request, "Connection", "close");
			httprequest_add_header(request, "User-Agent", "Mozilla/2.0 (compatible; Open Source WAP Gateway)");
			
		}

		/* Server returned "404 Authorization Required" */
		else if(response->status == 401) break;

		/* Server returned "404 Not Found" */
		else if(response->status == 404) break;
		
		/* Server returned "501 Not Implemented" */
		else if(response->status == 501) break;

		/* If we haven't handled the response code this far, abort. */
		else break;
		
	}

	/* Check the content of the "Content-Type" header. */
	ptr = httprequest_get_header_value(response, "Content-Type");
	if(ptr != NULL) {
		*type = strdup(ptr);
	} else {
		*type = NULL;
	}

	/* Fill in the variables which we return. */
	if( (response->data != NULL) && (response->data_length > 0) ) {
		*data = malloc(response->data_length+1);
		if(*data==NULL) goto error;
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
	error(errno, "http_get: failed");
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
		debug(0, "url_create: someone just called me with NULL input");
		goto error;
	}

	/* make a copy of the input string for myself */
	mycopy = strdup(text);
	if(mycopy == NULL) {
		goto error;
	}
	
	tmpURL = malloc(sizeof(struct URL));
	if(tmpURL == NULL) {
		goto error;
	}
	bzero(tmpURL, sizeof(struct URL));

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
		tmpURL->query = strdup(query_start+1);
		if(tmpURL->query==NULL) {
			error(errno, "url_create: memory allocation error");
			goto error;
		}
		*query_start = '\0';
	}

	/* Now see if we're using HTTP or FTP or gopher... */
	/* before: "scheme://username:password@server:port/abs_path" */
	/* after: "username:password@server:port/abs_path" */
	scheme_end = strstr(mycopy, "://");
	if (scheme_end != NULL) { /* awrighty! */
		scheme_start = mycopy;
		*scheme_end   = '\0';
		tmpURL->scheme = strdup(scheme_start);
		if(tmpURL->scheme==NULL) {
			error(errno, "url_create: memory allocation error");
			goto error;
		}
		host_start = scheme_end + 3;
	} else { /* no scheme found, default to http */
		tmpURL->scheme = strdup("http");
		if(tmpURL->scheme==NULL) {
			error(errno, "url_create: memory allocation error");
			goto error;
		}
		host_start = mycopy;
	}

	/* cut of the abs_path - stuff after and including the first / */
	/* before: "username:password@server:port/abs_path" */
	/* after: "username:password@server:port" */
	abs_path_start = strchr(host_start, '/');
	if(abs_path_start != NULL) { /* the hostname is followed by path */
		tmpURL->abs_path = strdup(abs_path_start);
		if(tmpURL->abs_path==NULL) {
			error(errno, "url_create: memory allocation error");
			goto error;
		}
		*abs_path_start = '\0';
	} else { /* default per RFC2616/3.2.2 */
		tmpURL->abs_path = strdup("/");
		if(tmpURL->abs_path==NULL) {
			error(errno, "url_create: memory allocation error");
			goto error;
		}
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
				tmpURL->password = strdup(password_start);
			}

			if( (username_start != NULL) && (username_end!=NULL) ) {
				*username_end = '\0';
				tmpURL->username = strdup(username_start);
			}


			host_start = password_end + 1;
		} else {
			host_start = username_end + 1;
		}
	}

	/* before: "server" */
	/* after: "" */
	tmpURL->host = strdup(host_start);
	if(tmpURL->host==NULL) {
		error(errno, "url_create: memory allocation error");
		goto error;
	}

	free(mycopy);
	return tmpURL;

error:
	debug(errno, "url_create: failed");
	free(mycopy);
	internal_url_destroy(tmpURL);
	return NULL;
}


/*********************************************************************
* Destroy a URL structure
*/
int internal_url_destroy(URL *url) {

	if(url==NULL) return -1;

	free(url->username);
	free(url->password);
	free(url->scheme);
	free(url->host);
	free(url->abs_path);
	free(url->query);
	free(url);

	return 0;

}


/*********************************************************************
* Create an exact duplicate of an URL structure
*/
URL* url_duplicate(URL* url) {

	URL *newurl = NULL;

	newurl = malloc(sizeof(struct URL));
	if(newurl == NULL) {
		error(errno, "url_duplicate: memory allocation error");
		goto error;
	}
	bzero(newurl, sizeof(struct URL));

	if(url->username != NULL) {
		newurl->username = strdup(url->username);
		if(newurl->username == NULL) goto mallocerror;
	}

	if(url->password != NULL) {
		newurl->password = strdup(url->password);
		if(newurl->password == NULL) goto mallocerror;
	}

	if(url->scheme != NULL) {
		newurl->scheme = strdup(url->scheme);
		if(newurl->scheme == NULL) goto mallocerror;
	}

	if(url->host != NULL) {
		newurl->host = strdup(url->host);
		if(newurl->host == NULL) goto mallocerror;
	}

	if(url->abs_path != NULL) {
		newurl->abs_path = strdup(url->abs_path);
		if(newurl->abs_path == NULL) goto mallocerror;
	}

	if(url->query != NULL) {
		newurl->query = strdup(url->query);
		if(newurl->query == NULL) goto mallocerror;
	}

	newurl->port = url->port;

	return newurl;

mallocerror:
	error(errno, "url_duplicate: memory allocation error");
	internal_url_destroy(newurl);
	return NULL;

error:
	return NULL;
}

URL* internal_url_relative_to_absolute(URL* baseURL, char *relativepath) {

	return NULL;
}

/*****************************************************************************
* Create a raw request from a URL
*/
HTTPRequest* httprequest_create(URL *url, char *payload) {

	HTTPRequest *request = NULL;

	/* initialize the variable to be returned */
	request = malloc(sizeof(struct HTTPRequest));
	if(request==NULL) {
		error(errno, "httprequest_create: memory allocation error");
		return NULL;
	}
	bzero(request, sizeof(struct HTTPRequest));

	if(url == NULL) {
		request->url = NULL;
	} else {
		request->url = url_duplicate(url);
		if(request->url == NULL) goto mallocerror;
	}

	if(payload == NULL) {
		request->data = NULL;
	} else {
		request->data = strdup(payload);
		if(request->data == NULL) goto mallocerror;
	}

	request->baseheader = NULL;

	return request;

mallocerror:
	httprequest_destroy(request);
	return NULL;
}


/*****************************************************************************
* Free a HTTPRequest structure
*/
int httprequest_destroy(HTTPRequest *request) {

	HTTPHeader *thisheader = NULL, *nextheader = NULL;

	if(request==NULL) {
		return 0;
	}

	if(request->url != NULL)	
		internal_url_destroy(request->url);

	/* free headers */
	thisheader = request->baseheader;
	for(;;) {
		if(thisheader==NULL) break;
		nextheader = thisheader->next;
		free(thisheader->key);
		free(thisheader->value);
		free(thisheader);
		thisheader = nextheader;
	}
	
	/* free data */
	free(request->data);

	/* free the structure */
	free(request);
	
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
		error(errno, "httprequest_wrap: faulty input");
		goto error;
	}

	mycopy = malloc(size);
	if(mycopy == NULL) goto error;
	memcpy(mycopy, from, size);

	request = malloc(sizeof(HTTPRequest));
	tmpbuff = malloc(10*1024);

	if( (mycopy==NULL) || (request==NULL) || (tmpbuff==NULL) ) {
		error(errno, "httprequest_wrap: memory allocation error 1");
		goto error;
	}

	bzero(request, sizeof(request));
	bzero(tmpbuff, 10*1024);

	request->url = NULL;
	request->baseheader = NULL;
	request->data = NULL;

	/* parse the status line */
	eol = strstr(mycopy, "\r\n");
	if(eol==NULL) { /* This ain't HTTP, mate */
		error(0, "httprequest_wrap: no HTTP header found");
		goto error;
	}
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

		if(ptr==eol) {    /* found "\r\n\r\n" - end of headers */
			ptr += 2; /* advance to start of data */
			break;
		}

		if(eol != NULL) *eol = '\0';
		if(midptr != NULL) *midptr = '\0';
		midptr++;
		while(isspace(*midptr)) midptr++;
		if( (eol!=NULL) && (midptr!=NULL) )
			httprequest_add_header(request, ptr, midptr);
	}

	request->data_length = size - ((int)ptr - (int)mycopy);

	/* The data might be chucked according to RFC2616/3.6.1, and in
	   practise quite often is. We detect this by checking the value of
	   the "Transfer-Encoding" header. */
	midptr = httprequest_get_header_value(request, "Transfer-Encoding");
	if(midptr == NULL) {

		if(httprequest_get_header_value(request, "Content-Length") != NULL)
			request->data_length = atoi(httprequest_get_header_value(request, "Content-Length"));

		/* No need to check Content-Length, just read it all in. */
		request->data = malloc(request->data_length+1);
		if(request->data==NULL) {
			error(errno, 
				"httprequest_wrap: memory allocation error 2");
			goto error;
		}
		bzero(request->data, request->data_length);
		memcpy(request->data, ptr, request->data_length);
		request->data[request->data_length] = '\0';

	} else if(strstr(midptr, "chunked") != NULL) {

		/* Get enough space to hold all the data. */
		request->data = malloc(request->data_length);
		if(request->data==NULL) {
			error(errno,
				"httprequest_wrap: memory allocation error");
			goto error;
		}
		bzero(request->data, request->data_length);

		/* Convert RFC2616/3.6.1 chunked data to normal. */
		for(;;) {

			/* Get the chunk size. */
			tmpint = strtol(ptr, NULL, 16);
			if( (tmpint2+tmpint) > size) {
				error(0, "httprequest_wrap: chunk size too big");
				break;
			}

			if(tmpint == 0) break;
			midptr = strstr(ptr, "\r\n");
			if(midptr == NULL) break;
			memcpy(request->data+tmpint2, midptr+2, tmpint);
			ptr = midptr + 2 /* CRLF */ + tmpint + 2 /* CRLF */;
			tmpint2 += tmpint;

		} /* for endless loop */

		/* Get the real data size */
		request->data_length = tmpint2;
		
		/* Shrink the buffer to the real size */
		request->data = realloc(request->data, request->data_length+1);
		if(request->data == NULL) goto error;
		request->data[request->data_length] = '\0';

	} else { /* Transfer-Encoding set to an unknown value */
		/* Someone at the server end seems to have botched
		   the job of implementing HTTP/1.1. 
		   The "Transfer-Encoding" header should hold 
		   either "chunked" or NULL. */
		   error(0, "Broken HTTP implementation on the server side.");
	}
	
	/* done */
	free(tmpbuff);
	free(mycopy);

	return request;

error:
	debug(errno, "httprequest_wrap: failed");
	httprequest_destroy(request);
	free(mycopy);
	free(tmpbuff);
	return NULL;
}

/*****************************************************************************
* Convert a HTTPRequest structure to data writable to a socket
*/
char* httprequest_unwrap(HTTPRequest *request) {

	char *tmpbuff = NULL, *tmpline = NULL, *bigbuff = NULL, *finalbuff = NULL;
	HTTPHeader *header = NULL;

	if(request==NULL) goto error; /* PEBKaC */

	bigbuff = malloc(16*1024);
	tmpbuff = malloc(1024);
	tmpline = malloc(1024);

	if( (bigbuff==NULL) || (tmpbuff==NULL) || (tmpline==NULL) ) {
		error(errno, "httprequest_unwrap: memory allocation error");
		goto error;
	}

	bzero(bigbuff, 16*1024);
	bzero(tmpbuff, 1024);
	bzero(tmpline, 1024);

	if(request->action == HTTP_SERVER) {

		strcat(bigbuff, "HTTP/1.1 200 OK\r\n");
	
	} else {

		/* the first GET line */
		if(request->url->query != NULL) {
			sprintf(tmpbuff, "%s?%s", 
				request->url->abs_path, request->url->query);
		} else {
			sprintf(tmpbuff, "%s", request->url->abs_path);
		}
		sprintf(tmpline, "GET %s HTTP/1.1\r\n", tmpbuff);
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

	/* data */
	if(request->data != NULL) {
		strcat(bigbuff, request->data);
	}

	/* done */
	finalbuff = strdup(bigbuff);
	if(finalbuff==NULL) {
		error(errno, "httprequest_unwrap: memory allocation error");
		goto error;
	}
	free(bigbuff);
	free(tmpbuff);
	free(tmpline);

	return finalbuff;

error:
	debug(errno, "httprequest_unwrap: failed");
	free(bigbuff);
	free(tmpbuff);
	free(tmpline);
	return NULL;
}


/*****************************************************************************
* Does TCP and returns whatever the server sez
*/
HTTPRequest* httprequest_execute(HTTPRequest *request) {

	char *datasend = NULL, *datareceive = NULL;
	int s = -1;
	size_t size = 0;
	HTTPRequest *result = NULL;

	if(request==NULL) goto error; /* PEBKaC */

	/* prepare data to be sent to server */
	request->action = HTTP_CLIENT;
	datasend = httprequest_unwrap(request);
	if(datasend==NULL) goto error;

	/* open socket */
	s = tcpip_connect_to_server(request->url->host, request->url->port);
	if (s == -1) {
		error(errno, "Couldn't HTTP connect to <%s>", request->url->host);
		goto error;
	}

	/* write data to socket */
	if (write_to_socket(s, datasend) == -1) {
		error(errno, "Error writing request to server");
		goto error;
	}

	/* make socket readonly */
	if (shutdown(s, 1) == -1) {
		error(errno, "Error closing writing end of socket");
		goto error;
	}

	/* read from socket */
	if (read_to_eof(s, &datareceive, &size) == -1) {
		error(errno, "Error receiving data from HTTP server");
		goto error;
	}

	/* close socket */
	if (close(s) == -1) {
		error(errno, "Error closing connection to HTTP server");
		goto error;
	}

	/* parse the results */
	result = httprequest_wrap(datareceive, size);

	/* return the result */	
	free(datasend);
	free(datareceive);
	return result;

error:
	error(errno, "httprequest_execute: failed");
	close(s);
	free(datasend);
	free(datareceive);
	return NULL;
}


/*****************************************************************************
* Add a HTTPHeader to the linked list HTTPRequst structure.
*/
int httprequest_add_header(HTTPRequest *request, char *key, char *value) {

	HTTPHeader *thisheader = NULL, *prev = NULL;

	if( (request == NULL) || (key == NULL) || (value == NULL) )
		goto error; /* PEBKaC */

	thisheader = request->baseheader;

	if(thisheader == NULL) {

		thisheader = malloc(sizeof(HTTPHeader));
		if(thisheader==NULL) goto error;
		
		thisheader->key = strdup(key);
		if(thisheader->key==NULL) goto error;
		
		thisheader->value = strdup(value);
		if(thisheader->value==NULL) goto error;
		
		thisheader->next = NULL;
		request->baseheader = thisheader;

		return 1;

	}

	/* Let's just hope that nobody has created a circular loop... */
	for(;;) {

		if(thisheader == NULL) {

			thisheader = malloc(sizeof(HTTPHeader));
			if(thisheader==NULL) goto error;

			thisheader->key = strdup(key);
			if(thisheader->key==NULL) goto error;

			thisheader->value = strdup(value);
			if(thisheader->value==NULL) goto error;

			thisheader->next = NULL;
			prev->next = thisheader;

			return 1;

		} /* if */

		prev = thisheader;
		thisheader = prev->next;

	} /* for */

	return 1;

error:
	error(errno, "httprequest_add_header: failed");
	return -1;
}
	
int httprequest_remove_header(HTTPRequest *request, char *key) {

	HTTPHeader *thisheader = NULL;

	if( (request==NULL) || (key==NULL) )
		goto error; /* PEBKaC */

	thisheader = request->baseheader;

	/* people who create circular loops don't deserve a paddle... */
	for(;;) {
		if(thisheader == NULL) /* end of chain */
			return -1;
			
		if(strcasecmp(thisheader->key, key) == 0) {
			/* XXX REMOVE IT XXX */
			error(0, "function not implemented yet");
			return 0;
		}

		thisheader = thisheader->next;
	}
	

error:
	error(errno, "httprequest_add_header: failed");
	return -1;
}

/*
 * This value must not be meddled with.
 */
char* httprequest_get_header_value(HTTPRequest *request, char *key) {

	HTTPHeader *thisheader = NULL;

	if( (request==NULL) || (key==NULL) )
		goto error; /* PEBKaC */

	thisheader = request->baseheader;

	/* people who create circular loops don't deserve a paddle... */
	for(;;) {
		if(thisheader == NULL) /* end of chain */
			return NULL;
			
		if(strcasecmp(thisheader->key, key) == 0)
			return thisheader->value;

		thisheader = thisheader->next;
	}
	

error:
	debug(errno, "httprequest_add_header: failed");
	return NULL;
}

static char *internal_base6t4(char *pass) {

	int ictr,ctr2;
	char onec,*result, temp[10];
	long twentyfour;
	char base64[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	result = malloc(strlen(pass) + strlen(pass) / 4 + strlen(pass) % 4 + 5);
	if(result==NULL) goto error;
	result[0] = 0;

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
				onec = base64[(int)(twentyfour / rint(pow(64,ctr2)))];
				sprintf(temp, "%c", onec);
				strcat(result, temp);
				twentyfour = twentyfour % (long)rint(pow(64,ctr2));
			} 
		}
	}
	
	switch (ictr%3) {
		case 1:
			for(ctr2=3;ctr2>1;ctr2--) {
				onec = base64[(int)(twentyfour / (long)rint(pow(64,ctr2)))];
				sprintf(temp, "%c", onec);
				strcat(result, temp);
				twentyfour = twentyfour % (long)rint(pow(64,ctr2));
			}
			strcat(result,"==");
			break;
		case 2:
			for(ctr2=3;ctr2>0;ctr2--) {
				onec = base64[(int)(twentyfour / rint(pow(64,ctr2)))];
				sprintf(temp, "%c", onec);
				strcat(result, temp);
				twentyfour = twentyfour % (long)rint(pow(64,ctr2));
			}
			strcat(result,"=");
		default:
			break;
	}

	return result;

error:
	return NULL;	
}

