/*
 * config.c - configuration file handling
 *
 * Lars Wirzenius for WapIT Ltd.
 */


#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gwlib.h"


static ConfigVar *find_var(ConfigGroup *grp, char *name);
static ConfigGroup *find_group_starting_with(ConfigGroup *grp, char *name, 
					     char *value);
static char *parse_value(char *str);


Config *config_create(char *filename) {
	Config *cfg;
	
	cfg = malloc(sizeof(Config));
	if (cfg == NULL)
		goto error;
	
	cfg->filename = strdup(filename);
	if (cfg->filename == NULL) {
	        free(cfg);
		goto error;
	}
	cfg->grouplist = NULL;
	cfg->lastgroup = NULL;
	return cfg;

error:
	error(errno, "config_create: Out of memory error");
	return NULL;
}


void config_destroy(Config *cfg) {
	config_clear(cfg);
	free(cfg->filename);
	free(cfg);
}


ConfigGroup *config_add_group(Config *cfg) {
	ConfigGroup *grp;
	
	grp = malloc(sizeof(ConfigGroup));
	if (grp == NULL) {
		error(errno, "config.c/create_group: Out of memory error");
		return NULL;
	}
	
	grp->next = NULL;
	grp->varlist = NULL;
	grp->lastvar = NULL;

	if (cfg->grouplist == NULL) {
	    cfg->grouplist = grp;
	    cfg->lastgroup = grp;
	} else {
	    cfg->lastgroup->next = grp;
	    cfg->lastgroup = grp;
	}

	return grp;
}


void config_remove_group(Config *cfg, ConfigGroup *grp) {
	ConfigGroup *g;
	ConfigVar *var, *next;

	g = cfg->grouplist;
	if (grp == NULL || g == NULL)
		return;
	else if (g == grp)
		cfg->grouplist = cfg->grouplist->next;
	else {
		while (g != NULL && g->next != grp)
			g = g->next;
		if (g == NULL)
			return;
		g->next = grp->next;
	}
	for (var = grp->varlist; var != NULL; var = next) {
		next = var->next;
		free(var->name);
		free(var->value);
		free(var);
	}
	free(grp);
}


char *config_get(ConfigGroup *grp, char *name) {
	ConfigVar *var;
	
	for (var = grp->varlist; var != NULL; var = var->next)
		if (strcmp(name, var->name) == 0)
			return var->value;
	return NULL;
}


int config_set(ConfigGroup *grp, char *name, char *value) {
	ConfigVar *var, *newvar;
	char *newvalue;

	if (value == NULL)
		return 0;

	newvar = NULL;
	var = find_var(grp, name);
	if (var == NULL) {
		newvar = malloc(sizeof(ConfigVar));
		if (newvar == NULL)
			goto error;

		newvar->value = NULL;
		newvar->next = NULL;
		newvar->name = strdup(name);
		if (newvar->name == NULL)
			goto error;

		if (grp->varlist == NULL) {
			grp->varlist = newvar;
			grp->lastvar = newvar;
		} else {
			grp->lastvar->next = newvar;
			grp->lastvar = newvar;
		}

		var = newvar;
	}

	newvalue = strdup(value);
	if (newvalue == NULL)
		goto error;
	if (var->value != NULL)
		free(var->value);
	var->value = newvalue;
	
	return 0;

error:
	error(errno, "config.c/config_set: Out of memory error.");
	if (newvar != NULL) {
		free(newvar->name);
		free(newvar->value);
		free(newvar);
	}
	return -1;
}


void config_clear(Config *cfg) {
	ConfigGroup *grp, *grp_next;
	ConfigVar *var, *var_next;
	
	for (grp = cfg->grouplist; grp != NULL; grp = grp_next) {
		grp_next = grp->next;
		for (var = grp->varlist; var != NULL; var = var_next) {
			var_next = var->next;
			free(var->name);
			free(var->value);
			free(var);
		}
		free(grp);
	}
	cfg->grouplist = NULL;
	cfg->lastgroup = NULL;
}


