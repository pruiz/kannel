
#define LEN 50000
#define CONTENT 100
#define NO_CONTENT 101
#define NO_CONTENT_END 102
#define END 200

struct wmlc
{	int wml_length;
	unsigned char wbxml[LEN];
};

struct string_reference
{	int offset;	
	unsigned char string[512];
	struct string_reference *next;
};

struct variable
{	unsigned char string[512];
	struct variable *next;
};
	
	struct wmlc * wml2wmlc(char *);
	struct string_reference * make_new_table_string (void);
	void init_new_table_string ( struct string_reference * pointer, int * offset, unsigned char * string );
	struct variable * make_new_variable (void);
	void init_new_variable ( struct variable * pointer );
	char * put_variable_in_memory ( char * temp, struct variable * variable, struct wmlc * binary_string, int * count);
	int newcr_to_space ( char *, char * );
	int tag_comment_del ( char *, char *, int );
	char * white_space_del (char * );
	int space_del ( char *, char * );
	char * define_tag ( char *, unsigned char * );
	int tag_list( char *, unsigned char * );
	int attribute_list( char *, unsigned char * );
	int attribute_value( char *, unsigned  char * );
	int check_next_tag( unsigned char *, char * );
	char * element(char *temp, unsigned char *hex, int *count, struct wmlc *binary_string, struct string_reference * string_table, int * , int *, struct variable * variable, int * event_variable, int * );
	char * inline_string( char *, int *, struct wmlc *, struct variable * variable,  int * event_variable );
	char * attribute_space(char *temp, int *count, struct wmlc *binary_string, struct string_reference * string_table, int * , int *, struct variable * variable, int * event_variable, int * string_table_counter );
	int chars_to_lower_case( char * );
	int check_tag_amount ( char * );

#if 0
/*
 * Octet and MultibyteInteger (variable length) functions
 * (Kalle Marjola 1999)
 */

	typedef unsigned char Octet;		/* 8-bit basic data */
	typedef unsigned int MultibyteInt;	/* limited to 32 bits, not 35 */

/* write given multibyte integer into given destination string, which
 * must be large enough to handle the number (5 bytes is always sufficient)
 * returns the total length of the written number, in bytes. Does not fail */
	int write_variable_value(MultibyteInt value, Octet *dest);

#endif
