/*
 * wapitlib.c - generally useful, non-application specific functions for WapIT
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
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

#include "wapitlib.h"



/*
 * List of currently open log files.
 */
#define MAX_LOGFILES 8
static struct {
	FILE *file;
	int minimum_output_level;
        char *filename;		/* to allow re-open */
} logfiles[MAX_LOGFILES];
static int num_logfiles = 0;


/* Make sure stderr is included in the list. */
static void add_stderr(void) {
	int i;
	
	for (i = 0; i < num_logfiles; ++i)
		if (logfiles[i].file == stderr)
			return;
	logfiles[num_logfiles].file = stderr;
	logfiles[num_logfiles].minimum_output_level = DEBUG;
	++num_logfiles;
}


void set_output_level(enum output_level level) {
	int i;
	
	add_stderr();
	for (i = 0; i < num_logfiles; ++i) {
		if (logfiles[i].file == stderr) {
			logfiles[i].minimum_output_level = level;
			break;
		}
	}
}

void reopen_log_files(void) {
	int i;
	
	for (i = 0; i < num_logfiles; ++i)
	    if (logfiles[i].file != stderr) {
		fclose(logfiles[i].file);
		logfiles[i].file = fopen(logfiles[i].filename, "a");
		if (logfiles[i].file == NULL) {
		    error(errno, "Couldn't re-open logfile `%s'.", logfiles[i].filename);
		}
	    }		
}

void open_logfile(char *filename, int level) {
	FILE *f;
	
	add_stderr();
	if (num_logfiles == MAX_LOGFILES) {
		error(0, "Too many log files already open, not adding `%s'",
			filename);
		return;
	}
	
	f = fopen(filename, "a");
	if (f == NULL) {
		error(errno, "Couldn't open logfile `%s'.", filename);
		return;
	}
	
	logfiles[num_logfiles].file = f;
	logfiles[num_logfiles].minimum_output_level = level;
	logfiles[num_logfiles].filename = strdup(filename);
	if (logfiles[num_logfiles].filename == NULL) {
	    error(errno, "Couldn't strdup filename");
	    fclose(f);
	}
	else {
	    ++num_logfiles;
	    info(0, "Added logfile `%s' with level `%d'.", filename, level);
	}
}


#define FORMAT_SIZE (10*1024)
static void format(char *buf, int level, int e, const char *fmt)
{
	static char *tab[] = {
		"DEBUG: ",
		"INFO: ",
		"WARNING: ",
		"ERROR: ",
		"PANIC: "
	};
	static int tab_size = sizeof(tab) / sizeof(tab[0]);
	time_t t;
	struct tm *tm;
	char *p, prefix[1024];
	
	p = prefix;
	time(&t);
	tm = gmtime(&t);
	strftime(p, sizeof(prefix), "%Y-%m-%d %H:%M:%S ", tm);

	p = strchr(p, '\0');
	sprintf(p, "[%d] ", (int) pthread_self());

	p = strchr(p, '\0');
	if (level < 0 || level >= tab_size)
		sprintf(p, "UNKNOWN: ");
	else
		sprintf(p, "%s", tab[level]);

	if (strlen(prefix) + strlen(fmt) > FORMAT_SIZE / 2) {
		sprintf(buf, "%s <OUTPUT message too long>\n", prefix);
		return;
	}

	if (e == 0)
		sprintf(buf, "%s%s\n", prefix, fmt);
	else
		sprintf(buf, "%s%s\n%sSystem error %d: %s\n",
			prefix, fmt, prefix, e, strerror(e));
}


static void output(FILE *f, char *buf, va_list args) {
	vfprintf(f, buf, args);
	fflush(f);
}


/* Almost all of the message printing functions are identical, except for
   the output level they use. This macro contains the identical parts of
   the functions so that the code needs to exist only once. It's a bit
   more awkward to edit, but that can't be helped. The "do {} while (0)"
   construct is a gimmick to be more like a function call in all syntactic
   situation. */
