/***********************************************************************************/
/***************                   WML to WMLC               ***********************/
/***************                   by: Peter Grönholm        ***********************/
/*************** 											 ***********************/
/***************  	This wml to binary wml converter will	 ***********************/
/***************  	soon be completely rewritten.            ***********************/
/***********************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "gwlib.h"
#include "wml.h"

/***********************************************************************************/
/***************           newcr_and_tabulator_to_space      ***********************/
/***********************************************************************************/

int newcr_to_space ( char* from, char* to ) {

	while(*from != '\0') {
		if(isspace(*from) || *from=='\n'|| *from=='\r' || *from=='\t') {
			*to=' '; to++;
		}
		else {
			*to=*from; to++;
		}

		from++;
		
	} /* while */
	
	*to='\0';
	return 0;
}

/***********************************************************************************/
/***********************          tag_comment_del      *****************************/
/***********************************************************************************/
/* This function removes all comments <!-- --> from the wml content  */
 
int tag_comment_del ( char *from, char *to, int length) {

	int my_int = 0;
	for(my_int=0;((my_int<strlen(from)) && (strlen(to)<length));my_int++) {

	   	if( (from[my_int] == '<') && (from[my_int+1] == '!') && (from[my_int+2] == '-') && (from[my_int+3] == '-'))    	{

	   		while( !((from[my_int] == '-') && (from[my_int+1] == '-') && (from[my_int+2] == '>')) ) {
     			my_int++;
     			if(my_int==strlen(from))
     			return 0;
			} /* while */
			my_int=my_int+2;
     	} /* if */

     	else {
     		to[strlen(to)]=from[my_int];
		} 

	} /* for */

	to[strlen(to)]='\0';
	
	return 0;
}	

/***********************************************************************************/
/*****************************          white_space_del        *********************/
/***********************************************************************************/
/* removes extra white spaces from beginning and end of wml content */
 
char * white_space_del (char *from) {

	char *to = NULL, *temp = NULL;
	int string_len = 0, string_len2 = 0;

	string_len = strlen(from);

	if (string_len == 0) {
		printf("No string to delete white spaces\n");
		return from;
	}

	temp = strdup(from);
	if(temp==NULL) goto error;

	/* delete spaces from beginning of string */
	while ( (isspace(temp[string_len2])) && (temp[string_len2] != '\0'))
		string_len2++;

	string_len = strlen(&temp[string_len2]);

	/* if there is only one character then send it back */

	if (string_len == 1) {	
		/* allocate memory only the needed amount */
		to =(char *) calloc(++string_len, sizeof(char));
		if(to==NULL) goto error;
		bzero(to,string_len);

		strncpy(to, &temp[string_len2], string_len);

		free (temp);
		return(to);
	} /* if */

	string_len--;

	/* delete spaces from end of string */

	while ( (isspace(temp[string_len])) && (string_len != 0) )
		string_len--;

	temp[++string_len] = '\0';

	string_len = strlen(temp);

	to =(char *) calloc(++string_len, sizeof(char));

	bzero(to,string_len);

	strncpy(to, &temp[string_len2], string_len);

	free (temp);
	return(to);

error:
	free(temp);
	return NULL;
}

/***********************************************************************************/
/**************************           space_del        *****************************/
/***********************************************************************************/
/* Function that deletes all extra white spaces. Multiple white spaces are reduced 
 * to a single white space */

int space_del ( char* from, char* to ) {

	while(*from != '\0') {

		if(isspace(*from)) {
		
			while(isspace(*from))  from++;
			
			from--;
			*to=*from;
			from++;
			to++;
			
		} else {
		
			*to=*from;
			to++;
			from++;
			
		} /* if isspace */
		
	} /* while */
	
	*to='\0';
	return 0;
}

/***********************************************************************************/
/*************************               define_tag         ************************/
/***********************************************************************************/

char * define_tag (char *tag_start, unsigned char * hex) {

	int i=0;
	char tag[500];

	bzero(tag,500);

	while( *tag_start!='>' ) {
		tag[i]=*tag_start;
		tag_start++;
		i++;
	}

	i--;

	if ( tag[i] == '/');

	else
		i++;

	tag[i]='\0';
	tag_list(tag, hex);		/*search the tag from the tag_list */

	return (tag_start);	

}

/***********************************************************************************/
/****************************         tag_list          ****************************/
/***********************************************************************************/

int tag_list( char * tag, unsigned char * hex) {	

	if(strcasecmp(tag,"a")==0)											*hex=0x1C;
	else if(strcasecmp(tag,"anchor")==0)								*hex=0x22;
	else if(strcasecmp(tag,"access")==0)								*hex=0x23;
	else if(strcasecmp(tag,"b")==0)										*hex=0x24;
	else if(strcasecmp(tag,"big")==0)									*hex=0x25;
	else if(strcasecmp(tag,"br")==0)									*hex=0x26;
	else if(strcasecmp(tag,"card")==0)									*hex=0x27;
	else if(strcasecmp(tag,"do")==0)									*hex=0x28;
	else if(strcasecmp(tag,"em")==0)									*hex=0x29;
	else if(strcasecmp(tag,"fieldset")==0)								*hex=0x2A;
	else if(strcasecmp(tag,"go")==0)									*hex=0x2B;
	else if(strcasecmp(tag,"head")==0)									*hex=0x2C;
	else if(strcasecmp(tag,"i")==0)										*hex=0x2D;
	else if(strcasecmp(tag,"img")==0)									*hex=0x2E;
	else if(strcasecmp(tag,"input")==0)									*hex=0x2F;
	else if(strcasecmp(tag,"meta")==0)									*hex=0x30;
	else if(strcasecmp(tag,"noop")==0)									*hex=0x31;
	else if(strcasecmp(tag,"p")==0)										*hex=0x20;
	else if(strcasecmp(tag,"postfield")==0)								*hex=0x21;
	else if(strcasecmp(tag,"prev")==0)									*hex=0x32;
	else if(strcasecmp(tag,"onevent")==0)								*hex=0x33;
	else if(strcasecmp(tag,"optgroup")==0)								*hex=0x34;
	else if(strcasecmp(tag,"option")==0)								*hex=0x35;
	else if(strcasecmp(tag,"refresh")==0)								*hex=0x36;
	else if(strcasecmp(tag,"select")==0)								*hex=0x37;
	else if(strcasecmp(tag,"setvar")==0)								*hex=0x3E;
	else if(strcasecmp(tag,"small")==0)									*hex=0x38;
	else if(strcasecmp(tag,"strong")==0)								*hex=0x39;
	else if(strcasecmp(tag,"table")==0)									*hex=0x1F;
	else if(strcasecmp(tag,"td")==0)									*hex=0x1D;
	else if(strcasecmp(tag,"template")==0)								*hex=0x3B;
	else if(strcasecmp(tag,"timer")==0)									*hex=0x3C;
	else if(strcasecmp(tag,"tr")==0)									*hex=0x1E;
	else if(strcasecmp(tag,"u")==0)										*hex=0x3D;
	else if(strcasecmp(tag,"wml")==0)									*hex=0x3F;
	else 																*hex=0x04;
	return 0;
}

