#define _GNU_SOURCE
#include "search.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasestr */

search_result_t *search_result_create(void)
{
	search_result_t *sr = calloc(1, sizeof(*sr));
	if (!sr)
		return NULL;
	sr->capacity = 256;
	sr->matches = calloc(sr->capacity, sizeof(proc_entry_t *));
	if (!sr->matches) {
		free(sr);
		return NULL;
	}
	return sr;
}

void search_result_free(search_result_t *sr)
{
	if (!sr)
		return;
	free(sr->matches);
	free(sr);
}

int search_filter(search_result_t *sr,
                  const proc_list_t *list,
                  const char *term)
{
	sr->count = 0;

	int use_filter = (term && term[0] != '\0');

	for (int i = 0; i < list->count; i++) {
		proc_entry_t *e = &list->entries[i];
		if (use_filter && !strcasestr(e->name, term))
			continue;

		if (sr->count >= sr->capacity) {
			int new_cap = sr->capacity * 2;
			proc_entry_t **tmp = realloc(
			        sr->matches,
			        (size_t)new_cap * sizeof(proc_entry_t *));
			if (!tmp)
				return -1;
			sr->matches = tmp;
			sr->capacity = new_cap;
		}
		sr->matches[sr->count++] = e;
	}
	return sr->count;
}