#define FUNCTION_GUTS(level) \
	do { \
		int i; \
		char buf[FORMAT_SIZE]; \
		va_list args; \
		add_stderr(); \
		format(buf, level, e, fmt); \
		for (i = 0; i < num_logfiles; ++i) { \
			if (level >= logfiles[i].minimum_output_level) { \
				va_start(args, fmt); \
				output(logfiles[i].file, buf, args); \
				va_end(args); \
			} \
		} \
	} while (0)

void panic(int e, const char *fmt, ...) {
	FUNCTION_GUTS(PANIC);
	exit(EXIT_FAILURE);
}


void error(int e, const char *fmt, ...) {
	FUNCTION_GUTS(ERROR);
}


void warning(int e, const char *fmt, ...) {
	FUNCTION_GUTS(WARNING);
}


void info(int e, const char *fmt, ...) {
	FUNCTION_GUTS(INFO);
}


void debug(int e, const char *fmt, ...) {
	FUNCTION_GUTS(DEBUG);
}


int make_server_socket(int port) {
	struct sockaddr_in addr;
	int s;
	int reuse;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == -1) {
		error(errno, "socket failed");
		goto error;
	}
	
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	reuse = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse, 
		       sizeof(reuse)) == -1)
	{
		error(errno, "setsockopt failed for server address");
		goto error;
	}

	if (bind(s, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
		error(errno, "bind failed");
		goto error;
	}
	
	if (listen(s, 10) == -1) {
		error(errno, "listen failed");
		goto error;
	}

	return s;

error:
	if (s >= 0)
		(void) close(s);
	return -1;
}


int tcpip_connect_to_server(char *hostname, int port) {
	struct sockaddr_in addr;
	struct hostent *hostinfo;
	struct linger dontlinger;
	int s;
	
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == -1)
		goto error;

	dontlinger.l_onoff = 1;
	dontlinger.l_linger = 0;
#ifdef BSD
	setsockopt(s, SOL_TCP, SO_LINGER, &dontlinger, sizeof(dontlinger));
#else
{ 
	#include <netdb.h>
	/* XXX no error trapping */
	struct protoent *p = getprotobyname("tcp");
	setsockopt(s, p->p_proto, SO_LINGER, &dontlinger, sizeof(dontlinger));
}
#endif

	hostinfo = gethostbyname(hostname);
	if (hostinfo == NULL)
		goto error;
	
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr = *(struct in_addr *) hostinfo->h_addr;
	
	if (connect(s, (struct sockaddr *) &addr, sizeof(addr)) == -1)
		goto error;
	
	return s;

error:
	error(errno, "error connecting to server `%s' at port `%d'",
		hostname, port);
	if (s >= 0)
		close(s);
	return -1;
}


int write_to_socket(int socket, char *str) {
	size_t len;
	int ret;

	len = strlen(str);
	while (len > 0) {
		ret = write(socket, str, len);
		if (ret == -1) {
			if(errno==EAGAIN) continue;
			if(errno==EINTR) continue;
			error(errno, "Writing to socket failed");
			return -1;
		}
		/* ret may be less than len, if the writing was interrupted
		   by a signal. */
		len -= ret;
		str += ret;
	}
	return 0;
}


int read_line(int fd, char *line, int max) {
        char *start;
        int ret;

        start = line;
        while (max > 0) {
                ret = read(fd, line, 1);
		if (ret == -1) {
			if(errno==EAGAIN) continue;
			if(errno==EINTR) continue;
                        error(errno, "read failed");
			return -1;
		}
                if (ret == 0)
                        break;
                ++line;
                --max;
                if (line[-1] == '\n')
                        break;
        }

	if (line == start)
		return 0;

	if (line[-1] == '\n')
		--line;
        if (line[-1] == '\r')
                --line;
	*line = '\0';

	return 1;
}


int read_to_eof(int fd, char **data, size_t *len) {
	size_t size;
	int ret;
	char *p;
	
	*len = 0;
	size = 0;
	*data = NULL;
	for (;;) {
		if (*len == size) {
			size += 16*1024;
			p = realloc(*data, size);
			if (p == NULL)
				goto error;
			*data = p;
		}
		ret = read(fd, *data + *len, size - *len);
		if (ret == -1) {
			error(errno, "Error while reading");
			goto error;
		}
		if (ret == 0)
			break;
		*len += ret;
	}

	return 0;

error:
	free(*data);
	return -1;
}