/***********************************************************************************/
/************************          attribute_list        ***************************/
/***********************************************************************************/

int attribute_list( char * attribute, unsigned char * hex) {	

	if(strcasecmp(attribute,"accept-charset")==0)						*hex=0x05;
	else if(strcasecmp(attribute,"align")==0)							*hex=0x52;
	else if(strcasecmp(attribute,"align=\"bottom\"")==0)				*hex=0x06;
	else if(strcasecmp(attribute,"align=\"center\"")==0)				*hex=0x07;
	else if(strcasecmp(attribute,"align=\"left\"")==0)					*hex=0x08;
	else if(strcasecmp(attribute,"align=\"middle\"")==0)				*hex=0x09;
	else if(strcasecmp(attribute,"align=\"right\"")==0)					*hex=0x0A;
	else if(strcasecmp(attribute,"align=\"top\"")==0)					*hex=0x0B;
	else if(strcasecmp(attribute,"alt")==0)								*hex=0x0C;
	else if(strcasecmp(attribute,"class")==0)							*hex=0x54;
	else if(strcasecmp(attribute,"columns")==0)							*hex=0x53;
	else if(strcasecmp(attribute,"content")==0)							*hex=0x0D;
	else if(strcasecmp(attribute,"content=\"application/vnd.\"")==0)	*hex=0x5C;
	else if(strcasecmp(attribute,"domain")==0)							*hex=0x0F;
	else if(strcasecmp(attribute,"emptyok=\"false\"")==0)				*hex=0x10;
	else if(strcasecmp(attribute,"emptyok=\"true\"")==0)				*hex=0x11;
	else if(strcasecmp(attribute,"format")==0)							*hex=0x12;
	else if(strcasecmp(attribute,"forua=\"false\"")==0)					*hex=0x56;
	else if(strcasecmp(attribute,"forua=\"true\"")==0)					*hex=0x57;
	else if(strcasecmp(attribute,"height")==0)							*hex=0x13;
	else if(strcasecmp(attribute,"href")==0)							*hex=0x4A;
	else if(strcasecmp(attribute,"href=\"http://")==0)					*hex=0x4B;
	else if(strcasecmp(attribute,"href=\"https://")==0)					*hex=0x4C;
	else if(strcasecmp(attribute,"hspace")==0)							*hex=0x14;
	else if(strcasecmp(attribute,"http-equiv")==0)						*hex=0x5A;
	else if(strcasecmp(attribute,"http-equiv=\"Content-type\"")==0)		*hex=0x5B;
	else if(strcasecmp(attribute,"http-equiv=\"Expires\"")==0)			*hex=0x5D;
	else if(strcasecmp(attribute,"id")==0)								*hex=0x55;
	else if(strcasecmp(attribute,"ivalue")==0)							*hex=0x15;
	else if(strcasecmp(attribute,"iname")==0)							*hex=0x16;
	else if(strcasecmp(attribute,"label")==0)							*hex=0x18;
	else if(strcasecmp(attribute,"localsrc")==0)						*hex=0x19;
	else if(strcasecmp(attribute,"maxlength")==0)						*hex=0x1A;
	else if(strcasecmp(attribute,"method=\"get\"")==0)					*hex=0x1B;
	else if(strcasecmp(attribute,"method=\"post\"")==0)					*hex=0x1C;
	else if(strcasecmp(attribute,"mode=\"nowrap\"")==0)					*hex=0x1D;
	else if(strcasecmp(attribute,"mode=\"wrap\"")==0)					*hex=0x1E;
	else if(strcasecmp(attribute,"multiple=\"false\"")==0)				*hex=0x1F;
	else if(strcasecmp(attribute,"multiple=\"true\"")==0)				*hex=0x20;
	else if(strcasecmp(attribute,"name")==0)							*hex=0x21;
	else if(strcasecmp(attribute,"newcontext=\"false\"")==0)			*hex=0x22;
	else if(strcasecmp(attribute,"newcontext=\"true\"")==0)				*hex=0x23;
	else if(strcasecmp(attribute,"onenterbackward")==0)					*hex=0x25;
	else if(strcasecmp(attribute,"onenterforward")==0)					*hex=0x26;
	else if(strcasecmp(attribute,"onpick")==0)							*hex=0x24;
	else if(strcasecmp(attribute,"ontimer")==0)							*hex=0x27;
	else if(strcasecmp(attribute,"optional=\"false\"")==0)				*hex=0x28;
	else if(strcasecmp(attribute,"optional=\"true\"")==0)				*hex=0x29;
	else if(strcasecmp(attribute,"path")==0)							*hex=0x2A;
	else if(strcasecmp(attribute,"scheme")==0)							*hex=0x2E;
	else if(strcasecmp(attribute,"sendreferer=\"false\"")==0)			*hex=0x2F;
	else if(strcasecmp(attribute,"sendreferer=\"true\"")==0)			*hex=0x30;
	else if(strcasecmp(attribute,"size")==0)							*hex=0x31;
	else if(strcasecmp(attribute,"src")==0)								*hex=0x32;
	else if(strcasecmp(attribute,"src=\"http://")==0)					*hex=0x58;
	else if(strcasecmp(attribute,"src=\"https://")==0)					*hex=0x59;
	else if(strcasecmp(attribute,"ordered=\"true\"")==0)				*hex=0x33;
	else if(strcasecmp(attribute,"ordered=\"false\"")==0)				*hex=0x34;
	else if(strcasecmp(attribute,"tabindex")==0)						*hex=0x35;
	else if(strcasecmp(attribute,"title")==0)							*hex=0x36;
	else if(strcasecmp(attribute,"type")==0)							*hex=0x37;
	else if(strcasecmp(attribute,"type=\"accept\"")==0)					*hex=0x38;
	else if(strcasecmp(attribute,"type=\"delete\"")==0)					*hex=0x39;
	else if(strcasecmp(attribute,"type=\"help\"")==0)					*hex=0x3A;
	else if(strcasecmp(attribute,"type=\"password\"")==0)				*hex=0x3B;
	else if(strcasecmp(attribute,"type=\"onpick\"")==0)					*hex=0x3C;
	else if(strcasecmp(attribute,"type=\"onenterbackward\"")==0)		*hex=0x3D;
	else if(strcasecmp(attribute,"type=\"onenterforward\"")==0)			*hex=0x3E;
	else if(strcasecmp(attribute,"type=\"ontimer\"")==0)				*hex=0x3F;
	else if(strcasecmp(attribute,"type=\"options\"")==0)				*hex=0x45;
	else if(strcasecmp(attribute,"type=\"prev\"")==0)					*hex=0x46;
	else if(strcasecmp(attribute,"type=\"reset\"")==0)					*hex=0x47;
	else if(strcasecmp(attribute,"type=\"text\"")==0)					*hex=0x48;
	else if(strcasecmp(attribute,"type=\"vnd.\"")==0)					*hex=0x49;
	else if(strcasecmp(attribute,"value")==0)							*hex=0x4D;
	else if(strcasecmp(attribute,"vspace")==0)							*hex=0x4E;
	else if(strcasecmp(attribute,"width")==0)							*hex=0x4F;
	else if(strcasecmp(attribute,"xml:lang")==0)						*hex=0x50;
	else 																*hex=0x04;
	return 0;
}

