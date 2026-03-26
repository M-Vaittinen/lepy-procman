#ifndef MODLIST_H
#define MODLIST_H

#include <sys/types.h>
#include "proc.h"

typedef struct {
	pid_t pid;
	char name[PROC_NAME_MAX];
	int oom_score_adj;
} mod_entry_t;

typedef struct {
	mod_entry_t *entries;
	int count;
	int capacity;
} modlist_t;

modlist_t *modlist_create(void);
void modlist_free(modlist_t *ml);

/* Record or update a modified process. */
void modlist_add(modlist_t *ml, pid_t pid, const char *name, int oom_score_adj);

/* Remove entries whose /proc/<pid> directory no longer exists. */
void modlist_purge_dead(modlist_t *ml);

/* Return 1 if /proc/<pid> exists (process still alive), 0 otherwise. */
int modlist_is_alive(pid_t pid);

#endif /* MODLIST_H */
