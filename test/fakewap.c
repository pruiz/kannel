/*
 * fakewap.c - simulate wap clients talking directly to wap gw.
 *
 * This module can be built also in Windows where you should
 * add unzipped ".\wininc" to your include directories.
 *
 * The protocol:
 *
 *
 *    A)    Fakewap -> Gateway
 *
 *        WTP: Invoke PDU
 *        WSP: Connect PDU
 *
 *    B)    Gateway -> Fakewap
 *
 *        WTP: Result PDU
 *        WSP: ConnectReply PDU
 *
 *    C)    Fakewap -> Gateway
 *
 *        WTP: Ack PDU
 *
 *    D)    Fakewap -> Gateway
 *
 *        WTP: Invoke PDU
 *        WSP: Get PDU (data: URL)
 *
 *    E)    Gateway -> Fakewap
 *
 *        WTP: Result PDU (data: WML page)
 *        WSP: Reply PDU
 *
 *    F)    Fakewap -> Gateway
 *
 *        WTP: Ack PDU
 *
 *    G)    Fakewap -> Gateway
 *
 *        WTP: Invoke PDU
 *        WSP: Disconnect PDU
 *
 *
 *    Packets A-C open a WAP session. Packets D-F fetch a WML page.
 *    Packet G closes the session.
 *
 * The test terminates when all packets have been sent.
 *
 *
 * Antti Saarenheimo for WapIT Ltd.
 */

#define MAX_SEND (0)

static char usage[] =
"Usage: \n\
fakewap [-v] <my port> <host> <port> <max> <interval> <thrds> <version> <pdu_type> <tcl> <tid_new> <tid_increase> <url1> <url2>... \n\\n\
where [-v] enables optional verbose mode, \n\
<my port> is the first port used in this machine, each thread has own port\n\
<host> and <port> is the host and the port to connect to, \n\
<max> is the maximum number of messages to send (0 means infinitum), \n\
<interval>, is the interval in seconds (floating point allowed), \n\
between automatically generated messages,\n\
<thrds> is the number of simultaneous client sessions,\n\
<version> protocol version field, as an integer,\n\
<pdu_type> pdu type, as an integer,\n\
<tcl> transaction class, as an integer, \n\
<tid_new> means that tid_new flag is set. This will force clearing of \n\
tid cache of the responder, \n\
<tid_increase> the difference between two tids,\n\
<url> is the url to be requested. If there are several urls, they are \n\
sent in random order.\n\
\n\
For example: fakewap -v 10008 my_host 9201 10 0 1 0 1 2 0 1 http://www.wapit.com/~liw/hello.wml\n";

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/param.h>
#include <math.h>

#ifdef _WINDOWS_
#undef DEBUG
#undef ERROR
#undef PANIC
#undef WARNING
#undef INFO
#else
typedef int SOCKET;
#endif


#include "gwlib.h"

#define GET_WTP_PDU_TYPE(hdr)  (hdr[0] >> 3)

#define WTP_PDU_INVOKE  1
#define WTP_PDU_RESULT  2
#define WTP_PDU_ACK     3
#define WTP_PDU_ABORT   4

/*
**  Common parameters
*/
char **urls;
int num_urls;
char * hostname;
double interval;
unsigned short port;
int max_send;
unsigned short tid_addition; 
Mutex *mutex;
int threads = 1;
int num_sent = 0;
time_t start_time, end_time;
double totaltime = 0, besttime = 1000000L,  worsttime = 0;
int verbose = 0;

/*
 * PDU type, version number and transaction class are supplied by a command line argument.
 */
unsigned char WSP_Connect[] = {0x06, 0x00, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00 };
unsigned char WSP_ConnectReply[] = {0x16, 0x80, 0x00, 0x02 };
unsigned char WTP_Ack[] =          {0x18, 0x00, 0x00 };
unsigned char WTP_TidVe[] =        {0x1C, 0x00, 0x00};
unsigned char WTP_Abort[] =        {0x20, 0x00, 0x00, 0x00 };
unsigned char WSP_Get[] =          {0x0E, 0x00, 0x00, 0x02, 0x40 };
unsigned char WSP_Reply[] =        {0x16, 0x80, 0x00, 0x04, 0x20, 0x01, 0x94 };
unsigned char WSP_Disconnect[] =   {0x0E, 0x00, 0x00, 0x00, 0x05 };