/***********************************************************************************/
/******************          attribute_value list       ****************************/
/***********************************************************************************/

int attribute_value(char * attribute_value, unsigned  char * hex) {

	if(strcmp(attribute_value,"accept")==0)							*hex=0x89;
	else if(strcmp(attribute_value,"bottom")==0)					*hex=0x8A;
	else if(strcmp(attribute_value,"clear")==0)						*hex=0x8B;
	else if(strcmp(attribute_value,"delete")==0)					*hex=0x8C;
	else if(strcmp(attribute_value,"help")==0)						*hex=0x8D;
	else if(strcmp(attribute_value,"middle")==0)					*hex=0x93;
	else if(strcmp(attribute_value,"nowrap")==0)					*hex=0x94;
	else if(strcmp(attribute_value,"onenterbackward")==0)			*hex=0x96;
	else if(strcmp(attribute_value,"onenterforward")==0)			*hex=0x97;
	else if(strcmp(attribute_value,"onpick")==0)					*hex=0x95;
	else if(strcmp(attribute_value,"ontimer")==0)					*hex=0x98;
	else if(strcmp(attribute_value,"options")==0)					*hex=0x99;
	else if(strcmp(attribute_value,"password")==0)					*hex=0x9A;
	else if(strcmp(attribute_value,"reset")==0)						*hex=0x9B;
	else if(strcmp(attribute_value,"text")==0)						*hex=0x9D;
	else if(strcmp(attribute_value,"top")==0)						*hex=0x9E;
	else if(strcmp(attribute_value,"unknown")==0)					*hex=0x9F;
	else if(strcmp(attribute_value,"wrap")==0)						*hex=0xA0;

/*no support for these values(unnessecery at this point), this results in
 *that the compression of the wml content isn't 100% */

/*	else if(strcasecmp(attribute_value,"Www.")==0)						*hex=0xA1;	
	else if(strcasecmp(attribute_value,"http://")==0)					*hex=0x8E;
	else if(strcasecmp(attribute_value,"http://www.")==0				*hex=0x8F;
	else if(strcasecmp(attribute_value,"https://")==0)					*hex=0x90;
	else if(strcasecmp(attribute_value,"https://www.")==0)				*hex=0x91;
	else if(strcasecmp(attribute_value,".com/")==0)						*hex=0x85;
	else if(strcasecmp(attribute_value,".edu/")==0)						*hex=0x86;
	else if(strcasecmp(attribute_value,".net/")==0)						*hex=0x87;
	else if(strcasecmp(attribute_value,".org/")==0)						*hex=0x88;*/

	else								*hex=0x04;

	return 0;
}

/***********************************************************************************/
/*******************          check_next_tag            ****************************/
/********** This function checks the next tag in order to determine  ***************/
/********** if the previous tag has content or not                   ***************/
/***********************************************************************************/

int check_next_tag(unsigned char *hex, char *temp) {

	char * temp_pointer;			
	unsigned char hex_temp;

	hex_temp=*hex;
	temp++;

	while(isspace(*temp))
		temp++;

	temp_pointer = temp;

	/* end of wml string, tag has no content */
	if( *temp=='\0') 
		goto no_content_end;
		
	temp_pointer++;

	if( (*temp=='<') && (*temp_pointer=='/') ) {
		temp_pointer++;
		/* define what the next tag is */
		define_tag(temp_pointer, hex);
		/* if the next tag is the same as the  previous then there's no content */
		if(hex_temp == *hex)
			goto no_content;
		else
			goto has_content;
		
	} else
		goto has_content;

has_content:
	return CONTENT;
	
no_content:
	return NO_CONTENT;

no_content_end:
	return NO_CONTENT_END;
}

/***********************************************************************************/
/**********************                element          ****************************/
/***********************************************************************************/

