/*
 * conffile.h - configuration file handling

This file supports configuration files that consist of groups of
variables.  For example, a configuration file for SMS services might
look like this:

	smsc = idefix.radiolinja.fi
	protocol = cimd
	port = 12345
	username = foo
	password = bar

	smsc = localhost
	protocol = fake
	port = 8989

	service = default
	url = %s
	
Each group of variables is stored in a separate object, and each group
can have its own set of variables.


 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef CONFIG_H
#define CONFIG_H

typedef struct ConfigVar {
	struct ConfigVar *next;
	char *name;
	char *value;
} ConfigVar;

typedef struct ConfigGroup {
	struct ConfigGroup *next;
	ConfigVar *varlist, *lastvar;
} ConfigGroup;

typedef struct Config {
	char *filename;
	ConfigGroup *grouplist, *lastgroup;
} Config;


/* Create a new Config object. It has a filename, but is otherwise empty. */
Config *config_create(char *filename);

/* Forget all configuration variables. */
void config_clear(Config *cfg);

/* Destroy the configuration object. */
void config_destroy(Config *cfg);

/* Read a configuration file. */
int config_read(Config *cfg);

/* Write a configuration file. */
int config_write(Config *cfg);

/* read from given file and create. 'default' is used if filename is NULL.
 * default MUST be set if filename is NULL! */
Config *config_from_file(char *filename, char *default_file);

/* Get the first configuration group. */
ConfigGroup *config_first_group(Config *cfg);

/* Get the next configuration group. */
ConfigGroup *config_next_group(ConfigGroup *grp);

/* Find the first group where variable `name' has value `value'. */
ConfigGroup *config_find_first_group(Config *cfg, char *name, char *value);

/* Find the next group where variable `name' has value `value'. */
ConfigGroup *config_find_next_group(ConfigGroup *grp, char *name, char *value);

/* Add a new group at end. */
ConfigGroup *config_add_group(Config *cfg);

/* Remove a group. */
void config_remove_group(Config *cfg, ConfigGroup *grp);

/* Get the value of a variable in a group. */
char *config_get(ConfigGroup *grp, char *name);

/* Set the value of a variable in a group. The variable need not exist
   before. `value' is copied, so caller does not have to worry about
   not tampering with `value'. */
int config_set(ConfigGroup *grp, char *name, char *value);

/* For debugging: dump contents of a Config object (using info() in 
   gwlib/log.h). */
void config_dump(Config *cfg);


#endif