/*
**  In this case it does not matter what is the byte order
*/
#define SET_GTR( hdr ) hdr[0] |= 0x04
#define SET_TID( hdr, tid) hdr[1] |= (0x7f & ((tid) >> 8)); hdr[2] = (char)(tid)
#define GET_TID( hdr ) (((hdr[1] & 0x7f) << 8) + hdr[2])
#define CONSTRUCT_EXPECTED_REPLY_HDR( dest, template, tid ) \
    if (sizeof(dest) < sizeof(template)) panic(0,"buffer overflow.");\
    memcpy( dest, template, sizeof(template));\
    SET_TID( dest, tid )

#ifndef min
#define min(a,b) (a < b ? a : b)
#endif


/* Choose a random message from a table of messages. */
static char *choose_message(char **urls, int num_urls) {
    /* the following doesn't give an even distribution, but who cares */
    return urls[rand() % num_urls];
}
/* returns TID */
static unsigned short get_tid() {
    static unsigned short tid = 0;
    tid += tid_addition;
    tid %= (unsigned short)pow(2,15);
    return tid;
}

/*
**  if -v option has been defined, function prints the trace message and
**  the first bytes in the message header
*/
void print_msg( unsigned short port, const char * trace, unsigned char * msg,
                int msg_len ) {
    int i;
    if (verbose) {
        mutex_lock( mutex );
        printf( "%d: %s (len %d): ", port, trace, msg_len );
        for (i = 0; i < msg_len && i < 16; i++) printf( "%02X ", msg[i] );
        printf( "\n");
        mutex_unlock( mutex );
    }
}

/*
**  Function stores WAP/WSP variable length integer to buffer and returns actual len
*/
int StoreVarInt( unsigned char *buf, unsigned long varInt )
{
    int i, len = 1, non_zero_bits = 7;

    /*
    **    Skip all zero high bits
    */
    while ((varInt >> non_zero_bits) != 0) {
        non_zero_bits += 7;
        len++;
    }
    /*
    **    Read the higest bits first.
    */
    for (i = 0; i < len; i++)
    {
        buf[i] = (unsigned char)(varInt >> (non_zero_bits-7)) & 0x7f;
        non_zero_bits -= 7;
    }
    buf[len-1] &= 0x7f;
    return len;
}


/*
**  Function length of WAP/WSP variable length integer in the buffer
*/
int ReadVarIntLen( const unsigned char *buf )
{
    int    len = 1;

    while (buf[len-1] & 0x80) len++;
    return len;
}


/*
**  Function sends message to WAP GW
*/
int
wap_msg_send( unsigned short port, SOCKET fd, const unsigned char * hdr,
            int hdr_len, const unsigned short * tid, unsigned char * data,
            int data_len )
{
    int ret;
    unsigned char msg[1024*64];
    int msg_len = 0;

    if (hdr != NULL) {
        memcpy( msg, hdr, hdr_len );
        msg_len = hdr_len;
    }
    if (tid != NULL) {
        SET_TID( msg, *tid );
        if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_INVOKE)
        {
            msg[3] |= 0x10; /* request ack every time */
        }
    }
    if (data != NULL) {
        memcpy( msg+msg_len, data, data_len );
        msg_len += data_len;
    }
    ret = send( fd, msg, msg_len, 0 );
    if (ret == -1) {
        error(errno, "Sending to socket failed");
        return -1;
    }
    else if (verbose) {
        print_msg( port, "Sent packet", msg, msg_len );
    }
    return ret;
}