char * element(char *temp, unsigned char *hex, int *count, struct wmlc *binary_string,
				struct string_reference * string_table, int * event, int *offset,
				struct variable * variable, int * event_variable, int * string_table_counter) {	
				
	int test=0;
	char *tag_start=NULL, *tag_start_temp = NULL;
	unsigned char hex_memory = 0x00;
	
	temp++;
	if(isspace(*temp))
		temp++;

	if (*temp == '/') {
		goto stop;
	}
	else {
		tag_start_temp = temp;
		tag_start = temp;
	}

	define_tag(tag_start_temp, &hex_memory);	/* if previus tag was eg. <p> and the next tag is(and there is no content) </p> then convert it to <p/> */
	while (*tag_start_temp!='>') 
			tag_start_temp++;
	
	test = check_next_tag(&hex_memory, tag_start_temp);

 	 /* tag with no content */
	if (test == NO_CONTENT) {
		
		while (*temp!='>') {
			temp++;
		temp++;
		while (*temp!='>') 
			temp++;
		binary_string->wbxml[*count]=hex_memory;
			(*count)++;
		hex_memory = 0x00;
		return(temp);
		}
	}
	else if (test==NO_CONTENT_END) {
		goto stop;
	}

	/* check if the tag is an end tag */
stop:
	if (*temp == '/') {
		binary_string->wbxml[*count]=0x01;
		(*count)++;
		while( *temp!= '>') temp++;
		return (temp);
	}

	while (*temp!='\0') {
		/* Are we entering tag with attribute space? */
		if(isspace(*temp)) {
			/*
			define_tag(tag_start, hex);

			tag_end=temp;
			while (*tag_end!='>')
			tag_end++;	

			if (*--tag_end=='/') */ /** if tag ends with /> then it is certain it has no content **/
	/*
			else
			{	content = check_next_tag(hex, tag_end); */	/** check if tag has content **/
		/*		if (content==NO_CONTENT) {
				}	
			}*/
			
			temp = attribute_space(tag_start, count, binary_string, string_table, event, offset, variable, event_variable, string_table_counter);

			/* temp ends with a '>' */
			return (temp); 

		} else if (*temp == '/') {
			temp = define_tag(tag_start, hex); /*tag with no content and no attribute*/
			if (*hex==0x04) {
				printf("Unknown tag\n");
				printf("The tag started here: %s\n",tag_start);
				exit(1);
			} else {
			binary_string->wbxml[*count]=*hex;
			(*count)++;
			} /* if unknown tag */
			return(temp); /*temp stops with > */

		} else if (*temp =='>') { 
			define_tag(tag_start, hex);
			
			test = check_next_tag(hex, temp);
			if ((test==NO_CONTENT) || (test==NO_CONTENT_END)) {	 /* tag with no content */
				if (*hex==0x04) {
				printf("Unknown tag or attribute, cannot continue\n");
				printf("The tag or attribute started here: %s\n",tag_start);
				exit(1);
			} else {
				binary_string->wbxml[*count]=*hex;
				(*count)++;
			} /* if unknown tag */
			return (temp);
			} else if (test==CONTENT) { /* tag with content */
			
				if (*hex==0x04) {
					printf("Unknown tag or attribute, cannot continue\n");
					printf("The tag or attribute started here: %s\n",tag_start);
					exit(1);
				} else {
					*hex = *hex + 0x40;
					binary_string->wbxml[*count]=*hex;
					(*count)++;
				}
				return(temp); /*temp stops with > */

			} /* if test == CONTENT||NO_CONTENT */
		}
		temp++;
	}

	return (temp);
} 

/***********************************************************************************/
/******************               inline_string            *************************/
/***********************************************************************************/

