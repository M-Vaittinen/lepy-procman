#ifndef PROC_H
#define PROC_H

#include <sys/types.h>

#define PROC_NAME_MAX 256
#define PROC_LIST_MAX 4096

typedef struct {
	pid_t pid;
	char name[PROC_NAME_MAX];
	long rss_kb;       /* resident set size in kB */
	int oom_score_adj; /* current /proc/<pid>/oom_score_adj */
} proc_entry_t;

typedef struct {
	proc_entry_t *entries;
	int count;
	int capacity;
} proc_list_t;

proc_list_t *proc_list_create(void);
void proc_list_free(proc_list_t *list);
int proc_list_refresh(proc_list_t *list);

#endif /* PROC_H */