/*
**  Function receives a wap wtl/wsp message. If the headers has been
**  given, it must match with the received message.
**  Return value:
**      >  0 => length of received data
**      == 0 => got acknowlengement or abort but not the expected data
**      < 0  => error,
*/
int
wap_msg_recv( unsigned short port, SOCKET fd, const char * hdr, int hdr_len,
              unsigned short tid, unsigned char * data, int data_len,
              int timeout )
{
    int ret;
    unsigned char msg[1024*64];
    int msg_len = 0;
    struct timeval  tv;
    fd_set readset;
    int    fResponderIsDead = 1;  /* assume this by default */

    /*
    **  Loop until we get the expected response or do timeout
    */
    for (;;)
    {
        if (timeout != 0)
        {
            tv.tv_sec = timeout;
            tv.tv_usec = 0;

            FD_ZERO(&readset);
            FD_SET( fd, &readset);

            ret = select(fd+1, &readset, NULL, NULL, &tv);
            if (ret == -1 || FD_ISSET(fd,&readset)==0) {
                info(0, "Timeout while receiving from socket.\n");
                return fResponderIsDead ? -1 : 0;  /* continue if we got ack? */
            }
        }
        ret = recv( fd, msg, sizeof(msg), 0 );

        if (ret == -1) {
            error(errno, "recv() from socket failed");
            return -1;
        }
        msg_len = ret;

        if (hdr != NULL) {
            /*
            **  Ignore extra header bits, WAP GWs return different values
            */
            if (msg_len >= hdr_len &&
                GET_WTP_PDU_TYPE(msg) == GET_WTP_PDU_TYPE(hdr) &&
                (hdr_len <= 3 || !memcmp( msg+3, hdr+3, hdr_len-3 ))) {
                break;
            }
            /*
            **  Handle TID test, the answer is: Yes, we have an outstanding
            **  transaction with this tid. We must turn on TID_OK-flag, too.
            **  We have a separate tid verification PDU.
            */
            else if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_ACK &&
                     GET_TID(msg) == tid) {
                print_msg( port, "Received tid verification", msg, msg_len );
                wap_msg_send( port, fd, WTP_TidVe, sizeof(WTP_TidVe), &tid,
                              NULL, 0 );
            }
            else if (GET_WTP_PDU_TYPE(msg) == WTP_PDU_ABORT) {
                print_msg( port, "Received WTP Abort", msg, msg_len );
            }
            else {
                print_msg( port, "Received unexpected message", msg, msg_len );
            }
            fResponderIsDead = 0;
        }
        else {
            hdr_len = 0;
            break;
        }
    }
    print_msg( port, "Received packet", msg, msg_len );

    if (data != NULL && msg_len > hdr_len) {
        data_len = min( data_len, msg_len - hdr_len );
        memcpy( data, msg+hdr_len, data_len);
    }
    else  data_len = 0;
    return data_len;
}


/*
**  Function initializes and binds datagram socket.
*/

SOCKET connect_to_server_with_port(char *hostname, unsigned short port,
                                         unsigned short our_port)
{
    struct sockaddr_in addr;
    struct sockaddr_in o_addr;
    struct hostent * hostinfo;
    struct linger dontlinger;
    SOCKET s;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == -1) {
	error(errno, "socket failed");
        goto error;
    }

    dontlinger.l_onoff = 1;
    dontlinger.l_linger = 0;
#if defined(SOL_TCP)
    setsockopt(s, SOL_TCP, SO_LINGER, &dontlinger, sizeof(dontlinger));
#else
{
    /* XXX no error trapping */
    struct protoent *p = getprotobyname("tcp");
    setsockopt(s, p->p_proto, SO_LINGER, (void *)&dontlinger, sizeof(dontlinger));
}
#endif
    hostinfo = gethostbyname(hostname);
    if (hostinfo == NULL) {
	error(errno, "gethostbyname failed");
        goto error;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;
    addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;

    if (our_port > 0) {
        o_addr.sin_family = AF_INET;
        o_addr.sin_port = htons(our_port);
        o_addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(s, (const void *)&o_addr, sizeof(o_addr)) == -1) {
            error(0, "bind to local port %d failed", our_port);
            goto error;
        }
    }
    if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
	error(errno, "error connecting to server `%s' at port `%d'",
	    hostname, port);
        goto error;
    }

    return s;

error:
    if (s >= 0)
        close(s);
    return -1;
}

#ifdef _WINDOWS_
/*
**  We cannot use wapit library in Windows
*/
void panic(int level, const char * args, ...)
{
    va_list ap;

    va_start(ap, args);
    vprintf( args, ap );
    va_end( ap );
    printf( "\n");
    exit(level);
}