char * inline_string(char *temp, int *count, struct wmlc *binary_string, struct variable * variable, int * event_variable) {

	int entity = 0, i = 0, hex_entity_count = 0; /* count_temp = 0;*/
	char hex_entity[10];
	char named_entity[10];
	char *tmp;
	static struct variable * variable_memory;
   		
	if ((*event_variable)!=0)
		variable = variable_memory;
	
	bzero (hex_entity,10);
	bzero (named_entity,10);

	if (*temp=='"') {
		temp++;
		return(++temp);
	}
	
	binary_string->wbxml[*count]=0x03;
    (*count)++;

	while (1) {
		if (*temp=='&') {
			temp++;
			
            if (*temp=='#')
            {	temp++;
               	if( *temp == ';') {
                	printf("entity without value\n");
                	binary_string->wbxml[*count]=0x00;
            		(*count)++;
					return (++temp);
                }	
				else if( *temp=='x' || *temp =='X') {
					strcat (hex_entity,"0x");
                    hex_entity_count=2;
                    temp++;
                    while( *temp!=';')
                    {       if (*temp=='\0')
                            {       printf("You have left an hexadecimal entity open ';'is missing\n");
                                    exit(1);
                            }
                            hex_entity[hex_entity_count]=*temp;
                            hex_entity_count++;
                            temp++;
                    }
                    hex_entity[hex_entity_count]='\0';
                    entity = strtol(hex_entity,NULL,16);
                    binary_string->wbxml[*count]=entity;
                    (*count)++;
                    temp++;
                    if (*temp=='$') {
					binary_string->wbxml[*count]=0x00;
            		(*count)++;}
                }

            	else {				
	                entity=atoi(temp);
	                binary_string->wbxml[*count]=entity;
	                (*count)++;
	                tmp = temp;

	                while(*temp!=';')
	                {	temp++;
	                    if (*temp=='\0')
	                    {	printf("You have left an entity open ';' is missing\n");
	                        binary_string->wbxml[*count]=0x00;
	            			(*count)++;
	                        return (--tmp);
	                    }
	                }
					temp++;
					if (*temp=='"' || *temp=='$' || *temp=='<' || *temp=='\0') {
						binary_string->wbxml[*count]=0x00;
	            		(*count)++;}
                }

			}
			else
			{	
            	if( *temp == ';') {
                	printf("entity without value\n");
                	binary_string->wbxml[*count]=0x00;
            		(*count)++;
					return (++temp);
                }	
				tmp = temp;

                while(*temp!=';')
                {	
					named_entity[i]=*temp;
					temp++;i++;

                    if (*temp=='\0')
                    {	printf("You have left an entity open ';' is missing\n");
                        binary_string->wbxml[*count]=0x00;
            			(*count)++;
                        return (--tmp);}
                }

				temp++;
			
				if(strstr(named_entity,"amp")!=NULL) {	
					binary_string->wbxml[*count]=0x26;
            		(*count)++;}
				else if(strstr(named_entity,"quot")!=NULL) {	
					binary_string->wbxml[*count]=0x22;
            		(*count)++;}
				else if(strstr(named_entity,"apos")!=NULL) {	
					binary_string->wbxml[*count]=0x27;
            		(*count)++;}
            	else if(strstr(named_entity,"lt")!=NULL) {	
					binary_string->wbxml[*count]=0x3c;
            		(*count)++;}
            	else if(strstr(named_entity,"gt")!=NULL) {	
					binary_string->wbxml[*count]=0x3e;
            		(*count)++;}
            	else if(strstr(named_entity,"nbsp")!=NULL) {	
					binary_string->wbxml[*count]=0xa0;
            		(*count)++;}
            	else if(strstr(named_entity,"shy")!=NULL) {	
					binary_string->wbxml[*count]=0xad;
            		(*count)++;}
            	else { printf("Unknown named entity %s", named_entity);}
            	
            	if (*temp=='$') {
					binary_string->wbxml[*count]=0x00;
            		(*count)++;}
            }
       
		}

		else if ( *temp == '$' )
		{	
			temp++;
			if ( *temp == '$' ) {
				binary_string->wbxml[*count]=0x03;
				(*count)++;
				binary_string->wbxml[*count]='$';
				(*count)++;
				binary_string->wbxml[*count]=0x00;
				(*count)++;
				
				temp++;
			}
			else {	
				
				temp--;
				temp = put_variable_in_memory (temp, variable, binary_string, count);
				variable = variable->next;
				variable_memory = variable;
				*event_variable=1;
			}				

			if (*temp!='"' && *temp!='$' && *temp!='<') {
				binary_string->wbxml[*count]=0x03;
				(*count)++;
			}
			if (*temp=='<'||*temp=='\0'||*temp=='\"') {
      			return(--temp);
      		}
		}
		
		else {
			if (*temp=='å') { binary_string->wbxml[*count]=0xe5;      (*count)++; temp++; } 
			if (*temp=='ä') { binary_string->wbxml[*count]=0xe4;      (*count)++; temp++; }
			if (*temp=='ö') { binary_string->wbxml[*count]=0xf6;      (*count)++; temp++; }
			if (*temp=='Å') { binary_string->wbxml[*count]=0xc5;      (*count)++; temp++; }
			if (*temp=='Ä') { binary_string->wbxml[*count]=0xc4;      (*count)++; temp++; }
			if (*temp=='Ö') { binary_string->wbxml[*count]=0xd6;      (*count)++; temp++; }
       		
       		if (*temp=='<'||*temp=='\0'||*temp=='\"') {
      			binary_string->wbxml[*count]=0x00;
          		(*count)++;
				return(--temp);
          	}
          	else {
          		binary_string->wbxml[*count]=*temp;
       			(*count)++;
       			temp++;
       			if ( *temp == '$' )
       			{	binary_string->wbxml[*count]=0x00;
       				(*count)++;
       			}
			}
        }
	}
}


/***********************************************************************************/
/**************************         attribute_space         ************************/
/***********************************************************************************/

char * attribute_space(char *temp, int *count, struct wmlc *binary_string, struct string_reference * string_table, int * event, 
		int *offset, struct variable * variable, int * event_variable, int * string_table_counter)

