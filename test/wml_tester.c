/*
 * wml_tester.c - a simple program to test the WML-compiler module
 *
 * Tuomas Luttinen <tuo@wapit.com>
 */

#include <stdlib.h>
#include <unistd.h>

#include "gwlib/gwlib.h"
#include "gw/wml_compiler.h"



static void help(void) {
    info(0, "Usage: wml_tester [-hsz] [-n number] [-f file] [-b file] "
	 "[-c charset] file.wml\n"
	 "where\n"
	 "  -h  this text\n"
	 "  -s  output also the WML source\n"
	 "  -z  insert a '\\0'-character in the midlle of the input\n"
	 "  -n number   the number of times the compiling is done\n"
	 "  -f file     direct the output into a file\n"
	 "  -b file     binary output into a file\n"
	 "  -c charset  character set as given by the http");
}


int main(int argc, char **argv)
{
    FILE *fp = NULL;
    FILE *fb = NULL;
    Octstr *output = NULL;
    Octstr *filename = NULL;
    Octstr *binary_file_name = NULL;
    Octstr *wml_text = NULL;
    Octstr *charset = NULL;
    Octstr *wml_binary = NULL;
    Octstr *number = NULL;

    int i, ret = 0, opt, file = 0, source = 0, zero = 0, numstatus = 0;
    long num = 0;

    gwlib_init();
    wml_init();

    /* You can give an wml text file as an argument './wml_tester main.wml' */

    while ((opt = getopt(argc, argv, "hszn:f:b:c:")) != EOF) {
	switch (opt) {
	case 'h':
	    help();
	    exit(0);
	case 's':
	    source = 1;
	    break;
	case 'z':
	    zero = 1;
	    break;
	case 'n':
	    number = octstr_create(optarg);
	    numstatus = octstr_parse_long(&num, number, 0, 0);
	    octstr_destroy(number);
	    if (numstatus == -1) { 
		/* Error in the octstr_parse_long */
		error(num, "Error in the handling of argument to option n");
		help();
		panic(0, "Stopping.");
	    }
	    break;
	case 'f':
	    file = 1;
	    filename = octstr_create(optarg);
	    fp = fopen(optarg, "a");
	    if (fp == NULL)
		panic(0, "Couldn't open output file.");	
	    break;
	case 'b':
	    binary_file_name = octstr_create(optarg);
	    break;
	case 'c':
	    charset = octstr_create(optarg);
	    break;
	case '?':
	default:
	    error(0, "Invalid option %c", opt);
	    help();
	    panic(0, "Stopping.");
	}
    }

    if (optind >= argc) {
	error(0, "Missing arguments.");
	help();
	panic(0, "Stopping.");
    }

    while (optind < argc) {
	wml_text = octstr_read_file(argv[optind]);
	if (wml_text == NULL)
	    panic(0, "Couldn't read WML source file.");

	if (zero)
	    octstr_set_char(wml_text, 
			    (1 + (int) (octstr_len(wml_text) *gw_rand()/
					(RAND_MAX+1.0))), '\0');

	for (i = 0; i <= num; i++) {
	    ret = wml_compile(wml_text, charset, &wml_binary);
	    if (i < num)
		octstr_destroy(wml_binary);
	}
	optind++;

	output = octstr_format("wml_compile returned: %d\n\n", ret);
    
	if (ret == 0) {
	    if (fp == NULL)
		fp = stdout;
	
	    if (source) {
		octstr_insert(output, wml_text, octstr_len(output));
		octstr_append_char(output, '\n');
	    }

	    octstr_append(output, octstr_imm(
		    "Here's the binary output: \n\n"));
	    octstr_print(fp, output);
	
	    if (binary_file_name) {
		fb = fopen(octstr_get_cstr(binary_file_name), "w");
		octstr_print(fb, wml_binary);
		fclose(fb);
		octstr_destroy(binary_file_name);
	    }
	    if (file) {
		fclose(fp);
		log_open(octstr_get_cstr(filename), 0);
		octstr_dump(wml_binary, 0);
		log_close_all();
		fp = fopen(octstr_get_cstr(filename), "a");
	    } else
		octstr_dump(wml_binary, 0);

	    octstr_destroy(output);
	    output = octstr_format("\n And as a text: \n\n");
	    octstr_print(fp, output);
      
	    octstr_pretty_print(fp, wml_binary);
	    octstr_destroy(output);
	    output = octstr_format("\n\n");
	    octstr_print(fp, output);
	}

	octstr_destroy(wml_text);
	octstr_destroy(output);
	octstr_destroy(wml_binary);
    }
	
    if (file) {
	fclose(fp);
	octstr_destroy(filename);
    }
    
    if (charset != NULL)
	octstr_destroy(charset);

    wml_shutdown();
    gwlib_shutdown();
    
    return ret;
}
