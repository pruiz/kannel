/*
 *
 * wsc.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 Markku Rossi, etc.
 *		 All rights reserved.
 *
 * Main for the WMLScript compiler.
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <ws.h>

/*
 * Prototypes for static functions.
 */

/* Print usage message to the stdout. */
static void usage(void);

/* A callback functio to receive the meta-pragmas. */
static void pragma_meta(const WsUtf8String *property_name,
			const WsUtf8String *content,
			const WsUtf8String *scheme,
			void *context);

/*
 * Static variables.
 */

/* The name of the compiler program. */
static char *program;

/* Use ws_compile_data() instead of ws_compile_file(). */
int eval_data = 0;


/*
 * Global functions.
 */

int
main(int argc, char *argv[])
{
  int i;
  WsCompilerParams params;
  WsCompilerPtr compiler;
  WsResult result;
  int opt;

  program = strrchr(argv[0], '/');
  if (program)
    program++;
  else
    program = argv[0];

  /* Process command line arguments. */
  while ((opt = getopt(argc, argv, "dh")) != -1)
    {
      switch (opt)
	{
	case 'd':
	  eval_data = 1;
	  break;

	case 'h':
	  usage();
	  exit(0);
	  break;

	case '?':
	  printf("Usage `%s -h' for a complete list of options.\n",
		 program);
	  exit(1);
	}
    }

  /* Create a compiler. */

  memset(&params, 0, sizeof(params));

  params.use_latin1_strings = 0;

  params.print_symbolic_assembler = 1;
  params.print_assembler = 0;

  params.meta_name_cb = pragma_meta;
  params.meta_name_cb_context = "meta name";

  params.meta_http_equiv_cb = pragma_meta;
  params.meta_http_equiv_cb_context = "meta http equiv";

  compiler = ws_create(&params);
  if (compiler == NULL)
    {
      fprintf(stderr, "wsc: could not create compiler\n");
      exit(1);
    }

  for (i = optind; i < argc; i++)
    {
      FILE *ifp, *ofp;
      char *outname;

      ifp = fopen(argv[i], "rb");
      if (ifp == NULL)
	{
	  fprintf(stderr, "wsc: could not open input file `%s': %s'\n",
		  argv[i], strerror(errno));
	  exit(1);
	}

      /* Create the output name. */
      outname = malloc(strlen(argv[i]) + 1 + 1);
      if (outname == NULL)
	{
	  fprintf(stderr, "wmlsc: could not create output file name: %s\n",
		  strerror(errno));
	  exit(1);
	}
      strcpy(outname, argv[i]);
      strcat(outname, "c");

      ofp = fopen(outname, "wb");
      if (ofp == NULL)
	{
	  fprintf(stderr, "wsc: could not create output file `%s': %s\n",
		  outname, strerror(errno));
	  exit(1);
	}

      if (eval_data)
	{
	  /* Use the ws_compile_data() interface. */
	  struct stat stat_st;
	  unsigned char *data;
	  unsigned char *output;
	  size_t output_len;

	  if (stat(argv[i], &stat_st) == -1)
	    {
	      fprintf(stderr, "wsc: could not stat input file `%s': %s\n",
		      argv[i], strerror(errno));
	      exit(1);
	    }

	  /* Allocate the input buffer. */
	  data = malloc(stat_st.st_size);
	  if (data == NULL)
	    {
	      fprintf(stderr, "wsc: could not allocate input buffer: %s\n",
		      strerror(errno));
	      exit(1);
	    }
	  if (fread(data, 1, stat_st.st_size, ifp) < stat_st.st_size)
	    {
	      fprintf(stderr, "wsc: could not read data: %s\n",
		      strerror(errno));
	      exit(1);
	    }
	  result = ws_compile_data(compiler, argv[i], data, stat_st.st_size,
				   &output, &output_len);
	  if (result == WS_OK)
	    {
	      /* Save the output to `ofp'. */
	      if (fwrite(output, 1, output_len, ofp) != output_len)
		{
		  fprintf(stderr,
			  "wsc: could not save output to file `%s': %s\n",
			  outname, strerror(errno));
		  exit(1);
		}
	    }
	  free(data);
	  ws_free_byte_code(output);
	}
      else
	{
	  /* Use the ws_compile_file() interface. */
	  result = ws_compile_file(compiler, argv[i], ifp, ofp);
	}

      /* Common cleanup. */
      fclose(ifp);
      fclose(ofp);

      if (result != WS_OK)
	{
	  remove(outname);
	  fprintf(stderr, "wsc: compilation failed: %s\n",
		  ws_result_to_string(result));
	  exit(1);
	}
      free(outname);
    }

  ws_destroy(compiler);

  return 0;
}

/*
 * Static functions.
 */

static void
usage(void)
{
  printf("Usage: %s OPTION... FILE...\n\
\n\
  -d		use ws_eval_data() function instead of ws_eval_file()\n\
  -h            print this message and exit successfully\n\
\n",
	 program);
}

static void
pragma_meta(const WsUtf8String *property_name, const WsUtf8String *content,
	    const WsUtf8String *scheme, void *context)
{
  FILE *fp = stdout;
  char *what = (char *) context;
  char *property_name_l = ws_utf8_to_latin1(property_name, '?', NULL);
  char *content_l = ws_utf8_to_latin1(content, '?', NULL);
  char *scheme_l = ws_utf8_to_latin1(scheme, '?', NULL);

  fprintf(fp, "%s: name=\"%s\", content=\"%s\",",
	  what,
	  property_name_l ? property_name_l : "",
	  content_l ? content_l : "");

  if (scheme)
    fprintf(fp, ", scheme=\"%s\"",
	    scheme_l ? scheme_l : "");

  fprintf(fp, "\n");

  ws_utf8_free_data(property_name_l);
  ws_utf8_free_data(content_l);
  ws_utf8_free_data(scheme_l);
}
