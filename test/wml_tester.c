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
	info(0, "Usage: wml_tester [-hzs] [-f file] [-b file] [-c charset] file.wml\n"
	        "where\n"
		"  -h  this text\n"
	        "  -z  insert a '\\0'-character in the midlle of the input\n"
	        "  -s  output also the WML source\n"
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

  int ret, opt, file = 0, source = 0, zero = 0;

  char buffer[100];

  gwlib_init();

  /* You can give an wml text file as an argument './wap_compile main.wml' */

  while ((opt = getopt(argc, argv, "hzsf:b:c:")) != EOF) {
    switch (opt) {
    case 'h':
      help();
      exit(0);
    case 'z':
      zero = 1;
      break;
    case 's':
      source = 1;
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

  if (optind >= argc) 
    {
      error(0, "Missing arguments.");
      help();
      panic(0, "Stopping.");
    }

  wml_text = octstr_read_file(argv[optind]);
  if (wml_text == NULL)
    panic(0, "Couldn't read WML source file.");

  if (zero)
    octstr_set_char(wml_text, (1 + (int) (octstr_len(wml_text)*gw_rand()/
						    (RAND_MAX+1.0))), '\0');

  ret = wml_compile(wml_text, charset, &wml_binary);

  sprintf(buffer, "wml_compile returned: %d\n\n", ret);
  output = octstr_create(buffer); 

  if (ret == 0)
    {
      if (fp == NULL)
	fp = stdout;

      if (source)
	{
	  octstr_insert(output, wml_text, octstr_len(output));
	  octstr_append_char(output, '\n');
	}

      sprintf(buffer, "Here's the binary output: \n\n");
      octstr_append_cstr(output, buffer);
      octstr_print(fp, output);

      if (binary_file_name)
	{
	  fb = fopen(octstr_get_cstr(binary_file_name), "w");
	  octstr_print(fb, wml_binary);
	  fclose(fb);
	  octstr_destroy(binary_file_name);
	}
      if (file)
	{
	  fclose(fp);
	  log_open(octstr_get_cstr(filename), 0);
	  octstr_dump(wml_binary, 0);
	  log_close_all();
	  fp = fopen(octstr_get_cstr(filename), "a");
	}
      else
	octstr_dump(wml_binary, 0);

      octstr_destroy(output);
      sprintf(buffer, "\n And as a text: \n\n");
      output = octstr_create(buffer);
      octstr_print(fp, output);
      
      octstr_pretty_print(fp, wml_binary);
      octstr_destroy(output);
      sprintf(buffer, "\n\n");
      output = octstr_create(buffer);
      octstr_print(fp, output);
    }

  if (file)
    {
      fclose(fp);
      octstr_destroy(filename);
    }

  if (charset != NULL)
    octstr_destroy(charset);

  octstr_destroy(wml_text);
  octstr_destroy(output);
  octstr_destroy(wml_binary);

  gwlib_shutdown();

  return ret;
}
