/*
 * numhash.h - (telephone) number storing/hashing system
 *
 * Kalle Marjola 2000 for project Kannel
 *
 * !!! NOTE NOTE NOTE !!!
 *
 * Phone number precision is limited according to sizeof(long)
 * in host machine. that is usually either 32 or 64 bits. In a
 * case of 32 bit longs, only last 9 digits are checked, otherwise
 * last 18 digits. This means that in some places several numbers
 * might map to same hash entry, and thus some caution is needed
 * especially with telephone number black lists
 *
 * USAGE:
 *  the system is not very dynamic; if you want to resize the table
 *  or hash, you must first nuke all old data and then recreate it
 *
 * MEMORY NEEDED:  (approximated)
 *
 * 2* (sizeof(long)+sizeof(void *)) bytes per number
 */

#ifndef NUMHASH_H
#define NUMHASH_H

#include <stdio.h>

/* number hashing/seeking functions
 * all return -1 on error and write to general Kannel log
 *
 * these 2 first are only required if you want to add the numbers
 * by hand - otherwise use the last function instead
 *
 * use prime_hash if you want an automatically generated hash size
 */

typedef struct numhash_table Numhash;	


/* get numbers from 'url' and create a new database out of them
 * Return NULL if cannot open database or other error, error is logged
 *
 * Numbers to datafile are saved as follows:
 *  - one number per line
 *  - number might have white spaces, '+' and '-' signs
 *  - number is ended with ':' or end-of-line
 *  - there can be additional comment after ':'
 *
 * For example, all following ones are valid lines:
 *  040 1234
 *  +358 40 1234
 *  +358 40-1234 : Kalle Marjola
 */
Numhash *numhash_create(char *url); 

/* destroy hash and all numbers in it */
void numhash_destroy(Numhash *table);

/* check if the number is in database, return 1 if found, 0 if not,
 * -1 on error */
int numhash_find_number(Numhash *table, Octstr *nro);
				      
/* if we already have the key */
int numhash_find_key(Numhash *table, long key);		

/* if we want to know the key */
long numhash_get_key(Octstr *nro);
long numhash_get_char_key(char *nro);


/* Return hash fill percent. If 'longest' != NULL, set as longest
 * trail in hash */
double numhash_hash_fill(Numhash *table, int *longest);

/* return number of numbers in hash */
int numhash_size(Numhash *table);

#endif
