/*
 * cfg.h - configuration file handling
 *
 * All returned octet strings are copies which the caller should destroy.
 *
 * Lars Wirzenius
 */


#ifndef CFG_H
#define CFG_H

typedef struct Cfg Cfg;
typedef struct CfgLoc CfgLoc; 
typedef struct CfgGroup CfgGroup;

Cfg *cfg_create(Octstr *filename);
void cfg_destroy(Cfg *cfg);
int cfg_read(Cfg *cfg);

CfgGroup *cfg_get_single_group(Cfg *cfg, Octstr *name);
List *cfg_get_multi_group(Cfg *cfg, Octstr *name);
Octstr *cfg_get_group_name(CfgGroup *grp);
Octstr *cfg_get_configfile(CfgGroup *grp);

Octstr *cfg_get_real(CfgGroup *grp, Octstr *varname, const char *file,
    	    	     long line, const char *func);
#define cfg_get(grp, varname) \
    cfg_get_real(grp, varname, __FILE__, __LINE__, __func__)
int cfg_get_integer(long *n, CfgGroup *grp, Octstr *varname);

/* Return -1 and set n to 0 if no varname found
 * otherwise return 0 and set n to 0 if value is no, false, off or 0,
 * and 1 if it is true, yes, on or 1. Set n to 1 for other values, too
 */
int cfg_get_bool(int *n, CfgGroup *grp, Octstr *varname);
List *cfg_get_list(CfgGroup *grp, Octstr *varname);
void cfg_set(CfgGroup *grp, Octstr *varname, Octstr *value);

void grp_dump(CfgGroup *grp);
void cfg_dump(Cfg *cfg);

#endif