{
 	unsigned char tag_hex;
	unsigned char test, *hex_temp = NULL, *hex_temp_memory = NULL, hex_input_test = 0;
	unsigned char multibyte_integer[5];
	char *tag_end_memory = NULL, *tag_end = NULL;
	static struct string_reference * string_table_memory = NULL;
	int i=0, x=0, y=0, z=0, content=0, variable_counter=0, octets_temp = 0, count_temp = 0, extra_tag = 0;
	char tag[1000];
	char attribute[1000];

	bzero (multibyte_integer,5);
	bzero (tag,1000);
	bzero (attribute,1000);

	hex_temp = malloc(sizeof(unsigned char));
	tag_end = malloc(sizeof(unsigned char));
	hex_temp_memory = hex_temp;
	tag_end_memory = tag_end;
	
	if ((*event)==0)
	{	string_table_memory = string_table;
	}

	while( !isspace(*temp) )
	{	tag[i]=*temp;
		temp++; i++;
	}
	tag[i]='\0';

	tag_list(tag, &tag_hex); /************** define tag *************/

	if (tag_hex==0x04)
	{	printf("Unknown tag cannot continue!\n");
		printf("The tag was: %s\n", tag);
		free(hex_temp_memory);free(tag_end_memory);
		exit(1);}
		
	hex_input_test = tag_hex;

	tag_end=temp;
	while (*tag_end!='>')
	tag_end++;
	--tag_end;
	if (*tag_end=='/') /** if tag ends with /> then it is certain it has no content **/
	{	tag_hex = tag_hex + 0x80;
		binary_string->wbxml[*count]=tag_hex;
		(*count)++;	}

	else
	{	tag_end++;
		content = check_next_tag(&tag_hex, tag_end); /** check if tag has content **/
		
		if ((content==NO_CONTENT) || (content==NO_CONTENT_END)) 
		{	if (tag_hex==0x04)
			{	printf("Unknown tag or attribute, cannot continue!\n");
				printf("The tag was: %s\n", tag);
		
				exit(1); /** if unknown then exit **/	
			}
			else
			{	extra_tag = 1;
				tag_hex = tag_hex + 0x80; /** if tag has no content then add 0x80 **/
				binary_string->wbxml[*count]=tag_hex;
				(*count)++;
			}
		}

		else if (content==CONTENT) 
		{	if (tag_hex==0x04)
			{	printf("Unknown tag or attribute, cannot continue!\n");
				printf("The tag was: %s\n", tag);
	
				exit(1); /** if unknown then exit **/
			} 
			else
			{	tag_hex = tag_hex + 0xC0; /** if tag has content then add 0xC0 **/
				binary_string->wbxml[*count]=tag_hex;
				(*count)++;
			}
		}
	}

	i=0;x=0;
	while( x!=END ) /** define attribute and value **/ 
	{	while(x!=2)
		{	if(*temp=='>')
			{	x=END; break; }
			else if(isspace(*temp))
				temp++; 
			else if(*temp=='\"') 
			{	x++;
				attribute[i]=(*temp);
				temp++;
				i++;}
			else
			{	attribute[i]=(*temp);
				temp++;
				i++;}
		}
		attribute[i]='\0';

		if(x==END) 
		break;

		attribute_list(attribute, &test); 

		if (test==0x04) /** Unknown attribute with value, checking only attribute **/
		{	i=0;

			while( attribute[i]!='=' )
			{	tag[i]=attribute[i];
				i++;
			}

			tag[i]='\0';

			attribute_list(tag, &test); /************** check attribute *************/

			if (test==0x04)
			{	printf("Unknown attribute cannot continue\n");
				printf("The attribute was: %s\n",tag);
			
				exit(1);} /** if unknown attribute then exit **/

			else if(test==0x21 && (hex_input_test==0x2f || hex_input_test==0x3e || hex_input_test==0x37) )		/** attrib = name **/ /* 'input' or 'setvar' 'select'*/
			{
				binary_string->wbxml[*count]=test;	
				(*count)++;
				binary_string->wbxml[*count]=0x83;	
				(*count)++;
				*event = 1;
				
				variable_counter=0;i=i+2;z=i;
				while( attribute[i]!='\"' )
				{	tag[variable_counter]=attribute[i];
					i++;variable_counter++;}
				tag[variable_counter]='\0';

				bzero(multibyte_integer,5);
				count_temp = *count;
				if ( *offset > 0x81) {		/* if string table is greater than 0x81 then make string reference multibyte */
					octets_temp = write_variable_value( *offset , multibyte_integer);
					
					while (octets_temp > 0) {		/* place multibyte in binary string */
						binary_string->wbxml[octets_temp + count_temp - 1] = multibyte_integer[(octets_temp-1)];
						printf("%x",binary_string->wbxml[octets_temp + *count]);
						octets_temp--;
						(*count)++;
					}
				}
				else {
					binary_string->wbxml[*count] = *offset;
					(*count)++;
				}
				
				init_new_table_string ( string_table_memory, offset , tag);
				string_table_memory->next = make_new_table_string();	
				string_table_memory = string_table_memory->next;
				*offset = *offset+variable_counter+1;
				
		/*		binary_string->wbxml[3] += variable_counter + 1;*/
				*string_table_counter = *string_table_counter + variable_counter + 1;

			}

			else
			{	
				binary_string->wbxml[*count]=test;
				(*count)++;
				y=0;i=i+2;
				z=i;
			
				while( attribute[i] != '"' )
				{	tag[y]=attribute[i];
					
					if (i>400)
					exit(1);
					i++;y++;}

				tag[y]='\0';
				attribute_value(tag, &test); /********* check attribute value 	***********/
				if(test==0x04)
				{	inline_string(&attribute[z], count, binary_string, variable, event_variable);		
					/*** if unknown then make value inline string ***/
				}
				else
				{	binary_string->wbxml[*count]=test;
					(*count)++;
				}
			}
		}
		else
		{	binary_string->wbxml[*count]=test; /* add attribute with value to binary string */
			(*count)++;
		}
		i=0;
		x=0;
	}
	binary_string->wbxml[*count]=0x01; /**** END of attributes ********/
	(*count)++;
	/* free(hex_temp); */
	free(tag_end_memory);
	free(hex_temp_memory);

	if (extra_tag == 1)
	{	temp++;
		while (*temp!='>') 
		temp++;
	}
		
	return (temp); 
}


/***********************************************************************************/
/***************************   chars_to_lower_case   *******************************/
/***********************************************************************************/

int chars_to_lower_case(char *temp)
{
	while(*temp!='\0')
	{	if(*temp=='<')
		{	temp++;
			while(*temp!='>')
			{	*temp=tolower(*temp);	/** convert all tags and attributes to lower case **/
				temp++;
			}
		}
	temp++;
	}
	return 0;
}

/***********************************************************************************/
/***************************   check_tag_amount      *******************************/
/***********************************************************************************/

int check_tag_amount (char *temp)
{	int a=0, b=0;
	while(*temp!='\0')
	{	if(*temp=='<')
			a++;
		else if(*temp=='>')
			b++;	
	
		temp++;
	}
	if (a!=b)
	{	printf("There is an open tag.\n");
		printf("This might be a problem when parsing the WML tags.\n");

		return 0;
	}			/** check if any tag is left open, if yes then exit **/

	return 0;
}

/***********************************************************************************/
/****************************        main          *********************************/
/***********************************************************************************/

#ifdef BXML_TEST
int main(int argc, char *argv[])
{	
	struct wmlc *wmlc_data;

	int i=0;
	int fd;
	char tmpbuff[100*1024];
	char *wml;

/* Strings for testing purposes */

/*	char data[] = "<wml><card><p>Hello World</p></card></wml>";*/
	char data[] = "<wml><card><p type=\"accept\"></p></wml>";

	if(argc > 1) {			/* you can give an wml text file as an argument './wap_wml main.wml' */
		fd = open(argv[1], O_NONBLOCK);
		memset(tmpbuff, 0, sizeof(tmpbuff));		
		i = read(fd, tmpbuff, sizeof(tmpbuff));
		close(fd);
		wml = tmpbuff;
	} else {
		wml = data;
	}

	i=0;								/*****!!!!!!!****/
	wmlc_data = wml2wmlc(wml);
		/** wml2wmlc returns struct with binary content and length of content **/
									/*****!!!!!!!****/
	if (wmlc_data == NULL)
		return 0;
	while(i<wmlc_data->wml_length)
	{	printf("%02x\t",wmlc_data->wbxml[i]);			/** print the result **/
		i++;}
	printf("\n(%d) bytes\n",wmlc_data->wml_length);			/** how many bytes **/
	free (wmlc_data);
	
	return 0;
	
}
#endif

