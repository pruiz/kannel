#include "gwlib/gwlib.h"
#include "gw/wml_compiler.h"


int main(int argc, char **argv)
{
  Octstr *wml_text = NULL;
  Octstr *wml_binary = NULL;

  int ret;
  int file = 0;

  gw_init_mem();

  /* You can give an wml text file as an argument './wap_compile main.wml' */
  if (argc > 2)
    {
      open_logfile(argv[2], DEBUG);
      file = 1;
    } 

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

  if (!file)
    set_output_level(DEBUG);

  ret = wml_compile(wml_text, &wml_binary);

  printf("wml_compile returned: %d\n", ret);

  if (ret == 0)
    {
      octstr_dump(wml_text, 0);
      printf("\n");

      printf("Here's the binary output: \n\n");
      octstr_dump(wml_binary, 0);
      printf("\n");

      printf("And as a text: \n\n");
      octstr_pretty_print(stdout, wml_binary);
      printf("\n\n");
    }

  octstr_destroy(wml_text);
  octstr_destroy(wml_binary);
  gw_check_leaks();
  return ret;
}
