/* XXX The #ifdef HAVE_LIBXML is a stupid hack to make this not break things
until libxml is installed everywhere we do development. --liw */

#ifndef HAVE_LIBXML
int wml_compiler_not_implemented = 1;
#else


/* FOR TESTING */ 

#include "wml_compiler.h"


int main(int argc, char **argv)
{
  Octstr *wml_text = NULL;
  Octstr *wml_binary = NULL;
  Octstr *wml_scripts = NULL;

  int ret;
  int i = 0;

  /* You can give an wml text file as an argument './wap_compile main.wml' */
  if (argc > 1) 
    {
      if (strcmp(argv[1], "--debug") == 0)
	{
	  wml_text = octstr_create("Test string number one.");
	  octstr_set_char(wml_text, 6, '\0');
	}
      else
	{
	  wml_text = octstr_read_file(argv[1]);
	  if (wml_text == NULL)
	    return -1;
	}
    } 
  else 
    {
      printf("Give the wml file as a parameter.\n");
      return 0;
    }

  set_output_level(DEBUG);

  ret = wml_compile(wml_text, &wml_binary, &wml_scripts);

  printf("wml_compile returned: %d\n", ret);

  if (ret == 0)
    {
      printf("Here's the binary output: \n\n");
  
      for (i = 0; i < octstr_len(wml_binary); i ++)
	{
	  printf("%X ", octstr_get_char(wml_binary, i));
	  if ((i % 25) == 0 && i != 0)
	    printf("\n");
	}
      printf("\n\n");

      printf("And as a text: \n\n");
  
      for (i = 0; i < octstr_len(wml_binary); i ++)
	{
	  printf("%c ", octstr_get_char(wml_binary, i));
	  if ((i % 25) == 0 && i != 0)
	    printf("\n");
	}
      printf("\n\n");
    }

  octstr_destroy(wml_text);
  octstr_destroy(wml_binary);
  octstr_destroy(wml_scripts);
  return ret;
}

#endif
