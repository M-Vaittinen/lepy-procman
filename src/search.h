#ifndef SEARCH_H
#define SEARCH_H

#include "proc.h"

typedef struct {
	proc_entry_t **matches; /* pointers into the source proc_list */
	int count;
	int capacity;
} search_result_t;

search_result_t *search_result_create(void);
void search_result_free(search_result_t *sr);

/*
 * Filter proc_list by name (case-insensitive substring).
 * If term is NULL or empty, all entries are included.
 * Results point into list->entries; do not free them separately.
 */
int search_filter(search_result_t *sr,
                  const proc_list_t *list,
                  const char *term);

#endif /* SEARCH_H */
