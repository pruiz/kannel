/*
 * cfg.h - configuration file handling
 *
 * Lars Wirzenius
 */


#ifndef CFG_H
#define CFG_H

typedef struct Cfg Cfg;
typedef struct CfgGroup CfgGroup;

Cfg *cfg_create(Octstr *filename);
void cfg_destroy(Cfg *cfg);
int cfg_read(Cfg *cfg);

CfgGroup *cfg_get_single_group(Cfg *cfg, Octstr *name);
List *cfg_get_multi_group(Cfg *cfg, Octstr *name);

Octstr *cfg_get(CfgGroup *grp, Octstr *varname);
void cfg_set(CfgGroup *grp, Octstr *varname, Octstr *value);

void cfg_dump(Cfg *cfg);

#endif