int read_available(int fd)
{
    fd_set rf;
    struct timeval to;
    int ret;

    FD_ZERO(&rf);
    FD_SET(fd, &rf);
    to.tv_sec = 0;
    to.tv_usec = 0;

    ret = select(FD_SETSIZE, &rf, NULL, NULL, &to);
    if (ret > 0 && FD_ISSET(fd, &rf))
	return 1;
    if (ret < 0)
	return -1;	/* some error */
    return 0;
}



int split_words(char *buf, int max, char **words) {
        int n;

        n = 0;
        while (n < max - 1 && *buf != '\0') {
                while (*buf == ' ')
                        ++buf;
                if (*buf != '\0') {
                        words[n++] = buf;
                        while (*buf != ' ' && *buf != '\0')
                                ++buf;
                        if (*buf == ' ')
                                *buf++ = '\0';
                }
        }
        while (*buf == ' ')
                ++buf;
        if (*buf != '\0')
                words[n++] = buf;
        return n;
}


char *trim_ends(char *str) {
	char *end;
	
	while (isspace(*str))
		++str;
	end = strchr(str, '\0');
	while (str < end && isspace(end[-1]))
		--end;
	*end = '\0';
	return str;
}


int count_occurences(char *str, char *pat) {
	int count;
	size_t len;
	
	count = 0;
	len = strlen(pat);
	while ((str = strstr(str, pat)) != NULL) {
		++count;
		str += len;
	}
	return count;
}



char *strndup(char *str, size_t n) {
	char *p;
	
	p = malloc(n + 1);
	if (p == NULL)
		return NULL;
	memcpy(p, str, n);
	p[n] = '\0';
	return p;
}


/*
 * Start a new thread, running function func, and giving it the argument
 * `arg'. If `size' is 0, `arg' is given as is; otherwise, `arg' is copied
 * into a memory area of size `size'.
 * 
 * If `detached' is non-zero, the thread is created detached, otherwise
 * it is created detached.
 */
pthread_t start_thread(int detached, Threadfunc *func, void *arg, size_t size)
{
	void *copy;
	pthread_t id;
#if HAVE_THREADS
	pthread_attr_t attr;
	int ret;
#endif
	
	if (size == 0)
		copy = arg;
	else {
		copy = malloc(size);
		if (copy == NULL) {
			error(errno, "malloc failed");
			goto error;
		}
		memcpy(copy, arg, size);
	}
	
#if HAVE_THREADS
	pthread_attr_init(&attr);
	if (detached)
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	do {
		ret = pthread_create(&id, &attr, func, copy);
		if (ret == EAGAIN) {
			error(0, "Too many threads, waiting to create one...");
			sleep(1);
		}
	} while (ret == EAGAIN);
	if (ret != 0) {
		error(errno, "pthread_create failed");
		goto error;
	}
#else
	id = 0;
	func(copy);
#endif

	return id;

error:
	return -1;
}

/*
 * new datatype functions
 */



MultibyteInt get_variable_value(Octet *source, int *len)
{
    MultibyteInt retval = 0;
    
    for(*len=1;; (*len)++, source++) {
	retval = retval * 0x80 + (*source & 0x7F);
	if (*source < 0x80)	/* if the continue-bit (high bit) is not set */
	    break;
    }
    return retval;
}


int write_variable_value(MultibyteInt value, Octet *dest)
{
    int i, loc = 0;
    Octet revbuffer[20];	/* we write it backwards */
    
    for (;;) {
	revbuffer[loc++] = (value & 0x7F) + 0x80;	
	if (value >= 0x80)
	    value = value >> 7;
	else
	    break;
    }
    for(i=0; i < loc; i++)		/* reverse the buffer */
	dest[i] = revbuffer[loc-i-1];
    
    dest[loc-1] &= 0x7F;	/* remove trailer-bit from last */

    return loc;
}

Octet reverse_octet(Octet source)
{
    Octet	dest;
    dest = (source & 1) <<7;
    dest += (source & 2) <<5;
    dest += (source & 4) <<3;
    dest += (source & 8) <<1;
    dest += (source & 16) >>1;
    dest += (source & 32) >>3;
    dest += (source & 64) >>5;
    dest += (source & 128) >>7;
    
    return dest;
}



