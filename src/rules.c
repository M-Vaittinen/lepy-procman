#include "rules.h"
#include "oom.h"
#include "userhome.h"
#include "cJSON.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* Returns path to rules file: ~/.config/lepy-procman/rules.json */
static int get_rules_path(char *buf, size_t size)
{
	const char *home = userhome_get();
	if (!home || !*home)
		home = "/root";
	snprintf(buf, size, "%s/.config/lepy-procman/rules.json", home);
	return 0;
}

static int ensure_config_dir(void)
{
	char path[512];
	const char *home = userhome_get();
	if (!home || !*home)
		home = "/root";
	snprintf(path, sizeof(path), "%s/.config", home);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/.config/lepy-procman", home);
	if (mkdir(path, 0755) < 0 && errno != EEXIST)
		return -1;
	return 0;
}

rules_t *rules_create(void)
{
	rules_t *r = calloc(1, sizeof(*r));
	if (!r)
		return NULL;
	r->capacity = 64;
	r->entries = calloc(r->capacity, sizeof(rule_t));
	if (!r->entries) {
		free(r);
		return NULL;
	}
	return r;
}

void rules_free(rules_t *r)
{
	if (!r)
		return;
	free(r->entries);
	free(r);
}

int rules_load(rules_t *r)
{
	r->count = 0;

	char path[512];
	get_rules_path(path, sizeof(path));

	FILE *f = fopen(path, "r");
	if (!f)
		return 0; /* no file yet is fine */

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	rewind(f);
	if (len <= 0) {
		fclose(f);
		return 0;
	}

	char *buf = malloc((size_t)len + 1);
	if (!buf) {
		fclose(f);
		return -1;
	}
	fread(buf, 1, (size_t)len, f);
	buf[len] = '\0';
	fclose(f);

	cJSON *root = cJSON_Parse(buf);
	free(buf);
	if (!root)
		return -1;

	cJSON *arr = cJSON_GetObjectItem(root, "rules");
	if (!cJSON_IsArray(arr)) {
		cJSON_Delete(root);
		return -1;
	}

	cJSON *item;
	cJSON_ArrayForEach(item, arr)
	{
		cJSON *jname = cJSON_GetObjectItem(item, "name");
		cJSON *jadj = cJSON_GetObjectItem(item, "oom_score_adj");
		if (!cJSON_IsString(jname) || !cJSON_IsNumber(jadj))
			continue;
		rules_upsert(r, jname->valuestring, (int)jadj->valuedouble);
	}

	cJSON_Delete(root);
	return r->count;
}

int rules_save(const rules_t *r)
{
	if (ensure_config_dir() < 0)
		return -1;

	cJSON *root = cJSON_CreateObject();
	cJSON *arr = cJSON_CreateArray();
	cJSON_AddItemToObject(root, "rules", arr);

	for (int i = 0; i < r->count; i++) {
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", r->entries[i].name);
		cJSON_AddNumberToObject(
		        item, "oom_score_adj", r->entries[i].oom_score_adj);
		cJSON_AddItemToArray(arr, item);
	}

	char *json = cJSON_Print(root);
	cJSON_Delete(root);
	if (!json)
		return -1;

	char path[512];
	get_rules_path(path, sizeof(path));
	FILE *f = fopen(path, "w");
	if (!f) {
		free(json);
		return -1;
	}
	fputs(json, f);
	fclose(f);
	free(json);
	return 0;
}

void rules_upsert(rules_t *r, const char *name, int oom_score_adj)
{
	/* Update existing entry */
	for (int i = 0; i < r->count; i++) {
		if (strcmp(r->entries[i].name, name) == 0) {
			r->entries[i].oom_score_adj = oom_clamp(oom_score_adj);
			return;
		}
	}
	/* Add new entry */
	if (r->count >= r->capacity) {
		int new_cap = r->capacity * 2;
		rule_t *tmp =
		        realloc(r->entries, (size_t)new_cap * sizeof(rule_t));
		if (!tmp)
			return;
		r->entries = tmp;
		r->capacity = new_cap;
	}
	rule_t *e = &r->entries[r->count++];
	strncpy(e->name, name, PROC_NAME_MAX - 1);
	e->name[PROC_NAME_MAX - 1] = '\0';
	e->oom_score_adj = oom_clamp(oom_score_adj);
}

int rules_remove(rules_t *r, const char *name)
{
	for (int i = 0; i < r->count; i++) {
		if (strcmp(r->entries[i].name, name) == 0) {
			/* Shift remaining entries down */
			for (int j = i; j < r->count - 1; j++)
				r->entries[j] = r->entries[j + 1];
			r->count--;
			return 1;
		}
	}
	return 0;
}

int rules_apply(const rules_t *r, proc_list_t *list)
{
	int updated = 0;
	for (int i = 0; i < list->count; i++) {
		proc_entry_t *p = &list->entries[i];
		const rule_t *rule = rules_find(r, p->name);
		if (!rule)
			continue;
		if (oom_write(p->pid, rule->oom_score_adj) == 0) {
			p->oom_score_adj = rule->oom_score_adj;
			updated++;
		}
	}
	return updated;
}

const rule_t *rules_find(const rules_t *r, const char *name)
{
	for (int i = 0; i < r->count; i++)
		if (strcmp(r->entries[i].name, name) == 0)
			return &r->entries[i];
	return NULL;
}
