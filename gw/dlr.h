/* dlr.h
*/


#ifndef	DLR_H
#define	DLR_H	1

#define	DLR_SUCCESS		0x01
#define	DLR_FAIL		0x02

void	dlr_init();
void	dlr_shutdown();

void	dlr_add(char *smsc, char *ts, char *dst, char *keyword, char *id, int mask);
Msg *  dlr_find(char *smsc, char *ts, char *dst, int type);
void	dlr_save(const char *filename);
void	dlr_load(const char *filename);

#endif