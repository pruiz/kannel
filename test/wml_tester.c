/*
 * wml_tester.c - a simple program to test the WML-compiler module
 *
 * Tuomas Luttinen <tuo@wapit.com>
 */

#include <stdlib.h>
#include <unistd.h>

#include "gwlib/gwlib.h"
#include "gw/wml_compiler.h"


typedef enum { NORMAL_OUT, SOURCE_OUT, BINARY_OUT } output_t;

static void help(void) {
    info(0, "Usage: wml_tester [-hsbz] [-n number] [-f file] "
	 "[-c charset] file.wml\n"
	 "where\n"
	 "  -h  this text\n"
	 "  -s  output also the WML source, cannot be used with b\n"
	 "  -b  output only the compiled binary, cannot be used with s\n"
	 "  -z  insert a '\\0'-character in the middle of the input\n"
	 "  -n number   the number of times the compiling is done\n"
	 "  -f file     direct the output into a file\n"
	 "  -c charset  character set as given by the http");
}


static void set_zero(Octstr *ostr)
{
    octstr_set_char(ostr, (1 + (int) (octstr_len(ostr) *gw_rand()/
				      (RAND_MAX+1.0))), '\0');
}


int main(int argc, char **argv)
{
    output_t outputti = NORMAL_OUT;
    FILE *fp = NULL;
    Octstr *output = NULL;
    Octstr *filename = NULL;
    Octstr *wml_text = NULL;
    Octstr *charset = NULL;
    Octstr *wml_binary = NULL;

    int i, ret = 0, opt, file = 0, zero = 0, numstatus = 0;
    long num = 0;

    /* You can give an wml text file as an argument './wml_tester main.wml' */

    gwlib_init();

    while ((opt = getopt(argc, argv, "hsbzn:f:c:")) != EOF) {
	switch (opt) {
	case 'h':
	    help();
	    exit(0);
	case 's':
	    if (outputti == NORMAL_OUT)
		outputti = SOURCE_OUT;
	    else {
		help();
		exit(0);
	    }
	    break;
	case 'b':
	    if (outputti == NORMAL_OUT)
		outputti = BINARY_OUT;
	    else {
		help();
		exit(0);
	    }
	    break;
	case 'z':
	    zero = 1;
	    break;
	case 'n':
	    numstatus = octstr_parse_long(&num, octstr_imm(optarg), 0, 0);
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

    if (outputti == BINARY_OUT)
	 log_set_output_level(GW_PANIC);
    wml_init();

    while (optind < argc) {
	wml_text = octstr_read_file(argv[optind]);
	if (wml_text == NULL)
	    panic(0, "Couldn't read WML source file.");

	if (zero)
	    set_zero(wml_text);

	for (i = 0; i <= num; i++) {
	    ret = wml_compile(wml_text, charset, &wml_binary, NULL);
	    if (i < num)
		octstr_destroy(wml_binary);
	}
	optind++;

	output = octstr_format("wml_compile returned: %d\n\n", ret);
    
	if (ret == 0) {
	    if (fp == NULL)
		fp = stdout;

	    if (outputti != BINARY_OUT) {
		if (outputti == SOURCE_OUT) {
		    octstr_insert(output, wml_text, octstr_len(output));
		    octstr_append_char(output, '\n');
		}

		octstr_append(output, octstr_imm(
		    "Here's the binary output: \n\n"));
		octstr_print(fp, output);
	    }

	    if (file && outputti != BINARY_OUT) {
		fclose(fp);
		log_open(octstr_get_cstr(filename), 0, GW_NON_EXCL);
		octstr_dump(wml_binary, 0);
		log_close_all();
		fp = fopen(octstr_get_cstr(filename), "a");
	    } else if (outputti != BINARY_OUT)
		octstr_dump(wml_binary, 0);
	    else 
		octstr_print(fp, wml_binary);

	    if (outputti != BINARY_OUT) {
		octstr_destroy(output);
		output = octstr_format("\n And as a text: \n\n");
		octstr_print(fp, output);
      
		octstr_pretty_print(fp, wml_binary);
		octstr_destroy(output);
		output = octstr_format("\n\n");
		octstr_print(fp, output);
	    }
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
