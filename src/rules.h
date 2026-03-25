#ifndef RULES_H
#define RULES_H

#include "proc.h"

typedef struct {
	char name[PROC_NAME_MAX];
	int oom_score_adj;
} rule_t;

typedef struct {
	rule_t *entries;
	int count;
	int capacity;
} rules_t;

rules_t *rules_create(void);
void rules_free(rules_t *r);

int rules_load(rules_t *r);
int rules_save(const rules_t *r);

/* Add or update a rule (by name). */
void rules_upsert(rules_t *r, const char *name, int oom_score_adj);

/* Remove a rule by name. Returns 1 if found and removed, 0 if not found. */
int rules_remove(rules_t *r, const char *name);

/* Apply rules to every matching process in list. Returns number of procs
 * updated. */
int rules_apply(const rules_t *r, proc_list_t *list);

/* Find stored rule for name, or NULL. */
const rule_t *rules_find(const rules_t *r, const char *name);

#endif /* RULES_H */
