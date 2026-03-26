#include "proc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

proc_list_t *proc_list_create(void)
{
	proc_list_t *list = calloc(1, sizeof(*list));
	if (!list)
		return NULL;
	list->capacity = 256;
	list->entries = calloc(list->capacity, sizeof(proc_entry_t));
	if (!list->entries) {
		free(list);
		return NULL;
	}
	return list;
}

void proc_list_free(proc_list_t *list)
{
	if (!list)
		return;
	free(list->entries);
	free(list);
}

static int is_pid_dir(const char *name)
{
	for (const char *p = name; *p; p++)
		if (!isdigit((unsigned char)*p))
			return 0;
	return name[0] != '\0';
}

int proc_list_refresh(proc_list_t *list)
{
	DIR *dir = opendir("/proc");
	if (!dir)
		return -1;

	list->count = 0;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (!is_pid_dir(ent->d_name))
			continue;

		pid_t pid = (pid_t)atoi(ent->d_name);

		/* Read process name from /proc/<pid>/comm */
		char path[64];
		snprintf(path, sizeof(path), "/proc/%d/comm", pid);
		FILE *f = fopen(path, "r");
		if (!f)
			continue;
		char name[PROC_NAME_MAX];
		if (!fgets(name, sizeof(name), f)) {
			fclose(f);
			continue;
		}
		fclose(f);
		/* Strip trailing newline */
		name[strcspn(name, "\n")] = '\0';

		/* Read RSS from /proc/<pid>/statm (field 2 = resident pages) */
		snprintf(path, sizeof(path), "/proc/%d/statm", pid);
		f = fopen(path, "r");
		long rss_pages = 0;
		if (f) {
			long dummy;
			fscanf(f, "%ld %ld", &dummy, &rss_pages);
			fclose(f);
		}
		long page_size_kb = 4; /* default 4 kB pages */
		                       /* Try to get actual page size */
#ifdef _SC_PAGESIZE
		long ps = sysconf(_SC_PAGESIZE);
		if (ps > 0)
			page_size_kb = ps / 1024;
#endif
		long rss_kb = rss_pages * page_size_kb;

		/* Read oom_score_adj */
		int oom_val = 0;
		snprintf(path, sizeof(path), "/proc/%d/oom_score_adj", pid);
		f = fopen(path, "r");
		if (f) {
			fscanf(f, "%d", &oom_val);
			fclose(f);
		}

		/* Grow if needed */
		if (list->count >= list->capacity) {
			int new_cap = list->capacity * 2;
			proc_entry_t *tmp =
			        realloc(list->entries,
			                (size_t)new_cap * sizeof(proc_entry_t));
			if (!tmp)
				break;
			list->entries = tmp;
			list->capacity = new_cap;
		}

		proc_entry_t *e = &list->entries[list->count++];
		e->pid = pid;
		strncpy(e->name, name, PROC_NAME_MAX - 1);
		e->name[PROC_NAME_MAX - 1] = '\0';
		e->rss_kb = rss_kb;
		e->oom_score_adj = oom_val;
	}

	closedir(dir);
	return list->count;
}