void error(int level, const char * args, ...)
{
    va_list ap;

    printf("Last socket error=%d\n", WSAGetLastError());
    va_start(ap, args);
    vprintf( args, ap );
    va_end( ap );
    printf( "\n");
}

void info(int level, const char * args, ...)
{
    va_list ap;

    va_start(ap, args);
    vprintf( args, ap );
    va_end( ap );
    printf( "\n");
}

/*
**  UNCHECKED! mappings to WINAPI
*/
Mutex *mutex_create() {
    return (Mutex *)CreateMutex( NULL, FALSE, NULL );
}

void mutex_lock( Mutex *mutex ) {
    WaitForSingleObject( (HANDLE)mutex, INFINITE );
}

void mutex_unlock( Mutex *mutex) {
    ReleaseMutex((HANDLE)mutex);
}

pthread_t start_thread(int detached, Threadfunc *func, void *arg, size_t size) {
    DWORD id;
    return CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)func,arg,0,&id)? id : -1;
}
#endif

int get_next_transaction() {
    int i_this;
    mutex_lock( mutex );
    i_this = num_sent + 1;
    if (max_send == MAX_SEND || num_sent < max_send) num_sent++;
    mutex_unlock( mutex );
    return i_this;
}

/*
**  Function (or thread) sets up a dgram socket.  Then it loops: WTL/WSP
**  Connect, Get a url and Disconnect until all requests are have been done.
*/
void *
client_session( void * arg)
{
    SOCKET fd;
    unsigned short our_port = 0;
    int ret;
    int url_len = 0, url_off = 0;
    double nowsec, lastsec, tmp;
    struct timeval now;
    struct timezone tz;
    char * url;
    unsigned char  sid[20];
    int            sid_len = 0;
    unsigned char  buf[64*1024];
    unsigned char reply_hdr[32];
    long timeout = 10;  /* wap gw is broken if no input */
    unsigned short tid = 0;
    int connection_retries = 0;
    int i_this;

    our_port = (unsigned short)(unsigned)arg;

    fd = connect_to_server_with_port( hostname, port, our_port);
    if (fd == -1)
        panic(0, "couldn't connect host ");
    debug("test.fakewap", 0, "Connected socket to host.");

    gettimeofday(&now, &tz);
    lastsec = (double) now.tv_sec + now.tv_usec / 1e6;

    /*
    **  Loop until all URLs have been requested
    */
    for (;;) {

        /*
        **  Get next transaction number or exit if too many transactions
        */
        i_this = get_next_transaction();
        if (max_send != MAX_SEND  && i_this > max_send) break;

        /*
        **  Connect, save sid from reply and finally ack the reply
        */
        tid = get_tid();
        ret = wap_msg_send( our_port, fd, WSP_Connect, sizeof(WSP_Connect),
                            &tid, NULL, 0 );

        if (ret == -1) panic(0, "Send WSP_Connect failed");

        CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WSP_ConnectReply, tid );
        ret = wap_msg_recv( our_port, fd, reply_hdr, sizeof(WSP_ConnectReply),
                            tid, buf, sizeof(buf), timeout );

        if (ret == -1) panic(0, "Receive WSP_ConnectReply failed");

        if (ret > 2)
        {
            sid_len = ReadVarIntLen(buf);
            memcpy( sid, buf, sid_len);
        }
        /*
        **  Send abort and continue if we get an unexpected reply
        */
        if (ret == 0)  {
            if (connection_retries++ > 3) {
                panic(0, "Cannot connect WAP GW!");
            }
            wap_msg_send( our_port, fd, WTP_Abort, sizeof(WTP_Abort), &tid,
                          NULL, 0 );
            continue;
        }
        else {
            connection_retries = 0;
        }
        ret = wap_msg_send( our_port, fd, WTP_Ack, sizeof(WTP_Ack), &tid,
                            NULL, 0 );

        if (ret == -1) panic(0, "Send WTP_Ack failed");

        /*
        **  Request WML page with the given URL
        */
        tid = get_tid();
        url = choose_message(urls, num_urls);
        url_len = strlen(url);
        url_off = StoreVarInt( buf, url_len );
        memcpy( buf+url_off, url, url_len );
        ret = wap_msg_send( our_port, fd, WSP_Get, sizeof(WSP_Get), &tid, buf,
                            url_len+3 );
        if (ret == -1) break;

        CONSTRUCT_EXPECTED_REPLY_HDR( reply_hdr, WSP_Reply, tid );
        ret = wap_msg_recv( our_port, fd, reply_hdr, sizeof(WSP_Reply),
                            tid, buf, sizeof(buf), timeout );
        if (ret == -1) break;

        ret = wap_msg_send( our_port, fd, WTP_Ack, sizeof(WTP_Ack), &tid, NULL,
                            0 );

        if (ret == -1) break;

        /*
        **  Finally disconnect with the sid returned by connect reply
        */
        ret = wap_msg_send( our_port, fd, WSP_Disconnect,
                            sizeof(WSP_Disconnect), &tid, sid, sid_len );

        if (ret == -1) break;

        gettimeofday(&now, &tz);
        nowsec = (double) now.tv_sec + now.tv_usec / 1e6;
        tmp = nowsec - lastsec;
        lastsec = nowsec;

        mutex_lock( mutex );
        if (tmp < besttime) besttime = tmp;
        if (tmp > worsttime) worsttime = tmp;
        totaltime += tmp;
        if (interval > 0.01) info(0, "fakewap: finished session # %d", i_this);
        mutex_unlock( mutex );

        if (tmp < (double)interval) {
            usleep( (long)(((double)interval - tmp) * 1e6) );
        }
    }
    close(fd);

    /*
    **  The last end_time stays, decrement the number of active threads
    */
    mutex_lock( mutex );
    time(&end_time);
    threads--;
    mutex_unlock( mutex );
    return 0;
}



