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
	int output_binary;

	if (argc < 2)
		panic(0, "WML file not given on command line.");
	if (argc == 2)
		output_binary = 1;
	else
		output_binary = 0;

		
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

	if (output_binary)
		fwrite(wmlc_data->wbxml, wmlc_data->wml_length, 1, stdout);
	else {
		Octstr *os;
		
		os = octstr_create_from_data(wmlc_data->wbxml, 
					     wmlc_data->wml_length);
		octstr_dump(os);
		octstr_pretty_print(stdout, os);
	}

	gw_free (wmlc_data);
	
	return 0;
	
}
