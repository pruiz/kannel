/*
 * conffile.c - configuration file handling
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


Config *config_create(char *filename)
{
    Config *cfg;

    cfg = gw_malloc(sizeof(Config));
    cfg->filename = gw_strdup(filename);
    cfg->grouplist = NULL;
    cfg->lastgroup = NULL;
    return cfg;
}


void config_destroy(Config *cfg)
{
    if (cfg == NULL)
        return;
    config_clear(cfg);
    gw_free(cfg->filename);
    gw_free(cfg);
}


ConfigGroup *config_add_group(Config *cfg)
{
    ConfigGroup *grp;

    grp = gw_malloc(sizeof(ConfigGroup));
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


void config_remove_group(Config *cfg, ConfigGroup *grp)
{
    ConfigGroup *g;
    ConfigVar *var, *next;

    if (cfg == NULL || grp == NULL)
        return;
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
        gw_free(var->name);
        gw_free(var->value);
        gw_free(var);
    }
    gw_free(grp);
}


char *config_get(ConfigGroup *grp, char *name)
{
    ConfigVar *var;
    if (grp == NULL) return NULL;

    for (var = grp->varlist; var != NULL; var = var->next)
        if (strcmp(name, var->name) == 0)
            return var->value;
    return NULL;
}


void config_set(ConfigGroup *grp, char *name, char *value)
{
    ConfigVar *var, *newvar;
    char *newvalue;

    if (value == NULL)
        return;

    newvar = NULL;
    var = find_var(grp, name);
    if (var == NULL) {
        newvar = gw_malloc(sizeof(ConfigVar));

        newvar->value = NULL;
        newvar->next = NULL;
        newvar->name = gw_strdup(name);

        if (grp->varlist == NULL) {
            grp->varlist = newvar;
            grp->lastvar = newvar;
        } else {
            grp->lastvar->next = newvar;
            grp->lastvar = newvar;
        }

        var = newvar;
    }

    newvalue = gw_strdup(value);
    if (var->value != NULL)
        gw_free(var->value);
    var->value = newvalue;
}


void config_clear(Config *cfg)
{
    ConfigGroup *grp, *grp_next;
    ConfigVar *var, *var_next;

    if (cfg == NULL)
        return;

    for (grp = cfg->grouplist; grp != NULL; grp = grp_next) {
        grp_next = grp->next;
        for (var = grp->varlist; var != NULL; var = var_next) {
            var_next = var->next;
            gw_free(var->name);
            gw_free(var->value);
            gw_free(var);
        }
        gw_free(grp);
    }
    cfg->grouplist = NULL;
    cfg->lastgroup = NULL;
}


int config_read(Config *cfg)
{
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
            error(0, "%s:%ld:syntax error", cfg->filename,
                  lineno);
            goto error;
        }
        *p++ = '\0';
        s = trim_ends(s);
        value = parse_value(p);
        if (value == NULL)
            goto error;

        if (grp == NULL) {
            grp = config_add_group(cfg);
        }
        config_set(grp, s, value);
        gw_free(value);
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
    gw_free(value);
    config_clear(cfg);
    return -1;
}


int config_write(Config *cfg)
{
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
        error(errno, "Error renaming new config file to "
              "correct name.");
        goto error;
    }

    return 0;

error:
    if (f != NULL)
        fclose(f);
    return -1;
}


Config *config_from_file(char *filename, char *default_file)
{
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


ConfigGroup *config_first_group(Config *cfg)
{
    return cfg->grouplist;
}


ConfigGroup *config_next_group(ConfigGroup *grp)
{
    if (grp == NULL) return NULL;
    return grp->next;
}


ConfigGroup *config_find_first_group(Config *cfg, char *name, char *value)
{
    return find_group_starting_with(cfg->grouplist, name, value);
}


ConfigGroup *config_find_next_group(ConfigGroup *grp, char *name, char *value)
{
    if (grp == NULL) return NULL;
    return find_group_starting_with(grp->next, name, value);
}


void config_dump(Config *cfg)
{
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


static ConfigVar *find_var(ConfigGroup *grp, char *name)
{
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


static char *parse_value(char *str)
{
    char *p, *str2;

    str = trim_ends(str);

    if (*str == '"') {
        str2 = gw_malloc(strlen(str) + 1);
        ++str; 	/* skip leading '"' */
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
        str2 = gw_strdup(str);
    }

    return str2;
}


int config_sanity_check(Config *config)
{
    ConfigGroup *grp;
    char *group;
    int core, smsbox, wapbox, smsc, sms_service;
    int errors = 0;

    core = smsbox = wapbox = smsc = sms_service = errors = 0;

    grp = config_first_group(config);
    while (grp != NULL) {
        group = config_get(grp, "group");
        if (group == NULL) {
            error(0, "A group without 'group' variable in configuration");
            return -1;
        } else if (strcmp(group, "core") == 0)
            core++;
        else if (strcmp(group, "smsbox") == 0)
            smsbox++;
        else if (strcmp(group, "wapbox") == 0)
            wapbox++;
        else if (strcmp(group, "smsc") == 0)
            smsc++;
        else if (strcmp(group, "sms-service") == 0)
            sms_service++;
        else if (strcmp(group, "sms-service") != 0
                 && strcmp(group, "sendsms-user") != 0) {
            error(0, "Unknown group '%s' in configuration", group);
	    errors++;
        }

        grp = config_next_group(grp);
    }
    if (core == 0) {
        error(0, "No 'core' group in configuration");
	errors++;
    }
    else if (core > 1) {
        error(0, "More than one 'core' group in configuration");
	errors++;
    }

    if (smsbox > 0) {
	if (smsbox > 1) {
	    error(0, "More than one 'core' group in configuration");
	    errors++;
	}
	if (smsc == 0) {
	    error(0, "'smsbox' group without 'smsc' groups");
	    errors++;
	} else if (sms_service == 0) {
	    error(0, "'smsbox' group without 'sms-service' groups");
	    errors++;
	}
    }
    if (wapbox > 1) {
        error(0, "More than one 'wapbox' group in configuration");
	errors++;
    }
    if (errors > 0)
	return -1;
    else
	return 0;  /* all OK - after initial check */
}