/* The main program. */
int main(int argc, char **argv)
{
    unsigned short our_port;
    int i, org_threads;
    double delta;

    if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'v') {
        verbose = 1;
        argv++;
        argc--;
    }
    if (argc < 12)
        panic(0, "%s", usage);

#ifdef _WINDOWS_
    {
        WORD wVersionRequested = MAKEWORD( 2, 0 );
        WSADATA wsaData;
	int ret;

        ret = WSAStartup( wVersionRequested, &wsaData );
        if ( ret != 0 ) {
            panic( 0, "Windows socket api version is not supported, v2,0 is required\n");
            /* Tell the user that we could not find a usable */
            /* WinSock DLL.                                  */
            return 0;
        }
    }
#endif
    time(&start_time);

    our_port = (unsigned short)atoi(argv[1]);
    hostname = argv[2];
    port = (unsigned short)atoi(argv[3]);
    max_send = atoi(argv[4]);
    interval = atof(argv[5]);
    threads = atoi(argv[6]);
    WSP_Connect[3] += (atoi(argv[7])&3)<<6;
    WSP_Connect[0] += (atoi(argv[8])&15)<<3;
    WSP_Connect[3] += atoi(argv[9])&3;
    WSP_Connect[3] += (atoi(argv[10])&1)<<5;
    tid_addition = (unsigned short)atof(argv[11]);
    urls = argv + 12;
    num_urls = argc - 12;
    srand((unsigned int) time(NULL));

    mutex = mutex_create();

    info(0, "fakewap starting...\n");

    if (threads < 1) threads = 1;
    org_threads = threads;

    /*
    **  Start 'extra' client threads and finally execute the
    **  session of main thread
    */
    for (i = 1; i < threads; i++) {
        start_thread( 0, client_session, (void *)(unsigned)our_port, 0);
        our_port++;
    }
    client_session((void *)(unsigned)our_port);

    /*
    **  Wait the other sessions to complete
    */
    while (threads > 0) usleep( 1000 );

    info(0, "\nfakewap complete.");
    info( 0, "fakewap: %d client threads made total %d transactions.", org_threads, num_sent);
    delta = difftime(end_time, start_time);
    info( 0, "fakewap: total running time %.1f seconds", delta);
    info( 0, "fakewap: %.1f messages/seconds on average", num_sent / delta);
    info( 0, "fakewap: time of best, worst and average transaction: %.1f s, %.1f s, %.1f s",
         besttime, worsttime, totaltime / num_sent );
    return 0;
}