/***********************************************************************************/
/*************************          wml2wmlc     ***********************************/
/***********************************************************************************/

struct wmlc * wml2wmlc(char *string_pointer_from) { 

	struct wmlc *binary_string = NULL;
	struct string_reference *string_table = NULL, *string_table_temp = NULL, *string_table_memory = NULL;
	struct variable *variable = NULL, *variable_temp = NULL, *variable_temp2 = NULL, *variable_temp3 = NULL, *variable_temp4 = NULL, *variable_memory = NULL;
	unsigned char hex = 0x00;
	unsigned char multibyte_integer[5];
	char *temp = NULL, *temp2 = NULL, *temp_memory = NULL;
	int length = 0, count = 0, count_temp = 0, count_temp2 = 0, count_temp3 = 0, count_temp4 = 0;
	int  event = 0, event_variable = 0, string_table_counter = 0, octets = 0, octets_temp = 0, count_temp_multibyte = 0, octets_temp2 = 0;
	int number_of_stored_variables = 0;
	int number_of_stored_strings = 0;
	static int offset = 0;
	char string_pointer_to[LEN];
	char string_pointer_to1[LEN];
	char string_pointer_to2[LEN];

	bzero(multibyte_integer,5);
	bzero(string_pointer_to,LEN); 
	bzero(string_pointer_to1,LEN);
	bzero(string_pointer_to2,LEN);

	string_table = make_new_table_string();
	string_table_temp = string_table;
	string_table_memory = string_table;		/* put adress in temp so memory can be freed later */

	variable = make_new_variable();
	init_new_variable(variable);
	variable_memory = variable;			/* put adress in temp so memory can be freed later */

	variable_temp = variable;
	variable_temp3 = variable;

	variable_temp2 = make_new_variable();
	init_new_variable(variable_temp2);
	variable_temp4 = variable_temp2;
	
	if(strlen(string_pointer_from)==0) 			/** if no content then stop **/
	{	printf("No wml content\n");
		return NULL;}
	
	binary_string = malloc(sizeof(struct wmlc));		/** allocate memory for binary string struct **/
	temp = malloc(sizeof(unsigned char)); 			/** allocate memory for temp **/
	temp_memory=temp;

	newcr_to_space ( string_pointer_from, string_pointer_to ); 		/** convert \n and \t to white space **/

	length = strlen (string_pointer_to);

	tag_comment_del ( string_pointer_to, string_pointer_to1, length ); 	/** delete comments **/
	space_del (string_pointer_to1, string_pointer_to2); 	/** delete extra spaces **/
	temp = white_space_del(string_pointer_to2); 		/** delete spaces from beginning and end of wml string **/

	
	/* chars_to_lower_case(temp); */ 			/** make all tags and attributes lower case **/
	/** remove unnessecery data before wml content **/
	temp2 = temp;
	temp=strstr(temp, "<wml");
	if (temp==NULL)
	{ 	temp = temp2; }

	temp2=strstr(temp, "</wml>");	/** ignore everything after </wml> **/
	if (temp2!=NULL) {
		temp2=temp2+6;
		*temp2='\0';
	}
	else {
		printf("</wml> is missing\n");
	}
	check_tag_amount(temp);			/*checks if there are equal amounts of < > chars, if not then there is a possible syntax error */

#ifdef BXML_TEST
	printf("\n%s\n",temp);  /** print the stripped string **/
#endif

	/** These values are assumptions **/
	binary_string->wbxml[count]=0x01; /** WBXML Version number 1.1 **/
		count++; 
	binary_string->wbxml[count]=0x04; /** WML 1.1 Public ID **/
		count++;
	binary_string->wbxml[count]=0x04; /** Charset=ISO-8859-1 **/
		count++;
	binary_string->wbxml[count]=0x00; /** String table length=0 **/
		count++;

	while (*temp != '\0') {
					/******* element begins *******/
		if (*temp == '<') {
			temp = element(temp, &hex, &count, binary_string, string_table, &event, &offset, variable, &event_variable, &string_table_counter);
			temp++;
		}
		else if (isspace(*temp)) {
					/******* if space then next *******/
			temp++;
			if (*temp!='<' && *temp != '$') {
			temp--;
			temp = inline_string(temp, &count, binary_string, variable, &event_variable);
			temp++;
			}
		}
		else if ( *temp == '$' )
		{	temp++;
			if ( *temp == '$' ) {
				binary_string->wbxml[count]=0x03;
				count++;
				binary_string->wbxml[count]='$';
				count++;
				binary_string->wbxml[count]=0x00;
				count++;
				temp++;}
			else {
				temp--;
				temp = inline_string(temp, &count, binary_string, variable, &event_variable);
				temp++;
			}
		}
		else /******* inline string follows *******/
		{	temp = inline_string(temp, &count, binary_string, variable, &event_variable);
			temp++;
		}
	
	}
	

	count_temp=0;
	while (variable_temp != NULL )	/* count the amount of stored variables */
	{	variable_temp = variable_temp->next;
		count_temp++;
	}
	count_temp--;
	number_of_stored_variables = count_temp;

#ifdef BXML_TEST
	printf("\nNumber of stored variables: %d ", count_temp);
#endif
	
	while (string_table_temp != NULL )			/* count the amount of stored strings */
	{	string_table_temp = string_table_temp->next;
		count_temp3++;
		}
	count_temp3--;	
		number_of_stored_strings = count_temp3;

#ifdef BXML_TEST
	printf("\nNumber of stored strings: %d \n", count_temp3);
#endif

	count_temp = count;
	if (string_table_counter > 0) {
		while (count_temp>=4)			/*** make room for table strings **/
		{	binary_string->wbxml[(count_temp+string_table_counter)] = binary_string->wbxml[count_temp];
			count_temp--;
		}
	}
	
	count = count + string_table_counter;
	count_temp = 4;
	string_table_temp = string_table;