int url_decode(char *string)
{
    long value;
    char *dptr = string;
    char buf[3];		/* buffer for strtol conversion */
    buf[2] = '\0';
    
    do {
	if (*string == '%') {
	    if (*(string+1) == '\0' || *(string+2) == '\0')
		goto error;
	    buf[0] = *(string+1);
	    buf[1] = *(string+2);
	    value =  strtol(buf, NULL, 16);
	    if (value > 0) {
		*dptr = (unsigned char)value;
		string += 3;
		dptr++;
		continue;
	    }
	}
	if (*string == '+') {
	    *dptr++ = ' ';
	    string++;
	}
	else
	    *dptr++ = *string++;
    } while(*string);
    *dptr = '\0';

    return 0;

error:
    *dptr = '\0';
    error(0, "url_decode: corrupted end-of-string <%s>", string);
    return -1;
}


/*
 * seek string 's' backward from offset 'start_offset'. Return offset of
 * the first occurance of any character in 'accept' string, or -1 if not
 * found  */
int str_reverse_seek(const char *s, int start_offset, const char *accept)
{
    char	*other;

    for(;start_offset >= 0; start_offset--) {
	for(other = (char *)accept; *other != '\0'; other++) {
	    if (*other == s[start_offset])
		return start_offset;
	}
    }
    return -1;		/* not found */
}


/* as above but ignoring case */
int str_reverse_case_seek(const char *s, int start_offset, const char *accept)
{
    char	*other;

    for(;start_offset >= 0; start_offset--) {
	for(other = (char *)accept; *other != '\0'; other++) {
	    if (toupper(*other) == toupper(s[start_offset]))
		return start_offset;
	}
    }
    return -1;		/* not found */
}




int get_and_set_debugs(int argc, char **argv,
		       int (*find_own) (int index, int argc, char **argv))
{
    int i, ret = -1;
    int debug_lvl = -1;
    int file_lvl = DEBUG;
    char *log_file = NULL;
    
    for(i=1; i < argc; i++) {
	if (strcmp(argv[i],"-v")==0 ||
	    strcmp(argv[i],"--verbosity")==0) {

	    if (i+1 < argc) {
		debug_lvl = atoi(argv[i+1]);
		i++;
	    } else
		fprintf(stderr, "Missing argument for option %s\n", argv[i]); 
	} else if (strcmp(argv[i],"-F")==0 ||
		   strcmp(argv[i],"--logfile")==0) {
	    if (i+1 < argc && *(argv[i+1]) != '-') {
		log_file = argv[i+1];
		i++;
	    } else
		fprintf(stderr, "Missing argument for option %s\n", argv[i]); 
	} else if (strcmp(argv[i],"-V")==0 ||
		   strcmp(argv[i],"--fileverbosity")==0) {
	    if (i+1 < argc) {
		file_lvl = atoi(argv[i+1]);
		i++;
	    } else
		fprintf(stderr, "Missing argument for option %s\n", argv[i]); 
	} else if(*argv[i] != '-')
	    break;
	else {
	    if (find_own != NULL) {
		ret = find_own(i, argc, argv);
	    }
	    if (ret < 0)
		fprintf(stderr, "Unknown option %s, ignoring\n", argv[i]);
	    else
		i += ret;	/* advance additional args */
	}
    }
    if (debug_lvl > -1)
	set_output_level(debug_lvl);
    if (log_file != NULL)
	open_logfile(log_file, file_lvl);

    info(0, "Debug_lvl = %d, log_file = %s, log_lvl = %d",
	  debug_lvl, log_file ? log_file : "<none>", file_lvl);
    
    return i;
}

void print_std_args_usage(FILE *stream)
{
    fprintf(stream,
	   " -v <level>     set stderr output level. 0 = DEBUG, 4 = PANIC\n"
	   " -F <logfile>   set logfile name\n"
	   " -V <level>     set logfile output level. Defaults to DEBUG\n"
	   " --verbosity, --logfile, --fileverbosity   aliased arguments\n");
}

