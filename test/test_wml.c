/*
 * test_wml.c - test the WML converter
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "gwlib.h"
#include "wml.h"

int main(int argc, char *argv[])
{	
	struct wmlc *wmlc_data;

	int i=0;
	int fd;
	char tmpbuff[100*1024];
	char *wml;

#if 0
/* Strings for testing purposes */

/*	char data[] = "<wml><card><p>Hello World</p></card></wml>";*/
	char data[] = "<wml><card><p type=\"accept\"></p></wml>";
#endif

	if (argc < 2)
		panic(0, "WML file not given on command line.");
		
	fd = open(argv[1], O_RDONLY);
	memset(tmpbuff, 0, sizeof(tmpbuff));		
	i = read(fd, tmpbuff, sizeof(tmpbuff));
	close(fd);
	wml = tmpbuff;

	i=0;								/*****!!!!!!!****/
	wmlc_data = wml2wmlc(wml);
		/** wml2wmlc returns struct with binary content and length of content **/
									/*****!!!!!!!****/
	if (wmlc_data == NULL)
		return 0;

#if 0
	while(i<wmlc_data->wml_length)
	{	printf("%02x\t",wmlc_data->wbxml[i]);			/** print the result **/
		i++;}
	printf("\n(%d) bytes\n",wmlc_data->wml_length);			/** how many bytes **/
#else
	fwrite(wmlc_data->wbxml, wmlc_data->wml_length, 1, stdout);
#endif

	gw_free (wmlc_data);
	
	return 0;
	
}
