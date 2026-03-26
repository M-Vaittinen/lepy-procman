#include "modlist.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

modlist_t *modlist_create(void)
{
	modlist_t *ml = calloc(1, sizeof(*ml));

	if (!ml)
		return NULL;
	ml->capacity = 64;
	ml->entries = calloc(ml->capacity, sizeof(mod_entry_t));
	if (!ml->entries) {
		free(ml);
		return NULL;
	}

	return ml;
}

void modlist_free(modlist_t *ml)
{
	if (!ml)
		return;
	free(ml->entries);
	free(ml);
}

int modlist_is_alive(pid_t pid)
{
	char path[64];
	struct stat st;

	snprintf(path, sizeof(path), "/proc/%d", (int)pid);

	return stat(path, &st) == 0;
}

void modlist_add(modlist_t *ml, pid_t pid, const char *name, int oom_score_adj)
{
	mod_entry_t *e;
	int i;

	/* Update existing entry if PID already tracked */
	for (i = 0; i < ml->count; i++) {
		if (ml->entries[i].pid == pid) {
			ml->entries[i].oom_score_adj = oom_score_adj;
			strncpy(ml->entries[i].name, name, PROC_NAME_MAX - 1);
			ml->entries[i].name[PROC_NAME_MAX - 1] = '\0';

			return;
		}
	}

	/* Grow if needed */
	if (ml->count >= ml->capacity) {
		int new_cap = ml->capacity * 2;
		mod_entry_t *tmp = realloc(
		        ml->entries, (size_t)new_cap * sizeof(mod_entry_t));

		if (!tmp)
			return;
		ml->entries = tmp;
		ml->capacity = new_cap;
	}

	e = &ml->entries[ml->count++];
	e->pid = pid;
	strncpy(e->name, name, PROC_NAME_MAX - 1);
	e->name[PROC_NAME_MAX - 1] = '\0';
	e->oom_score_adj = oom_score_adj;
}

void modlist_purge_dead(modlist_t *ml)
{
	int i = 0;

	while (i < ml->count) {
		if (!modlist_is_alive(ml->entries[i].pid)) {
			/* Remove by swapping with last entry */
			ml->entries[i] = ml->entries[ml->count - 1];
			ml->count--;
		} else {
			i++;
		}
	}
}