int config_read(Config *cfg) {
	FILE *f;
	char line[10*1024];
	char *s, *p, *value;
	ConfigGroup *grp;
	long lineno;
	
	config_clear(cfg);
	grp = NULL;
	value = NULL;

	f = fopen(cfg->filename, "r");
	if (f == NULL) {
		error(errno,
			"config_read: couldn't read configuration file `%s'",
			cfg->filename);
		goto error;
	}

	lineno = 0;
	for (;;) {
		++lineno;
		if (fgets(line, sizeof(line), f) == NULL) {
			if (!ferror(f))
				break;
			error(errno,
				"config_read: Error reading `%s'",
				cfg->filename);
			goto error;
		}
		s = trim_ends(line);
		if (*s == '#')
			continue;
		if (*s == '\0') {
			grp = NULL;
			continue;
		}

		p = strchr(s, '=');
		if (p == NULL) {
			error(0, "%s:%ld:syntax error", cfg->filename, lineno);
			goto error;
		}
		*p++ = '\0';
		s = trim_ends(s);
		value = parse_value(p);
		if (value == NULL)
			goto error;

		if (grp == NULL) {
			grp = config_add_group(cfg);
			if (grp == NULL)
				goto error;
		}
		if (config_set(grp, s, value) == -1)
			goto error;
		free(value);
		value = NULL;
	}

	if (fclose(f) != 0) {
		error(errno, "Error closing `%s'", cfg->filename);
		goto error;
	}
	return 0;

error:
	if (f != NULL)
		fclose(f);
	free(value);
	config_clear(cfg);
	return -1;
}


int config_write(Config *cfg) {
	FILE *f;
	ConfigGroup *grp;
	ConfigVar *var;
	char tempname[FILENAME_MAX * 2];

	sprintf(tempname, "%s.new", cfg->filename);
	f = fopen(tempname, "w");
	if (f == NULL) {
		error(errno, "Couldn't open `%s' for writing.", tempname);
		goto error;
	}
	
	for (grp = cfg->grouplist; grp != NULL; grp = grp->next) {
		for (var = grp->varlist; var != NULL; var = var->next)
			fprintf(f, "%s = %s\n", var->name, var->value);
		fprintf(f, "\n");
	}

	if (fclose(f) != 0) {
		error(errno, "Error closing `%s'.", tempname);
		goto error;
	}
	
	if (rename(tempname, cfg->filename) == -1) {
		error(errno, "Error renaming new config file to correct name.");
		goto error;
	}

	return 0;

error:
	if (f != NULL)
		fclose(f);
	return -1;
}


Config *config_from_file(char *filename, char *default_file) {
    Config *cfg;

    if (filename == NULL)
	filename = default_file;
    
    info(0, "Reading configuration from <%s>", filename);
    cfg = config_create(filename);
    if (cfg == NULL)
        return NULL;
    if (config_read(cfg) == -1) {
        config_destroy(cfg);
        return NULL;
    }
    return cfg;
}


ConfigGroup *config_first_group(Config *cfg) {
	return cfg->grouplist;
}


ConfigGroup *config_next_group(ConfigGroup *grp) {
	return grp->next;
}


ConfigGroup *config_find_first_group(Config *cfg, char *name, char *value) {
	return find_group_starting_with(cfg->grouplist, name, value);
}


ConfigGroup *config_find_next_group(ConfigGroup *grp, char *name, char *value)
{
	return find_group_starting_with(grp->next, name, value);
}


void config_dump(Config *cfg) {
	ConfigGroup *grp;
	ConfigVar *var;
	
	info(0, "Config dump begins:");
	info(0, "filename = <%s>", cfg->filename);
	for (grp = cfg->grouplist; grp != NULL; grp = grp->next) {
		info(0, "group:");
		for (var = grp->varlist; var != NULL; var = var->next)
			info(0, "  <%s> = <%s>", var->name, var->value);
	}
	info(0, "Config dump ends.");
}


static ConfigVar *find_var(ConfigGroup *grp, char *name) {
	ConfigVar *var;
	
	for (var = grp->varlist; var != NULL; var = var->next)
		if (strcmp(var->name, name) == 0)
			return var;
	return NULL;
}


static ConfigGroup *find_group_starting_with(ConfigGroup *grp,
	char *name, char *value)
{
	char *v;
	
	while (grp != NULL) {
		v = config_get(grp, name);
		if (v != NULL && strcmp(v, value) == 0)
			break;
		grp = grp->next;
	}
	return grp;
}


static char *parse_value(char *str) {
	char *p, *str2;

	str = trim_ends(str);

	if (*str == '"') {
		str2 = malloc(strlen(str) + 1);
		if (str2 == NULL)
			goto error;
		++str;	/* skip leading '"' */
		p = str2;
		while (*str != '"' && *str != '\0') {
			switch (*str) {
			case '\\':
				switch (str[1]) {
				case '\0':
					*p++ = '\\';
					str += 1;
					break;
				case '\\':
					*p++ = '\\';
					str += 2;
					break;
				case '"':
					*p++ = '"';
					str += 2;
					break;
				default:
					*p++ = '\\';
					*p++ = str[1];
					str += 2;
					break;
				}
				break;
			default:
				*p++ = *str++;
				break;
			}
		}
		*p = '\0';
	} else {
		str2 = strdup(str);
		if (str2 == NULL)
			goto error;
	}

	return str2;

error:
	error(errno, "Out of memory allocating parsed value");
	return NULL;
}