	while( count_temp3>0)			/*** put the strings in the right place in the string table ***/
	{	while(string_table_temp->string[count_temp2] != '\0')
		{	binary_string->wbxml[count_temp] = string_table_temp->string[count_temp2];
			count_temp++;
			count_temp2++;	
		}

		binary_string->wbxml[count_temp] = 0x00;
		string_table_temp = string_table_temp->next;
		count_temp++;
		count_temp2 = 0;
		count_temp3--;
	}

	if ( string_table_counter > 0x81) {	/*put the length of the string table in the binary string*/
		octets = write_variable_value( string_table_counter , multibyte_integer);
		octets_temp = octets;	
		count_temp = count;

		while (count_temp >= 4)			/* make room for multibyte in binary string */
		{	binary_string->wbxml[(count_temp + octets_temp)] = binary_string->wbxml[count_temp];
			count_temp--;
		}

		while (octets_temp > 0) {		/* place multibyte in binary string */
			binary_string->wbxml[octets_temp+2] = multibyte_integer[octets_temp-1];
			octets_temp--;
			count++;
		}
	}
		
	else {
		binary_string->wbxml[3] = string_table_counter;
	}
	
	string_table_temp = string_table;
									/*** give the variables the corresponding reference to the string table ***/
	count_temp = 0;
	while (count_temp < count)
	{	if (binary_string->wbxml[count_temp] == 0x80)
		{	count_temp++;
			if ( binary_string->wbxml[count_temp] == 0x00)
			{	
				while( strstr(string_table_temp->string, variable_temp3->string) == 0)
				{	
					string_table_temp = string_table_temp->next;
					if (string_table_temp == NULL)
					{	printf("\nReference for undefined variable: %s\n",variable_temp3->string);
						binary_string->wbxml[--count_temp] = 0x03;
						goto end;
					}
					
					
				}

				bzero (multibyte_integer,5);
				if ( string_table_temp->offset > 0x81) {
					octets_temp = write_variable_value( string_table_temp->offset , multibyte_integer);
					octets_temp2 = octets_temp;
					count_temp_multibyte = count_temp;
					count_temp4 = count;
					count_temp4--;
					
					while (count_temp4  > count_temp_multibyte )			/* make room for multibyte in binary string */
					{	binary_string->wbxml[(count_temp4 + octets_temp - 1)] = binary_string->wbxml[count_temp4];
						count_temp4--;
					}

					while (octets_temp > 0) {		/* place multibyte in binary string */
						binary_string->wbxml[count_temp_multibyte + octets_temp - 1] = multibyte_integer[octets_temp - 1];
						octets_temp--;
						count++;
					}
					count--;
					count_temp = count_temp + octets_temp2 -1;
					
				}
				else {
					binary_string->wbxml[count_temp] = string_table_temp->offset;
				}
				variable_temp3 = variable_temp3->next;
				if (variable_temp3 == NULL)
					break;

				string_table_temp = string_table;
			}
			else count_temp--;
		}
		count_temp++;
			
	}

end:
	event = 0;
	offset = 0;
	event_variable = 0;

	binary_string->wml_length=count; /** save the length result in the struct **/

	variable_temp = variable_memory->next;	 /*free memory which were used for variables*/
	while (variable_memory != NULL) {
		free ( variable_memory );
		variable_memory = variable_temp;
		if (variable_temp != NULL)
		variable_temp = variable_temp->next;
	}

	string_table_temp = string_table_memory->next;	 /*free memory which were used for strings*/
	while (string_table_memory != NULL) {
		free ( string_table_memory );
		string_table_memory = string_table_temp;
		if (string_table_temp != NULL)
		string_table_temp = string_table_temp->next;
	}

	free (temp_memory);

	return (binary_string);
}


/***********************************************************************************/
/*************************       put_variable_in_memory  ***************************/
/***********************************************************************************/

char * put_variable_in_memory ( char * temp, struct variable * variable, struct wmlc * binary_string, int * count) {
	int count_temp = 0;

	temp++;
	
	if (*temp=='(') {
		temp++;
		while ( *temp != ')')
		{	if( *temp ==':')
			{	while ( *temp !=')')
				temp++;
				break;
			}
			variable->string[count_temp] = *temp;
			temp++;
			count_temp++;
		}
	}
	else if (*temp!='(') {
		
		while ( *temp != ' ' )
		{	
			if (*temp =='<') {
				temp--;
				break;
			}
			
			variable->string[count_temp] = *temp;
			temp++;
			count_temp++;
			
		}
	}
	variable->string[count_temp]='\0';
	variable->next = make_new_variable();
	variable = variable->next;

	init_new_variable(variable);
	binary_string->wbxml[*count]=0x80;
	(*count)++;
	binary_string->wbxml[*count]=0x00;
	(*count)++;
	temp++;
	return (temp);
}

/***********************************************************************************/
/*************************    make_new_table_string      ***************************/
/***********************************************************************************/
 
struct string_reference * make_new_table_string (void)
{	
	struct string_reference *pointer;
	pointer = (struct string_reference *) calloc ( 1, sizeof( struct string_reference));
	if (pointer==NULL)
	{	
		printf("Out of memory");
		exit(1);
	}
	else
		return pointer;
}

/***********************************************************************************/
/*************************     init_new_table_string     ***************************/
/***********************************************************************************/

void init_new_table_string ( struct string_reference * pointer, int * offset, unsigned char * string )
{	
	bzero (pointer->string, 512);
	pointer->offset = *offset;
	strcpy(pointer->string, string);
	pointer->next = NULL;
}

/***********************************************************************************/
/*************************     make_new_variable         ***************************/
/***********************************************************************************/

struct variable * make_new_variable (void)
{	
	struct variable *pointer;
	pointer = (struct variable *) calloc ( 1, sizeof( struct variable));
	if (pointer==NULL)
	{	
		printf("Out of memory");
		exit(1);
	}
	else
		return pointer;
}

/***********************************************************************************/
/*************************     init_new_variable         ***************************/
/***********************************************************************************/

void init_new_variable ( struct variable * pointer )
{	
	bzero (pointer->string, 512);
	pointer->next = NULL;
}

