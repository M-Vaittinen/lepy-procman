#include "oom.h"

#include <stdio.h>
#include <errno.h>

int oom_clamp(int value)
{
	if (value < OOM_SCORE_ADJ_MIN)
		return OOM_SCORE_ADJ_MIN;
	if (value > OOM_SCORE_ADJ_MAX)
		return OOM_SCORE_ADJ_MAX;
	return value;
}

int oom_read(pid_t pid, int *out_value)
{
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/oom_score_adj", (int)pid);
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	int ret = (fscanf(f, "%d", out_value) == 1) ? 0 : -1;
	fclose(f);
	return ret;
}

int oom_write(pid_t pid, int value)
{
	value = oom_clamp(value);
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/oom_score_adj", (int)pid);
	FILE *f = fopen(path, "w");
	if (!f)
		return -errno;
	fprintf(f, "%d\n", value);
	fclose(f);
	return 0;
}
